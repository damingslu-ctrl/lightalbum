#include "MapView.h"
#include "../core/MotionPref.h"
#include "../core/GeoHotspot.h"   // 方向 C·联动：热点聚合（与渲染/门禁同源）
#include "Theme.h"                // 终审 M1：glow 字面量收编 Lumen token（spec §1.6/§1.7），直接 include 不靠传递
#include <QPainter>
#include <QFile>
#include <QImage>
#include "HiDpi.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QPainterPath>
#include <QLineF>
#include <QDebug>
#include <cmath>
#include <algorithm>
#include <QDate>
#include <QRadialGradient>
#include <QHash>
#include <QPoint>
#include <QCoreApplication>

// —— 终审 M1（spec §1.6 glow 收编）：辉光字面量改引 Lumen token，各站点 alpha 原样保留 ——
// 不用 Theme 预设 glowJade()/glowGold()（alpha 钉死 115/102 且 gold 基色为 GOLD_HI，
// 与本仓站点原 alpha 均不符）。jade 按 spec 归一 EMERALD_HI #6adba6（自 #5fdca6 微漂移
// R+11/G−1，收编即此意，终审裁定认可）；gold 与 GOLD #d9a94e 逐字节相等，纯字面量→token。
static QColor jadeGlow(int a) { QColor c = Lumen::EMERALD_HI; c.setAlpha(a); return c; }
static QColor goldGlow(int a) { QColor c = Lumen::GOLD;       c.setAlpha(a); return c; }

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
    setFocusPolicy(Qt::StrongFocus);   // F3 a11y：地图可键盘聚焦（缩放/平移/下钻）
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

    // A 方向第二步：轨迹播放时钟。仅播放期挂载、播完自停（不常驻空转）
    connect(&trackTimer_, &QTimer::timeout, this, [this] {
        if (!trackActive_) { trackTimer_.stop(); return; }
        update();
        if (trackClock_.elapsed() >= kTrackDurMs) {
            trackTimer_.stop();
            trackActive_ = false;   // 终态：末点脉冲驻留，update 收尾
            update();
        }
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
        // ★判定必须存在（铁律34）：此前只打数字，而 runner 的 greenPat 含 "rings="
        //   → rings=0 也算绿，门永远不可能红。补两条真不变量：
        //   ① 各层几何非空（数据文件缺项即红）；
        //   ② provZh_ 按省英文名索引（zhOf 就查它），所以「中文名条目数 == 几何里
        //      各省数组条目总数」是「每个省都有中文名」的可判定形式（漏译即红）。
        //   注：Qt6 的 size() 返回 qsizetype，必须 int() 后再进 %d，否则 varargs 类型
        //   不符是 UB（原实现即踩此坑，判定整行打不出来）。
        // 反例（铁律35）：LA_MAP_SMOKE_FORCE_BROKEN=1 删掉一个省的中文名，这正是
        //   「漏译一个省」的真实形状。它只破坏数据，不碰下面的判定式（铁律22）。
        if (!qEnvironmentVariableIsEmpty("LA_MAP_SMOKE_FORCE_BROKEN") && !provZh_.isEmpty())
            provZh_.remove(provZh_.constBegin().key());
        const int zhKeys = int(provZh_.size());
        const bool zhComplete = zhKeys == tot;
        const bool pass = worldRings > 0 && !regions_.isEmpty()
                       && tot > 0 && !provJson_.isEmpty() && zhComplete
                       && !worldNames_.isEmpty();
        std::fprintf(stderr,
                     "LA_MAP_SMOKE %s rings=%d regions=%d provinces=%d provKeys=%d "
                     "zhKeys=%d names=%d zhComplete=%d\n",
                     pass ? "PASS" : "FAIL",
                     worldRings, int(regions_.size()), tot, int(provJson_.size()),
                     zhKeys, int(worldNames_.size()), int(zhComplete));
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
    // 显示分辨率烘焙（方向 A·地图性能）：按当前 widget 尺寸 × DPR × worldZoom_
    // 烘焙单条世界带，每帧 renderWorldInto 仅做 1:1 平移 blit（不再逐帧缩放固定
    // 3000×1500 源）。尺寸/zoom/dpr 任一变化由 ensureWorldPix 触发本函数重烘焙；
    // 未 show / 尺寸退化（QA 探针直接调用）时回退到逻辑 1000×500，保证 bake 非空。
    const qreal dpr = devicePixelRatioF();
    double bakeScale;
    int logW, logH;
    if (qaFixedBake_) {
        // QA 探针专用：固定 3x 超采样（3000×1500），复现方向 A 之前的确定性像素断言
        // （显示分辨率烘焙在 offscreen 下尺寸不定 → 像素计数随分辨率漂移 → 门禁 flaky）
        bakeScale = 3.0; logW = int(MAP_W * 3); logH = int(MAP_H * 3);
    } else if (width() > 100 && height() > 100) {
        const QRectF dst = rect().adjusted(8, 8, -8, -8);
        bakeScale = dst.height() / MAP_H * worldZoom_;
        logW = qMax(1, qRound(MAP_W * bakeScale));
        logH = qMax(1, qRound(MAP_H * bakeScale));
    } else {
        bakeScale = 1.0; logW = int(MAP_W); logH = int(MAP_H);
    }
    worldPix_ = HiDpi::bakePixmap(QSize(logW, logH));
    worldPix_.fill(Qt::transparent);
    QPainter p(&worldPix_);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.scale(bakeScale, bakeScale);   // 仍按 1000×500 逻辑坐标绘制（线宽随变换同步放大）

    const QColor ocean(0x0e, 0x16, 0x1f);   // 深海（雾隐深色）
    const QColor land(0x2f, 0x46, 0x58);    // 陆地基色（与 web .cty 对齐）
    const QColor border(0x4a, 0x66, 0x78);

    p.fillRect(0, 0, MAP_W, MAP_H, ocean);
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
        p.setPen(QPen(goldGlow(110), 1.0));
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
        p.setPen(QPen(Lumen::GOLD, 1.4));
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
    // A 方向：轨迹/热力（默认 FL_Dots 零回归；仅用户切换或门禁探针才绘制）
    if (footLayer_ == FL_Track && gpsPts_.size() >= 2) {
        drawTrajectoryWorld(p);
    } else if (footLayer_ == FL_Heat && !gpsPts_.isEmpty()) {
        drawHeatWorld(p);
    }
    p.end();
    // 记录烘焙参数供 ensureWorldPix 惰性校验（逻辑尺寸 + zoom 任一不符即重烘焙）
    worldPixW_ = logW; worldPixH_ = logH;
    worldPixZoom_ = worldZoom_;
    worldDirty_ = false;
}

void MapView::ensureWorldPix()
{
    const qreal dpr = devicePixelRatioF();
    // 未 show / 尺寸退化（QA 探针直接调用）时回退：仅当未烘焙或内容脏时烘焙
    if (width() <= 100 || height() <= 100) {
        if (worldPix_.isNull() || worldDirty_) buildWorldPixmap();
        return;
    }
    const QRectF dst = rect().adjusted(8, 8, -8, -8);
    const double scale = dst.height() / MAP_H * worldZoom_;
    const int needW = qRound(MAP_W * scale);
    const int needH = qRound(MAP_H * scale);
    // 注意：不再比较 dpr 浮点。offscreen 平台 devicePixelRatioF() 不稳定（项目记忆确证），
    // 对 dpr 浮点做 qFuzzyCompare 会在 paint 期间偶发触发重型 buildWorldPixmap() 冷重建，
    // 在 GEO 嵌套 processEvents 下死锁并撞 20s watchdog（rc=3）。
    // 世界层仅需「逻辑尺寸 + zoom」一致即可保证 1:1 blit 正确；dpr 仅影响烘焙内部分辨率，
    // 由 HiDpi::bakePixmap 内嵌，已烘焙逻辑尺寸不变则无需重烘焙。
    if (worldDirty_ || worldPix_.isNull()
        || worldPixW_ != needW || worldPixH_ != needH
        || !qFuzzyCompare(worldPixZoom_, worldZoom_)) {
        buildWorldPixmap();
    }
}

void MapView::setGpsPoints(const QVector<QPointF>& pts)
{
    gpsPts_ = pts;
    densDirty_ = true;          // 密度分级缓存失效
    arcTargetsDirty_ = true;    // 航线目标集缓存失效
    invalidateViewCache();      // 下钻静态层含足迹点 → 失效
    buildWorldPixmap();         // 急切实调（保持 qaShoot* 探针契约：世界层立即就绪）
    update();
}

void MapView::setGpsDates(const QVector<QDate>& dates)
{
    gpsDates_ = dates;
}

// A 方向第三步：按年筛选。数据侧已按年过滤（注入点），此处只记录当前筛选态供 UI 回显
// 与门禁断言；重复设置同一年不重烘焙（幂等）。
void MapView::setFootYearFilter(int year)
{
    // A 国家层深化：国层内改年/改库必须实时刷新快照，否则点击命中集与相册载荷
    // 停留进入国层那一刻的年份（footYear 未贯通 country 层的真实形态）。
    if (!countryYearForceBroken_
        && countryFeature_ >= 0 && gpsPts_.size() == gpsPaths_.size()) {
        recomputeCountryPts();
        invalidateViewCache();
    }
    if (footYearFilter_ == year) { update(); return; }
    footYearFilter_ = year;
    update();
}

void MapView::setFootLayerMode(int mode)
{
    const int m = (mode < FL_Dots || mode > FL_Heat) ? FL_Dots : mode;
    if (m == footLayer_) return;
    footLayer_ = m;
    if (m != FL_Track) stopTrackPlayback();   // 离开轨迹层即停播（防播放态残留到光点/热力）
    buildWorldPixmap();         // 急切实调（含轨迹/热力，保持 qaShoot* 探针契约）
    invalidateViewCache();  // 下钻静态层下次 paint 重烘焙
    update();
}

// P16.4.2 金色航线基点（经纬度）。重烘焙世界层并刷新下钻视图。
void MapView::setHomePoint(double lon, double lat)
{
    if (lon == homeLon_ && lat == homeLat_) return;
    homeLon_ = lon;
    homeLat_ = lat;
    buildWorldPixmap();         // 急切实调（航线基点在世界层，保持 qaShoot* 探针契约）
    update();
}

void MapView::setArcRegionIds(const QStringList& ids)
{
    if (ids == arcRegionIds_) return;
    arcRegionIds_ = ids;
    arcTargetsDirty_ = true;    // 航线目标集缓存失效
    buildWorldPixmap();         // 急切实调（航线在世界层，保持 qaShoot* 探针契约）
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

// —— 方向 C·联动：热点半径 / 绘制 / 命中 ——
double MapView::hotspotRadius(int count) const
{
    return qBound(7.0, 7.0 + 2.4 * std::log(double(count) + 1.0), 24.0);
}

// World 级徽章的预投影锚点：命中大区锚点（金点）时用 RegionInfo::proj（稳定已知点），
// 否则回退照片质心。理由（实测 LA_MAP_CLICK_QA 红）：质心 = 全部该国照片 GPS 均值，
// 随个人库数据漂移，可能正好压在大区锚点上 → 大徽章（r=24）吞掉锚点 10px 命中区 →
// 「点国家下钻」失效。锚点是固定点，抬升 r+8 后与锚点命中区**证明不重叠**（r+8 > r+6）。
QPointF MapView::hotspotPrePos(const GeoHotspot& h) const
{
    if (level_ == World) {
        for (const RegionInfo& ri : regions_)
            if (ri.id.compare(h.key, Qt::CaseInsensitive) == 0) return ri.proj;
    }
    return h.centroid;
}

void MapView::drawHotspot(QPainter& p, const QPointF& pos, const GeoHotspot& h) const
{
    const double r = hotspotRadius(h.count);
    // 外光晕（翡翠绿，与暗色弹层选中色一致）
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x4f, 0xc0, 0x8a, 60));
    p.drawEllipse(pos, r + 4.0, r + 4.0);
    // 主体
    p.setBrush(QColor(0x4f, 0xc0, 0x8a));
    p.setPen(QPen(QColor(0x0e, 0x13, 0x0f), 1.2));
    p.drawEllipse(pos, r, r);
    // 计数文本（暗底亮字，WCAG 比对）
    p.setPen(QColor(0x0e, 0x13, 0x0f));
    p.setBrush(Qt::NoBrush);
    QFont f = p.font(); f.setBold(true); f.setPointSizeF(qMax(8.0, r * 0.9));
    p.setFont(f);
    p.drawText(QRectF(pos.x() - r, pos.y() - r, 2.0 * r, 2.0 * r),
               Qt::AlignCenter, QString::number(h.count));
}

