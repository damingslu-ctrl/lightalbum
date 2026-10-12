#include "MapPage.h"
#include "../core/GeoHotspot.h"   // 方向 C·联动：热点聚合（与渲染/门禁同源）
#include <QCoreApplication>
#include "Theme.h"
#include <QPointF>
#include <QDir>
#include <QPixmap>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QDialog>
#include <QComboBox>
#include <QLineEdit>
#include <QDoubleValidator>
#include <QDialogButtonBox>
#include <QDate>
#include <QButtonGroup>
#include <QComboBox>
#include <QList>
#include <QSet>
#include <algorithm>
#include <functional>

MapPage::MapPage(const QVector<Photo>* photos, const QString& thumbsDir,
                 QWidget* parent) : QWidget(parent), photos_(photos), thumbsDir_(thumbsDir)
{
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    // ---- 左侧：面包屑条 + 地图 ----
    auto* left = new QWidget(this);
    auto* ll = new QVBoxLayout(left);
    ll->setContentsMargins(0, 0, 0, 0);
    ll->setSpacing(0);

    auto* bar = new QWidget(left);
    bar->setStyleSheet(QString::fromUtf8("background:%1;border-bottom:1px solid %2;")
        .arg(Lumen::hex(Lumen::PANEL), Lumen::hex(Lumen::LINE)));
    auto* bl = new QHBoxLayout(bar);
    bl->setContentsMargins(16, 8, 16, 8);

    back_ = new QPushButton(QCoreApplication::translate("LightAlbum", "\u2190 返回"), bar);
    back_->setEnabled(false);
    back_->setStyleSheet(Lumen::btnGhost());   // 默认 GOLD tint = 原字面金
    crumb_ = new QLabel(QCoreApplication::translate("LightAlbum", "世界"), bar);
    crumb_->setStyleSheet(QStringLiteral("color:%1;")
        .arg(Lumen::hex(Lumen::GOLD_HI))
        + Lumen::Type::css(Lumen::Type::Tier::Body));   // T8：13px=T4 原位
    bl->addWidget(back_);
    bl->addWidget(crumb_);
    // 迭代 1：世界层导航 —— 平移/缩放后一键回到默认视角（缩放归 1 + 中国居中）
    recenterBtn_ = new QPushButton(QString::fromUtf8("\u2302 \u56de\u5230\u4e2d\u56fd"), bar);
    recenterBtn_->setCursor(Qt::PointingHandCursor);
    recenterBtn_->setToolTip(QCoreApplication::translate("LightAlbum", "回到默认视角：缩放归 1、中国居中"));
    recenterBtn_->setStyleSheet(Lumen::btnGhost(Lumen::EMERALD));
    connect(recenterBtn_, &QPushButton::clicked, this, &MapPage::onRecenter);
    bl->addWidget(recenterBtn_);
    bl->addStretch(1);
    // P16.4.2 重新定位（web 版 relocateBtn 对应物：基点=「我的位置」，金色航线起点）
    relocBtn_ = new QPushButton(QCoreApplication::translate("LightAlbum", "◎ 重新定位"), bar);
    relocBtn_->setCursor(Qt::PointingHandCursor);
    relocBtn_->setToolTip(QCoreApplication::translate("LightAlbum", "设置金色航线的起点（我的位置）"));
    relocBtn_->setStyleSheet(Lumen::btnGhost(Lumen::EMERALD));
    bl->addWidget(relocBtn_);
    // A 方向：足迹层三态开关（光点/轨迹/热力）。默认光点 → 零回归。
    // 铁律：三串必须以 translate() 字面量直调——经 lambda 形参传入会让 lupdate
    // 无法静态抽取（形参是变量非字面量），导致英文模式回落中文硬编码。
    footLayerGroup_ = new QButtonGroup(this);
    footLayerGroup_->setExclusive(true);
    // 幽灵底 + checked 态翡翠点亮：btnGhost 无 checked 语义，:checked 追加串走 token
    //（T4 先例：可勾选按钮保留 btnGhost() + 追加 :checked 规则）
    const QString segStyle = Lumen::btnGhost(Lumen::MUTED) + QStringLiteral(
        "QPushButton:checked{background:%1;border-color:%2;color:%2;}")
        .arg(Lumen::hex(Lumen::CARD_HI), Lumen::hex(Lumen::EMERALD));
    auto mkSeg = [&](const QString& label, int mode) {
        QPushButton* b = new QPushButton(label, bar);
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setStyleSheet(segStyle);
        footLayerGroup_->addButton(b, mode);
        bl->addWidget(b);
        return b;
    };
    mkSeg(QCoreApplication::translate("LightAlbum", "光点"), 0)->setChecked(true);
    mkSeg(QCoreApplication::translate("LightAlbum", "轨迹"), 1);
    mkSeg(QCoreApplication::translate("LightAlbum", "热力"), 2);
    connect(footLayerGroup_, QOverload<int>::of(&QButtonGroup::idClicked),
            this, [this](int id) { view_->setFootLayerMode(id); });
    // A 方向第二步：播放足迹（仅轨迹层可用；离开轨迹层即禁用）
    trackPlayBtn_ = new QPushButton(
        QCoreApplication::translate("LightAlbum", "▶ 播放足迹"), bar);
    trackPlayBtn_->setCursor(Qt::PointingHandCursor);
    trackPlayBtn_->setEnabled(false);
    trackPlayBtn_->setToolTip(QCoreApplication::translate(
        "LightAlbum", "沿轨迹按拍摄日期依次播放足迹（需先切到「轨迹」层）"));
    trackPlayBtn_->setStyleSheet(Lumen::btnGhost(Lumen::EMERALD));
    connect(trackPlayBtn_, &QPushButton::clicked, this, [this] {
        if (view_->trackPlaying()) view_->stopTrackPlayback();
        else                       view_->startTrackPlayback();
    });
    connect(footLayerGroup_, QOverload<int>::of(&QButtonGroup::idClicked),
            this, [this](int id) { trackPlayBtn_->setEnabled(id == 1); });  // 仅轨迹层
    bl->addWidget(trackPlayBtn_);
    // A 方向第三步：按年筛选。只列库中真实出现的年份（数据诚实，不补空档）。
    yearBox_ = new QComboBox(bar);
    yearBox_->setCursor(Qt::PointingHandCursor);
    yearBox_->setToolTip(QCoreApplication::translate(
        "LightAlbum", "按拍摄年份筛选足迹（对光点/轨迹/热力三层同时生效）"));
    yearBox_->addItem(QCoreApplication::translate("LightAlbum", "全部年份"), 0);
    for (int y : computeFootYears())
        yearBox_->addItem(QString::number(y), y);
    yearBox_->setStyleSheet(QString::fromUtf8(
        "QComboBox{background:%1;border:1px solid %2;border-radius:9px;"
        "padding:4px 8px;color:%3;min-width:86px;}"
        "QComboBox QAbstractItemView{background:%4;color:%5;"
        "selection-background-color:%6;selection-color:%7;}")
        .arg(Lumen::hex(Lumen::CARD))      // %1 盘底（token 值字面量归引用）
        .arg(Lumen::hex(Lumen::LINE))      // %2
        .arg(Lumen::hex(Lumen::INK))       // %3（原抄写盘文字 → INK，spec §1.7）
        .arg(Lumen::hex(Lumen::PANEL))     // %4 下拉盘（原菜单底 → PANEL，spec §1.7）
        .arg(Lumen::hex(Lumen::INK))       // %5
        .arg(Lumen::hex(Lumen::CARD_HI))   // %6 选中底（原菜单选中底 → CARD_HI：选中态归宿）
        .arg(Lumen::hex(Lumen::EMERALD))); // %7
    connect(yearBox_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        footYear_ = yearBox_->currentData().toInt();   // 0 = 全部年份
        syncFootData();                                // 三层同步过滤
        view_->setArcRegionIds(computeArcRegionIds());
        view_->stopTrackPlayback();                    // 集合已变，旧的播放进度失效
    });
    bl->addWidget(yearBox_);
    // A 方向第四步：热力色带（按密度分级）。仅热力层可用。
    heatRampBox_ = new QComboBox(bar);
    heatRampBox_->setCursor(Qt::PointingHandCursor);
    heatRampBox_->setToolTip(QCoreApplication::translate(
        "LightAlbum", "热力色带：按拍摄密度分级着色（需先切到「热力」层）"));
    heatRampBox_->addItem(QCoreApplication::translate("LightAlbum", "暖橙"), 0);
    heatRampBox_->addItem(QCoreApplication::translate("LightAlbum", "翡翠"), 1);
    heatRampBox_->addItem(QCoreApplication::translate("LightAlbum", "绯红"), 2);
    heatRampBox_->setStyleSheet(QString::fromUtf8(
        "QComboBox{background:%1;border:1px solid %2;border-radius:9px;"
        "padding:4px 8px;color:%3;min-width:64px;}"
        "QComboBox QAbstractItemView{background:%4;color:%5;"
        "selection-background-color:%6;selection-color:%7;}")
        .arg(Lumen::hex(Lumen::CARD))
        .arg(Lumen::hex(Lumen::LINE))
        .arg(Lumen::hex(Lumen::INK))
        .arg(Lumen::hex(Lumen::PANEL))
        .arg(Lumen::hex(Lumen::INK))
        .arg(Lumen::hex(Lumen::CARD_HI))
        .arg(Lumen::hex(Lumen::EMERALD)));
    connect(heatRampBox_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        view_->setHeatRamp(heatRampBox_->currentData().toInt());
    });
    connect(footLayerGroup_, QOverload<int>::of(&QButtonGroup::idClicked),
            this, [this](int id) { heatRampBox_->setEnabled(id == 2); });  // 仅热力层
    bl->addWidget(heatRampBox_);
    ll->addWidget(bar);

    view_ = new MapView(left);
    syncFootData();   // A 方向第三步：足迹数据同步统一入口（含 pts/dates/paths/计数）
    view_->setArcRegionIds(computeArcRegionIds()); // P16.4.2：有作品大区 → 航线目标
    ll->addWidget(view_, 1);
    lay->addWidget(left, 1);

    // ---- 右侧：统计侧栏 ----
    stats_ = new QWidget(this);
    stats_->setFixedWidth(272);
    stats_->setStyleSheet(QString::fromUtf8(
        "QWidget{background:%1;border-left:1px solid %2;}")
        .arg(Lumen::hex(Lumen::PANEL),                        // T7 裁定：0a0e0a→PANEL（ΔRGB≤8 不可辨，侧栏=面板语义）
             Lumen::hex(Lumen::LINE)));
    auto* sl = new QVBoxLayout(stats_);
    sl->setContentsMargins(18, 18, 18, 18);
    sl->setSpacing(12);

    auto* head = new QLabel(QCoreApplication::translate("LightAlbum", "区域统计"), stats_);
    head->setFont(Lumen::Type::head());   // T8：14pt 粗体区块标题归 T3(12pt 粗)，ls 3.0 随档归零 §2.4
    head->setStyleSheet(QStringLiteral("color:%1;").arg(Lumen::hex(Lumen::INK)));
    sl->addWidget(head);

    statsTitle_ = new QLabel(QCoreApplication::translate("LightAlbum", "尚未选择区域"), stats_);
    statsTitle_->setFont(Lumen::Type::head());   // T8：13pt 粗体归 T3(12pt 粗)
    statsTitle_->setStyleSheet(QStringLiteral("color:%1;").arg(Lumen::hex(Lumen::GOLD)));
    statsTitle_->setWordWrap(true);
    sl->addWidget(statsTitle_);

    statsBody_ = new QLabel(QStringLiteral(
        "点击地图上的大区或省份，查看该地作品统计，并可一键在图库筛选。"), stats_);
    statsBody_->setStyleSheet(QStringLiteral("color:%1;")
        .arg(Lumen::hex(Lumen::MUTED))
        + Lumen::Type::css(Lumen::Type::Tier::Small));   // T8：12px=T5 原位
    statsBody_->setWordWrap(true);
    sl->addWidget(statsBody_);

    viewBtn_ = new QPushButton(QCoreApplication::translate("LightAlbum", "在图库查看 →"), stats_);
    viewBtn_->setCursor(Qt::PointingHandCursor);
    viewBtn_->setEnabled(false);
    viewBtn_->setStyleSheet(Lumen::btnGold());
    sl->addWidget(viewBtn_);

    // 迭代 2：一键用爆炸弹层播放「当前区域」全部作品（省 / 大区 / 国家三层级通用）
    playBtn_ = new QPushButton(QCoreApplication::translate("LightAlbum", "▶ 播放本区作品"), stats_);
    playBtn_->setCursor(Qt::PointingHandCursor);
    playBtn_->setToolTip(QCoreApplication::translate("LightAlbum", "用爆炸弹层播放该省/国家/大区的全部作品（最多 10 张）"));
    playBtn_->setEnabled(false);
    playBtn_->setStyleSheet(Lumen::btnGhost(Lumen::EMERALD));
    connect(playBtn_, &QPushButton::clicked, this, &MapPage::onPlayAll);
    sl->addWidget(playBtn_);

    // P16.4.2 航线基点提示（数据事实：默认上海，可经「重新定位」修改）
    homeHint_ = new QLabel(stats_);
    homeHint_->setStyleSheet(QStringLiteral("color:%1;")
        .arg(Lumen::hex(Lumen::MUTED))
        + Lumen::Type::css(Lumen::Type::Tier::Small));   // T8：11px→T5(12px)
    homeHint_->setWordWrap(true);
    homeHint_->setText(QString::fromUtf8(
        "<span style='color:%1;'>✈ 航线基点：%2</span><br>"
        "金色航线从这里出发，连向每一处拍摄地；点上方「◎ 重新定位」可更改。")
        .arg(Lumen::hex(Lumen::GOLD), homeName_));
    sl->addWidget(homeHint_);
    sl->addStretch(1);
    lay->addWidget(stats_);

    connect(view_, &MapView::breadcrumbChanged, crumb_, &QLabel::setText);
    connect(back_, &QPushButton::clicked, view_, &MapView::goUp);
    connect(view_, &MapView::breadcrumbChanged, this, [this](const QString& t) {
        Q_UNUSED(t);
        back_->setEnabled(view_->level() != MapView::World);
        recenterBtn_->setEnabled(view_->level() == MapView::World);   // 仅世界层可回到默认视角
        if (view_->level() == MapView::World) {
            lastCountryPaths_.clear();          // 回世界层：清理国家视图上下文
            playBtn_->setEnabled(false);
        }
        syncGeoHotspots();                       // 方向 C·联动：层级切换后重算热点层
    });
    connect(view_, &MapView::picked, this, &MapPage::onPicked);
    connect(view_, &MapView::hotspotActivated, this, &MapPage::onHotspotActivated);
    connect(view_, &MapView::burstRequested, this, &MapPage::onBurstRequest);
    connect(view_, &MapView::openPhotoRequested, this, &MapPage::requestLightbox);
    connect(view_, &MapView::countryPicked, this, &MapPage::onCountryPicked);
    connect(view_, &MapView::countryBurstRequested, this, &MapPage::onCountryBurst);
    connect(viewBtn_, &QPushButton::clicked, this, &MapPage::onViewInGallery);
    connect(relocBtn_, &QPushButton::clicked, this, &MapPage::onRelocate);
}

