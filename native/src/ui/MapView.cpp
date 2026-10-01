#include "MapView.h"
#include "../core/MotionPref.h"
#include <QPainter>
#include <QFile>
#include <QImage>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMouseEvent>
#include <QPainterPath>
#include <QLineF>
#include <QDebug>
#include <cmath>
#include <algorithm>
#include <QHash>
#include <QPoint>
#include <QCoreApplication>

// —— 预投影：经纬度 → [0,MAP_W]x[0,MAP_H]（与 web 版 eqX/eqY 一致）——
static double eqX(double lon) { return (lon + 180.0) / 360.0 * MapView::MAP_W; }
static double eqY(double lat) { return (90.0 - lat) / 180.0 * MapView::MAP_H; }
static double wrapX(double x) { return fmod(fmod(x, MapView::MAP_W) + MapView::MAP_W, MapView::MAP_W); }

MapView::MapView(QWidget* parent) : QWidget(parent)
{
    setMinimumSize(320, 200);
    setMouseTracking(true);
    reduceMotion_ = Lumen::systemReducedMotion();   // P15：尊重系统「减弱动效」

    loadData();
    computeRegionBBox();
    renderFootDotSprite();              // 很精细：预渲染翡翠足迹点精灵

    connect(&driftTimer_, &QTimer::timeout, this, [this] {
        const double dt = timer_.isValid() ? timer_.restart() / 1000.0 : 0.0;
        if (reduceMotion_) return;
        if (level_ == World) {
            driftPx_ = wrapX(driftPx_ + DRIFT_SPEED * dt);
            update();
        } else {
            // 很精细：下钻视图金色航线流动虚线
            arcDashOffset_ = std::fmod(arcDashOffset_ + 16.0 * dt, 24.0);
            update();
        }
    });
    timer_.start();
    driftTimer_.start(33); // ~30fps 漂移

    emit breadcrumbChanged(breadcrumb());
}

void MapView::loadData()
{
    QFile f(":/data/mapdata.json");
    const bool opened = f.open(QIODevice::ReadOnly);
    if (!opened) {
        std::fprintf(stderr, "LA_MAP_OPEN_FAIL :/data/mapdata.json\n");
        std::fflush(stderr);
        return;
    }
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    f.close();

    // 世界环（高精度：Natural Earth 50m 国家/海岸线，含湖泊镂空）
    const QJsonArray world = root.value("world").toArray();
    worldFeatures_.clear();
    worldFeatures_.reserve(world.size());
    for (const QJsonValue& fv : world) {
        const QJsonArray rings = fv.toArray();
        if (rings.isEmpty()) continue;
        WorldFeature wf;
        const QJsonArray ext = rings[0].toArray();
        wf.exterior.reserve(ext.size() / 2);
        for (int i = 0; i + 1 < ext.size(); i += 2)
            wf.exterior.append(QPointF(ext[i].toDouble(), ext[i + 1].toDouble()));
        for (int r = 1; r < rings.size(); ++r) {
            const QJsonArray ha = rings[r].toArray();
            QVector<QPointF> hole; hole.reserve(ha.size() / 2);
            for (int i = 0; i + 1 < ha.size(); i += 2)
                hole.append(QPointF(ha[i].toDouble(), ha[i + 1].toDouble()));
            if (hole.size() >= 3) wf.holes.append(hole);
        }
        if (wf.exterior.size() >= 3) worldFeatures_.append(wf);
    }

    // 大区
    const QJsonArray regs = root.value("regions").toArray();
    regions_.clear();
    regions_.reserve(regs.size());
    for (const QJsonValue& v : regs) {
        const QJsonObject o = v.toObject();
        RegionInfo ri;
        ri.id   = o.value("id").toString();
        ri.name = o.value("name").toString();
        ri.lon  = o.value("lon").toDouble();
        ri.lat  = o.value("lat").toDouble();
        ri.proj = QPointF(eqX(ri.lon), eqY(ri.lat));
        regions_.append(ri);
    }

    provJson_ = root.value("provinces").toObject();
    provZh_   = root.value("provZh").toObject();

    buildWorldPixmap();

    if (!qEnvironmentVariableIsEmpty("LA_MAP_SMOKE")) {
        int tot = 0;
        for (auto it = provJson_.constBegin(); it != provJson_.constEnd(); ++it)
            tot += it.value().toArray().size();
        int worldRings = 0;
        for (const WorldFeature& wf : worldFeatures_) worldRings += 1 + wf.holes.size();
        std::fprintf(stderr, "LA_MAP_SMOKE rings=%d regions=%d provinces=%d zh=%d\n",
                     worldRings, regions_.size(), tot, provZh_.size());
        std::fflush(stderr);
    }
}

QString MapView::zhOf(const QString& en) const
{
    const QJsonValue v = provZh_.value(en);
    return v.isString() ? v.toString() : QString();
}

void MapView::computeProvinceBBox(ProvinceInfo& pv)
{
    if (pv.ring.pts.isEmpty()) { pv.bbox = QRectF(); return; }
    // 注意：零尺寸 QRectF 被 Qt 视为 isEmpty()，operator|= 会短路返回右操作数，
    // 导致并集永远不累积 → bbox 退化成单点。故用显式 min/max 计算。
    double l = pv.ring.pts.first().x(), r = l;
    double t = pv.ring.pts.first().y(), b = t;
    for (const QPointF& p : pv.ring.pts) {
        if (p.x() < l) l = p.x();
        if (p.x() > r) r = p.x();
        if (p.y() < t) t = p.y();
        if (p.y() > b) b = p.y();
    }
    pv.bbox = QRectF(l, t, r - l, b - t);
}