QPointF MapView::qaHotspotWidgetPos(int i) const
{
    if (i < 0 || i >= geoHotspots_.size()) return QPointF();
    const GeoHotspot& h = geoHotspots_.at(i);
    const QRectF dst = rect().adjusted(8, 8, -8, -8);
    if (level_ == World) {
        const double scale = dst.height() / MAP_H * worldZoom_;
        const double offY = worldOffYClamped();
        const double span = MAP_W * scale;
        QPointF w = preToWidget(hotspotPrePos(h), dst, scale, -driftPx_);
        double wx = dst.x() + fmod(w.x() - dst.x(), span);
        if (wx < dst.x()) wx += span;
        return QPointF(wx, w.y() - offY - hotspotLift(h.count));
    } else if (level_ == Region && !curProvinces_.isEmpty()) {
        QRectF bbox; bool first = true;
        for (const ProvinceInfo& pv : curProvinces_) {
            if (first) { bbox = pv.bbox; first = false; } else bbox |= pv.bbox;
        }
        double scale = 1; const QPointF vo = viewOrigin(bbox, dst, scale);
        return vo + h.centroid * scale;
    }
    return QPointF();
}

int MapView::hotspotHitIndex(const QPoint& pos) const
{
    const QRectF dst = rect().adjusted(8, 8, -8, -8);
    if (level_ == World) {
        const double scale = dst.height() / MAP_H * worldZoom_;
        const double offY = worldOffYClamped();
        const double span = MAP_W * scale;
        for (int i = 0; i < geoHotspots_.size(); ++i) {
            QPointF w = preToWidget(hotspotPrePos(geoHotspots_[i]), dst, scale, -driftPx_);
            double wx = dst.x() + fmod(w.x() - dst.x(), span);
            if (wx < dst.x()) wx += span;
            const QPointF p(wx, w.y() - offY - hotspotLift(geoHotspots_[i].count));
            const double r = hotspotRadius(geoHotspots_[i].count) + 6.0;   // 命中容差
            if (std::hypot(p.x() - pos.x(), p.y() - pos.y()) <= r) return i;
        }
    } else if (level_ == Region && !curProvinces_.isEmpty()) {
        QRectF bbox; bool first = true;
        for (const ProvinceInfo& pv : curProvinces_) {
            if (first) { bbox = pv.bbox; first = false; } else bbox |= pv.bbox;
        }
        double scale = 1; const QPointF vo = viewOrigin(bbox, dst, scale);
        for (int i = 0; i < geoHotspots_.size(); ++i) {
            const QPointF p = vo + geoHotspots_[i].centroid * scale;
            const double r = hotspotRadius(geoHotspots_[i].count) + 6.0;
            if (std::hypot(p.x() - pos.x(), p.y() - pos.y()) <= r) return i;
        }
    }
    return -1;
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
    // 显示分辨率烘焙 → 每帧仅 1:1 平移 blit（不再逐帧缩放，消除漂移边缘靠整数/近整数偏移）
    // 横向环绕：绘制足够副本覆盖 dst 宽度（屏外剔除）
    double off = -wrapX(driftPx_) * scale;
    for (double x0 = off; x0 < dst.right() + mapWpx; x0 += mapWpx) {
        if (x0 + mapWpx < dst.left() || x0 > dst.right()) continue;
        const QRectF d(x0, dst.y() - offY, mapWpx, mapHpx);
        if (d.bottom() < dst.y() || d.top() > dst.bottom()) continue;
        // 1:1 blit：worldPix_ 已按显示分辨率烘焙，src=整图逻辑矩形 → 目标尺寸一致即逐像素拷贝
        p.drawPixmap(d, worldPix_, worldPix_.rect());
    }
    // 大区旅行锚点（金色点 + 名）
    QColor gold(0xc9, 0xa2, 0x4b);
    if (!qaSkipWorldOverlay_) {
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
    // 方向 C·联动：地理热点层（World=国家/大区），点击 → 图库筛选
    if (!qaSkipWorldOverlay_) {
    for (const GeoHotspot& h : geoHotspots_) {
        QPointF w = preToWidget(hotspotPrePos(h), dst, scale, -driftPx_);
        double wx = dst.x() + fmod(w.x() - dst.x(), mapWpx);
        if (wx < dst.x()) wx += mapWpx;
        const QPointF pos(wx, w.y() - offY - hotspotLift(h.count));   // 上抬让锚点可点
        drawHotspot(p, pos, h);
    }
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
    // 方向 C·联动：省热点层（Region=省），点击 → 图库按省筛选
    {
        const QPointF o = viewOrigin(bbox, dst, scale);
        for (const GeoHotspot& h : geoHotspots_) {
            const QPointF pos = o + h.centroid * scale;
            drawHotspot(p, pos, h);
        }
    }
    // A 方向：轨迹/热力（默认光点，零回归）
    if (footLayer_ == FL_Track && gpsPts_.size() >= 2) drawTrajectory(p, bbox, dst);
    else if (footLayer_ == FL_Heat && !gpsPts_.isEmpty()) drawHeat(p, bbox, dst);
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
    // A 方向：轨迹/热力
    if (footLayer_ == FL_Track && gpsPts_.size() >= 2) drawTrajectory(p, bbox, dst);
    else if (footLayer_ == FL_Heat && !gpsPts_.isEmpty()) drawHeat(p, bbox, dst);
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
    // A 方向：轨迹/热力
    if (footLayer_ == FL_Track && gpsPts_.size() >= 2) drawTrajectory(p, curProvince_.bbox, dst);
    else if (footLayer_ == FL_Heat && !gpsPts_.isEmpty()) drawHeat(p, curProvince_.bbox, dst);
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
    QPen pen(goldGlow(150), 1.2);
    if (flow) { pen.setStyle(Qt::DashLine); pen.setDashPattern({6.0, 6.0}); pen.setDashOffset(-arcDashOffset_); }
    p.setPen(pen);
    p.drawPath(arcs);
    p.setPen(Qt::NoPen);
    p.setBrush(goldGlow(210));
    p.drawPath(glowDots);
    // 基点标记（在 bbox 内才画；世界层已画过一次）
    if (eqX(homeLon_) >= bbox.left() && eqX(homeLon_) <= bbox.right() &&
        eqY(homeLat_) >= bbox.top() && eqY(homeLat_) <= bbox.bottom()) {
        p.setPen(QPen(Lumen::GOLD, 1.5));
        p.setBrush(QColor(0xf0, 0xd0, 0x8a));
        p.drawEllipse(h, 3.6, 3.6);
    }
    p.restore();
}

// A 现状墨韵·抛光 —— 像素自测：注入确定性合成足迹点，离屏渲染世界层并统计翡翠像素
// （核心+光晕）。用于 CI/offscreen 验证「光晕已落地」，颜色校验铁律：实测不许猜。
void MapView::qaShootA(const QString& path)
{
    qaFixedBake_ = true;   // 固定 3x 超采样，复现确定性像素断言
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

// A 方向：轨迹像素自测。注入确定性足迹点（footLayer=轨迹），离屏渲染世界层，
// 统计青色（蓝主导）轨迹像素；FORCE_BROKEN 强制光点 → 轨迹像素为 0 → RED。
void MapView::qaShootTrack(const QString& path)
{
    qaFixedBake_ = true;   // 固定 3x 超采样，复现确定性像素断言
    const bool broken = !qEnvironmentVariableIsEmpty("LA_MAP_TRACK_QA_FORCE_BROKEN");
    footLayer_ = broken ? FL_Dots : FL_Track;
    arcRegionIds_.clear();
    gpsDates_.clear();
    for (int i = 0; i < 5; ++i) gpsDates_.append(QDate(2024, 1, 1 + i));
    setGpsPoints({
        QPointF(eqX(116.40), eqY(39.90)),
        QPointF(eqX(121.47), eqY(31.23)),
        QPointF(eqX(113.26), eqY(23.13)),
        QPointF(eqX(104.07), eqY(30.57)),
        QPointF(eqX(87.62),  eqY(43.82)),
    });

    const int W = int(MAP_W), H = int(MAP_H);
    QImage img(W, H, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    {
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.drawPixmap(QRectF(0, 0, W, H), worldPix_, worldPix_.rect());
    }
    int cyan = 0;
    const uchar* bits = img.constBits();
    const int bpl = img.bytesPerLine();
    for (int y = 0; y < H; ++y) {
        const uchar* row = bits + y * bpl;
        for (int x = 0; x < W; ++x) {
            const uchar* px = row + x * 4;
            const int b = px[0], g = px[1], r = px[2];
            if (b > 140 && b > r + 15 && b > g + 15) ++cyan;
        }
    }
    const int thr = 30;
    if (cyan < thr) {
        std::fprintf(stderr, "LA_MAP_TRACK_QA FAIL cyan=%d threshold=%d broken=%d\n", cyan, thr, int(broken));
        std::fflush(stderr);
        return;
    }
    if (!path.isEmpty()) img.save(path);
    std::fprintf(stderr, "LA_MAP_TRACK_QA trackPx=%d points=%d broken=%d\n", cyan, gpsPts_.size(), int(broken));
    std::fflush(stderr);
}

// A 方向：热力像素自测。注入确定性足迹点（footLayer=热力），离屏渲染世界层，
// 统计暖色（红主导）热力像素；FORCE_BROKEN 强制光点 → 暖色像素为 0 → RED。
void MapView::qaShootHeat(const QString& path)
{
    qaFixedBake_ = true;   // 固定 3x 超采样，复现确定性像素断言
    const bool broken = !qEnvironmentVariableIsEmpty("LA_MAP_HEAT_QA_FORCE_BROKEN");
    footLayer_ = broken ? FL_Dots : FL_Heat;
    arcRegionIds_.clear();
    gpsDates_.clear();
    for (int i = 0; i < 5; ++i) gpsDates_.append(QDate(2024, 1, 1 + i));
    setGpsPoints({
        QPointF(eqX(116.40), eqY(39.90)),
        QPointF(eqX(121.47), eqY(31.23)),
        QPointF(eqX(113.26), eqY(23.13)),
        QPointF(eqX(104.07), eqY(30.57)),
        QPointF(eqX(87.62),  eqY(43.82)),
    });

    const int W = int(MAP_W), H = int(MAP_H);
    QImage img(W, H, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    {
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.drawPixmap(QRectF(0, 0, W, H), worldPix_, worldPix_.rect());
    }
    int warm = 0;
    const uchar* bits = img.constBits();
    const int bpl = img.bytesPerLine();
    for (int y = 0; y < H; ++y) {
        const uchar* row = bits + y * bpl;
        for (int x = 0; x < W; ++x) {
            const uchar* px = row + x * 4;
            const int b = px[0], g = px[1], r = px[2];
            if (r > 140 && r - g > 90 && r - b > 90) ++warm;
        }
    }
    const int thr = 30;
    if (warm < thr) {
        std::fprintf(stderr, "LA_MAP_HEAT_QA FAIL warm=%d threshold=%d broken=%d\n", warm, thr, int(broken));
        std::fflush(stderr);
        return;
    }
    if (!path.isEmpty()) img.save(path);
    std::fprintf(stderr, "LA_MAP_HEAT_QA warmPx=%d points=%d broken=%d\n", warm, gpsPts_.size(), int(broken));
    std::fflush(stderr);
}

    // A 方向第二步：轨迹箭头 + 播放头像素/位置自测。// 断言 A：箭头像素 > 0（B 主导的三角，沿轨迹分布）。
// 断言 B（核心）：播放头在 t=0 / t=mid / t=end 三时刻**必须落在不同位置** ——
//   这是「真沿线移动」与「原地闪」的区分器；只断言「有像素」会是空心断言。
//   位置经 trackPosAt()（与产品绘制同一真源）求出，FORCE_BROKEN 强制 frac 恒 0
//   → 三时刻全等 → 反例必红。
void MapView::qaShootTrackAnim(const QString& path)
{
    qaFixedBake_ = true;   // 固定 3x 超采样，复现确定性像素断言
    const bool broken = !qEnvironmentVariableIsEmpty("LA_MAP_TRACK_ANIM_QA_FORCE_BROKEN");
    footLayer_ = FL_Track;
    arcRegionIds_.clear();
    gpsDates_.clear();
    for (int i = 0; i < 5; ++i) gpsDates_.append(QDate(2024, 1, 1 + i));
    setGpsPoints({
        QPointF(eqX(116.40), eqY(39.90)),
        QPointF(eqX(121.47), eqY(31.23)),
        QPointF(eqX(113.26), eqY(23.13)),
        QPointF(eqX(104.07), eqY(30.57)),
        QPointF(eqX(87.62),  eqY(43.82)),
    });

    // 播放头三时刻位置（经唯一真源 trackPosAt；FORCE_BROKEN 时恒回首点 → 三者全等）
    QPointF p0, pM, pE;
    if (broken) {
        p0 = pM = pE = trackPosAt(0.0);
    } else {
        p0 = trackPosAt(0.0);
        pM = trackPosAt(0.5);
        pE = trackPosAt(1.0);
    }
    const double dPM = std::hypot(pM.x() - p0.x(), pM.y() - p0.y());   // 首→中位移
    const double dEM = std::hypot(pE.x() - pM.x(), pE.y() - pM.y());   // 中→末位移
    const double dPE = std::hypot(pE.x() - p0.x(), pE.y() - p0.y());   // 首→末位移

    // 箭头像素统计：离屏渲染世界层（footLayer=Track → 轨迹+箭头进烘焙层）
    const int W = int(MAP_W), H = int(MAP_H);
    QImage img(W, H, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    {
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.drawPixmap(QRectF(0, 0, W, H), worldPix_, worldPix_.rect());
    }
    // 箭头用比轨迹更亮的青白（0xbf,0xf0,0xff → b 显著高于轨迹亮芯），
    // 统计「最亮一档」的青色像素作为箭头存在性证据。
    int arrow = 0;
    const uchar* bits = img.constBits();
    const int bpl = img.bytesPerLine();
    for (int y = 0; y < H; ++y) {
        const uchar* row = bits + y * bpl;
        for (int x = 0; x < W; ++x) {
            const uchar* px = row + x * 4;
            const int b = px[0], g = px[1], r = px[2];
            if (b > 235 && g > 225 && r > 170 && b > r + 25) ++arrow;
        }
    }
    if (!path.isEmpty()) img.save(path);

    // 判定：箭头像素 > 0，且三时刻两两位移均超过阈值（阈值按折线总长 ~ 实测尺度给 8px）
    const double thr = 8.0;
    const bool arrowOk = arrow > 0;
    const bool moveOk = (dPM > thr) && (dEM > thr) && (dPE > thr);
    if (!arrowOk || !moveOk) {
        std::fprintf(stderr,
            "LA_MAP_TRACK_ANIM_QA FAIL arrow=%d dPM=%.2f dEM=%.2f dPE=%.2f thr=%.1f broken=%d\n",
            arrow, dPM, dEM, dPE, thr, int(broken));
        std::fflush(stderr);
        return;
    }
    std::fprintf(stderr,
        "LA_MAP_TRACK_ANIM_QA arrow=%d dPM=%.2f dEM=%.2f dPE=%.2f broken=%d\n",
        arrow, dPM, dEM, dPE, int(broken));
    std::fflush(stderr);
}

// A 方向第三步：按年筛选自测。注入跨年份确定性 GPS 点，断言
//   A 可见点数：筛选 2024 后 gpsPts_ 只剩该年点（FORCE_BROKEN 下失效 → 全量）
//   B 下标对齐：gpsPts_.size() == gpsPaths_.size() == gpsDates_.size()
//     （铁律：国家视图 gpsPaths_[i] 依赖该对齐，筛选若只动其一即错位）
//   C 筛选态可读：setFootYearFilter 幂等 + 0 表示全部
void MapView::qaShootYear(const QString& path)
{
    Q_UNUSED(path);
    const bool broken = !qEnvironmentVariableIsEmpty("LA_MAP_YEAR_QA_FORCE_BROKEN");
    // 构造 6 点：2019×2 / 2021×2 / 2024×2，年份与点一一对应
    struct Row { int year; double lon, lat; };
    const Row rows[6] = {
        {2019, 116.40, 39.90}, {2019, 121.47, 31.23},
        {2021, 113.26, 23.13}, {2021, 104.07, 30.57},
        {2024,  87.62, 43.82}, {2024, 116.40, 39.90},
    };
    const int targetYear = 2024;
    const int expectKept = 2;      // 2024 的点数
    const int expectAll  = 6;      // 未筛选的总点数

    auto inject = [&](int yearFilter) {
        QVector<QPointF> pts;
        QStringList paths;
        QVector<QDate> dates;
        for (int i = 0; i < 6; ++i) {
            if (yearFilter > 0 && rows[i].year != yearFilter) continue;   // 注入点过滤
            pts.append(QPointF(eqX(rows[i].lon), eqY(rows[i].lat)));
            paths.append(QStringLiteral("synthetic/%1.jpg").arg(i));
            dates.append(QDate(rows[i].year, 1, 1 + (i % 4)));
        }
        setGpsPoints(pts);
        setGpsPaths(paths);
        setGpsDates(dates);
    };
    // 反例注入：仍按 targetYear 注入数据，但**跳过注入点过滤**（模拟筛选彻底失效的缺陷）
    inject(broken ? 0 : targetYear);
    // 筛选态照常设定为 targetYear（缺陷场景下 UI 认为是 2024，实际数据未过滤）
    setFootYearFilter(targetYear);

    const int nPts    = gpsPts_.size();
    const int nPaths  = gpsPaths_.size();
    const int nDates  = gpsDates_.size();
    const int yearNow = footYearFilter_;
    // ★期望值恒为 expectKept，**绝不随 broken 变化** —— 否则反例会自证通过（空心门禁）。
    // 反例须让 nPts(=6) 与 expect(=2) 相等 → 不等 → RED。
    const bool countOk = (nPts == expectKept);
    const bool yearOk  = (yearNow == targetYear);   // 筛选态本身必须生效
    const bool alignOk = (nPts == nPaths) && (nPts == nDates);
    // 幂等：重复设置同一年不改变态
    setFootYearFilter(targetYear);
    const bool idemOk = (footYearFilter_ == targetYear);
    // 全部年份（0）应恢复全量 —— 反例下同样要求，证明 0 是真「全部」而非绕过筛选
    inject(0);
    setFootYearFilter(0);
    const int nAll = gpsPts_.size();
    const bool allOk = (nAll == expectAll);

    if (!countOk || !yearOk || !alignOk || !idemOk || !allOk) {
        std::fprintf(stderr,
            "LA_MAP_YEAR_QA FAIL pts=%d expect=%d paths=%d dates=%d all=%d expectAll=%d year=%d yearOk=%d alignOk=%d idemOk=%d allOk=%d broken=%d\n",
            nPts, expectKept, nPaths, nDates, nAll, expectAll, yearNow,
            int(yearOk), int(alignOk), int(idemOk), int(allOk), int(broken));
        std::fflush(stderr);
        return;
    }
    std::fprintf(stderr,
        "LA_MAP_YEAR_QA PASS pts=%d paths=%d dates=%d all=%d year=%d broken=%d\n",
        nPts, nPaths, nDates, nAll, yearNow, int(broken));
    std::fflush(stderr);
}

// A 方向第四步：热力色带分级自测。三档色相必须各自出现在渲染结果中 ——
// 断言方式是把每个像素归类到「最接近哪一档」（按 R/G/B 相对关系），
// 统计三档像素数。FORCE_BROKEN 强制所有点用同一档（忽略密度），
// 则只有一档像素>0、另两档=0 → RED。
void MapView::qaShootHeatRamp(const QString& path)
{
    const bool broken = !qEnvironmentVariableIsEmpty("LA_MAP_HEAT_RAMP_QA_FORCE_BROKEN");
    heatRamp_ = HR_Warm;
    arcRegionIds_.clear();
    // 反例：强制所有密度取同一档（抹掉密度分级）→ heatRamp 三档同色 → graded=false
    heatRampForceTier_ = broken ? 0 : -1;
    // 三簇密度真分离（实测 computeDensity cell=22.0 网格）：低 1 / 中 5 / 高 14。
    // ★必须实测校验：若两簇落在同一网格内密度会相同、三档塌成两档（曾发生）。
    QVector<QPointF> heatPts;
    for (int i = 0; i < 3; ++i)   // 低密度：彼此远离（各自独立网格）
        heatPts.append(QPointF(eqX(100.0 + i * 60.0), eqY(20.0)));
    for (int i = 0; i < 5; ++i)   // 中密度：5 点小簇
        heatPts.append(QPointF(eqX(120.0 + (i % 3) * 0.8), eqY(45.0 + (i / 3) * 0.8)));
    for (int i = 0; i < 14; ++i)  // 高密度：14 点密簇（与中簇隔 50 经度，跨网格）
        heatPts.append(QPointF(eqX(170.0 + (i % 4) * 0.6), eqY(60.0 + (i / 4) * 0.6)));
    // ★铁律 12：改数据必须经 setter（置 densDirty_/arcTargetsDirty_ + 重烘焙）。
    // 直接赋值 gpsPts_ 会让 computeDensity 沿用真实库的旧密度（dmin=311/dmax=3268
    // 即此症状），三档判定全被真实库密度支配。
    setGpsPoints(heatPts);
    QVector<QDate> hdates;
    for (int i = 0; i < heatPts.size(); ++i) hdates.append(QDate(2024, 1, 1 + (i % 20)));
    setGpsDates(hdates);

    // 期望（恒为规格值，**不随 broken 变化** —— 铁律 22）：
    //  heatRamp(d) 对三个代表密度必须给出三个**互不相同**的中心色，
    //  即密度分级真的参与了选色。
    const QColor cLow  = heatRamp(1).c0;
    const QColor cMid  = heatRamp(5).c0;
    const QColor cHigh = heatRamp(50).c0;
    const bool graded = (cLow != cMid) && (cMid != cHigh) && (cLow != cHigh);

    // 渲染实测（颜色校验铁律：像素必须真画出来，不能只断言纯函数返回值）：
    // 统计「暖橙系」像素（r>g+30）—— 三档都在暖橙区间，故反例强制 tier0 后
    // 像素数不变、**只有 graded 会变 false**，两者共同构成非空心判别。
    const int W = int(MAP_W), H = int(MAP_H);
    QImage img(W, H, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    {
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.drawPixmap(QRectF(0, 0, W, H), worldPix_, worldPix_.rect());
    }
    int warm = 0;
    const uchar* bits = img.constBits();
    const int bpl = img.bytesPerLine();
    for (int y = 0; y < H; ++y) {
        const uchar* row = bits + y * bpl;
        for (int x = 0; x < W; ++x) {
            const uchar* px = row + x * 4;
            const int r = px[2], g = px[1], bl = px[0];
            if (r > 90 && r > g + 30 && r > bl + 20) ++warm;
        }
    }
    if (!path.isEmpty()) img.save(path);

    // 附带输出实测密度极值（诊断用）
    const QVector<int> dchk = computeDensity();
    int dmin = 9999, dmax = 0;
    for (int v : dchk) { dmin = std::min(dmin, v); dmax = std::max(dmax, v); }

    // 反例：heatRampForceTier_=0 使三档同色 → graded 必为 false → RED。
    if (!graded || warm < 30) {
        std::fprintf(stderr, "LA_MAP_HEAT_RAMP_QA FAIL graded=%d warm=%d dmin=%d dmax=%d broken=%d tiers=%d,%d,%d\n",
                     int(graded), warm, dmin, dmax, int(broken),
                     cLow.red(), cMid.red(), cHigh.red());
        std::fflush(stderr);
        return;
    }
    std::fprintf(stderr, "LA_MAP_HEAT_RAMP_QA graded=1 warm=%d dmin=%d dmax=%d broken=%d tiers=%d,%d,%d\n",
                 warm, dmin, dmax, int(broken),
                 cLow.red(), cMid.red(), cHigh.red());
    std::fflush(stderr);
}

// A 国家层深化（2.27.0）：footYear 贯通国层。注入同国跨年份点，模拟「改年 → gpsPts_
// 重新注入年过滤集 → setFootYearFilter」全链路，断言国层 countryPts_/countryPaths_
// 实时收敛到筛选年。FORCE_BROKEN 跳过 setFootYearFilter 内的重算 → 快照不缩 → RED。
// 期望值恒为 expectKept，不随 broken 变（铁律22/32）。
void MapView::qaCountryYearFilter()
{
    const bool broken = !qEnvironmentVariableIsEmpty("LA_MAP_COUNTRY_YEAR_QA_FORCE_BROKEN");
    countryYearForceBroken_ = broken;
    int fi = -1;
    for (int i = 0; i < worldNames_.size(); ++i)
        if (worldNames_[i] == QStringLiteral("India")) { fi = i; break; }
    if (fi < 0) {
        std::fprintf(stderr, "LA_MAP_COUNTRY_YEAR_QA name=India NOT_FOUND\n");
        std::fflush(stderr);
        return;
    }
    const QRectF& bb = featureBbox_[fi];
    const double cx = (bb.left() + bb.right()) / 2, cy = (bb.top() + bb.bottom()) / 2;
    const int years[6] = {2024, 2024, 2024, 2024, 2023, 2023};
    QVector<QPointF> allPts; QStringList allPaths; QVector<QDate> allDates;
    for (int i = 0; i < 6; ++i) {
        allPts.append(QPointF(cx + (i - 2.5) * 0.8, cy));
        allPaths.append(QStringLiteral("synthetic/c%1.jpg").arg(i));
        allDates.append(QDate(years[i], 6, 1));
    }
    auto refilter = [&](int year) {
        gpsPts_.clear(); gpsPaths_.clear(); gpsDates_.clear();
        for (int i = 0; i < allPts.size(); ++i) {
            if (year > 0 && allDates[i].year() != year) continue;
            gpsPts_.append(allPts[i]); gpsPaths_.append(allPaths[i]); gpsDates_.append(allDates[i]);
        }
    };
    refilter(0); enterCountry(fi);                 // 进入：全量 6 点
    const int n0 = countryPts_.size();
    refilter(2024); setFootYearFilter(2024);       // 改年：gpsPts_ 重注入 2024 → 应缩到 4
    const int n1 = countryPts_.size();
    const int p1 = countryPaths_.size();
    refilter(2023); setFootYearFilter(2023);
    const int n2 = countryPts_.size();
    refilter(0);    setFootYearFilter(0);           // 真"全部"
    const int nAll = countryPts_.size();

    const bool ok0   = (n0 == 6);
    const bool ok1   = (n1 == 4) && (p1 == n1);    // 2024：4 点且 paths 对齐
    const bool ok2   = (n2 == 2);
    const bool okAll = (nAll == 6);
    if (!ok0 || !ok1 || !ok2 || !okAll) {
        std::fprintf(stderr,
            "LA_MAP_COUNTRY_YEAR_QA FAIL n0=%d n1=%d p1=%d n2=%d nAll=%d broken=%d\n",
            n0, n1, p1, n2, nAll, int(broken));
        std::fflush(stderr);
        return;
    }
    std::fprintf(stderr,
        "LA_MAP_COUNTRY_YEAR_QA PASS n0=%d n1=%d n2=%d nAll=%d broken=%d\n",
        n0, n1, n2, nAll, int(broken));
    std::fflush(stderr);
}

// A 国家层深化（2.27.0）：国层相册按年分桶聚类。注入跨年点 → 进入国层 → 改年筛选后，
// countryAlbumGroups() 须给出正确的逐年分组（2024 单年、4 张）。
// FORCE_BROKEN：分组忽略年份（全归一组）→ 组数/年数不符 → RED。
void MapView::qaCountryAlbum()
{
    const bool broken = !qEnvironmentVariableIsEmpty("LA_MAP_COUNTRY_ALBUM_QA_FORCE_BROKEN");
    albumForceBroken_ = broken;
    int fi = -1;
    for (int i = 0; i < worldNames_.size(); ++i)
        if (worldNames_[i] == QStringLiteral("India")) { fi = i; break; }
    if (fi < 0) {
        std::fprintf(stderr, "LA_MAP_COUNTRY_ALBUM_QA name=India NOT_FOUND\n");
        std::fflush(stderr);
        return;
    }
    const QRectF& bb = featureBbox_[fi];
    const double cx = (bb.left() + bb.right()) / 2, cy = (bb.top() + bb.bottom()) / 2;
    const int years[6] = {2024, 2024, 2024, 2024, 2023, 2023};
    QVector<QPointF> allPts; QStringList allPaths; QVector<QDate> allDates;
    for (int i = 0; i < 6; ++i) {
        allPts.append(QPointF(cx + (i - 2.5) * 0.8, cy));
        allPaths.append(QStringLiteral("synthetic/a%1.jpg").arg(i));
        allDates.append(QDate(years[i], 6, 1));
    }
    auto refilter = [&](int year) {
        gpsPts_.clear(); gpsPaths_.clear(); gpsDates_.clear();
        for (int i = 0; i < allPts.size(); ++i) {
            if (year > 0 && allDates[i].year() != year) continue;
            gpsPts_.append(allPts[i]); gpsPaths_.append(allPaths[i]); gpsDates_.append(allDates[i]);
        }
    };
    refilter(0); enterCountry(fi);
    refilter(2024); setFootYearFilter(2024);
    const QVector<QPair<int, QStringList>> groups = countryAlbumGroups();
    int totalPaths = 0, nYears = 0;
    for (const auto& g : groups) { totalPaths += g.second.size(); if (g.first != 0) ++nYears; }
    const bool okTotal = (totalPaths == 4);        // 2024 筛选后 4 张
    const bool okYears = (nYears == 1) && !groups.isEmpty() && (groups.first().first == 2024);
    if (!okTotal || !okYears) {
        std::fprintf(stderr,
            "LA_MAP_COUNTRY_ALBUM_QA FAIL total=%d years=%d groups=%d broken=%d\n",
            totalPaths, nYears, groups.size(), int(broken));
        std::fflush(stderr);
        return;
    }
    std::fprintf(stderr,
        "LA_MAP_COUNTRY_ALBUM_QA PASS total=%d years=%d broken=%d\n",
        totalPaths, nYears, int(broken));
    std::fflush(stderr);
}

// A 国家层深化（2.27.0）：热力分档临界点可调。默认 3/8：dens=5 → tier1；
// setHeatTiers(2,3) 后 dens=5 → tier2。FORCE_BROKEN：setHeatTiers 失效 + heatTierOf
// 回退硬编码 → 档位不变 → RED。期望值（tier 由 1→2）不随 broken 变（铁律22/32）。
void MapView::qaHeatTier()
{
    const bool broken = !qEnvironmentVariableIsEmpty("LA_MAP_HEAT_TIER_QA_FORCE_BROKEN");
    heatTierForceBroken_ = broken;
    const int t0 = heatTierOf(5);            // 默认 3/8：5>=3 → tier1
    setHeatTiers(2, 3);
    const int t1 = heatTierOf(5);            // 2/3：5>=3 → tier2
    const bool ok = (t0 == 1) && (t1 == 2) && (t0 != t1);
    if (!ok) {
        std::fprintf(stderr,
            "LA_MAP_HEAT_TIER_QA FAIL t0=%d t1=%d broken=%d\n", t0, t1, int(broken));
        std::fflush(stderr);
        return;
    }
    std::fprintf(stderr,
        "LA_MAP_HEAT_TIER_QA PASS t0=%d t1=%d broken=%d\n", t0, t1, int(broken));
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
        pm.fill(Lumen::EMERALD_HI);
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
    ring(0,  dst.width() * 0.30, goldGlow(180), 2.2);
    ring(90, dst.width() * 0.22, jadeGlow(150), 1.6);

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
        QColor lc = Lumen::GOLD;
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
        QColor bc = Lumen::GOLD;
        bc.setAlphaF(0.9 * e);
        p.setPen(QPen(bc, 1.4));
        p.setBrush(Qt::NoBrush);
        p.drawPath(bd);
    }
    // 中心爆发点（金环 + 实心芯，与世界层「我的位置」同语言）
    p.setPen(QPen(Lumen::GOLD, 1.5));
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
    const QColor cardCols[3] = { Lumen::EMERALD_HI, Lumen::GOLD,
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
    g.setColorAt(0.0,  jadeGlow(150));
    g.setColorAt(0.35, jadeGlow(80));
    g.setColorAt(1.0,  jadeGlow(0));
    p.setBrush(g);
    p.setPen(Qt::NoPen);
    p.drawEllipse(c, D * 0.45, D * 0.45);
    // 翡翠描边环
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(jadeGlow(140), 1.4));
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

// A 方向：轨迹/热力绘制。颜色校验铁律：青色轨迹 / 暖色热力均实测，
// 与翡翠光点（绿主导）、金色航线（金）色相分离，门禁像素统计互不污染。
QVector<int> MapView::sortedTrackIdx() const
{
    QVector<int> idx(gpsPts_.size());
    for (int i = 0; i < idx.size(); ++i) idx[i] = i;
    std::stable_sort(idx.begin(), idx.end(), [this](int a, int b) {
        const bool va = a < gpsDates_.size() && gpsDates_[a].isValid();
        const bool vb = b < gpsDates_.size() && gpsDates_[b].isValid();
        if (va != vb) return va;                          // 有日期的在前
        if (va && gpsDates_[a] != gpsDates_[b])
            return gpsDates_[a] < gpsDates_[b];           // 按日期升序连折线
        return a < b;                                     // 稳定：保持原序
    });
    return idx;
}

// 世界层轨迹（预投影逻辑坐标直接绘制，painter 已 scale(PIX)）
void MapView::drawTrajectoryWorld(QPainter& p) const
{
    const QVector<int> ord = sortedTrackIdx();
    if (ord.size() < 2) return;
    QPainterPath path;
    path.moveTo(gpsPts_[ord[0]]);
    for (int i = 1; i < ord.size(); ++i) path.lineTo(gpsPts_[ord[i]]);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(0x3f, 0xb0, 0xe8, 90), 3.0)); p.drawPath(path);  // 柔光底
    p.setPen(QPen(QColor(0x7f, 0xe0, 0xff), 1.3));      p.drawPath(path);  // 亮芯
    p.setPen(QPen(QColor(0xbf, 0xf0, 0xff), 1.0)); p.setBrush(Qt::NoBrush);
    for (int i = 0; i < ord.size(); ++i) p.drawEllipse(gpsPts_[ord[i]], 1.6, 1.6);
    // A 方向第二步：方向箭头（静态层，一次栅格化）
    QVector<QPointF> w;
    w.reserve(ord.size());
    for (int i : ord) w.append(gpsPts_[i]);
    drawTrackArrows(p, w);
}

// 方向箭头：每段中点画一个朝向下一段的三角（静态烘焙层内，随轨迹一起栅格化）。
// pts 已变换到目标坐标系（世界层=预投影；下钻层=已乘 scale 的 widget 坐标）。
void MapView::drawTrackArrows(QPainter& p, const QVector<QPointF>& pts) const
{
    if (pts.size() < 2) return;
    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0xbf, 0xf0, 0xff, 210));
    for (int i = 0; i + 1 < pts.size(); ++i) {
        const QPointF a = pts[i], b = pts[i + 1];
        const double dx = b.x() - a.x(), dy = b.y() - a.y();
        const double len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-6) continue;                 // 退化段（重合点）不画
        const QPointF mid((a.x() + b.x()) / 2.0, (a.y() + b.y()) / 2.0);
        const double ux = dx / len, uy = dy / len; // 单位方向
        const double L = 3.4, Wd = 2.3;           // 箭头长 / 半宽
        QPainterPath tri;
        tri.moveTo(mid + QPointF(ux * L, uy * L));                       // 尖（朝前）
        tri.lineTo(mid + QPointF(-ux * L * 0.5 - uy * Wd, -uy * L * 0.5 + ux * Wd));
        tri.lineTo(mid + QPointF(-ux * L * 0.5 + uy * Wd, -uy * L * 0.5 - ux * Wd));
        tri.closeSubpath();
        p.drawPath(tri);
    }
    p.restore();
}

// 播放头归一化位置 → 预投影点。frac∈[0,1] 按「累计弧长」插值（非按点序号），
// 使匀速沿折线前进。超出范围钳到端点。绘制与门禁断言共用本函数（契约同源铁律）。
QPointF MapView::trackPosAt(double frac) const
{
    const QVector<int> ord = sortedTrackIdx();
    if (ord.isEmpty()) return QPointF();
    if (ord.size() == 1) return gpsPts_[ord[0]];
    const double f = std::max(0.0, std::min(1.0, frac));
    // 累计弧长
    QVector<double> acc(ord.size(), 0.0);
    for (int i = 1; i < ord.size(); ++i) {
        const QPointF a = gpsPts_[ord[i - 1]], b = gpsPts_[ord[i]];
        const double dx = b.x() - a.x(), dy = b.y() - a.y();
        acc[i] = acc[i - 1] + std::sqrt(dx * dx + dy * dy);
    }
    const double total = acc.last();
    if (total < 1e-9) return gpsPts_[ord.first()];   // 全部重合
    const double target = f * total;
    for (int i = 1; i < ord.size(); ++i) {
        if (target <= acc[i]) {
            const double segLen = acc[i] - acc[i - 1];
            const double localT = segLen < 1e-9 ? 0.0 : (target - acc[i - 1]) / segLen;
            const QPointF a = gpsPts_[ord[i - 1]], b = gpsPts_[ord[i]];
            return a + (b - a) * localT;
        }
    }
    return gpsPts_[ord.last()];
}

// 播放头（动态层）：翡翠高亮圆 + 外扩脉冲环 + 身后拖尾。
// 铁律：本函数只在 paintMap 动态段调用，**禁**进 bakeViewPix/buildWorldPixmap ——
// 否则每帧重栅格化整条轨迹，LA_MAP_PERF 预算会被击穿。
void MapView::drawTrackPlayback(QPainter& p) const
{
    if (footLayer_ != FL_Track) return;           // 仅轨迹层播放
    const QVector<int> ord = sortedTrackIdx();
    if (ord.size() < 2) return;
    // 播放进度：reduceMotion_ 或非播放态 → 终态（1.0，末点脉冲驻留）
    double frac = 1.0;
    if (trackActive_ && !reduceMotion_) {
        const double e = double(trackClock_.elapsed()) / double(kTrackDurMs);
        frac = std::max(0.0, std::min(1.0, e));
    }
    if (trackTOverride_ >= 0) frac = std::max(0.0, std::min(1.0, trackTOverride_ / double(kTrackDurMs)));
    const QPointF pos = trackPosAt(frac);

    const QRectF dst = rect().adjusted(8, 8, -8, -8);
    QPointF w = pos;
    if (level_ == World) {
        // 世界层：与 buildWorldPixmap 同变换（scale + 漂移 + 纵向偏移）
        const double scale = dst.height() / MAP_H * worldZoom_;
        w = preToWidget(pos, dst, scale, -driftPx_);
        w.ry() += worldOffYClamped() * scale;
    } else {
        double s = 1;
        const QPointF o = viewOrigin(curViewBbox_, dst, s);
        w = o + pos * s;
    }
    p.save();
    p.setClipRect(dst);
    // 拖尾：沿轨迹回溯 3 段递减半径的半透明点（示意来路）
    for (int k = 3; k >= 1; --k) {
        const double f2 = std::max(0.0, frac - 0.035 * k);
        QPointF t = trackPosAt(f2);
        QPointF tw = t;
        if (level_ == World) {
            const double scale = dst.height() / MAP_H * worldZoom_;
            tw = preToWidget(t, dst, scale, -driftPx_);
            tw.ry() += worldOffYClamped() * scale;
        } else {
            double s = 1;
            const QPointF o = viewOrigin(curViewBbox_, dst, s);
            tw = o + t * s;
        }
        p.setPen(Qt::NoPen);
        p.setBrush(jadeGlow(60 * (4 - k) / 3));
        p.drawEllipse(tw, 2.0 + k * 0.5, 2.0 + k * 0.5);
    }
    // 脉冲环（随进度收缩）
    const double pulse = trackActive_ && !reduceMotion_
        ? std::fmod(trackClock_.elapsed() / 260.0, 1.0) : 1.0;
    p.setPen(QPen(jadeGlow(200), 1.6));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(w, 4.0 + pulse * 5.0, 4.0 + pulse * 5.0);
    // 实心芯
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x9f, 0xf2, 0xcf));
    p.drawEllipse(w, 3.2, 3.2);
    p.restore();
}