void MapPage::setHidePrivate(bool hide)
{
    hidePrivate_ = hide;
    syncFootData();   // 隐私门切换 → 足迹集合可能变小（年份筛选下同样生效）
    view_->setArcRegionIds(computeArcRegionIds()); // P16.4.2：航线目标同步
    // 已选区域时重算一次统计，保持口径一致
    if (!lastRegion_.isEmpty() || !lastProv_.isEmpty())
        onPicked(lastIsProv_ ? lastProv_ : lastRegion_,
                 statsTitle_->text(), lastIsProv_);
}

// A 方向第三步：足迹数据同步唯一入口。pts / dates / paths 三者由同一过滤循环产出
// （computeGpsPoints 内保证下标对齐），再依次注入 MapView —— setGpsPoints 内部会
// 触发 densDirty_/arcTargetsDirty_/invalidateViewCache + 重烘焙世界层。
void MapPage::syncFootData()
{
    view_->setGpsCount(computeGpsCount());
    QStringList gpsPaths;
    QVector<QDate> gpsDates;
    view_->setGpsPoints(computeGpsPoints(&gpsPaths, &gpsDates));
    view_->setGpsPaths(gpsPaths);
    view_->setGpsDates(gpsDates);
    view_->setFootYearFilter(footYear_);
    syncGeoHotspots();                          // 方向 C·联动：足迹集合变化 → 热点同步刷新
}