void MapView::computeRegionBBox()
{
    // 仅用于预计算各省 bbox（curProvinces_ 在 enterRegion 时填充）
}

void MapView::buildWorldPixmap()
{
    const int W = int(MAP_W), H = int(MAP_H);
    worldPix_ = QPixmap(W, H);
    worldPix_.fill(Qt::transparent);
    QPainter p(&worldPix_);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QColor ocean(0x0e, 0x16, 0x1f);   // 深海（雾隐深色）
    const QColor land(0x2f, 0x46, 0x58);    // 陆地基色（与 web .cty 对齐）
    const QColor border(0x4a, 0x66, 0x78);

    p.fillRect(0, 0, W, H, ocean);
    // 很精细：经纬网格（baked，仅落于海洋，陆地覆盖其上→干净）
    {
        QPen gpen(QColor(0x7f, 0x97, 0xa8, 28));
        gpen.setWidthF(0.6);
        p.setPen(gpen);
        for (int lon = -180; lon <= 180; lon += 30) {
            const double x = eqX(lon);
            p.drawLine(QPointF(x, 0), QPointF(x, MAP_H));
        }
        for (int lat = -90; lat <= 90; lat += 30) {
            const double y = eqY(lat);
            p.drawLine(QPointF(0, y), QPointF(MAP_W, y));
        }
    }
    QPen borderPen(border, 0.8);
    QPen rimPen(QColor(0x9f, 0xc4, 0xd6, 40), 0.7);
    for (const WorldFeature& wf : worldFeatures_) {
        if (wf.exterior.isEmpty()) continue;
        QPainterPath path;
        path.moveTo(wf.exterior.first());
        for (int i = 1; i < wf.exterior.size(); ++i) path.lineTo(wf.exterior[i]);
        path.closeSubpath();
        for (const QVector<QPointF>& hole : wf.holes) {   // 湖泊内环 → 镂空
            if (hole.size() < 3) continue;
            path.moveTo(hole.first());
            for (int i = 1; i < hole.size(); ++i) path.lineTo(hole[i]);
            path.closeSubpath();
        }
        p.fillPath(path, land);                 // 默认 OddEvenFill → 湖泊镂空为水
        p.setPen(borderPen); p.drawPath(path);  // 海岸 + 国界 + 湖岸
        p.setPen(rimPen);     p.drawPath(path);  // 海岸 rim-light 二次描边
    }
    // P16.4.2 金色航线：以「我的位置」为基点，向每一处拍摄地延伸（画在足迹点下层）。
    // 与 web 版 arcsA/arcsB 同思路：经度是循环的，整条弧在 ±MAP_W 各复制一份，接缝处自然衔接。
    if (!gpsPts_.isEmpty() || !arcRegionIds_.isEmpty()) {
        const QPointF h(eqX(homeLon_), eqY(homeLat_));
        const QVector<QPointF> targets = arcTargets();
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(0xd9, 0xa9, 0x4e, 110), 1.0));
        for (const QPointF& pt : targets) {
            QPointF t = pt;
            if (t.x() - h.x() > MAP_W / 2) t.rx() -= MAP_W;   // 短路径方向
            if (h.x() - t.x() > MAP_W / 2) t.rx() += MAP_W;
            const double dx = t.x() - h.x(), dy = t.y() - h.y();
            const double len = std::sqrt(dx * dx + dy * dy);
            const double lift = std::min(90.0, len * 0.22);   // 控制点向上（极向）拱起
            const QPointF mid((h.x() + t.x()) / 2.0, (h.y() + t.y()) / 2.0 - lift);
            for (double off : { -MAP_W, 0.0, MAP_W }) {
                QPainterPath arc;
                arc.moveTo(h.x() + off, h.y());
                arc.quadTo(mid.x() + off, mid.y(), t.x() + off, t.y());
                p.drawPath(arc);
            }
        }
        // 基点标记：金色圆环 + 实心芯
        p.setPen(QPen(QColor(0xd9, 0xa9, 0x4e), 1.4));
        p.setBrush(QColor(0xf0, 0xd0, 0x8a));
        p.drawEllipse(h, 3.2, 3.2);
    }
    // 很精细：翡翠足迹点（精灵 blit + 密度分级柔光晕 + 翡翠描边环），与世界烘焙层一致
    if (!gpsPts_.isEmpty()) {
        const QVector<int> dens = computeDensity();
        for (int i = 0; i < gpsPts_.size(); ++i) {
            const QPointF& pt = gpsPts_[i];
            if (pt.x() < 0 || pt.x() > MAP_W || pt.y() < 0 || pt.y() > MAP_H) continue;
            drawFootprintDot(p, pt, 2.0, dens[i]);
        }
    }
    p.end();
}

void MapView::setGpsPoints(const QVector<QPointF>& pts)
{
    gpsPts_ = pts;
    buildWorldPixmap();          // 重新烘焙世界静态层（含足迹点，随漂移一起移动）
    update();
}