void MapView::startTrackPlayback()
{
    if (footLayer_ != FL_Track || sortedTrackIdx().size() < 2) return;  // 无轨迹/非轨迹层不播
    trackClock_.restart();
    trackActive_ = true;
    if (!reduceMotion_) trackTimer_.start(16);   // 减弱动效：直接终态，不空转
    update();
}

void MapView::stopTrackPlayback()
{
    trackActive_ = false;
    trackTimer_.stop();
    update();
}

// A 方向第四步：热力色带唯一真源。dens 决定档位（低 <3 / 中 3-7 / 高 >=8），
// 每档三色（中心/中段/边缘透明）。三条色带的**色相刻意分离**且都与既有元素
// 不撞色：翡翠光点绿主导、金色航线 r≈g、青色轨迹蓝主导、暖橙 r 主导。
// 铁律：绘制与门禁断言共用本函数，禁在 drawHeatWorld / drawHeat 各写一份。
MapView::HeatColors MapView::heatRampOf(int rampId, int tier) const
{
    // tier: 0=低 1=中 2=高
    if (rampId == HR_Jade) {          // 翡翠分级：青绿→翠→亮青（呼应「翡翠足迹点」）
        static const QColor k[3][3] = {
            { QColor(0x2f, 0xa8, 0x7a, 150), QColor(0x3f, 0xc8, 0x92, 70), QColor(0x4f, 0xd8, 0xa6, 0) },
            { QColor(0x1f, 0x88, 0x9a, 180), QColor(0x2f, 0xa8, 0xb0, 85), QColor(0x3f, 0xc8, 0xc0, 0) },
            { QColor(0x14, 0x6a, 0xb4, 215), QColor(0x24, 0x8a, 0xd4, 100), QColor(0x34, 0xaa, 0xf0, 0) },
        };
        return { k[tier][0], k[tier][1], k[tier][2] };
    }
    if (rampId == HR_Crimson) {       // 绯红分级：暗红→绯红→亮粉（高密度最刺眼）
        static const QColor k[3][3] = {
            { QColor(0x8e, 0x24, 0x4a, 160), QColor(0xa8, 0x34, 0x5c, 75), QColor(0xc0, 0x44, 0x70, 0) },
            { QColor(0xc0, 0x1e, 0x3c, 195), QColor(0xdc, 0x2e, 0x50, 90), QColor(0xf0, 0x3e, 0x64, 0) },
            { QColor(0xf0, 0x14, 0x34, 230), QColor(0xff, 0x3a, 0x58, 110), QColor(0xff, 0x66, 0x7c, 0) },
        };
        return { k[tier][0], k[tier][1], k[tier][2] };
    }
    // HR_Warm（默认 = 2.12.0 基线，LA_MAP_HEAT_QA 的 r-g>90 && r-b>90 断言依赖它）
    static const QColor k[3][3] = {
        { QColor(0xd9, 0x7a, 0x2a, 165), QColor(0xe0, 0x8c, 0x36, 75), QColor(0xe8, 0x9c, 0x44, 0) },
        { QColor(0xf0, 0x5a, 0x22, 195), QColor(0xf8, 0x6e, 0x30, 90), QColor(0xff, 0x82, 0x40, 0) },
        { QColor(0xff, 0x3a, 0x18, 225), QColor(0xff, 0x52, 0x2a, 105), QColor(0xff, 0x6a, 0x3a, 0) },
    };
    return { k[tier][0], k[tier][1], k[tier][2] };
}

