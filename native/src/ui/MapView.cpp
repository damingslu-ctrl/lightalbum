#include "MapView.h"
#include "../core/MotionPref.h"
#include <QPainter>
#include <QFile>
#include <QImage>
#include "HiDpi.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMouseEvent>
#include <QWheelEvent>
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

// 航线「流动虚线动画」vs「并入静态层」阈值：超过后逐帧描数千条弧不可读也过重
static constexpr int kArcBakeThreshold = 256;

// 偶奇规则点在多边形内（与 QPainterPath OddEvenFill 语义一致；用于国家轮廓命中）
static bool pip(const QVector<QPointF>& r, const QPointF& p)
{
    bool inside = false;
    const int n = r.size();
    for (int i = 0, j = n - 1; i < n; j = i++) {
        const QPointF& a = r[i];
        const QPointF& b = r[j];
        if (((a.y() > p.y()) != (b.y() > p.y())) &&
            (p.x() < (b.x() - a.x()) * (p.y() - a.y()) / (b.y() - a.y()) + a.x()))
            inside = !inside;
    }
    return inside;
}

// 点在世界要素内（外环内 且 不在任何湖泊内环内）
static bool pointInFeature(const QPointF& p, const WorldFeature& wf)
{
    if (!pip(wf.exterior, p)) return false;
    for (const QVector<QPointF>& h : wf.holes)
        if (pip(h, p)) return false;
    return true;
}

// 世界层逆变换：widget 坐标 → 预投影（与 renderWorldInto 的 worldPix_ 平铺一致，
// 含漂移回绕、缩放与纵向偏移）
// 绘制：screenX = (pre.x - wrapX(drift))*scale + k*MAP_W*scale
//       screenY = dst.y + pre.y*scale - offY
static QPointF widgetToPre(const QPointF& w, const QRectF& dst, double scale,
                           double drift, double offY)
{
    double px = w.x() / scale + drift;
    px = fmod(px, MapView::MAP_W);
    if (px < 0) px += MapView::MAP_W;
    return QPointF(px, (w.y() - dst.y() + offY) / scale);
}

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

    // 爆炸弹层动画时钟：动画期 16ms 驱动重绘，播完自停（终态静态保持）
    connect(&burstTimer_, &QTimer::timeout, this, [this] {
        if (!burstActive_) { burstTimer_.stop(); return; }
        update();
        if (burstClock_.elapsed() > 1600) burstTimer_.stop();
    });

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

    // 国家名（与 world 要素逐一对齐）：无专辑国家点击下钻 + 悬停提示
    worldNames_.clear();
    const QJsonArray wnames = root.value("worldNames").toArray();
    worldNames_.reserve(wnames.size());
    for (const QJsonValue& v : wnames) worldNames_.append(v.toString());

    // 国家轮廓命中：一次性标记「锚点落入其中的世界要素」→ 点国家任意处即可下钻
    assignFeatureRegions();

    provJson_ = root.value("provinces").toObject();
    provZh_   = root.value("provZh").toObject();

    buildWorldPixmap();

    if (!qEnvironmentVariableIsEmpty("LA_MAP_SMOKE")) {
        int tot = 0;
        for (auto it = provJson_.constBegin(); it != provJson_.constEnd(); ++it)
            tot += it.value().toArray().size();
        int worldRings = 0;
        for (const WorldFeature& wf : worldFeatures_) worldRings += 1 + wf.holes.size();
        std::fprintf(stderr, "LA_MAP_SMOKE rings=%d regions=%d provinces=%d zh=%d names=%d\n",
                     worldRings, regions_.size(), tot, provZh_.size(), worldNames_.size());
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
    // 3x 超采样烘焙：逻辑坐标仍 1000×500，device 像素 3000×1500 ——
    // 滚轮缩放到 4x 时依旧有充足源像素（18MB 常驻，桌面端可接受）
    constexpr double PIX = 3.0;
    const int W = int(MAP_W), H = int(MAP_H);
    worldPix_ = QPixmap(int(MAP_W * PIX), int(MAP_H * PIX));
    worldPix_.fill(Qt::transparent);
    QPainter p(&worldPix_);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.scale(PIX, PIX);   // 之后全部按 1000×500 逻辑坐标绘制（线宽随变换同步放大）

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
    densDirty_ = true;          // 密度分级缓存失效
    arcTargetsDirty_ = true;    // 航线目标集缓存失效
    invalidateViewCache();      // 下钻静态层含足迹点 → 失效
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
    arcTargetsDirty_ = true;    // 航线目标集缓存失效
    buildWorldPixmap();
    update();
}

// 航线目标集 = GPS 足迹点 ∪ 有作品大区的中心点（预投影空间，去重）
// 结果缓存（流畅性）：此前每帧复制 6400+ 点向量 + O(n) 去重
QVector<QPointF> MapView::arcTargets() const
{
    if (!arcTargetsDirty_) return arcTargetsCache_;
    QVector<QPointF> ts = gpsPts_;
    for (const QString& id : arcRegionIds_) {
        for (const RegionInfo& ri : regions_) {
            if (ri.id == id) {
                if (!ts.contains(ri.proj)) ts.append(ri.proj);
                break;
            }
        }
    }
    arcTargetsCache_ = ts;
    arcTargetsDirty_ = false;
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

// 纵向视口偏移按当前 zoom 夹取（zoom=1 时恒 0）
double MapView::worldOffYClamped() const
{
    const QRectF dst = rect().adjusted(8, 8, -8, -8);
    const double scale = dst.height() / MAP_H * worldZoom_;
    return qBound(0.0, worldOffY_, qMax(0.0, MAP_H * scale - dst.height()));
}

// 默认视口中心：中国大区锚点（数据缺失时回退 105°E）
double MapView::defaultCenterX() const
{
    double cx = eqX(105.0);
    for (const RegionInfo& ri : regions_)
        if (ri.id == QLatin1String("china")) { cx = ri.proj.x(); break; }
    return cx;
}

// 回到默认视角：缩放归 1、纵向偏移归 0、中国居中（平移/缩放后防「迷路」）
void MapView::resetWorldView()
{
    worldZoom_ = 1.0;
    centerWorldOn(defaultCenterX(), 150.0);
    worldOffY_ = 0.0;   // zoom=1 时夹取后本就为 0，显式归零避免与断言/调试读数分歧
    update();
}

// 把预投影点 (preX,preY) 置于视口中心：调整横向漂移与纵向偏移（缩放前后均可用）
void MapView::centerWorldOn(double preX, double preY)
{
    const QRectF dst = rect().adjusted(8, 8, -8, -8);
    const double scale = dst.height() / MAP_H * worldZoom_;
    driftPx_  = wrapX(preX - dst.center().x() / scale);
    worldOffY_ = preY * scale - dst.height() / 2.0;
}

void MapView::renderWorldInto(QPainter& p, const QRectF& dst) const
{
    // 离屏世界按 dst 高度 × zoom 缩放，宽度=2*高度（等距圆柱 2:1）；zoom>1 时纵向可平移
    const double scale = dst.height() / MAP_H * worldZoom_;
    const double mapWpx = MAP_W * scale, mapHpx = MAP_H * scale;
    const double offY = worldOffYClamped();
    const QRectF src(0, 0, worldPix_.width(), worldPix_.height());
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);   // 3x 烘焙 → 屏幕缩放采样，消除漂移边缘闪烁
    // 横向环绕：绘制足够副本覆盖 dst 宽度（屏外剔除）
    double off = -wrapX(driftPx_) * scale;
    for (double x0 = off; x0 < dst.right() + mapWpx; x0 += mapWpx) {
        if (x0 + mapWpx < dst.left() || x0 > dst.right()) continue;
        const QRectF d(x0, dst.y() - offY, mapWpx, mapHpx);
        if (d.bottom() < dst.y() || d.top() > dst.bottom()) continue;
        p.drawPixmap(d, worldPix_, src);
    }
    // 大区旅行锚点（金色点 + 名）
    QColor gold(0xc9, 0xa2, 0x4b);
    for (const RegionInfo& ri : regions_) {
        const QPointF w = preToWidget(ri.proj, dst, scale, -driftPx_);
        // 环绕后的实际绘制 x（含纵向偏移）
        double wx = w.x();
        const double span = mapWpx;
        wx = dst.x() + fmod(wx - dst.x(), span);
        if (wx < dst.x()) wx += span;
        const QPointF pos(wx, w.y() - offY);
        if (pos.y() < dst.y() - 20 || pos.y() > dst.bottom() + 20) continue;
        p.setPen(QPen(gold, 1.4));
        p.setBrush(gold);
        p.drawEllipse(pos, 3.2, 3.2);
        p.setBrush(Qt::NoBrush);
        drawLabel(p, pos + QPointF(6, 4), ri.name, QColor(0xe8, 0xe2, 0xd4));
    }
}