// P16.4.2 金色航线基点（经纬度）。重烘焙世界层并刷新下钻视图。
void MapView::setHomePoint(double lon, double lat)
{
    if (lon == homeLon_ && lat == homeLat_) return;
    homeLon_ = lon;
    homeLat_ = lat;
    buildWorldPixmap();
    update();
}

void MapView::setArcRegionIds(const QStringList& ids)
{
    if (ids == arcRegionIds_) return;
    arcRegionIds_ = ids;
    buildWorldPixmap();
    update();
}

// 航线目标集 = GPS 足迹点 ∪ 有作品大区的中心点（预投影空间，去重）
QVector<QPointF> MapView::arcTargets() const
{
    QVector<QPointF> ts = gpsPts_;
    for (const QString& id : arcRegionIds_) {
        for (const RegionInfo& ri : regions_) {
            if (ri.id == id) {
                if (!ts.contains(ri.proj)) ts.append(ri.proj);
                break;
            }
        }
    }
    return ts;
}

// —— 坐标变换：预投影 → 目标矩形（含偏移与缩放）——
QPointF MapView::preToWidget(const QPointF& pre, const QRectF& dst, double scale,
                             double offX) const
{
    return QPointF(dst.x() + (pre.x() + offX) * scale,
                   dst.y() + pre.y() * scale);
}

double MapView::fitScale(const QRectF& bbox, const QRectF& dst) const
{
    if (bbox.isEmpty()) return 1.0;
    const double sx = dst.width() / bbox.width();
    const double sy = dst.height() / bbox.height();
    return std::min(sx, sy);
}

QRectF MapView::fitRect(const QRectF& bbox, const QRectF& dst, double& outScale) const
{
    outScale = fitScale(bbox, dst);
    const double w = bbox.width() * outScale;
    const double h = bbox.height() * outScale;
    const double x = dst.x() + (dst.width() - w) / 2.0 - bbox.x() * outScale;
    const double y = dst.y() + (dst.height() - h) / 2.0 - bbox.y() * outScale;
    return QRectF(x, y, w, h);
}

void MapView::renderWorldInto(QPainter& p, const QRectF& dst) const
{
    // 离屏世界按 dst 高度等比缩放，宽度=2*高度（等距圆柱 2:1）
    const double scale = dst.height() / MAP_H;
    const double mapWpx = MAP_W * scale;
    const QRectF src(0, 0, MAP_W, MAP_H);
    // 横向环绕：绘制足够副本覆盖 dst 宽度
    double off = -wrapX(driftPx_) * scale;
    for (double x0 = off; x0 < dst.right() + mapWpx; x0 += mapWpx) {
        QRectF d(x0, dst.y(), mapWpx, dst.height());
        p.drawPixmap(d, worldPix_, src);
    }
    // 大区旅行锚点（金色点 + 名）
    QColor gold(0xc9, 0xa2, 0x4b);
    for (const RegionInfo& ri : regions_) {
        const QPointF w = preToWidget(ri.proj, dst, scale, -driftPx_);
        // 环绕后的实际绘制 x
        double wx = w.x();
        const double span = mapWpx;
        wx = dst.x() + fmod(wx - dst.x(), span);
        if (wx < dst.x()) wx += span;
        const QPointF pos(wx, w.y());
        p.setPen(QPen(gold, 1.4));
        p.setBrush(gold);
        p.drawEllipse(pos, 3.2, 3.2);
        p.setBrush(Qt::NoBrush);
        drawLabel(p, pos + QPointF(6, 4), ri.name, QColor(0xe8, 0xe2, 0xd4));
    }
}

void MapView::renderRegion(QPainter& p, const QRectF& dst) const
{
    if (curProvinces_.isEmpty()) return;
    QRectF bbox;
    bool first = true;
    for (const ProvinceInfo& pv : curProvinces_) {
        if (first) { bbox = pv.bbox; first = false; }
        else bbox |= pv.bbox;
    }
    double scale = 1;
    const QRectF fr = fitRect(bbox, dst, scale);
    const QColor land(0x2f, 0x46, 0x58);
    const QColor border(0x4a, 0x66, 0x78);
    const QColor hl(0xc9, 0xa2, 0x4b);
    drawGraticule(p, bbox, dst);   // 很精细：经纬网格
    for (const ProvinceInfo& pv : curProvinces_) {
        QPainterPath path;
        const QPointF o = fr.topLeft();
        path.moveTo(o + pv.ring.pts.first() * scale);
        for (int i = 1; i < pv.ring.pts.size(); ++i)
            path.lineTo(o + pv.ring.pts[i] * scale);
        path.closeSubpath();
        const bool isCur = hasCurProvince_ && pv.name == curProvince_.name;
        p.fillPath(path, isCur ? hl : land);
        p.setPen(QPen(isCur ? QColor(0xe8, 0xc8, 0x7a) : border, isCur ? 1.6 : 0.9));
        p.drawPath(path);
        // 省名标签（很精细：描边 halo 保证可读）
        const QPointF c = o + pv.ctr * scale;
        drawLabel(p, c + QPointF(4, 4), pv.zh.isEmpty() ? pv.name : pv.zh, QColor(0xe8, 0xe2, 0xd4));
    }
    drawArcs(p, bbox, dst);      // P16.4.2 金色航线
    drawGpsDots(p, bbox, dst);   // P16.4
}