// 按密度取当前色带的对应档位（分档临界点可调：heatTierLo_/heatTierHi_）
MapView::HeatColors MapView::heatRamp(int dens) const
{
    if (heatRampForceTier_ >= 0)   // QA 反例：强制单档，抹掉密度分级
        return heatRampOf(heatRamp_, heatRampForceTier_);
    return heatRampOf(heatRamp_, heatTierOf(dens));
}

// A 国家层深化（2.27.0）：当前可调阈值下的密度档位（0=低 1=中 2=高）。
// 与绘制共用同一阈值，杜绝门禁与实现漂移。heatTierForceBroken_ 反例下
// 回退硬编码 3/8，使 setHeatTiers 的改动不可见 → 门禁 RED。
int MapView::heatTierOf(int dens) const
{
    const int d = std::max(0, dens);
    if (heatTierForceBroken_)
        return (d >= 8) ? 2 : (d >= 3 ? 1 : 0);   // 回退硬编码（反例）
    return (d >= heatTierHi_) ? 2 : (d >= heatTierLo_ ? 1 : 0);
}

// A 国家层深化（2.27.0）：热力分档临界点可调。
void MapView::setHeatTiers(int lo, int hi)
{
    if (heatTierForceBroken_) return;   // QA 反例：setHeatTiers 失效 → 阈值不变
    const int l = qBound(1, lo, 100);
    const int h = qBound(l + 1, hi, 200);
    if (l == heatTierLo_ && h == heatTierHi_) return;
    heatTierLo_ = l;
    heatTierHi_ = h;
    buildWorldPixmap();         // 急切实调（热力在静态层内，保持 qaShoot* 探针契约）
    invalidateViewCache();      // 下钻静态层重烘焙
    update();
}