// 下钻视图变换「唯一真源」：s = 拟合 scale × viewZoom_，原点含 viewPan_ 平移。
// 绘制（省面/国界/网格/足迹/航线/爆炸）与命中（省点击/国家点击/卡片/空白）
// 全部走它 —— 否则放大后「看得见却点不着」（铁律：绘制与命中同源）。
// viewZoom_=1 且无平移时与 fitRect().topLeft() 逐像素等价。
QPointF MapView::viewOrigin(const QRectF& bbox, const QRectF& dst, double& s) const
{
    s = fitScale(bbox, dst) * viewZoom_;
    const QPointF bc((bbox.left() + bbox.right()) * 0.5, (bbox.top() + bbox.bottom()) * 0.5);
    return dst.center() + viewPan_ - QPointF(bc.x() * s, bc.y() * s);
}

// 平移夹取：保证内容至少部分可见（防止拖到完全空白）
void MapView::clampViewPan(const QRectF& bbox, const QRectF& dst, double s)
{
    const double limX = bbox.width() * s * 0.5 + dst.width() * 0.5;
    const double limY = bbox.height() * s * 0.5 + dst.height() * 0.5;
    viewPan_.setX(qBound(-limX, viewPan_.x(), limX));
    viewPan_.setY(qBound(-limY, viewPan_.y(), limY));
}

// 进入任一下钻视图：重置缩放/平移
void MapView::resetViewZoom()
{
    viewZoom_ = 1.0;
    viewPan_ = QPointF();
}

void MapView::renderRegion(QPainter& p, const QRectF& dst) const
{
    renderRegionStatic(p, dst);
    drawArcs(p, curViewBbox_, dst);   // P16.4.2 金色航线
}