void MapView::renderProvince(QPainter& p, const QRectF& dst) const
{
    if (!hasCurProvince_) return;
    double scale = 1;
    const QRectF fr = fitRect(curProvince_.bbox, dst, scale);
    const QPointF o = fr.topLeft();
    QPainterPath path;
    path.moveTo(o + curProvince_.ring.pts.first() * scale);
    for (int i = 1; i < curProvince_.ring.pts.size(); ++i)
        path.lineTo(o + curProvince_.ring.pts[i] * scale);
    path.closeSubpath();
    p.fillPath(path, QColor(0x35, 0x50, 0x63));
    p.setPen(QPen(QColor(0xc9, 0xa2, 0x4b), 1.8));
    p.drawPath(path);
    drawGraticule(p, curProvince_.bbox, dst);   // 很精细：经纬网格
    const QPointF c = o + curProvince_.ctr * scale;
    const QString label = (curProvince_.zh.isEmpty() ? curProvince_.name : curProvince_.zh);
    drawLabel(p, c + QPointF(6, 5), label, QColor(0xe8, 0xe2, 0xd4));
    drawArcs(p, curProvince_.bbox, dst);      // P16.4.2 金色航线
    drawGpsDots(p, curProvince_.bbox, dst);   // P16.4
}

void MapView::drawGpsDots(QPainter& p, const QRectF& bbox, const QRectF& dst) const
{
    if (gpsPts_.isEmpty()) return;
    double scale = 1;
    const QRectF fr = fitRect(bbox, dst, scale);
    const QPointF o = fr.topLeft();
    p.save();
    p.setClipRect(dst);
    // 很精细：密度分级 + 精灵 blit（与世界烘焙层共用 drawFootprintDot）
    const QVector<int> dens = computeDensity();
    for (int i = 0; i < gpsPts_.size(); ++i) {
        const QPointF& pt = gpsPts_[i];
        if (pt.x() < bbox.left() - 2 || pt.x() > bbox.right() + 2 ||
            pt.y() < bbox.top() - 2 || pt.y() > bbox.bottom() + 2) continue;
        drawFootprintDot(p, o + pt * scale, 2.4 * std::sqrt(scale), dens[i]); // sqrt：下钻放大时光点次线性增长，避免 zoom 后光晕成巨斑
    }
    p.restore();
}

// P16.4.2 金色航线下钻视图：与 drawGpsDots 同一拟合变换；基点在 bbox 外也照画
// （弧从画面边缘切入，clipRect 兜底），保持「基点 → 每处足迹」的叙事一致。
void MapView::drawArcs(QPainter& p, const QRectF& bbox, const QRectF& dst) const
{
    if (gpsPts_.isEmpty() && arcRegionIds_.isEmpty()) return;
    const QVector<QPointF> targets = arcTargets();
    double scale = 1;
    const QRectF fr = fitRect(bbox, dst, scale);
    const QPointF o = fr.topLeft();
    const QPointF h(o.x() + eqX(homeLon_) * scale, o.y() + eqY(homeLat_) * scale);
    p.save();
    p.setClipRect(dst);
    p.setBrush(Qt::NoBrush);
    const bool flow = !reduceMotion_;
    for (const QPointF& pt : targets) {
        if (pt.x() < bbox.left() - 2 || pt.x() > bbox.right() + 2 ||
            pt.y() < bbox.top() - 2 || pt.y() > bbox.bottom() + 2) continue;
        const QPointF t(o.x() + pt.x() * scale, o.y() + pt.y() * scale);
        const double dx = t.x() - h.x(), dy = t.y() - h.y();
        const double len = std::sqrt(dx * dx + dy * dy);
        const double lift = std::min(120.0, len * 0.25);
        QPainterPath arc;
        arc.moveTo(h);
        arc.quadTo(QPointF((h.x() + t.x()) / 2.0, (h.y() + t.y()) / 2.0 - lift), t);
        // 很精细：流动金色虚线（reduceMotion 时退化为实线）
        p.setBrush(Qt::NoBrush);
        QPen pen(QColor(0xd9, 0xa9, 0x4e, 150), 1.2);
        if (flow) { pen.setStyle(Qt::DashLine); pen.setDashPattern({6.0, 6.0}); pen.setDashOffset(-arcDashOffset_); }
        p.setPen(pen);
        p.drawPath(arc);
        // 端点光点（目标 = 拍摄地）
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0xd9, 0xa9, 0x4e, 210));
        p.drawEllipse(t, 2.2, 2.2);
        p.setBrush(Qt::NoBrush);
    }
    // 基点标记（在 bbox 内才画；世界层已画过一次）
    if (eqX(homeLon_) >= bbox.left() && eqX(homeLon_) <= bbox.right() &&
        eqY(homeLat_) >= bbox.top() && eqY(homeLat_) <= bbox.bottom()) {
        p.setPen(QPen(QColor(0xd9, 0xa9, 0x4e), 1.5));
        p.setBrush(QColor(0xf0, 0xd0, 0x8a));
        p.drawEllipse(h, 3.6, 3.6);
    }
    p.restore();
}