// 方向 C·联动：依据当前层级 + 隐私门重算地理热点并回灌 MapView。
// World=按 region（国家热点）；Region=按 prov（省热点，限定当前大区）。
void MapPage::syncGeoHotspots()
{
    if (!photos_) { view_->setGeoHotspots({}); return; }
    const MapView::Level lvl = view_->level();
    const QString rid = (lvl == MapView::Region) ? view_->currentRegionId() : QString();
    view_->setGeoHotspots(GeoHotspot::compute(*photos_, hidePrivate_,
                                               static_cast<GeoHotspot::Level>(lvl), rid));
}

// 方向 C·联动：地图热点点击 → 直接进入图库并应用地理筛选（一键联动）。
void MapPage::onHotspotActivated(const QString& key, const QString& label, bool isProvince)
{
    // 记录用于统计（与 onPicked 口径一致），并更新 lastRegion_/lastProv_
    onPicked(key, label, isProvince);
    // 一键联动：region 热点 → setRegionFilter；province 热点 → setProvFilter
    emit requestGallery(isProvince ? QString() : key, isProvince ? key : QString());
}

// P16.1 真实 GPS 足迹数：hasGps 为真且未被隐私门屏蔽的照片计数（诚实，不编造）。
int MapPage::computeGpsCount() const
{
    if (!photos_) return 0;
    int n = 0;
    for (const Photo& p : *photos_)
        if (p.hasGps && !(hidePrivate_ && p.priv)) ++n;
    return n;
}