void MapView::setHeatRamp(int rampId)
{
    const int r = (rampId < HR_Warm || rampId > HR_Crimson) ? HR_Warm : rampId;
    if (r == heatRamp_) return;
    heatRamp_ = r;
    buildWorldPixmap();         // 急切实调（热力在静态层内，保持 qaShoot* 探针契约）
    invalidateViewCache();  // 下钻静态层重烘焙
    update();
}

// 世界层热力（按密度分档色带：密度越高 halo 越大、色相越靠高档）
void MapView::drawHeatWorld(QPainter& p) const
{
    if (gpsPts_.isEmpty()) return;
    const QVector<int> dens = computeDensity();
    for (int i = 0; i < gpsPts_.size(); ++i) {
        const QPointF& pt = gpsPts_[i];
        if (pt.x() < 0 || pt.x() > MAP_W || pt.y() < 0 || pt.y() > MAP_H) continue;
        const int d = dens.isEmpty() ? 0 : dens[i];
        const double r = 6.0 + std::min(d, 8) * 1.6;
        const HeatColors hc = heatRamp(d);
        QRadialGradient g(pt, r);
        g.setColorAt(0.0, hc.c0);
        g.setColorAt(0.5, hc.c1);
        g.setColorAt(1.0, hc.c2);
        p.setBrush(g); p.setPen(Qt::NoPen);
        p.drawEllipse(pt, r, r);
    }
}