// A 现状墨韵·抛光 —— 像素自测：注入确定性合成足迹点，离屏渲染世界层并统计翡翠像素
// （核心+光晕）。用于 CI/offscreen 验证「光晕已落地」，颜色校验铁律：实测不许猜。
void MapView::qaShootA(const QString& path)
{
    // 确定性合成足迹点（北京/上海/广州/成都/乌鲁木齐），与真实库无关，保证可复现
    gpsPts_ = {
        QPointF(eqX(116.40), eqY(39.90)),
        QPointF(eqX(121.47), eqY(31.23)),
        QPointF(eqX(113.26), eqY(23.13)),
        QPointF(eqX(104.07), eqY(30.57)),
        QPointF(eqX(87.62),  eqY(43.82)),
    };
    arcRegionIds_.clear();
    buildWorldPixmap();

    const int W = int(MAP_W), H = int(MAP_H);
    QImage img(W, H, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    {
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.drawPixmap(0, 0, worldPix_);
    }
    int jade = 0;
    const uchar* bits = img.constBits();
    const int bpl = img.bytesPerLine();
    for (int y = 0; y < H; ++y) {
        const uchar* row = bits + y * bpl;
        for (int x = 0; x < W; ++x) {
            const uchar* px = row + x * 4;
            const int b = px[0], g = px[1], r = px[2];
            if (g > 70 && g > r + 15 && g > b) ++jade;
        }
    }
    if (!path.isEmpty()) img.save(path);
    std::fprintf(stderr, "LA_MAP_A_QA jade=%d points=%d png=%s\n",
                 jade, gpsPts_.size(), qPrintable(path));
    std::fflush(stderr);
}

// 下钻区/省视图像素自测：强制 enterRegion(id) 后离屏渲染该大区省视图并统计省面像素。
// 用于 CI/offscreen 验证「中国 34 省 DataV 几何已落地」「省面真实绘制」（颜色校验铁律：实测不许猜）。
void MapView::qaShootRegion(const QString& path, const QString& id)
{
    // 确定性合成足迹点（北京/上海/广州/成都/乌鲁木齐，均落在中国 bbox 内）→ 验证下钻视图航线/足迹
    gpsPts_ = {
        QPointF(eqX(116.40), eqY(39.90)),
        QPointF(eqX(121.47), eqY(31.23)),
        QPointF(eqX(113.26), eqY(23.13)),
        QPointF(eqX(104.07), eqY(30.57)),
        QPointF(eqX(87.62),  eqY(43.82)),
    };
    arcRegionIds_.clear();
    enterRegion(id);                       // 载入该大区省几何，level_ -> Region
    const int W = int(MAP_W), H = int(MAP_H);
    QImage img(W, H, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    {
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF dst = QRectF(8, 8, W - 16, H - 16);
        renderRegion(p, dst);
    }
    int land = 0;
    const uchar* bits = img.constBits();
    const int bpl = img.bytesPerLine();
    for (int y = 0; y < H; ++y) {
        const uchar* row = bits + y * bpl;
        for (int x = 0; x < W; ++x) {
            const uchar* px = row + x * 4;
            const int b = px[0], g = px[1], r = px[2];
            // 省面基色 land(47,70,88)：与背景透明/深色及金色高亮均有显著差
            if (std::abs(r - 47) + std::abs(g - 70) + std::abs(b - 88) < 42) ++land;
        }
    }
    if (!path.isEmpty()) img.save(path);
    std::fprintf(stderr, "LA_MAP_REGION_QA region=%s provinces=%d land=%d png=%s\n",
                 qPrintable(id), curProvinces_.size(), land, qPrintable(path));
    std::fflush(stderr);
}

// 点击链路 QA：在「用户看见的位置」合成鼠标事件，实测两级命中是否触发。
// ① 世界层：按绘制数学算出中国金色锚点的屏幕坐标 → 合成左键点击 → 应进入 Region；
// ② 大区层：enterRegion(china) 后按 fitRect 算出青海省中心屏幕坐标 → 合成点击 → 应进入 Province。
// （颜色校验铁律的交互版：命中必须实测，不许猜。）
void MapView::qaClickProbe()
{
    // —— 世界层 ——
    const QRectF dst = rect().adjusted(8, 8, -8, -8);
    const double scale = dst.height() / MAP_H;
    const double span = MAP_W * scale;
    const RegionInfo* china = nullptr;
    for (const RegionInfo& ri : regions_)
        if (ri.id == QLatin1String("china")) { china = &ri; break; }
    if (!china && !regions_.isEmpty()) china = &regions_.first();
    QString worldResult = QStringLiteral("skip");
    if (china) {
        const QPointF w = preToWidget(china->proj, dst, scale, -driftPx_);
        double wx = dst.x() + fmod(w.x() - dst.x(), span);
        if (wx < dst.x()) wx += span;
        const QPointF pos(wx, w.y());
        QMouseEvent press(QEvent::MouseButtonPress, pos,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(this, &press);
        worldResult = (level_ == Region)
            ? QStringLiteral("OK") : QStringLiteral("FAIL(level=%1)").arg(int(level_));
    }
    // —— 大区层：中国省面 ——
    QString provResult = QStringLiteral("skip");
    enterRegion(QStringLiteral("china"));
    if (!curProvinces_.isEmpty()) {
        const ProvinceInfo* pick = nullptr;
        for (const ProvinceInfo& pv : curProvinces_)
            if (pv.name == QLatin1String("青海省")) { pick = &pv; break; }
        if (!pick) pick = &curProvinces_.first();
        QRectF bbox; bool first = true;
        for (const ProvinceInfo& pv : curProvinces_) {
            if (first) { bbox = pv.bbox; first = false; } else bbox |= pv.bbox;
        }
        double sc = 1;
        const QRectF fr = fitRect(bbox, rect().adjusted(8, 8, -8, -8), sc);
        const QPointF pos = fr.topLeft() + pick->ctr * sc;
        QMouseEvent press(QEvent::MouseButtonPress, pos,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(this, &press);
        provResult = (level_ == Province && hasCurProvince_)
            ? QStringLiteral("OK(%1)").arg(pick->zh.isEmpty() ? pick->name : pick->zh)
            : QStringLiteral("FAIL(level=%1)").arg(int(level_));
    }
    std::fprintf(stderr, "LA_MAP_CLICK_QA world->region=%s china-province=%s\n",
                 qPrintable(worldResult), qPrintable(provResult));
    std::fflush(stderr);
}

// —— 很精细优化辅助 ——

// 预渲染翡翠足迹点精灵（一次），逐帧 blit，避免每帧重建径向梯度
void MapView::renderFootDotSprite()
{
    const int D = 40;
    footDotPix_ = QPixmap(D, D);
    footDotPix_.fill(Qt::transparent);
    QPainter p(&footDotPix_);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QPointF c(D / 2.0, D / 2.0);
    // 柔光晕（径向梯度，翡翠→透明）
    QRadialGradient g(c, D * 0.45);
    g.setColorAt(0.0,  QColor(0x5f, 0xdc, 0xa6, 150));
    g.setColorAt(0.35, QColor(0x5f, 0xdc, 0xa6, 80));
    g.setColorAt(1.0,  QColor(0x5f, 0xdc, 0xa6, 0));
    p.setBrush(g);
    p.setPen(Qt::NoPen);
    p.drawEllipse(c, D * 0.45, D * 0.45);
    // 翡翠描边环
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(0x5f, 0xdc, 0xa6, 140), 1.4));
    p.drawEllipse(c, D * 0.18, D * 0.18);
    // 实心翡翠芯
    p.setBrush(QColor(0x7f, 0xe6, 0xb6));
    p.setPen(Qt::NoPen);
    p.drawEllipse(c, D * 0.10, D * 0.10);
}

// 密度分级足迹点：blit 精灵，密度越高光晕越大越亮
void MapView::drawFootprintDot(QPainter& p, const QPointF& c, double coreR, int dens) const
{
    if (footDotPix_.isNull()) return;
    const double d = std::min(dens, 8);
    const double sizeFactor = 0.85 + d * 0.05;     // 0.85..1.25
    const double op = 0.55 + d * 0.06;             // 0.55..1.03
    const double draw = 5.0 * coreR * sizeFactor; // 精灵核(4px)→coreR 映射（5.0：光点收小一档，世界层光晕直径 ~8-11px）
    const qreal saved = p.opacity();
    p.setOpacity(qMin(1.0, op));
    p.drawPixmap(QRectF(c.x() - draw / 2, c.y() - draw / 2, draw, draw),
                 footDotPix_, footDotPix_.rect());
    p.setOpacity(saved);
}

// 局部密度（网格计数，O(n)）：用于足迹点分级
QVector<int> MapView::computeDensity() const
{
    const double cell = 22.0;
    QHash<QPoint, int> grid;
    for (const QPointF& pt : gpsPts_) {
        const int ix = int(std::floor(pt.x() / cell));
        const int iy = int(std::floor(pt.y() / cell));
        grid[QPoint(ix, iy)]++;
    }
    QVector<int> out;
    out.reserve(gpsPts_.size());
    for (const QPointF& pt : gpsPts_) {
        const int ix = int(std::floor(pt.x() / cell));
        const int iy = int(std::floor(pt.y() / cell));
        int c = 0;
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy)
                c += grid.value(QPoint(ix + dx, iy + dy), 0);
        out.append(c);
    }
    return out;
}

// 经纬网格（下钻视图）：预投影空间 → fitRect 变换
void MapView::drawGraticule(QPainter& p, const QRectF& bbox, const QRectF& dst) const
{
    double scale = 1;
    const QRectF fr = fitRect(bbox, dst, scale);
    const QPointF o = fr.topLeft();
    p.save();
    p.setClipRect(dst);
    QPen pen(QColor(0x7f, 0x97, 0xa8, 30));
    pen.setWidthF(0.6);
    p.setPen(pen);
    for (int lon = -180; lon <= 180; lon += 30) {
        const double x = o.x() + eqX(lon) * scale;
        if (x < dst.left() - 1 || x > dst.right() + 1) continue;
        p.drawLine(QPointF(x, dst.top()), QPointF(x, dst.bottom()));
    }
    for (int lat = -90; lat <= 90; lat += 30) {
        const double y = o.y() + eqY(lat) * scale;
        if (y < dst.top() - 1 || y > dst.bottom() + 1) continue;
        p.drawLine(QPointF(dst.left(), y), QPointF(dst.right(), y));
    }
    p.restore();
}

// 文字描边 halo（深色描边 + 亮色填充），保证浅色标签在地图背景可读
void MapView::drawLabel(QPainter& p, const QPointF& pos, const QString& text, const QColor& fill) const
{
    if (text.isEmpty()) return;
    // save/restore：绝不让 pen/brush 泄漏给调用方 —— 否则后续 drawPath 会用
    // 残留的 cream 笔刷把省面整体误填充（下钻视图省份全变奶油色的根因）
    p.save();
    const QFont f = p.font();
    QPainterPath path;
    path.addText(pos, f, text);
    p.setPen(QPen(QColor(0x06, 0x0c, 0x10, 210), 2.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
    p.setPen(QPen(fill, 1));
    p.setBrush(fill);
    p.drawPath(path);
    p.restore();
}

// 世界视图右下角图例
void MapView::drawLegend(QPainter& p) const
{
    const QRectF r = rect();
    const double bw = 150, bh = 66;
    const double lx = r.right() - bw - 10, ly = r.bottom() - bh - 10;
    p.setPen(QPen(QColor(0x3a, 0x4e, 0x5a, 180), 1));
    p.setBrush(QColor(0x0c, 0x14, 0x1a, 150));
    p.drawRoundedRect(lx, ly, bw, bh, 6, 6);
    int y = int(ly) + 14;
    // 拍摄地
    p.setBrush(QColor(0x7f, 0xe6, 0xb6));
    p.setPen(Qt::NoPen);
    p.drawEllipse(QPointF(lx + 12, y), 3, 3);
    drawLabel(p, QPointF(lx + 22, y - 3), "拍摄地", QColor(0xd8, 0xe2, 0xd4));
    y += 20;
    // 航线
    p.setPen(QPen(QColor(0xd9, 0xa9, 0x4e), 1.5));
    p.setBrush(Qt::NoBrush);
    p.drawLine(QLineF(lx + 6, y, lx + 18, y));
    drawLabel(p, QPointF(lx + 22, y - 3), "航线", QColor(0xd8, 0xe2, 0xd4));
    y += 20;
    // 我的位置
    p.setPen(QPen(QColor(0xd9, 0xa9, 0x4e), 1.5));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QPointF(lx + 12, y), 4, 4);
    drawLabel(p, QPointF(lx + 22, y - 3), "我的位置", QColor(0xd8, 0xe2, 0xd4));
}

void MapView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QColor bg(0x0a, 0x10, 0x16);
    p.fillRect(rect(), bg);

    const QRectF dst = rect().adjusted(8, 8, -8, -8);
    if (level_ == World)
        renderWorldInto(p, dst);
    else if (level_ == Region)
        renderRegion(p, dst);
    else
        renderProvince(p, dst);

    if (level_ == World)
        drawLegend(p);   // 很精细：世界视图图例

    // 诚实足迹提示（GPS 数由 MapPage 经 setGpsCount 注入，绝不硬编码）
    p.setPen(QColor(0x8d, 0xa3, 0x96));
    const QString foot = (level_ == World)
        ? QString("足迹地图 · 10 大区 / 233 省 · 本库 GPS 足迹 %1")
            .arg(gpsCount_)
        : QString();
    if (!foot.isEmpty())
        p.drawText(rect().adjusted(12, 0, 0, -10), Qt::AlignLeft | Qt::AlignBottom, foot);
    if (!hoverHint_.isEmpty()) {
        p.setPen(QColor(0xe8, 0xc8, 0x7a));
        p.drawText(rect().adjusted(12, 10, 0, 0), Qt::AlignLeft | Qt::AlignTop, hoverHint_);
    }
}

void MapView::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    update();
}