// 大区层静态部分：网格 + 省面 + 省名 + 足迹点（航线动态，单独画）。
// 供「静态层缓存」一次性栅格化，避免每帧重建 34 条省路径。
void MapView::renderRegionStatic(QPainter& p, const QRectF& dst) const
{
    if (curProvinces_.isEmpty()) {
        if (countryFeature_ >= 0) renderCountryOnly(p, dst);
        return;
    }
    QRectF bbox;
    bool first = true;
    for (const ProvinceInfo& pv : curProvinces_) {
        if (first) { bbox = pv.bbox; first = false; }
        else bbox |= pv.bbox;
    }
    double scale = 1;
    const QPointF vo = viewOrigin(bbox, dst, scale);
    const QColor land(0x2f, 0x46, 0x58);
    const QColor border(0x4a, 0x66, 0x78);
    const QColor hl(0xc9, 0xa2, 0x4b);
    drawGraticule(p, bbox, dst);   // 很精细：经纬网格
    for (const ProvinceInfo& pv : curProvinces_) {
        QPainterPath path;
        const QPointF o = vo;
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
    drawGpsDots(p, bbox, dst);   // P16.4
}

// 国家视图：国界轮廓（含湖泊镂空）+ 经纬网格 + 国名 halo + 该国 GPS 足迹/航线
// 配色沿用省视图（0x35,0x50,0x63 填充 + 金描边），与整体墨韵一致
void MapView::renderCountryOnly(QPainter& p, const QRectF& dst) const
{
    if (countryFeature_ < 0 || countryFeature_ >= worldFeatures_.size()) return;
    const WorldFeature& wf = worldFeatures_[countryFeature_];
    const QRectF& bbox = featureBbox_[countryFeature_];
    double scale = 1;
    const QPointF o = viewOrigin(bbox, dst, scale);
    QPainterPath path;
    path.moveTo(o + wf.exterior.first() * scale);
    for (int i = 1; i < wf.exterior.size(); ++i)
        path.lineTo(o + wf.exterior[i] * scale);
    path.closeSubpath();
    for (const QVector<QPointF>& h : wf.holes) {
        QPainterPath hp;
        hp.moveTo(o + h.first() * scale);
        for (int i = 1; i < h.size(); ++i)
            hp.lineTo(o + h[i] * scale);
        hp.closeSubpath();
        path.addPath(hp);   // 同一路径多子环 → 默认 OddEven 镂空
    }
    drawGraticule(p, bbox, dst);
    p.fillPath(path, QColor(0x35, 0x50, 0x63));
    p.setPen(QPen(QColor(0xc9, 0xa2, 0x4b), 1.8));
    p.drawPath(path);
    const QPointF c = o + QPointF((bbox.left() + bbox.right()) * 0.5 * scale,
                                  (bbox.top() + bbox.bottom()) * 0.5 * scale);
    drawLabel(p, c, curRegionName_, QColor(0xe8, 0xe2, 0xd4));
    drawGpsDots(p, bbox, dst);
}

void MapView::renderProvince(QPainter& p, const QRectF& dst) const
{
    renderProvinceStatic(p, dst);
    drawArcs(p, curProvince_.bbox, dst);      // P16.4.2 金色航线
}

// 省层静态部分：轮廓 + 网格 + 省名 + 足迹点（航线动态，单独画）
void MapView::renderProvinceStatic(QPainter& p, const QRectF& dst) const
{
    if (!hasCurProvince_) return;
    double scale = 1;
    const QPointF o = viewOrigin(curProvince_.bbox, dst, scale);
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
    drawGpsDots(p, curProvince_.bbox, dst);   // P16.4
}

void MapView::drawGpsDots(QPainter& p, const QRectF& bbox, const QRectF& dst) const
{
    if (gpsPts_.isEmpty()) return;
    double scale = 1;
    const QPointF o = viewOrigin(bbox, dst, scale);
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
    const QPointF o = viewOrigin(bbox, dst, scale);
    const QPointF h(o.x() + eqX(homeLon_) * scale, o.y() + eqY(homeLat_) * scale);
    p.save();
    p.setClipRect(dst);
    // 流畅性：全部弧线合并为一条路径、端点光点合并为一条路径——
    // 数千条逐条 drawPath（含每条建路径/切笔）曾是下钻视图每帧最大开销
    const bool flow = !reduceMotion_;
    QPainterPath arcs;
    QPainterPath glowDots;
    for (const QPointF& pt : targets) {
        if (pt.x() < bbox.left() - 2 || pt.x() > bbox.right() + 2 ||
            pt.y() < bbox.top() - 2 || pt.y() > bbox.bottom() + 2) continue;
        const QPointF t(o.x() + pt.x() * scale, o.y() + pt.y() * scale);
        const double dx = t.x() - h.x(), dy = t.y() - h.y();
        const double len = std::sqrt(dx * dx + dy * dy);
        const double lift = std::min(120.0, len * 0.25);
        arcs.moveTo(h);
        arcs.quadTo(QPointF((h.x() + t.x()) / 2.0, (h.y() + t.y()) / 2.0 - lift), t);
        glowDots.addEllipse(t, 2.2, 2.2);   // 端点光点（目标 = 拍摄地）
    }
    // 很精细：流动金色虚线（reduceMotion 时退化为实线）——整条路径一次描边
    p.setBrush(Qt::NoBrush);
    QPen pen(QColor(0xd9, 0xa9, 0x4e, 150), 1.2);
    if (flow) { pen.setStyle(Qt::DashLine); pen.setDashPattern({6.0, 6.0}); pen.setDashOffset(-arcDashOffset_); }
    p.setPen(pen);
    p.drawPath(arcs);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0xd9, 0xa9, 0x4e, 210));
    p.drawPath(glowDots);
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
    setGpsPoints({
        QPointF(eqX(116.40), eqY(39.90)),
        QPointF(eqX(121.47), eqY(31.23)),
        QPointF(eqX(113.26), eqY(23.13)),
        QPointF(eqX(104.07), eqY(30.57)),
        QPointF(eqX(87.62),  eqY(43.82)),
    });
    arcRegionIds_.clear();
    buildWorldPixmap();

    const int W = int(MAP_W), H = int(MAP_H);
    QImage img(W, H, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    {
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        // worldPix_ 为 3x 超采样（3000×1500），缩放绘制到逻辑 1000×500
        p.drawPixmap(QRectF(0, 0, W, H), worldPix_, worldPix_.rect());
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

// 国家视图像素自测：按国家名 enterCountry → 离屏渲染国界轮廓 + 石板色统计
// （颜色校验铁律：国界轮廓真实绘制必须实测）
void MapView::qaShootCountry(const QString& path, const QString& name)
{
    gpsPts_.clear();
    gpsPaths_.clear();
    arcRegionIds_.clear();
    int fi = -1;
    for (int i = 0; i < worldNames_.size(); ++i)
        if (worldNames_[i] == name) { fi = i; break; }
    if (fi < 0) {
        std::fprintf(stderr, "LA_MAP_COUNTRY_QA name=%s NOT_FOUND\n", qPrintable(name));
        std::fflush(stderr);
        return;
    }
    enterCountry(fi);
    const int W = int(MAP_W), H = int(MAP_H);
    QImage img(W, H, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    {
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF dst = QRectF(8, 8, W - 16, H - 16);
        renderRegion(p, dst);
    }
    int slate = 0;
    const uchar* bits = img.constBits();
    const int bpl = img.bytesPerLine();
    for (int y = 0; y < H; ++y) {
        const uchar* row = bits + y * bpl;
        for (int x = 0; x < W; ++x) {
            const uchar* px = row + x * 4;
            const int b = px[0], g = px[1], r = px[2];
            if (std::abs(r - 53) + std::abs(g - 80) + std::abs(b - 99) < 42) ++slate;
        }
    }
    if (!path.isEmpty()) img.save(path);
    std::fprintf(stderr, "LA_MAP_COUNTRY_QA name=%s slate=%d paths=%d png=%s\n",
                 qPrintable(name), slate, countryPaths_.size(), qPrintable(path));
    std::fflush(stderr);
}

// 下钻区/省视图像素自测：强制 enterRegion(id) 后离屏渲染该大区省视图并统计省面像素。
// 用于 CI/offscreen 验证「中国 34 省 DataV 几何已落地」「省面真实绘制」（颜色校验铁律：实测不许猜）。
void MapView::qaShootRegion(const QString& path, const QString& id)
{
    // 确定性合成足迹点（北京/上海/广州/成都/乌鲁木齐，均落在中国 bbox 内）→ 验证下钻视图航线/足迹
    setGpsPoints({
        QPointF(eqX(116.40), eqY(39.90)),
        QPointF(eqX(121.47), eqY(31.23)),
        QPointF(eqX(113.26), eqY(23.13)),
        QPointF(eqX(104.07), eqY(30.57)),
        QPointF(eqX(87.62),  eqY(43.82)),
    });
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
    const QRectF dst = rect().adjusted(8, 8, -8, -8);
    const double scale = dst.height() / MAP_H;
    const double span = MAP_W * scale;
    const RegionInfo* china = nullptr;
    for (const RegionInfo& ri : regions_)
        if (ri.id == QLatin1String("china")) { china = &ri; break; }
    if (!china && !regions_.isEmpty()) china = &regions_.first();

    auto sendClick = [this](const QPointF& pos) {
        QMouseEvent press(QEvent::MouseButtonPress, pos,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(this, &press);
    };
    auto sendTap = [this](const QPointF& pos) {   // 完整点击：按下 + 原地松开
        QMouseEvent press(QEvent::MouseButtonPress, pos,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent rel(QEvent::MouseButtonRelease, pos,
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(this, &press);
        QCoreApplication::sendEvent(this, &rel);
    };

    // ① 世界层·国家轮廓命中：预投影 (乌鲁木齐 87.62E,43.82E)（启动时漂移≈0 且可见区内）
    QString countryResult = QStringLiteral("skip");
    {
        const QPointF pre(eqX(87.62), eqY(43.82));
        const double sx = (pre.x() - wrapX(driftPx_)) * scale;
        double wx = dst.x() + fmod(sx - dst.x(), span);
        if (wx < dst.x()) wx += span;
        sendClick(QPointF(wx, dst.y() + pre.y() * scale));
        countryResult = (level_ == Region)
            ? QStringLiteral("OK") : QStringLiteral("FAIL(level=%1)").arg(int(level_));
    }
    // ② 世界层·锚点命中（回退路径）
    QString worldResult = QStringLiteral("skip");
    if (china) {
        goUp();   // 回到世界层
        const QPointF w = preToWidget(china->proj, dst, scale, -driftPx_);
        double wx = dst.x() + fmod(w.x() - dst.x(), span);
        if (wx < dst.x()) wx += span;
        sendClick(QPointF(wx, w.y()));
        worldResult = (level_ == Region)
            ? QStringLiteral("OK") : QStringLiteral("FAIL(level=%1)").arg(int(level_));
    }
    // ③ 大区层省面 → 省 + 爆炸弹层 + 卡片灯箱（MapPage/MainWindow 已连接）
    //    库依赖：逐省尝试，取第一个「有照片」的省做爆炸/卡片断言。
    QString provResult = QStringLiteral("skip");
    QString burstResult = QStringLiteral("skip");
    QString cardResult = QStringLiteral("skip");
    QString blankResult = QStringLiteral("skip");
    enterRegion(QStringLiteral("china"));
    if (!curProvinces_.isEmpty()) {
        const QRectF dstP = rect().adjusted(8, 8, -8, -8);
        for (const ProvinceInfo& pv : curProvinces_) {
            enterProvince(pv.name);
            QRectF bbox; bool first = true;
            for (const ProvinceInfo& q : curProvinces_) {
                if (first) { bbox = q.bbox; first = false; } else bbox |= q.bbox;
            }
            double sc = 1;
            const QPointF vo = viewOrigin(bbox, dstP, sc);
            provResult = QStringLiteral("OK");
            burstActive_ = false;                 // 去陈旧状态，断言只反映本次点击
            sendClick(vo + pv.ctr * sc);   // 省面点击 → 爆炸
            if (!burstActive_) continue;          // 未触发（点在空白被回退等）→ 下一省
            burstResult = QStringLiteral("OK(thumbs=%1)").arg(burstThumbs_.size());
            if (!burstPaths_.isEmpty()) {
                bool opened = false;
                const QMetaObject::Connection oc =
                    connect(this, &MapView::openPhotoRequested,
                            [&opened](const QString&) { opened = true; });
                sendClick(burstCardRect(0, dstP).center());   // 卡片点击 → 灯箱
                disconnect(oc);
                cardResult = opened ? QStringLiteral("OK") : QStringLiteral("FAIL");
            }
            // 空白点击 → 返回上一级（省 → 大区；Region 层走同一 goUp 路径）
            const int lvlBefore = int(level_);
            sendTap(QPointF(rect().left() + 3.0, rect().top() + 3.0));
            blankResult = (int(level_) == lvlBefore - 1)
                ? QStringLiteral("OK") : QStringLiteral("FAIL(%1->%2)").arg(lvlBefore).arg(int(level_));
            break;
        }
    }
    // ④ 滚轮缩放一致性：放大两档 → 居中乌鲁木齐 → 国家轮廓点击仍命中
    //    （验证缩放后 render/命中两套变换仍逐像素一致）
    QString zoomResult = QStringLiteral("skip");
    while (level_ != World) goUp();
    worldZoom_ = 1.0;
    worldOffY_ = 0.0;
    for (int i = 0; i < 2; ++i) {
        const QPointF c0 = rect().center();
        QWheelEvent we(c0, c0, QPoint(), QPoint(0, 120),
                       Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(this, &we);
    }
    centerWorldOn(eqX(87.62), eqY(43.82));   // 乌鲁木齐置于视口中心
    sendClick(rect().center());
    zoomResult = (level_ == Region)
        ? QStringLiteral("OK(zoom=%1)").arg(worldZoom_, 0, 'f', 2)
        : QStringLiteral("FAIL(zoom=%1 level=%2)").arg(worldZoom_, 0, 'f', 2).arg(int(level_));

    // ⑤ 十大区国家轮廓全部可点：逐国居中 → 点屏幕中心 → 应进入大区
    //    （居中消解"美洲国家初始在屏外"——它们本来就可点，只是看不到）
    QString allResult = QStringLiteral("skip");
    while (level_ != World) goUp();
    {
        int okN = 0;
        QStringList bad;
        // zoom=1 纵向不可平移（全图可见）→ 横向居中 + 点击锚点的实际屏幕位置
        worldZoom_ = 1.0;
        worldOffY_ = 0.0;
        const double sc1 = dst.height() / MAP_H;
        const double span1 = MAP_W * sc1;
        for (const RegionInfo& ri : regions_) {
            driftPx_ = wrapX(ri.proj.x() - dst.center().x() / sc1);
            worldOffY_ = 0.0;
            const QPointF pos(rect().center().x(), dst.y() + ri.proj.y() * sc1);
            sendClick(pos);
            if (level_ == Region) ++okN;
            else bad.append(ri.id);
            while (level_ != World) goUp();
        }
        allResult = (okN == regions_.size())
            ? QStringLiteral("OK(%1/%2)").arg(okN).arg(regions_.size())
            : QStringLiteral("FAIL(%1/%2 bad:%3)").arg(okN).arg(regions_.size()).arg(bad.join(QLatin1Char(';')));
    }

    // ⑥ 无专辑国家 → 国家视图 + 爆炸：逐个亚洲候选国找第一个「有 GPS 作品」的，
    //    点其照片光点的屏幕位置 → 应进入国家视图并爆炸（thumbs>0）
    QString ctryResult = QStringLiteral("skip");
    QString cburstResult = QStringLiteral("skip");
    while (level_ != World) goUp();
    worldZoom_ = 1.0;
    worldOffY_ = 0.0;
    {
        static const char* kCands[] = {
            "Thailand", "India", "Vietnam", "Indonesia", "South Korea", "Malaysia",
            "Philippines", "Mongolia", "Myanmar", "Nepal", "Kazakhstan", "Laos",
            "Cambodia", "Sri Lanka", "Pakistan", "North Korea", "Bhutan", "Bangladesh",
        };
        int cfi = -1;
        for (const char* cn : kCands) {
            const QString qn = QString::fromLatin1(cn);
            int fi = -1;
            for (int i = 0; i < worldNames_.size(); ++i)
                if (worldNames_[i] == qn) { fi = i; break; }
            if (fi < 0) continue;
            enterCountry(fi);
            if (!countryPaths_.isEmpty()) { cfi = fi; break; }
            while (level_ != World) goUp();
        }
        if (cfi >= 0) {
            ctryResult = QStringLiteral("OK(%1 paths=%2)")
                             .arg(curRegionName_, QString::number(countryPaths_.size()));
            // 点击第一个匹配照片光点的屏幕位置（pip 已保证在国界内）
            const double sc2 = dst.height() / MAP_H * worldZoom_;
            const double span2 = MAP_W * sc2;
            const QPointF& pt0 = countryPts_.first();
            double sx = (pt0.x() - wrapX(driftPx_)) * sc2;
            sx = dst.x() + fmod(sx - dst.x(), span2);
            if (sx < dst.x()) sx += span2;
            sendClick(QPointF(sx, dst.y() + pt0.y() * sc2 - worldOffYClamped()));
            cburstResult = (burstActive_ && !burstThumbs_.isEmpty())
                ? QStringLiteral("OK(thumbs=%1)").arg(burstThumbs_.size())
                : QStringLiteral("FAIL(burst=%1 thumbs=%2)")
                      .arg(burstActive_ ? 1 : 0).arg(burstThumbs_.size());
        } else {
            ctryResult = QStringLiteral("OK(库内无亚洲非专辑 GPS 作品，跳过)");
        }
    }

    // ⑦ 回到中国（resetWorldView）：扰动后重置 → zoom=1 / offY=0 / 中国锚点回到视口中心
    QString resetResult = QStringLiteral("skip");
    while (level_ != World) goUp();
    {
        worldZoom_ = 2.5;
        worldOffY_ = 60.0;
        driftPx_ = 3.0;
        resetWorldView();
        const double sc3 = dst.height() / MAP_H;
        const double preCx = wrapX(dst.center().x() / sc3 + wrapX(driftPx_));
        const double d = fmod(wrapX(preCx - defaultCenterX()) + MAP_W * 1.5, MAP_W) - MAP_W * 0.5;
        resetResult = (worldZoom_ == 1.0 && worldOffYClamped() == 0.0 && std::abs(d) < 1.5)
            ? QStringLiteral("OK")
            : QStringLiteral("FAIL(zoom=%1 dx=%2)").arg(worldZoom_, 0, 'f', 2).arg(std::abs(d), 0, 'f', 2);
    }

    // ⑧ 大区层（专辑国家）爆炸：startBurst 在大区层应被接受
    QString regBurstResult = QStringLiteral("skip");
    while (level_ != World) goUp();
    enterRegion(QStringLiteral("china"));
    {
        QVector<QPixmap> th;
        QPixmap pm(96, 72);
        pm.fill(QColor(0x5f, 0xdc, 0xa6));
        th.append(pm);
        burstActive_ = false;
        startBurst(th, QStringList(QStringLiteral("synthetic/region.jpg")));
        regBurstResult = burstActive_ ? QStringLiteral("OK") : QStringLiteral("FAIL");
    }

    // ⑨ 下钻视图滚轮缩放：放大后命中仍与绘制一致（viewOrigin 为唯一变换真源）
    QString vzoomResult = QStringLiteral("skip");
    while (level_ != World) goUp();
    enterRegion(QStringLiteral("china"));
    {
        const QPointF c0 = rect().center();
        for (int i = 0; i < 2; ++i) {
            QWheelEvent we(c0, c0, QPoint(), QPoint(0, 120),
                           Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(this, &we);
        }
        // 取某省中心点，用当前 viewOrigin 正变换出屏幕位置，点击应进入该省
        const ProvinceInfo* pv0 = &curProvinces_.first();
        const double zBefore = viewZoom_;   // 进入省视图会重置缩放 → 点击前取值
        double sv = 1;
        const QPointF ov = viewOrigin(curViewBbox_, rect().adjusted(8, 8, -8, -8), sv);
        sendClick(ov + pv0->ctr * sv);
        vzoomResult = (level_ == Province && zBefore > 1.0)
            ? QStringLiteral("OK(zoom=%1)").arg(zBefore, 0, 'f', 2)
            : QStringLiteral("FAIL(zoom=%1 lvl=%2)").arg(zBefore, 0, 'f', 2).arg(int(level_));
    }

    std::fprintf(stderr, "LA_MAP_CLICK_QA country=%s anchor=%s province=%s burst=%s card=%s blank=%s zoom=%s all=%s ctry=%s cburst=%s reset=%s regBurst=%s vzoom=%s\n",
                 qPrintable(countryResult), qPrintable(worldResult),
                 qPrintable(provResult), qPrintable(burstResult),
                 qPrintable(cardResult), qPrintable(blankResult),
                 qPrintable(zoomResult), qPrintable(allResult),
                 qPrintable(ctryResult), qPrintable(cburstResult), qPrintable(resetResult),
                 qPrintable(regBurstResult),
                 qPrintable(vzoomResult));
    std::fflush(stderr);
}

// 世界层国家命中标记：锚点落入其外环（且不在湖泊内）的要素 → 对应大区下标。
// 同时预计算要素 bbox（显式 min/max），命中测试早退用。
void MapView::assignFeatureRegions()
{
    featureRegion_.assign(worldFeatures_.size(), -1);
    featureBbox_.clear();
    featureBbox_.reserve(worldFeatures_.size());
    for (int fi = 0; fi < worldFeatures_.size(); ++fi) {
        const WorldFeature& wf = worldFeatures_[fi];
        if (wf.exterior.isEmpty()) { featureBbox_.append(QRectF()); continue; }
        double lx = wf.exterior.first().x(), hx = lx;
        double ly = wf.exterior.first().y(), hy = ly;
        for (const QPointF& q : wf.exterior) {
            lx = qMin(lx, q.x()); hx = qMax(hx, q.x());
            ly = qMin(ly, q.y()); hy = qMax(hy, q.y());
        }
        featureBbox_.append(QRectF(lx, ly, hx - lx, hy - ly));
        for (int r = 0; r < regions_.size(); ++r) {
            const QPointF& ap = regions_[r].proj;
            if (!featureBbox_.last().contains(ap)) continue;
            if (pointInFeature(ap, wf)) { featureRegion_[fi] = r; break; }
        }
    }
}

// 省视图爆炸弹层：MapPage 注入缩略图后触发。从省中心逐张错峰飞出，
// 金色连线 + 圆角照片卡 + 冲击波双环；reduceMotion 直接呈现终态。
// paths 与 thumbs 一一对应（点击卡片 → openPhotoRequested）。
// 国家视图（Region 层 + countryFeature_）同样可播——焦点几何由 focusGeometry 决定。
void MapView::startBurst(const QVector<QPixmap>& thumbs, const QStringList& paths)
{
    // 省视图 / 国家视图（Region+国家要素）/ 大区视图（Region+省数据）均可播放
    const bool inProvince = (level_ == Province && hasCurProvince_);
    const bool inRegion   = (level_ == Region);   // 含专辑大区与国家视图
    if (!inProvince && !inRegion) return;
    burstThumbs_ = thumbs;
    burstPaths_ = paths;
    burstActive_ = true;
    burstClock_.start();
    if (!reduceMotion_) burstTimer_.start(16);
    update();
}

// 当前爆炸焦点：省视图=省 bbox/中心；国家视图=国 bbox/中心
void MapView::focusGeometry(QRectF& bbox, QPointF& ctr) const
{
    // 当前爆炸焦点：省视图=省 bbox/中心；国家视图=国 bbox/中心；大区视图=全区 bbox/中心
    if (level_ == Province && hasCurProvince_) {
        bbox = curProvince_.bbox;
        ctr = curProvince_.ctr;
    } else if (countryFeature_ >= 0 && curProvinces_.isEmpty() &&
               countryFeature_ < featureBbox_.size()) {
        bbox = featureBbox_[countryFeature_];
        ctr = QPointF((bbox.left() + bbox.right()) * 0.5, (bbox.top() + bbox.bottom()) * 0.5);
    } else {
        bbox = curViewBbox_;
        ctr = QPointF((bbox.left() + bbox.right()) * 0.5, (bbox.top() + bbox.bottom()) * 0.5);
    }
}

// 稳态卡片矩形：角度扇形布点 + 画布内钳制（DataV center 贴省边界如西宁，防飞出）。
// 绘制（动画终点）与命中测试共用，保证「画在哪就能点哪」。
QRectF MapView::burstCardRect(int i, const QRectF& dst) const
{
    const int n = burstThumbs_.size();
    if (i < 0 || i >= n) return QRectF();
    QRectF bbox; QPointF ctr;
    focusGeometry(bbox, ctr);
    double sc = 1;
    const QPointF c = viewOrigin(bbox, dst, sc) + ctr * sc;
    const double rx = dst.width() * 0.34, ry = dst.height() * 0.32;
    const double tw = 96.0, th = 72.0;
    const double a = qDegreesToRadians(-90.0 + i * 360.0 / n);
    const QPointF raw = c + QPointF(rx * std::cos(a), ry * std::sin(a));
    const QPointF pos(qBound(dst.left() + tw / 2 + 6.0, raw.x(), dst.right() - tw / 2 - 6.0),
                      qBound(dst.top() + th / 2 + 6.0, raw.y(), dst.bottom() - th / 2 - 6.0));
    return QRectF(pos.x() - tw / 2, pos.y() - th / 2, tw, th);
}

void MapView::drawBurst(QPainter& p, const QRectF& dst) const
{
    if (!burstActive_) return;
    const qint64 t0 = burstTOverride_ >= 0 ? burstTOverride_
                     : (reduceMotion_ ? 99999 : qint64(burstClock_.elapsed()));
    // 中心 = 当前焦点中心（省视图=省中心；国家视图=国 bbox 中心），不随点击点抖动
    QRectF bbox; QPointF ctr;
    focusGeometry(bbox, ctr);
    double sc = 1;
    const QPointF c = viewOrigin(bbox, dst, sc) + ctr * sc;

    // —— 冲击波双环（金 + 翡翠）：扩散 + 淡出，爆炸感核心 ——
    auto ring = [&](qint64 delay, double maxR, const QColor& base, double w) {
        const double lt = qBound(0.0, double(t0 - delay) / 650.0, 1.0);
        if (lt <= 0.0 || lt >= 1.0) return;
        const double e = 1.0 - std::pow(1.0 - lt, 3.0);
        QColor col = base;
        col.setAlphaF(base.alphaF() * (1.0 - lt));
        p.setPen(QPen(col, w));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(c, 10.0 + maxR * e, 10.0 + maxR * e);
    };
    ring(0,  dst.width() * 0.30, QColor(0xd9, 0xa9, 0x4e, 180), 2.2);
    ring(90, dst.width() * 0.22, QColor(0x5f, 0xdc, 0xa6, 150), 1.6);

    // —— 金色连线 + 圆角照片卡：逐张错峰飞出（70ms/张，520ms OutCubic）——
    const int n = burstThumbs_.size();
    const double tw = 96.0, th = 72.0;
    for (int i = 0; i < n; ++i) {
        const QPointF tgt = burstCardRect(i, dst).center();   // 钳制后的稳态落点
        const double lt = qBound(0.0, double(t0 - i * 70) / 520.0, 1.0);
        if (lt <= 0.0) continue;
        const double e = 1.0 - std::pow(1.0 - lt, 3.0);
        const QPointF pos = c + (tgt - c) * e;
        // 连线（金色，中心 → 卡片）
        QColor lc(0xd9, 0xa9, 0x4e);
        lc.setAlphaF(0.75 * e);
        p.setPen(QPen(lc, 1.2));
        p.setBrush(Qt::NoBrush);
        p.drawLine(c, pos);
        p.setPen(Qt::NoPen);
        p.setBrush(lc);
        p.drawEllipse(pos, 2.2, 2.2);
        // 照片卡（圆角裁剪 + 中心裁剪铺满 + 金描边）
        const double cardS = 0.6 + 0.4 * e;
        const QRectF cr(pos.x() - tw * cardS / 2, pos.y() - th * cardS / 2,
                        tw * cardS, th * cardS);
        const QPixmap& pm = burstThumbs_[i];
        p.save();
        QPainterPath clip;
        clip.addRoundedRect(cr, 6, 6);
        p.setClipPath(clip);
        p.setOpacity(e);
        if (!pm.isNull()) {
            const double wantAr = tw / th;
            const double srcAr = double(pm.width()) / double(pm.height());
            QRectF s;
            if (srcAr > wantAr) {
                const double cw = pm.height() * wantAr;
                s = QRectF((pm.width() - cw) / 2.0, 0, cw, pm.height());
            } else {
                const double ch = pm.width() / wantAr;
                s = QRectF(0, (pm.height() - ch) / 2.0, pm.width(), ch);
            }
            p.drawPixmap(cr, pm, s);
        } else {
            p.fillRect(cr, QColor(0x14, 0x1c, 0x18));
        }
        p.restore();
        QPainterPath bd;
        bd.addRoundedRect(cr, 6, 6);
        QColor bc(0xd9, 0xa9, 0x4e);
        bc.setAlphaF(0.9 * e);
        p.setPen(QPen(bc, 1.4));
        p.setBrush(Qt::NoBrush);
        p.drawPath(bd);
    }
    // 中心爆发点（金环 + 实心芯，与世界层「我的位置」同语言）
    p.setPen(QPen(QColor(0xd9, 0xa9, 0x4e), 1.5));
    p.setBrush(QColor(0xf0, 0xd0, 0x8a));
    p.drawEllipse(c, 3.6, 3.6);
}

// 爆炸弹层像素自测：进入省视图 + 合成缩略图 → 离屏渲染终态并分色统计
// （颜色校验铁律：连线/卡片/冲击波是否落地必须实测）
void MapView::qaShootBurst(const QString& path)
{
    setGpsPoints({
        QPointF(eqX(116.40), eqY(39.90)), QPointF(eqX(121.47), eqY(31.23)),
        QPointF(eqX(113.26), eqY(23.13)), QPointF(eqX(104.07), eqY(30.57)),
        QPointF(eqX(87.62),  eqY(43.82)),
    });
    arcRegionIds_.clear();
    enterRegion(QStringLiteral("china"));
    const ProvinceInfo* pick = nullptr;
    for (const ProvinceInfo& pv : curProvinces_)
        if (pv.name == QStringLiteral("青海省")) { pick = &pv; break; }
    if (!pick && !curProvinces_.isEmpty()) pick = &curProvinces_.first();
    if (!pick) return;
    enterProvince(pick->name);
    // 合成缩略图：翡翠 / 金 / 奶油三张实心卡（paths 同步合成，卡片命中测试可跑）
    burstThumbs_.clear();
    burstPaths_.clear();
    const QColor cardCols[3] = { QColor(0x5f, 0xdc, 0xa6), QColor(0xd9, 0xa9, 0x4e),
                                 QColor(0xe8, 0xe2, 0xd4) };
    for (int i = 0; i < 3; ++i) {
        QPixmap pm(96, 72);
        pm.fill(cardCols[i]);
        burstThumbs_.append(pm);
        burstPaths_.append(QStringLiteral("synthetic/%1.jpg").arg(i));
    }
    burstActive_ = true;
    burstTOverride_ = 2000;   // 终态（连线/卡片驻留，冲击波已散）

    const int W = int(MAP_W), H = int(MAP_H);
    QImage img(W, H, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    {
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF dst = QRectF(8, 8, W - 16, H - 16);
        renderProvince(p, dst);
        drawBurst(p, dst);
    }
    int gold = 0, emerald = 0, cream = 0;
    const uchar* bits = img.constBits();
    const int bpl = img.bytesPerLine();
    for (int y = 0; y < H; ++y) {
        const uchar* row = bits + y * bpl;
        for (int x = 0; x < W; ++x) {
            const uchar* px = row + x * 4;
            const int b = px[0], g = px[1], r = px[2];
            if (r > 170 && g > 130 && b < 120) ++gold;                  // 金（线/描边/芯）
            else if (g > 150 && g > r + 30 && g > b + 10) ++emerald;    // 翡翠卡
            else if (r > 200 && g > 195 && b > 175) ++cream;            // 奶油卡
        }
    }
    if (!path.isEmpty()) img.save(path);
    std::fprintf(stderr, "LA_MAP_BURST_QA gold=%d emerald=%d cream=%d png=%s\n",
                 gold, emerald, cream, qPrintable(path));
    std::fflush(stderr);
}

// —— 很精细优化辅助 ——

// 预渲染翡翠足迹点精灵（一次），逐帧 blit，避免每帧重建径向梯度
void MapView::renderFootDotSprite()
{
    // 120px 高分辨率精灵：世界烘焙层在 3x 变换下按逻辑尺寸 blit 仍清晰；
    // 下钻层（无变换）绘制时为缩小采样，同样清晰
    const int D = 120;
    footDotPix_ = HiDpi::bakePixmap(QSize(D, D));
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
    const double draw = 3.5 * coreR * sizeFactor; // 精灵核(4px)→coreR 映射（3.5：光点再收小一档，世界层光晕直径 ~6-8px）
    const qreal saved = p.opacity();
    p.setOpacity(qMin(1.0, op));
    p.drawPixmap(QRectF(c.x() - draw / 2, c.y() - draw / 2, draw, draw),
                 footDotPix_, footDotPix_.rect());
    p.setOpacity(saved);
}

// 局部密度（网格计数，O(n)）：用于足迹点分级。
// 结果缓存（流畅性）：gpsPts_ 最多 6000+ 点，此前每帧重算 + QHash 分配。
QVector<int> MapView::computeDensity() const
{
    if (!densDirty_) return densCache_;
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
    densCache_ = out;
    densDirty_ = false;
    return out;
}

// 经纬网格（下钻视图）：预投影空间 → fitRect 变换
void MapView::drawGraticule(QPainter& p, const QRectF& bbox, const QRectF& dst) const
{
    double scale = 1;
    const QPointF o = viewOrigin(bbox, dst, scale);
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
    // 预渲染缓存（流畅性）：同 文本|颜色|字体 的 halo 只栅格化一次，逐帧 blit。
    // 世界层锚点/图例与下钻省名每帧文字栅格化曾是每帧主要开销之一。
    const QString key = QString::number(fill.rgba(), 16) + QLatin1Char('|')
                        + text + QLatin1Char('|') + p.font().key();
    QPixmap pix = labelPixCache_.value(key).pm;
    QPointF off;
    if (pix.isNull()) {
        QPainterPath path;
        path.addText(QPointF(0, 0), p.font(), text);
        const QRectF br = path.boundingRect();
        const qreal pad = 4.0;
        QPixmap pm(int(br.width()) + int(pad * 2) + 2, int(br.height()) + int(pad * 2) + 2);
        pm.fill(Qt::transparent);
        QPainter lp(&pm);
        lp.setRenderHint(QPainter::Antialiasing, true);
        lp.translate(-br.left() + pad, -br.top() + pad);
        lp.setPen(QPen(QColor(0x06, 0x0c, 0x10, 210), 2.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        lp.setBrush(Qt::NoBrush);
        lp.drawPath(path);
        lp.setPen(QPen(fill, 1));
        lp.setBrush(fill);
        lp.drawPath(path);
        lp.end();
        off = br.topLeft() - QPointF(pad, pad);
        labelPixCache_.insert(key, CacheEntry{ pm, off });
        pix = pm;
    } else {
        off = labelPixCache_.value(key).off;
    }    // pixmap(0,0) 对应场景坐标 pos+br.topLeft()-pad → 与原 addText(pos) 逐像素同位
    p.drawPixmap(pos + off, pix);
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
    paintMap(p);
}

// 绘制主体（paintEvent 与 LA_MAP_PERF 帧耗时自测共用同一调度）
void MapView::paintMap(QPainter& p)
{
    const QColor bg(0x0a, 0x10, 0x16);
    p.fillRect(rect(), bg);

    const QRectF dst = rect().adjusted(8, 8, -8, -8);
    if (level_ == World) {
        // 首帧初始化：中国居中（zoom=1 时纵向自然居中，无需额外偏移）
        if (!worldInited_ && width() > 100) {
            worldInited_ = true;
            centerWorldOn(defaultCenterX(), 150.0);
        }
        renderWorldInto(p, dst);
    }
    else {
        // 静态层缓存（流畅性核心）：网格/省面/国界/标签/足迹点每视图只栅格化一次，
        // 每帧仅 1 次 blit + 动态航线（流动虚线）+ 爆炸弹层。
        // 失效时机：进入/退出任一层级、窗口尺寸变化、足迹点更新。
        if (!viewPixValid_) {
            bakeViewPix(dst);
        }
        p.drawPixmap(0, 0, viewPix_);
        if (arcTargets().size() <= kArcBakeThreshold)
            drawArcs(p, curViewBbox_, dst);   // 少量航线：流动虚线动画
    }

    const bool burstView = (level_ == Province && hasCurProvince_) || (level_ == Region);
    if (burstView && burstActive_)
        drawBurst(p, dst);   // 爆炸弹层：金色连线 + 照片卡 + 冲击波（省视图/国家视图共用）

    if (level_ == World)
        drawLegend(p);   // 很精细：世界视图图例

    // 诚实足迹提示（GPS 数由 MapPage 经 setGpsCount 注入，绝不硬编码）
    p.setPen(QColor(0x8d, 0xa3, 0x96));
    const QString foot = (level_ == World)
        ? QCoreApplication::translate("LightAlbum", "足迹地图 · 10 大区 / 233 省 · 本库 GPS 足迹 %1（滚轮缩放 · 拖动平移）")
            .arg(gpsCount_)
        : QString();
    if (!foot.isEmpty())
        p.drawText(rect().adjusted(12, 0, 0, -10), Qt::AlignLeft | Qt::AlignBottom, foot);
    // 缩放倍数可见（>1 时才显示，避免常态噪音）
    if (level_ == World && worldZoom_ > 1.01) {
        p.setPen(QColor(0xd9, 0xa9, 0x4e));
        p.drawText(rect().adjusted(0, 0, -12, -10), Qt::AlignRight | Qt::AlignBottom,
                   QCoreApplication::translate("LightAlbum", "缩放 ×%1").arg(worldZoom_, 0, 'f', 2));
    }
    if (!hoverHint_.isEmpty()) {
        p.setPen(QColor(0xe8, 0xc8, 0x7a));
        p.drawText(rect().adjusted(12, 10, 0, 0), Qt::AlignLeft | Qt::AlignTop, hoverHint_);
    }
}

// F 高 DPR 渲染（方向 F 第一步）：下钻静态层 DPR 感知烘焙。
// 按设备像素比出图 + setDevicePixelRatio，绘制仍用逻辑坐标 → 缩放后清晰。
void MapView::bakeViewPix(const QRectF& dst)
{
    viewPix_ = HiDpi::bakePixmap(size());
    viewPix_.fill(Qt::transparent);
    QPainter vp(&viewPix_);
    vp.setRenderHint(QPainter::Antialiasing, true);
    const bool bakeArcs = arcTargets().size() > kArcBakeThreshold;
    if (level_ == Region)
        renderRegionStatic(vp, dst);
    else
        renderProvinceStatic(vp, dst);
    if (bakeArcs)
        drawArcs(vp, curViewBbox_, dst);   // 航线数千条时流动动画不可读 → 并入静态层
    vp.end();
    viewPixValid_ = true;
}

void MapView::qaBakeViewPix()
{
    bakeViewPix(rect().adjusted(8, 8, -8, -8));
}

int MapView::qaViewPixDeviceWidth() const
{
    if (viewPix_.isNull()) return -1;
    // 注意：QPixmap::width() 已返回设备像素（实际缓冲宽），不可再乘 devicePixelRatio
    // （否则 2400×2=4800 假红）。正确 HiDPI 烘焙下 width()==逻辑宽×dpr。
    return viewPix_.width();
}

// 帧耗时自测：LA_MAP_PERF=1 时逐层级各渲染 20 帧并打印平均毫秒/帧
// （离屏 QImage + 与 paintEvent 完全相同的调度；先测再改）
void MapView::qaPerf()
{
    resize(1200, 800);   // 离屏门里 MapView 是堆叠页非当前页、尺寸极小 → 强制真实尺寸
    invalidateViewCache();
    QImage img(size(), QImage::Format_ARGB32_Premultiplied);
    img.fill(0xff0a1016);
    auto bench = [&](const char* name, const std::function<void()>& enter) {
        enter();
        {   // warmup：首帧含懒加载/缓存烘焙，不计入
            QPainter p(&img);
            paintMap(p);
        }
        QElapsedTimer t;
        t.start();
        for (int i = 0; i < 20; ++i) {
            QPainter p(&img);
            paintMap(p);
        }
        std::fprintf(stderr, "LA_MAP_PERF %s=%.2fms\n", name, double(t.elapsed()) / 20.0);
        std::fflush(stderr);
    };
    bench("world", [] {});
    bench("region", [this] { enterRegion(QStringLiteral("china")); });
    bench("province", [this] {
        if (!curProvinces_.isEmpty()) enterProvince(curProvinces_.first().name);
    });
    bench("country", [this] {
        while (level_ != World) goUp();
        for (int i = 0; i < worldNames_.size(); ++i)
            if (worldNames_[i] == QStringLiteral("Vietnam")) { enterCountry(i); break; }
    });
    while (level_ != World) goUp();
}

void MapView::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    invalidateViewCache();
    update();
}

// 拖拽平移收尾：原地松开（位移 ≤4px）视为「点击空白」→ 返回上一级
void MapView::mouseReleaseEvent(QMouseEvent* e)
{
    Q_UNUSED(e);
    if (!panning_) return;
    panning_ = false;
    if (!panMoved_) goUp();   // 世界层 goUp 为空操作
}

// 世界层滚轮缩放（1x..4x，光标锚定）：缩放前后光标下的预投影点保持不动——
// 横向调 driftPx_、纵向调 worldOffY_（越界自动夹取）
void MapView::wheelEvent(QWheelEvent* e)
{
    const QRectF dst = rect().adjusted(8, 8, -8, -8);
    if (level_ != World) {
        // 下钻视图（大区/省/国家）：同样支持滚轮缩放，光标为锚点（变换与绘制同源）
        e->accept();
        double s0 = 1;
        const QPointF o0 = viewOrigin(curViewBbox_, dst, s0);
        const QPointF pre = (e->position() - o0) / s0;
        const double f = e->angleDelta().y() > 0 ? 1.25 : 1.0 / 1.25;
        const double nz = qBound(1.0, viewZoom_ * f, 8.0);
        if (qFuzzyCompare(nz, viewZoom_)) return;
        viewZoom_ = nz;
        double s1 = 1;
        viewOrigin(curViewBbox_, dst, s1);
        const QPointF bc((curViewBbox_.left() + curViewBbox_.right()) * 0.5,
                         (curViewBbox_.top() + curViewBbox_.bottom()) * 0.5);
        // 光标下的预投影点不动：o1 = cursor - pre*s1 → 反解 viewPan_
        viewPan_ = e->position() - QPointF(pre.x() * s1, pre.y() * s1)
                   - dst.center() + QPointF(bc.x() * s1, bc.y() * s1);
        clampViewPan(curViewBbox_, dst, s1);
        invalidateViewCache();
        update();
        return;
    }
    const double base = dst.height() / MAP_H;
    const double oldScale = base * worldZoom_;
    const double oldOffY = worldOffYClamped();
    const QPointF cur = e->position();
    const QPointF pre = widgetToPre(cur, dst, oldScale, wrapX(driftPx_), oldOffY);

    const double f = e->angleDelta().y() > 0 ? 1.25 : 1.0 / 1.25;
    const double nz = qBound(1.0, worldZoom_ * f, 4.0);
    if (qFuzzyCompare(nz, worldZoom_)) { e->accept(); return; }

    const double newScale = base * nz;
    driftPx_  = wrapX(pre.x() - cur.x() / newScale);
    worldOffY_ = pre.y() * newScale - (cur.y() - dst.y());
    worldZoom_ = nz;
    e->accept();
    update();
}

void MapView::mouseMoveEvent(QMouseEvent* e)
{
    // 拖拽平移：世界层→调漂移/纵向偏移；下钻视图→按屏幕像素平移 viewPan_
    if (panning_) {
        const QRectF dst = rect().adjusted(8, 8, -8, -8);
        const QPointF d = e->pos() - panLast_;
        panLast_ = e->pos();
        if (level_ == World) {
            const double scale = dst.height() / MAP_H * worldZoom_;
            driftPx_  = wrapX(driftPx_ - d.x() / scale);
            worldOffY_ -= d.y() / scale;
        } else {
            viewPan_ += d;
            double sp = 1;
            viewOrigin(curViewBbox_, dst, sp);
            clampViewPan(curViewBbox_, dst, sp);
            invalidateViewCache();
        }
        if (std::abs(e->pos().x() - panStart_.x()) +
            std::abs(e->pos().y() - panStart_.y()) > 4.0) panMoved_ = true;
        update();
        return;
    }
    if (level_ == World) {
        // 命中测试：大区锚点（scale 与 renderWorldInto 绘制基准一致，见 mousePressEvent 注释）
        const QRectF dst = rect().adjusted(8, 8, -8, -8);
        const double scale = dst.height() / MAP_H * worldZoom_;
        const double offY = worldOffYClamped();
        const double span = MAP_W * scale;
        // ① 国家轮廓悬停：任意国家 → 手型光标 + 名称提示（专辑大区显中文名）
        const QPointF pre = widgetToPre(e->pos(), dst, scale, wrapX(driftPx_), offY);
        for (int fi = 0; fi < worldFeatures_.size(); ++fi) {
            if (!featureBbox_[fi].contains(pre)) continue;
            if (!pointInFeature(pre, worldFeatures_[fi])) continue;
            const int r = featureRegion_.value(fi, -1);
            const QString nm = (r >= 0) ? regions_[r].name : worldNames_.value(fi);
            if (hoverHint_ != nm) { hoverHint_ = nm; setCursor(Qt::PointingHandCursor); update(); }
            return;
        }
        // ② 回退：大区锚点圆
        for (const RegionInfo& ri : regions_) {
            QPointF w = preToWidget(ri.proj, dst, scale, -driftPx_);
            double wx = dst.x() + fmod(w.x() - dst.x(), span);
            if (wx < dst.x()) wx += span;
            if (std::hypot(wx - e->pos().x(), (w.y() - offY) - e->pos().y()) < 8) {
                if (hoverHint_ != ri.name) { hoverHint_ = ri.name; setCursor(Qt::PointingHandCursor); update(); }
                return;
            }
        }
        if (!hoverHint_.isEmpty()) { hoverHint_.clear(); setCursor(Qt::ArrowCursor); update(); }
    } else if (level_ == Region) {
        double scale = 1;
        QRectF bbox; bool first = true;
        for (const ProvinceInfo& pv : curProvinces_) { if (first){bbox=pv.bbox;first=false;} else bbox|=pv.bbox; }
        const QPointF vo = viewOrigin(bbox, rect().adjusted(8,8,-8,-8), scale);
        for (const ProvinceInfo& pv : curProvinces_) {
            QPainterPath path;
            const QPointF o = vo;
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
        const double scale = dst.height() / MAP_H * worldZoom_;
        const double offY = worldOffYClamped();
        const double span = MAP_W * scale;
        // ① 国家轮廓命中（优先）：点任意国家即下钻——
        //    有专辑的 10 大区 → 省级下钻；其余国家 → 国家视图（轮廓 + 该国 GPS 作品）
        const QPointF pre = widgetToPre(e->pos(), dst, scale, wrapX(driftPx_), offY);
        for (int fi = 0; fi < worldFeatures_.size(); ++fi) {
            if (!featureBbox_[fi].contains(pre)) continue;
            if (!pointInFeature(pre, worldFeatures_[fi])) continue;
            const int r = featureRegion_.value(fi, -1);
            if (r >= 0) {
                emit picked(regions_[r].id, regions_[r].name, false);
                enterRegion(regions_[r].id);
            } else {
                enterCountry(fi);
            }
            return;
        }
        // ② 回退：大区锚点圆命中（10px）
        for (const RegionInfo& ri : regions_) {
            QPointF w = preToWidget(ri.proj, dst, scale, -driftPx_);
            double wx = dst.x() + fmod(w.x() - dst.x(), span);
            if (wx < dst.x()) wx += span;
            if (std::hypot(wx - e->pos().x(), (w.y() - offY) - e->pos().y()) < 10) {
                emit picked(ri.id, ri.name, false);
                enterRegion(ri.id);
                return;
            }
        }
        // ③ 空白/海洋（含无作品国家）：进入拖拽平移待命；原地松开 = 点空白 → 返回上一级
        panning_ = true;
        panMoved_ = false;
        panStart_ = panLast_ = e->pos();
    } else if (level_ == Region) {
        // 国家视图（无省数据）：点国内 → 该国爆炸弹层；点空白 → 返回上一级
        if (curProvinces_.isEmpty() && countryFeature_ >= 0) {
            const WorldFeature& wf = worldFeatures_[countryFeature_];
            double scc = 1;
            const QPointF vocc = viewOrigin(featureBbox_[countryFeature_],
                                            rect().adjusted(8, 8, -8, -8), scc);
            QPainterPath cpath;
            const QPointF oc = vocc;
            cpath.moveTo(oc + wf.exterior.first() * scc);
            for (int i = 1; i < wf.exterior.size(); ++i)
                cpath.lineTo(oc + wf.exterior[i] * scc);
            cpath.closeSubpath();
            for (const QVector<QPointF>& h : wf.holes) {
                QPainterPath hp;
                hp.moveTo(oc + h.first() * scc);
                for (int i = 1; i < h.size(); ++i)
                    hp.lineTo(oc + h[i] * scc);
                hp.closeSubpath();
                cpath.addPath(hp);
            }
            if (cpath.contains(e->pos())) {
                emit countryBurstRequested(curRegionName_, countryPaths_);
            } else {
                // 离岸/近海照片点 pip 在国界外但可见（bbox 内绘制）——
                // 点在光点附近同样触发爆炸，不误判为「点空白返回」
                bool nearDot = false;
                for (const QPointF& pt : countryPts_) {
                    const QPointF sp = oc + pt * scc;
                    if (QLineF(sp, e->pos()).length() < 14.0) { nearDot = true; break; }
                }
                if (nearDot) {
                    emit countryBurstRequested(curRegionName_, countryPaths_);
                } else {
                    panning_ = true;
                    panMoved_ = false;
                    panStart_ = panLast_ = e->pos();
                }
            }
            return;
        }
        double scale = 1;
        QRectF bbox; bool first = true;
        for (const ProvinceInfo& pv : curProvinces_) { if (first){bbox=pv.bbox;first=false;} else bbox|=pv.bbox; }
        const QPointF vo = viewOrigin(bbox, rect().adjusted(8,8,-8,-8), scale);
        for (const ProvinceInfo& pv : curProvinces_) {
            QPainterPath path;
            const QPointF o = vo;
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
        // 空白处：按下进入平移待命，松开时未拖动 → 返回上一级（大区 → 世界）
        panning_ = true;
        panMoved_ = false;
        panStart_ = panLast_ = e->pos();
    } else if (level_ == Province && hasCurProvince_) {
        // ① 爆炸卡片命中 → 打开该照片灯箱
        if (burstActive_ && !burstPaths_.isEmpty()) {
            const QRectF dst = rect().adjusted(8, 8, -8, -8);
            const int nCard = qMin(burstThumbs_.size(), burstPaths_.size());
            for (int i = 0; i < nCard; ++i) {
                if (burstCardRect(i, dst).contains(e->pos())) {
                    emit openPhotoRequested(burstPaths_[i]);
                    return;
                }
            }
        }
        // ② 省面命中 → 爆炸弹层（MapPage 按省收集缩略图回灌 startBurst）
        double sc = 1;
        const QPointF vo = viewOrigin(curProvince_.bbox, rect().adjusted(8, 8, -8, -8), sc);
        QPainterPath path;
        const QPointF o = vo;
        path.moveTo(o + curProvince_.ring.pts.first() * sc);
        for (int i = 1; i < curProvince_.ring.pts.size(); ++i)
            path.lineTo(o + curProvince_.ring.pts[i] * sc);
        path.closeSubpath();
        if (path.contains(e->pos())) {
            const QString label = curProvince_.zh.isEmpty() ? curProvince_.name : curProvince_.zh;
            emit burstRequested(curProvince_.name, label);
            return;
        }
        // ③ 空白处：按下进入平移待命，松开时未拖动 → 返回上一级（省 → 大区）
        panning_ = true;
        panMoved_ = false;
        panStart_ = panLast_ = e->pos();
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

// 进入国家视图（无省级数据的国家）：国界轮廓 + 该国 GPS 足迹/航线。
// 复用 Region 层级（curProvinces_ 为空 + countryFeature_ 生效），返回键/面包屑自然兼容。
void MapView::enterCountry(int fi)
{
    if (fi < 0 || fi >= worldFeatures_.size()) return;
    countryFeature_ = fi;
    curRegionId_.clear();
    curRegionName_ = worldNames_.value(fi);
    curProvinces_.clear();
    hasCurProvince_ = false;
    level_ = Region;
    // 该国 GPS 照片路径（pip 于外环；离岸容差：距国界顶点 <3 预投影单位 ≈1°，
    // 顶点距离 ≥ 真实边界距离 → 判定方向保守）。湖泊内环中的点不接受容差。
    countryPaths_.clear();
    countryPts_.clear();
    const WorldFeature& wf = worldFeatures_[fi];
    const QRectF& bb = featureBbox_[fi];
    for (int i = 0; i < gpsPts_.size(); ++i) {
        const QPointF& pt = gpsPts_[i];
        if (!bb.contains(pt)) continue;
        bool inHole = false;
        for (const QVector<QPointF>& h : wf.holes)
            if (pip(h, pt)) { inHole = true; break; }
        if (inHole) continue;
        bool ok = pip(wf.exterior, pt);
        if (!ok) {
            double best = 1e18;
            for (const QPointF& v : wf.exterior) {
                const double dd = (v.x() - pt.x()) * (v.x() - pt.x())
                                + (v.y() - pt.y()) * (v.y() - pt.y());
                if (dd < best) best = dd;
            }
            ok = (best < 9.0);
        }
        if (!ok) continue;
        if (i < gpsPaths_.size()) countryPaths_.append(gpsPaths_[i]);
        countryPts_.append(pt);
    }
    curViewBbox_ = bb;
    resetViewZoom();
    invalidateViewCache();
    update();
    emit breadcrumbChanged(breadcrumb());
    emit countryPicked(curRegionName_, countryPaths_);
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
    curViewBbox_ = QRectF();
    {
        bool first = true;
        for (const ProvinceInfo& pv : curProvinces_) {
            if (first) { curViewBbox_ = pv.bbox; first = false; }
            else curViewBbox_ |= pv.bbox;
        }
    }
    resetViewZoom();
    invalidateViewCache();
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
            curViewBbox_ = pv.bbox;
            resetViewZoom();
            invalidateViewCache();
            update();
            emit breadcrumbChanged(breadcrumb());
            return;
        }
    }
}

void MapView::goUp()
{
    if (level_ == Province) { level_ = Region; hasCurProvince_ = false; }
    else if (level_ == Region) { level_ = World; curProvinces_.clear(); countryFeature_ = -1; countryPaths_.clear(); countryPts_.clear(); }
    resetViewZoom();
    invalidateViewCache();
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