// 下钻层轨迹（与 drawGpsDots 同一 viewOrigin 变换）
void MapView::drawTrajectory(QPainter& p, const QRectF& bbox, const QRectF& dst) const
{
    const QVector<int> ord = sortedTrackIdx();
    if (ord.size() < 2) return;
    double scale = 1;
    const QPointF o = viewOrigin(bbox, dst, scale);
    p.save();
    p.setClipRect(dst);
    QPainterPath path;
    path.moveTo(o + gpsPts_[ord[0]] * scale);
    for (int i = 1; i < ord.size(); ++i) path.lineTo(o + gpsPts_[ord[i]] * scale);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(0x3f, 0xb0, 0xe8, 90), 3.2)); p.drawPath(path);
    p.setPen(QPen(QColor(0x7f, 0xe0, 0xff), 1.4));      p.drawPath(path);
    p.setPen(QPen(QColor(0xbf, 0xf0, 0xff), 1.0)); p.setBrush(Qt::NoBrush);
    for (int i = 0; i < ord.size(); ++i) p.drawEllipse(o + gpsPts_[ord[i]] * scale, 2.2, 2.2);
    // A 方向第二步：方向箭头（静态层，随下钻静态层一起栅格化）
    QVector<QPointF> w;
    w.reserve(ord.size());
    for (int i : ord) w.append(o + gpsPts_[i] * scale);
    p.restore();
    drawTrackArrows(p, w);
}