void MapView::mouseMoveEvent(QMouseEvent* e)
{
    if (level_ == World) {
        // 命中测试：大区锚点（scale 与 renderWorldInto 绘制基准一致，见 mousePressEvent 注释）
        const QRectF dst = rect().adjusted(8, 8, -8, -8);
        const double scale = dst.height() / MAP_H;
        const double span = MAP_W * scale;
        for (const RegionInfo& ri : regions_) {
            QPointF w = preToWidget(ri.proj, dst, scale, -driftPx_);
            double wx = dst.x() + fmod(w.x() - dst.x(), span);
            if (wx < dst.x()) wx += span;
            if (std::hypot(wx - e->pos().x(), w.y() - e->pos().y()) < 8) {
                if (hoverHint_ != ri.name) { hoverHint_ = ri.name; setCursor(Qt::PointingHandCursor); update(); }
                return;
            }
        }
        if (!hoverHint_.isEmpty()) { hoverHint_.clear(); setCursor(Qt::ArrowCursor); update(); }
    } else if (level_ == Region) {
        double scale = 1;
        QRectF bbox; bool first = true;
        for (const ProvinceInfo& pv : curProvinces_) { if (first){bbox=pv.bbox;first=false;} else bbox|=pv.bbox; }
        const QRectF fr = fitRect(bbox, rect().adjusted(8,8,-8,-8), scale);
        for (const ProvinceInfo& pv : curProvinces_) {
            QPainterPath path;
            const QPointF o = fr.topLeft();
            path.moveTo(o + pv.ring.pts.first()*scale);
            for (int i=1;i<pv.ring.pts.size();++i) path.lineTo(o + pv.ring.pts[i]*scale);
            path.closeSubpath();
            if (path.contains(e->pos())) {
                const QString t = pv.zh.isEmpty()?pv.name:pv.zh;
                if (hoverHint_ != t) { hoverHint_=t; setCursor(Qt::PointingHandCursor); update(); }
                return;
            }
        }
        if (!hoverHint_.isEmpty()) { hoverHint_.clear(); setCursor(Qt::ArrowCursor); update(); }
    }
}