// P16.4 真实 GPS 足迹点：仅 hasGps 且未被隐私门屏蔽的照片；经纬度 → 预投影空间（与 MapView 一致）
// pathsOut 非空时同步给出与点一一对应的照片原路径（国家视图 pip 匹配用）
QVector<QPointF> MapPage::computeGpsPoints(QStringList* pathsOut, QVector<QDate>* datesOut) const
{
    QVector<QPointF> pts;
    if (pathsOut) pathsOut->clear();
    if (datesOut) datesOut->clear();
    if (!photos_) return pts;
    pts.reserve(photos_->size());
    for (const Photo& p : *photos_) {
        if (!p.hasGps || (hidePrivate_ && p.priv)) continue;
        if (p.la == 0.0 && p.lo == 0.0) continue;   // 数据诚实：零坐标视为无效
        // A 方向第三步：按年筛选。铁律：pts / paths / dates 三者必须同在此处一起过滤，
        // 否则 gpsPaths_ 与 gpsPts_ 下标错位 → 国家视图照片路径对错点。
        if (footYear_ > 0 && p.year != footYear_) continue;
        const double x = (p.lo + 180.0) / 360.0 * MapView::MAP_W;
        const double y = (90.0 - p.la) / 180.0 * MapView::MAP_H;
        pts.append(QPointF(x, y));
        if (pathsOut) pathsOut->append(p.path);
        if (datesOut) {   // A 方向：解析拍摄日期（解析失败 → 无效 QDate，轨迹排序时置后）
            QDate dt = QDate::fromString(p.date.left(10), QStringLiteral("yyyy-MM-dd"));
            if (!dt.isValid())
                dt = QDate::fromString(p.date.left(10), QStringLiteral("yyyy:MM:dd"));
            if (!dt.isValid())
                dt = QDate::fromString(p.date, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
            datesOut->append(dt);
        }
    }
    return pts;
}

// A 方向第三步：库中出现的全部年份（降序）。数据诚实：只列真实有照片的年份，
// 不按 kpi 的 min/max 补齐中间空档。
QList<int> MapPage::computeFootYears() const
{
    QList<int> ys;
    if (!photos_) return ys;
    QSet<int> seen;
    for (const Photo& p : *photos_) {
        if (hidePrivate_ && p.priv) continue;
        if (p.year > 0) seen.insert(p.year);
    }
    ys = seen.values();
    std::sort(ys.begin(), ys.end(), std::greater<int>());
    return ys;
}

// P16.4.2 有作品的大区 id：至少 1 幅（未被隐私门屏蔽）照片的大区，作为航线目标。
// 与 web 版「连向每一处拍摄地」一致；海外大区无 GPS 原图也照连（大区中心是真实经纬度）。
QStringList MapPage::computeArcRegionIds() const
{
    QStringList ids;
    if (!photos_) return ids;
    QSet<QString> seen;
    for (const Photo& p : *photos_) {
        if (hidePrivate_ && p.priv) continue;
        if (!p.region.isEmpty() && !seen.contains(p.region)) {
            seen.insert(p.region);
            ids.append(p.region);
        }
    }
    return ids;
}

void MapPage::onPicked(const QString& key, const QString& label, bool isProvince)
{
    if (isProvince) { lastProv_ = key; lastRegion_ = QString(); }
    else            { lastRegion_ = key; lastProv_ = QString(); }
    lastIsProv_ = isProvince;

    const int total = photos_ ? photos_->size() : 0;
    int match = 0;
    if (photos_) {
        const QString k = key.toLower();
        for (const Photo& p : *photos_) {
            if (hidePrivate_ && p.priv) continue;   // P16 隐私门
            if (isProvince) { if (p.prov.toLower() == k) ++match; }
            else            { if (p.region.toLower() == k) ++match; }
        }
    }
    statsTitle_->setText(label);
    QString body = QCoreApplication::translate("LightAlbum", "本库作品总计：<b>%1</b> 幅<br>").arg(total);
    body += isProvince
        ? QCoreApplication::translate("LightAlbum", "该省作品：<b>%1</b> 幅").arg(match)
        : QCoreApplication::translate("LightAlbum", "该大区作品：<b>%1</b> 幅").arg(match);
    if (match == 0)
        body += QCoreApplication::translate("LightAlbum", "<br><span style='color:%1;'>（该区域暂无 GPS 作品，可导入带 GPS 的照片）</span>")
                    .arg(Lumen::hex(Lumen::MUTED));
    statsBody_->setText(body);
    viewBtn_->setEnabled(true);
    playBtn_->setEnabled(match > 0);
    lastCountryPaths_.clear();   // 进入省/大区视图：清掉国家视图上下文
}

void MapPage::onViewInGallery()
{
    emit requestGallery(lastRegion_, lastProv_);
}

// 一键播放「当前区域」全部作品：国家视图用 MapView 算好的该国 GPS 路径，
// 其余按 region/prov 键匹配（与统计口径一致，尊重隐私门），最多 10 张。
void MapPage::onPlayAll()
{
    QStringList paths;
    const bool countryView = !lastCountryPaths_.isEmpty();
    if (countryView) {
        paths = lastCountryPaths_;
    } else if (photos_) {
        const QString k = (lastIsProv_ ? lastProv_ : lastRegion_).toLower();
        if (!k.isEmpty()) {
            for (const Photo& ph : *photos_) {
                if (paths.size() >= 10) break;
                if (hidePrivate_ && ph.priv) continue;
                if ((lastIsProv_ ? ph.prov : ph.region).toLower() != k) continue;
                paths.append(ph.path);
            }
        }
    }
    QVector<QPixmap> thumbs;
    if (photos_) {
        for (const QString& p : paths) {
            if (thumbs.size() >= 10) break;
            for (const Photo& ph : *photos_) {
                if (ph.path != p) continue;
                if (ph.thumbFile.isEmpty()) break;
                QPixmap pm(QDir(thumbsDir_).filePath(ph.thumbFile));
                if (pm.isNull()) break;
                thumbs.append(pm.scaled(192, 144,
                                        Qt::KeepAspectRatioByExpanding,
                                        Qt::SmoothTransformation));
                break;
            }
        }
    }
    view_->startBurst(thumbs, paths);
}

// 回到默认视角（世界层）：缩放归 1 + 中国居中
void MapPage::onRecenter()
{
    view_->resetWorldView();
}

// 国家视图统计：该国 GPS 作品数（pip 由 MapView 完成），不接入图库筛选（无 region 键）
void MapPage::onCountryPicked(const QString& name, const QStringList& paths)
{
    lastRegion_.clear();
    lastProv_.clear();
    lastIsProv_ = false;
    statsTitle_->setText(name);
    QString body = QCoreApplication::translate("LightAlbum", "本库作品总计：<b>%1</b> 幅<br>").arg(photos_ ? photos_->size() : 0);
    body += QCoreApplication::translate("LightAlbum", "该国家/地区 GPS 作品：<b>%1</b> 幅").arg(paths.size());
    if (paths.isEmpty())
        body += QCoreApplication::translate("LightAlbum", "<br><span style='color:%1;'>（该国暂无 GPS 作品）</span>")
                    .arg(Lumen::hex(Lumen::MUTED));
    statsBody_->setText(body);
    viewBtn_->setEnabled(false);
    lastCountryPaths_ = paths;          // 供「▶ 播放本区作品」使用
    playBtn_->setEnabled(!paths.isEmpty());
}

// 国家视图爆炸弹层：按路径匹配照片 → 缩略图（与省视图同一爆炸演出）
void MapPage::onCountryBurst(const QString& name, const QStringList& paths)
{
    Q_UNUSED(name);
    QVector<QPixmap> thumbs;
    if (photos_) {
        for (const QString& p : paths) {
            if (thumbs.size() >= 10) break;
            for (const Photo& ph : *photos_) {
                if (ph.path != p) continue;
                if (ph.thumbFile.isEmpty()) continue;
                QPixmap pm(QDir(thumbsDir_).filePath(ph.thumbFile));
                if (pm.isNull()) break;
                thumbs.append(pm.scaled(192, 144,
                                        Qt::KeepAspectRatioByExpanding,
                                        Qt::SmoothTransformation));
                break;
            }
        }
    }
    view_->startBurst(thumbs, paths);
}

// 省视图爆炸弹层：按省收集照片缩略图（≤10 张，尊重隐私门），回灌 MapView 播放。
// 缩略图读 thumbsDir/thumbFile；无照片 → 空表（MapView 只播冲击波，不假装有图）。
void MapPage::onBurstRequest(const QString& key, const QString& label)
{
    Q_UNUSED(label);
    QVector<QPixmap> thumbs;
    QStringList paths;
    if (photos_) {
        const QString k = key.toLower();
        for (const Photo& ph : *photos_) {
            if (thumbs.size() >= 10) break;
            if (hidePrivate_ && ph.priv) continue;
            if (ph.prov.toLower() != k) continue;
            if (ph.thumbFile.isEmpty()) continue;
            QPixmap pm(QDir(thumbsDir_).filePath(ph.thumbFile));
            if (pm.isNull()) continue;
            thumbs.append(pm.scaled(192, 144,
                                    Qt::KeepAspectRatioByExpanding,
                                    Qt::SmoothTransformation));
            paths.append(ph.path);
        }
    }
    view_->startBurst(thumbs, paths);
}

// P16.4.2 重新定位：城市预设 / 自定义经纬度 → setHomePoint。
// 本机桌面无浏览器定位 API，采用显式选择（诚实：不假装自动定位）。
void MapPage::onRelocate()
{
    QDialog dlg(this);
    dlg.setWindowTitle(QCoreApplication::translate("LightAlbum", "重新定位 · 航线基点"));
    dlg.setStyleSheet(QString::fromUtf8(
        "QDialog{background:%1;}"
        "QLabel{color:%2;%10}"
        "QComboBox,QLineEdit{background:%3;border:1px solid %4;border-radius:8px;"
        "padding:6px 8px;color:%5;}"
        "QPushButton{background:%6;border:1px solid %7;border-radius:8px;"
        "padding:6px 14px;color:%8;}"
        "QPushButton QPushButton{color:%9;}")
        .arg(Lumen::hex(Lumen::PANEL))    // %1 弹层底（原菜单底 → PANEL，spec §1.7）
        .arg(Lumen::hex(Lumen::INK))      // %2（原抄写盘文字 → INK，spec §1.7）
        .arg(Lumen::hex(Lumen::CARD))     // %3（token 值字面量）
        .arg(Lumen::hex(Lumen::LINE))     // %4
        .arg(Lumen::hex(Lumen::INK))      // %5
        .arg(Lumen::hex(Lumen::CARD))     // %6
        .arg(Lumen::hex(Lumen::LINE))     // %7
        .arg(Lumen::hex(Lumen::INK))      // %8
        .arg(Lumen::hex(Lumen::INK))      // %9
        .arg(Lumen::Type::css(Lumen::Type::Tier::Small)));   // T8（%10）：12px=T5 原位
    auto* vl = new QVBoxLayout(&dlg);
    vl->setContentsMargins(18, 16, 18, 14);
    vl->setSpacing(10);

    vl->addWidget(new QLabel(QString::fromUtf8(
        "金色航线以「我的位置」为基点，连向每一处拍摄地。选择常驻城市，或直接输入经纬度："), &dlg));
    auto* combo = new QComboBox(&dlg);
    struct City { const char* zh; double lon, lat; };
    const City cities[] = {
        {"上海", 121.47, 31.23}, {"北京", 116.40, 39.90}, {"广州", 113.26, 23.13},
        {"深圳", 114.06, 22.55}, {"成都", 104.07, 30.57}, {"杭州", 120.15, 30.27},
        {"西安", 108.94, 34.34}, {"武汉", 114.31, 30.59}, {"乌鲁木齐", 87.62, 43.82},
        {"拉萨", 91.11, 29.66}, {"中国香港", 114.17, 22.32}, {"中国台北", 121.56, 25.03},
    };
    int curIdx = 0;
    for (int i = 0; i < int(sizeof(cities) / sizeof(City)); ++i) {
        combo->addItem(QString::fromUtf8(cities[i].zh));
        if (QString::fromUtf8(cities[i].zh) == homeName_) curIdx = i;
    }
    combo->addItem(QCoreApplication::translate("LightAlbum", "自定义经纬度…"));
    combo->setCurrentIndex(curIdx);
    vl->addWidget(combo);

    auto* row = new QHBoxLayout();
    auto* lonEd = new QLineEdit(&dlg);
    auto* latEd = new QLineEdit(&dlg);
    auto* dv = new QDoubleValidator(-180.0, 180.0, 4, &dlg);
    lonEd->setValidator(dv);
    latEd->setValidator(new QDoubleValidator(-90.0, 90.0, 4, &dlg));
    lonEd->setPlaceholderText(QCoreApplication::translate("LightAlbum", "经度 (东经为正)"));
    latEd->setPlaceholderText(QCoreApplication::translate("LightAlbum", "纬度 (北纬为正)"));
    row->addWidget(lonEd);
    row->addWidget(latEd);
    vl->addLayout(row);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    bb->button(QDialogButtonBox::Ok)->setText(QCoreApplication::translate("LightAlbum", "确定"));
    bb->button(QDialogButtonBox::Cancel)->setText(QCoreApplication::translate("LightAlbum", "取消"));
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    vl->addWidget(bb);

    auto apply = [&](double lon, double lat, const QString& name) {
        view_->setHomePoint(lon, lat);
        homeName_ = name;
        homeHint_->setText(QString::fromUtf8(
            "<span style='color:%1;'>✈ 航线基点：%2</span><br>"
            "金色航线从这里出发，连向每一处拍摄地；点上方「◎ 重新定位」可更改。")
            .arg(Lumen::hex(Lumen::GOLD), name));
    };
    connect(combo, &QComboBox::currentIndexChanged, &dlg, [=, &dlg](int idx) {
        const bool custom = (idx == combo->count() - 1);
        lonEd->setEnabled(custom);
        latEd->setEnabled(custom);
        Q_UNUSED(dlg);
    });
    // 默认填充当前基点
    lonEd->setText(QString::number(view_->homeLon(), 'f', 2));
    latEd->setText(QString::number(view_->homeLat(), 'f', 2));
    const bool isPreset = (combo->currentIndex() != combo->count() - 1);
    lonEd->setEnabled(!isPreset);
    latEd->setEnabled(!isPreset);

    if (dlg.exec() != QDialog::Accepted) return;
    const int idx = combo->currentIndex();
    if (idx < int(sizeof(cities) / sizeof(City))) {
        apply(cities[idx].lon, cities[idx].lat, QString::fromUtf8(cities[idx].zh));
    } else {
        bool ok1 = false, ok2 = false;
        const double lon = lonEd->text().toDouble(&ok1);
        const double lat = latEd->text().toDouble(&ok2);
        if (!ok1 || !ok2) return;   // 输入无效则不动基点
        apply(lon, lat, QString::fromUtf8("%1, %2")
              .arg(latEd->text(), lonEd->text()));
    }
}