// 下钻层热力
void MapView::drawHeat(QPainter& p, const QRectF& bbox, const QRectF& dst) const
{
    if (gpsPts_.isEmpty()) return;
    double scale = 1;
    const QPointF o = viewOrigin(bbox, dst, scale);
    const QVector<int> dens = computeDensity();
    p.save();
    p.setClipRect(dst);
    for (int i = 0; i < gpsPts_.size(); ++i) {
        const QPointF pt = o + gpsPts_[i] * scale;
        if (pt.x() < dst.left() || pt.x() > dst.right() ||
            pt.y() < dst.top()  || pt.y() > dst.bottom()) continue;
        const int d = dens.isEmpty() ? 0 : dens[i];
        const double r = (6.0 + std::min(d, 8) * 1.6) * scale;
        const HeatColors hc = heatRamp(d);   // 与世界层同一色带真源
        QRadialGradient g(pt, r);
        g.setColorAt(0.0, hc.c0);
        g.setColorAt(0.5, hc.c1);
        g.setColorAt(1.0, hc.c2);
        p.setBrush(g); p.setPen(Qt::NoPen);
        p.drawEllipse(pt, r, r);
    }
    p.restore();
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
    p.setPen(QPen(Lumen::GOLD, 1.5));
    p.setBrush(Qt::NoBrush);
    p.drawLine(QLineF(lx + 6, y, lx + 18, y));
    drawLabel(p, QPointF(lx + 22, y - 3), "航线", QColor(0xd8, 0xe2, 0xd4));
    y += 20;
    // 我的位置
    p.setPen(QPen(Lumen::GOLD, 1.5));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QPointF(lx + 12, y), 4, 4);
    drawLabel(p, QPointF(lx + 22, y - 3), "我的位置", QColor(0xd8, 0xe2, 0xd4));
}

void MapView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    paintMap(p);
    // F3 a11y：键盘聚焦可见反馈——翡翠焦点环（与全局 focusRing / GalleryTile 同语言）。
    // 仅自绘 QWidget 需手动描边；按钮类已由 Lumen::focusRing() 全局覆盖。
    if (hasFocus()) {
        p.save();
        p.setPen(QPen(Lumen::EMERALD_HI, 2));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(rect().adjusted(2, 2, -3, -3), 6, 6);
        p.restore();
    }
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
        ensureWorldPix();
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

    // A 方向第二步：轨迹播放头（动态层 —— 禁入 bakeViewPix，否则每帧重栅格化破 PERF）
    drawTrackPlayback(p);

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
        p.setPen(Lumen::GOLD);
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
int MapView::qaPerf()
{
    resize(1200, 800);   // 离屏门里 MapView 是堆叠页非当前页、尺寸极小 → 强制真实尺寸
    invalidateViewCache();
    QImage img(size(), QImage::Format_ARGB32_Premultiplied);
    img.fill(0xff0a1016);
    int fail = 0;
    // 证据化门槛（先测再定 + 铁律25/31）：world 每帧 = 世界层 1:1 blit + 图例/页脚。
    // LA_MAP_PERF_DETAIL 实测本机稳态 world_base≈1.15ms、overlay≈0.15ms；跑完整套件、
    // 多进程并发时 20 帧均值会因偶发调度/降频长帧飘到 ~1.5ms。原预算 1.2ms 距稳态仅
    // ~4% 余量 → 在负载下必然 flaky。要防的真实回退是改回每帧缩放 blit 固定 3000×1500
    // ≈2.30ms。预算取 1.6ms：高于含负载飘移的稳态均值，又明显低于 2.30 回退签名 →
    // 真回退时每帧整体右移照样越线（判定不空心）。region/province/country 同为 1:1
    // blit，基线 ≈0.25-0.55ms，预算 1.5ms 留足防回退余量。
    auto bench = [&](const char* name, double budgetMs, const std::function<void()>& enter) -> double {
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
        const double ms = double(t.elapsed()) / 20.0;
        std::fprintf(stderr, "LA_MAP_PERF %s=%.2fms\n", name, ms);
        if (ms > budgetMs) {
            std::fprintf(stderr, "LA_MAP_PERF FAIL %s=%.2fms > budget=%.2fms\n", name, ms, budgetMs);
            fail = 1;
        }
        std::fflush(stderr);
        return ms;
    };
    bench("world", 1.6, [] {});
    // A 取证：拆出世界层「基础地图 blit + 图例 + 页脚」与「每帧覆盖层」各自耗时。
    // 覆盖层 = 大区锚点标签(10) + 地理热点徽章(N，各含 setFont+drawText)，当前未并入 worldPix_。
    if (!qEnvironmentVariableIsEmpty("LA_MAP_PERF_DETAIL")) {
        auto timeWorld = [this, &img]() -> double {
            { QPainter p(&img); paintMap(p); }   // warmup
            QElapsedTimer tw; tw.start();
            for (int i = 0; i < 20; ++i) { QPainter p(&img); paintMap(p); }
            return double(tw.elapsed()) / 20.0;
        };
        const double full = timeWorld();
        qaSkipWorldOverlay_ = true;
        const double base = timeWorld();
        qaSkipWorldOverlay_ = false;
        std::fprintf(stderr, "LA_MAP_PERF world_base=%.2fms world_overlay=%.2fms\n",
                     base, full - base);
        std::fflush(stderr);
    }
    bench("region", 1.5, [this] { enterRegion(QStringLiteral("china")); });
    bench("province", 1.5, [this] {
        if (!curProvinces_.isEmpty()) enterProvince(curProvinces_.first().name);
    });
    bench("country", 1.5, [this] {
        while (level_ != World) goUp();
        for (int i = 0; i < worldNames_.size(); ++i)
            if (worldNames_[i] == QStringLiteral("Vietnam")) { enterCountry(i); break; }
    });
    while (level_ != World) goUp();
    return fail;
}

void MapView::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    invalidateViewCache();
    worldDirty_ = true;   // 世界层显示分辨率随尺寸变化 → 惰性重烘焙
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
        // 方向 C·联动：省热点优先命中（徽章绘于省面之上，点徽章即按省联动图库，不抢下钻）
        {
            const int hi = hotspotHitIndex(e->pos());
            if (hi >= 0) {
                const GeoHotspot& h = geoHotspots_.at(hi);
                emit hotspotActivated(h.key, h.label, h.isProvince);
                return;
            }
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
        // 方向 C·联动：热点优先命中（徽章绘于国/区之上，点徽章即联动图库，不抢下方下钻）
        // ★ 锚点优先豁免：徽章已上抬 r+8，理论上不压锚点；但质心回退分支（无 RegionInfo 的
        //   国家）仍可能偶然靠近某锚点。故先判「是否点在任一锚点 10px 内」——是则让位给下钻，
        //   保证「点国家下钻」这一既有交互永不被联动徽章吞掉（LA_MAP_CLICK_QA anchor/all）。
        {
            bool onAnchor = false;
            for (const RegionInfo& ri : regions_) {
                QPointF aw = preToWidget(ri.proj, dst, scale, -driftPx_);
                double awx = dst.x() + fmod(aw.x() - dst.x(), span);
                if (awx < dst.x()) awx += span;
                if (std::hypot(awx - e->pos().x(), (aw.y() - offY) - e->pos().y()) < 10) {
                    onAnchor = true; break;
                }
            }
            if (!onAnchor) {
                const int hi = hotspotHitIndex(e->pos());
                if (hi >= 0) {
                    const GeoHotspot& h = geoHotspots_.at(hi);
                    emit hotspotActivated(h.key, h.label, h.isProvince);
                    return;
                }
            }
        }
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
// A 国家层深化（2.27.0）：国层快照重算。从当前 gpsPts_/gpsPaths_/gpsDates_ 按几何 pip
// 重新筛选属于当前国家的点，刷新 countryPts_/countryPaths_/countryDates_。
// 关键：footYear 过滤已在 MapPage::computeGpsPoints 注入 gpsPts_ 时完成，
// 此处只做「几何归属 + 快照」，故改年/改库后只要 gpsPts_ 变化并触发本函数，
// 国层的点击命中集与相册聚类载荷即与显示点（drawGpsDots 用 gpsPts_）保持一致。
void MapView::recomputeCountryPts()
{
    if (countryFeature_ < 0) return;
    if (gpsPts_.size() != gpsPaths_.size()) return;  // 对齐守卫：gpsPaths_ 未就绪不重算
    countryPaths_.clear();
    countryPts_.clear();
    countryDates_.clear();
    const WorldFeature& wf = worldFeatures_[countryFeature_];
    const QRectF& bb = featureBbox_[countryFeature_];
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
        countryPaths_.append(gpsPaths_[i]);
        countryPts_.append(pt);
        if (i < gpsDates_.size()) countryDates_.append(gpsDates_[i]);
    }
}