void MapView::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) return;
    if (level_ == World) {
        // scale 基准必须与 renderWorldInto 的绘制 scale（dst.height()/MAP_H）完全一致，
        // 否则热区相对可见金点整体偏移（8px 内边距 × 预投影坐标，最大 ~30px），
        // 而点击半径只有 10px → 用户对着金点点击却毫无反应。
        const QRectF dst = rect().adjusted(8, 8, -8, -8);
        const double scale = dst.height() / MAP_H;
        const double span = MAP_W * scale;
        for (const RegionInfo& ri : regions_) {
            QPointF w = preToWidget(ri.proj, dst, scale, -driftPx_);
            double wx = dst.x() + fmod(w.x() - dst.x(), span);
            if (wx < dst.x()) wx += span;
            if (std::hypot(wx - e->pos().x(), w.y() - e->pos().y()) < 10) {
                emit picked(ri.id, ri.name, false);
                enterRegion(ri.id);
                return;
            }
        }
    } else if (level_ == Region) {
        double scale = 1;
        QRectF bbox; bool first = true;
        for (const ProvinceInfo& pv : curProvinces_) { if (first){bbox=pv.bbox;first=false;} else bbox|=pv.bbox; }
        const QRectF fr = fitRect(bbox, rect().adjusted(8,8,-8,-8), scale);
        for (const ProvinceInfo& pv : curProvinces_) {
            QPainterPath path;
            const QPointF o = fr.topLeft();
            path.moveTo(o + pv.ring.pts.first()*scale);
            for (int i=1;i<pv.ring.pts.size();++i) path.lineTo(o + pv.ring.pts[i]*scale);
            path.closeSubpath();
            if (path.contains(e->pos())) {
                const QString label = pv.zh.isEmpty() ? pv.name : pv.zh;
                emit picked(pv.name, label, true);
                enterProvince(pv.name);
                return;
            }
        }
    }
}