// A 国家层深化（2.27.0）：国层相册按年分桶。footYear 已在 gpsPts_ 注入时过滤，
// 故此处分组即「当前筛选年下的逐年聚类」，供 onCountryPicked 展示与相册聚类门禁断言。
// albumForceBroken_：反例下忽略年份，全部归为 year=0 一组（抹掉分桶）。
QVector<QPair<int, QStringList>> MapView::countryAlbumGroups() const
{
    QVector<QPair<int, QStringList>> out;
    for (int i = 0; i < countryPaths_.size(); ++i) {
        const int y = albumForceBroken_ ? 0
                      : (i < countryDates_.size() && countryDates_[i].isValid())
                        ? countryDates_[i].year() : 0;
        int g = -1;
        for (int k = 0; k < out.size(); ++k) if (out[k].first == y) { g = k; break; }
        if (g < 0) { out.append(QPair<int, QStringList>(y, QStringList())); g = out.size() - 1; }
        out[g].second.append(countryPaths_[i]);
    }
    // 年份降序；0（无日期/全部）置底
    std::stable_sort(out.begin(), out.end(), [](const QPair<int, QStringList>& a,
                                                const QPair<int, QStringList>& b) {
        if (a.first == 0) return false;
        if (b.first == 0) return true;
        return a.first > b.first;
    });
    return out;
}

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
    recomputeCountryPts();   // A 国家层深化：抽出的几何 pip 过滤（含 countryDates_ 快照）
    curViewBbox_ = featureBbox_[countryFeature_];
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

// ============================================================================
// F3 a11y·键盘可达（方向 F 第三步，2.11.0）
//   滚轮缩放 / 拖拽平移 / 下钻 的键盘等价，视口中心锚定，保证纯键盘可操作。
//   与鼠标/滚轮逻辑共用同一套坐标变换（铁律：绘制与命中同源），仅触发方式不同。
// ============================================================================

// 键盘缩放：factor>1 放大，锚定视口中心（保持中心预投影点不动）。
void MapView::zoomBy(double factor)
{
    const QRectF dst = rect().adjusted(8, 8, -8, -8);
    if (level_ == World) {
        const double base = dst.height() / MAP_H;
        if (base <= 0) return;
        const double oldScale = base * worldZoom_;
        const double oldOffY = worldOffYClamped();
        const QPointF cur = rect().center();
        const QPointF pre = widgetToPre(cur, dst, oldScale, wrapX(driftPx_), oldOffY);
        const double nz = qBound(1.0, worldZoom_ * factor, 4.0);
        if (qFuzzyCompare(nz, worldZoom_)) return;
        const double newScale = base * nz;
        driftPx_  = wrapX(pre.x() - cur.x() / newScale);
        worldOffY_ = pre.y() * newScale - (cur.y() - dst.y());
        worldZoom_ = nz;
        update();
    } else {
        const double nz = qBound(1.0, viewZoom_ * factor, 8.0);
        if (qFuzzyCompare(nz, viewZoom_)) return;
        viewZoom_ = nz;
        const QPointF cur = rect().center();
        double s0 = 1;
        const QPointF o0 = viewOrigin(curViewBbox_, dst, s0);
        const QPointF pre = (cur - o0) / s0;
        double s1 = 1;
        viewOrigin(curViewBbox_, dst, s1);
        const QPointF bc((curViewBbox_.left() + curViewBbox_.right()) * 0.5,
                         (curViewBbox_.top() + curViewBbox_.bottom()) * 0.5);
        viewPan_ = cur - QPointF(pre.x() * s1, pre.y() * s1)
                   - dst.center() + QPointF(bc.x() * s1, bc.y() * s1);
        clampViewPan(curViewBbox_, dst, s1);
        invalidateViewCache();
        update();
    }
}

// 键盘平移：以屏幕像素为单位移动视口（世界层→漂移/纵向偏移；下钻层→viewPan_）。
void MapView::panBy(double dx, double dy)
{
    const QRectF dst = rect().adjusted(8, 8, -8, -8);
    if (level_ == World) {
        const double scale = dst.height() / MAP_H * worldZoom_;
        if (scale > 0) {
            driftPx_  = wrapX(driftPx_ - dx / scale);
            worldOffY_ -= dy / scale;
        }
    } else {
        viewPan_ += QPointF(dx, dy);
        double sp = 1;
        viewOrigin(curViewBbox_, dst, sp);
        clampViewPan(curViewBbox_, dst, sp);
        invalidateViewCache();
    }
    update();
}

// 键盘下钻：在视口中心触发与鼠标点击一致的下钻（命中同一套变换数学）。
// 世界层→大区/国家视图；大区层→省；省层→若爆炸弹层激活则打开首张照片，否则返回上一级。
void MapView::drillAtCenter()
{
    if (level_ == World) {
        const QRectF dst = rect().adjusted(8, 8, -8, -8);
        const double scale = dst.height() / MAP_H * worldZoom_;
        const double offY = worldOffYClamped();
        const QPointF cur = rect().center();
        const QPointF pre = widgetToPre(cur, dst, scale, wrapX(driftPx_), offY);
        const double span = MAP_W * scale;
        for (int fi = 0; fi < worldFeatures_.size(); ++fi) {
            if (!featureBbox_[fi].contains(pre)) continue;
            if (!pointInFeature(pre, worldFeatures_[fi])) continue;
            const int r = featureRegion_.value(fi, -1);
            if (r >= 0) { emit picked(regions_[r].id, regions_[r].name, false); enterRegion(regions_[r].id); }
            else enterCountry(fi);
            return;
        }
        for (const RegionInfo& ri : regions_) {       // 回退：大区锚点圆命中
            QPointF w = preToWidget(ri.proj, dst, scale, -driftPx_);
            double wx = dst.x() + fmod(w.x() - dst.x(), span);
            if (wx < dst.x()) wx += span;
            if (std::hypot(wx - cur.x(), (w.y() - offY) - cur.y()) < 10) {
                emit picked(ri.id, ri.name, false); enterRegion(ri.id); return;
            }
        }
    } else if (level_ == Region) {
        if (curProvinces_.isEmpty()) { goUp(); return; }   // 国家视图无更深层级 → 返回
        const QRectF dst = rect().adjusted(8, 8, -8, -8);
        double scale = 1;
        QRectF bbox; bool first = true;
        for (const ProvinceInfo& pv : curProvinces_) { if (first){bbox=pv.bbox;first=false;} else bbox|=pv.bbox; }
        const QPointF vo = viewOrigin(bbox, dst, scale);
        const QPointF cur = rect().center();
        for (const ProvinceInfo& pv : curProvinces_) {
            QPainterPath path; const QPointF o = vo;
            path.moveTo(o + pv.ring.pts.first() * scale);
            for (int i = 1; i < pv.ring.pts.size(); ++i) path.lineTo(o + pv.ring.pts[i] * scale);
            path.closeSubpath();
            if (path.contains(cur)) {
                const QString label = pv.zh.isEmpty() ? pv.name : pv.zh;
                emit picked(pv.name, label, true); enterProvince(pv.name); return;
            }
        }
    } else if (level_ == Province) {
        if (burstActive_ && !burstPaths_.isEmpty())
            emit openPhotoRequested(burstPaths_.first());
        else
            goUp();
    }
}

void MapView::keyPressEvent(QKeyEvent* e)
{
    switch (e->key()) {
    case Qt::Key_Plus:
    case Qt::Key_Equal:        // = / + 放大
        zoomBy(1.25); e->accept(); break;
    case Qt::Key_Minus:
    case Qt::Key_Underscore:    // - 缩小
        zoomBy(1.0 / 1.25); e->accept(); break;
    case Qt::Key_Left:  panBy(-44, 0); e->accept(); break;
    case Qt::Key_Right: panBy( 44, 0); e->accept(); break;
    case Qt::Key_Up:    panBy(0, -44); e->accept(); break;
    case Qt::Key_Down:  panBy(0,  44); e->accept(); break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        drillAtCenter(); e->accept(); break;
    case Qt::Key_Escape:
        if (level_ != World) { goUp(); e->accept(); }   // 键盘返回上一级
        else QWidget::keyPressEvent(e);
        break;
    default:
        QWidget::keyPressEvent(e);
    }
}

// F3 a11y·键盘可达门禁（offscreen 自退）：构造 MapView（资源随 exe 嵌入、QApplication 已存在），
// 断言 ① 焦点策略为 StrongFocus；② Key_Plus 触发 worldZoom_ 增大（键盘等价滚轮放大）。
// 反例 LA_A11Y_KEYBOARD_QA_FORCE_BROKEN=1 → 强制 NoFocus → 门红。
int MapView::qaKeyboardCheck()
{
    MapView mv;
    mv.resize(900, 600);   // 离屏门里尺寸最小 → 强制真实几何，zoom 数学避免除零
    const bool broken = !qEnvironmentVariableIsEmpty("LA_A11Y_KEYBOARD_QA_FORCE_BROKEN");
    mv.setFocusPolicy(broken ? Qt::NoFocus : Qt::StrongFocus);

    // 断言 1：键盘可达（StrongFocus）。broken 模式模拟“修复缺失”→ 无焦点策略 → 门红
    if (!(mv.focusPolicy() & Qt::StrongFocus)) {
        std::fprintf(stderr, "LA_A11Y_KEYBOARD_QA FAIL mapview not keyboard-focusable\n");
        std::fflush(stderr);
        return 1;
    }
    // 断言 2：Key_Plus 触发世界层缩放（worldZoom_ 增大），键盘等价滚轮放大
    const double z0 = mv.worldZoom_;
    QKeyEvent ev(QEvent::KeyPress, Qt::Key_Plus, Qt::NoModifier);
    QCoreApplication::sendEvent(&mv, &ev);
    if (!(mv.worldZoom_ > z0 + 1e-9)) {
        std::fprintf(stderr, "LA_A11Y_KEYBOARD_QA FAIL Plus did not zoom map (z0=%.4f z1=%.4f)\n", z0, mv.worldZoom_);
        std::fflush(stderr);
        return 1;
    }
    std::fprintf(stderr, "LA_A11Y_KEYBOARD_QA PASS mapview=keyboard-navigable worldZoom %.4f->%.4f\n", z0, mv.worldZoom_);
    std::fflush(stderr);
    return 0;
}