// 鲁棒解析省环：兼容 扁平 [x,y,x,y,...]、双层 [[x,y,...]]、点对 [[x,y],[x,y],...]
// （历史数据各 region 格式不统一，统一收敛避免下钻视图退化为单点）
static QVector<QPointF> parseProvinceRing(const QJsonValue& v)
{
    QVector<QPointF> pts;
    const QJsonArray a = v.toArray();
    if (a.isEmpty()) return pts;
    if (a[0].isArray()) {
        if (a.size() == 1) {                       // 双层：唯一子数组即扁平环
            const QJsonArray inner = a[0].toArray();
            for (int i = 0; i + 1 < inner.size(); i += 2)
                pts.append(QPointF(inner[i].toDouble(), inner[i + 1].toDouble()));
            return pts;
        }
        for (const QJsonValue& sub : a) {          // 点对列表
            const QJsonArray p = sub.toArray();
            if (p.size() >= 2) pts.append(QPointF(p[0].toDouble(), p[1].toDouble()));
        }
        return pts;
    }
    for (int i = 0; i + 1 < a.size(); i += 2)      // 扁平
        pts.append(QPointF(a[i].toDouble(), a[i + 1].toDouble()));
    return pts;
}

void MapView::enterRegion(const QString& id)
{
    const QJsonValue arr = provJson_.value(id);
    if (arr.isUndefined()) return;
    curRegionId_ = id;
    curRegionName_ = QString();
    for (const RegionInfo& ri : regions_)
        if (ri.id == id) { curRegionName_ = ri.name; break; }
    curProvinces_.clear();
    for (const QJsonValue& v : arr.toArray()) {
        const QJsonArray e = v.toArray();
        if (e.size() < 4) continue;
        ProvinceInfo pv;
        pv.name = e[0].toString();
        pv.ring.pts = parseProvinceRing(e[1]);
        pv.ctr = QPointF(e[2].toDouble(), e[3].toDouble());
        pv.zh = zhOf(pv.name);
        computeProvinceBBox(pv);
        if (pv.ring.pts.size() >= 3) curProvinces_.append(pv);
    }
    hasCurProvince_ = false;
    level_ = Region;
    update();
    emit breadcrumbChanged(breadcrumb());
}

void MapView::enterProvince(const QString& name)
{
    for (const ProvinceInfo& pv : curProvinces_) {
        if (pv.name == name) {
            curProvince_ = pv;
            hasCurProvince_ = true;
            level_ = Province;
            update();
            emit breadcrumbChanged(breadcrumb());
            return;
        }
    }
}

void MapView::goUp()
{
    if (level_ == Province) { level_ = Region; hasCurProvince_ = false; }
    else if (level_ == Region) { level_ = World; curProvinces_.clear(); }
    update();
    emit breadcrumbChanged(breadcrumb());
}

QString MapView::breadcrumb() const
{
    if (level_ == World)  return QString("世界");
    if (level_ == Region) return QString("世界 › %1").arg(curRegionName_.isEmpty() ? curRegionId_ : curRegionName_);
    return QString("世界 › %1 › %2")
        .arg(curRegionName_.isEmpty() ? curRegionId_ : curRegionName_)
        .arg(curProvince_.zh.isEmpty() ? curProvince_.name : curProvince_.zh);
}
