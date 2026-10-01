#include "MapPage.h"
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
    bar->setStyleSheet(QStringLiteral("background:#0c100d;border-bottom:1px solid #26312a;"));
    auto* bl = new QHBoxLayout(bar);
    bl->setContentsMargins(16, 8, 16, 8);

    back_ = new QPushButton(QStringLiteral("\u2190 返回"), bar);
    back_->setEnabled(false);
    back_->setStyleSheet(QStringLiteral(
        "QPushButton{background:#131a15;border:1px solid #26312a;border-radius:9px;"
        "padding:5px 12px;color:#d9a94e;}"
        "QPushButton:disabled{color:#5a6b60;}"));
    crumb_ = new QLabel(QStringLiteral("世界"), bar);
    crumb_->setStyleSheet(QStringLiteral("color:#f0d08a;font-size:13px;"));
    bl->addWidget(back_);
    bl->addWidget(crumb_);
    bl->addStretch(1);
    // P16.4.2 重新定位（web 版 relocateBtn 对应物：基点=「我的位置」，金色航线起点）
    relocBtn_ = new QPushButton(QString::fromUtf8("◎ 重新定位"), bar);
    relocBtn_->setCursor(Qt::PointingHandCursor);
    relocBtn_->setToolTip(QString::fromUtf8("设置金色航线的起点（我的位置）"));
    relocBtn_->setStyleSheet(QStringLiteral(
        "QPushButton{background:#131a15;border:1px solid #26312a;border-radius:9px;"
        "padding:5px 12px;color:#4fc08a;}"
        "QPushButton:hover{border-color:#4fc08a;}"));
    bl->addWidget(relocBtn_);
    ll->addWidget(bar);

    view_ = new MapView(left);
    view_->setGpsCount(computeGpsCount());     // P16.1：注入真实 GPS 足迹数
    view_->setGpsPoints(computeGpsPoints());   // P16.4：注入真实 GPS 足迹点
    view_->setArcRegionIds(computeArcRegionIds()); // P16.4.2：有作品大区 → 航线目标
    ll->addWidget(view_, 1);
    lay->addWidget(left, 1);

    // ---- 右侧：统计侧栏 ----
    stats_ = new QWidget(this);
    stats_->setFixedWidth(272);
    stats_->setStyleSheet(QStringLiteral(
        "QWidget{background:#0a0e0a;border-left:1px solid #26312a;}"));
    auto* sl = new QVBoxLayout(stats_);
    sl->setContentsMargins(18, 18, 18, 18);
    sl->setSpacing(12);

    auto* head = new QLabel(QStringLiteral("区域统计"), stats_);
    QFont hf = head->font(); hf.setPointSizeF(14); hf.setBold(true);
    hf.setLetterSpacing(QFont::AbsoluteSpacing, 3.0);
    head->setFont(hf);
    head->setStyleSheet("color:#f4f7f4;");
    sl->addWidget(head);

    statsTitle_ = new QLabel(QStringLiteral("尚未选择区域"), stats_);
    QFont tf = statsTitle_->font(); tf.setPointSizeF(13); tf.setBold(true);
    statsTitle_->setFont(tf);
    statsTitle_->setStyleSheet("color:#d9a94e;");
    statsTitle_->setWordWrap(true);
    sl->addWidget(statsTitle_);

    statsBody_ = new QLabel(QStringLiteral(
        "点击地图上的大区或省份，查看该地作品统计，并可一键在图库筛选。"), stats_);
    statsBody_->setStyleSheet("color:#8da396;font-size:12px;");
    statsBody_->setWordWrap(true);
    sl->addWidget(statsBody_);

    viewBtn_ = new QPushButton(QStringLiteral("在图库查看 →"), stats_);
    viewBtn_->setCursor(Qt::PointingHandCursor);
    viewBtn_->setEnabled(false);
    viewBtn_->setStyleSheet(Lumen::btnGold());
    sl->addWidget(viewBtn_);

    // P16.4.2 航线基点提示（数据事实：默认上海，可经「重新定位」修改）
    homeHint_ = new QLabel(stats_);
    homeHint_->setStyleSheet("color:#8da396;font-size:11px;");
    homeHint_->setWordWrap(true);
    homeHint_->setText(QString::fromUtf8(
        "<span style='color:#d9a94e;'>✈ 航线基点：%1</span><br>"
        "金色航线从这里出发，连向每一处拍摄地；点上方「◎ 重新定位」可更改。")
        .arg(homeName_));
    sl->addWidget(homeHint_);
    sl->addStretch(1);
    lay->addWidget(stats_);

    connect(view_, &MapView::breadcrumbChanged, crumb_, &QLabel::setText);
    connect(back_, &QPushButton::clicked, view_, &MapView::goUp);
    connect(view_, &MapView::breadcrumbChanged, this, [this](const QString& t) {
        Q_UNUSED(t);
        back_->setEnabled(view_->level() != MapView::World);
    });
    connect(view_, &MapView::picked, this, &MapPage::onPicked);
    connect(view_, &MapView::burstRequested, this, &MapPage::onBurstRequest);
    connect(viewBtn_, &QPushButton::clicked, this, &MapPage::onViewInGallery);
    connect(relocBtn_, &QPushButton::clicked, this, &MapPage::onRelocate);
}

void MapPage::setHidePrivate(bool hide)
{
    hidePrivate_ = hide;
    view_->setGpsCount(computeGpsCount());         // P16.1：隐私门切换 → 足迹数同步
    view_->setGpsPoints(computeGpsPoints());       // P16.4：隐私门切换 → 足迹点同步
    view_->setArcRegionIds(computeArcRegionIds()); // P16.4.2：航线目标同步
    // 已选区域时重算一次统计，保持口径一致
    if (!lastRegion_.isEmpty() || !lastProv_.isEmpty())
        onPicked(lastIsProv_ ? lastProv_ : lastRegion_,
                 statsTitle_->text(), lastIsProv_);
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
QVector<QPointF> MapPage::computeGpsPoints() const
{
    QVector<QPointF> pts;
    if (!photos_) return pts;
    pts.reserve(photos_->size());
    for (const Photo& p : *photos_) {
        if (!p.hasGps || (hidePrivate_ && p.priv)) continue;
        if (p.la == 0.0 && p.lo == 0.0) continue;   // 数据诚实：零坐标视为无效
        const double x = (p.lo + 180.0) / 360.0 * MapView::MAP_W;
        const double y = (90.0 - p.la) / 180.0 * MapView::MAP_H;
        pts.append(QPointF(x, y));
    }
    return pts;
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
    QString body = QStringLiteral("本库作品总计：<b>%1</b> 幅<br>").arg(total);
    body += isProvince
        ? QStringLiteral("该省作品：<b>%1</b> 幅").arg(match)
        : QStringLiteral("该大区作品：<b>%1</b> 幅").arg(match);
    if (match == 0)
        body += QStringLiteral("<br><span style='color:#5a6b60;'>（该区域暂无 GPS 作品，可导入带 GPS 的照片）</span>");
    statsBody_->setText(body);
    viewBtn_->setEnabled(true);
}

void MapPage::onViewInGallery()
{
    emit requestGallery(lastRegion_, lastProv_);
}

// 省视图爆炸弹层：按省收集照片缩略图（≤10 张，尊重隐私门），回灌 MapView 播放。
// 缩略图读 thumbsDir/thumbFile；无照片 → 空表（MapView 只播冲击波，不假装有图）。
void MapPage::onBurstRequest(const QString& key, const QString& label)
{
    Q_UNUSED(label);
    QVector<QPixmap> thumbs;
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
        }
    }
    view_->startBurst(thumbs);
}

// P16.4.2 重新定位：城市预设 / 自定义经纬度 → setHomePoint。
// 本机桌面无浏览器定位 API，采用显式选择（诚实：不假装自动定位）。
void MapPage::onRelocate()
{
    QDialog dlg(this);
    dlg.setWindowTitle(QString::fromUtf8("重新定位 · 航线基点"));
    dlg.setStyleSheet(QStringLiteral(
        "QDialog{background:#0e130f;}"
        "QLabel{color:#d9e4dc;font-size:12px;}"
        "QComboBox,QLineEdit{background:#131a15;border:1px solid #26312a;border-radius:8px;"
        "padding:6px 8px;color:#f4f7f4;}"
        "QPushButton{background:#131a15;border:1px solid #26312a;border-radius:8px;"
        "padding:6px 14px;color:#d9e4dc;}"
        "QPushButton QPushButton{color:#d9e4dc;}"));
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
    combo->addItem(QString::fromUtf8("自定义经纬度…"));
    combo->setCurrentIndex(curIdx);
    vl->addWidget(combo);

    auto* row = new QHBoxLayout();
    auto* lonEd = new QLineEdit(&dlg);
    auto* latEd = new QLineEdit(&dlg);
    auto* dv = new QDoubleValidator(-180.0, 180.0, 4, &dlg);
    lonEd->setValidator(dv);
    latEd->setValidator(new QDoubleValidator(-90.0, 90.0, 4, &dlg));
    lonEd->setPlaceholderText(QString::fromUtf8("经度 (东经为正)"));
    latEd->setPlaceholderText(QString::fromUtf8("纬度 (北纬为正)"));
    row->addWidget(lonEd);
    row->addWidget(latEd);
    vl->addLayout(row);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    bb->button(QDialogButtonBox::Ok)->setText(QString::fromUtf8("确定"));
    bb->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("取消"));
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    vl->addWidget(bb);

    auto apply = [&](double lon, double lat, const QString& name) {
        view_->setHomePoint(lon, lat);
        homeName_ = name;
        homeHint_->setText(QString::fromUtf8(
            "<span style='color:#d9a94e;'>✈ 航线基点：%1</span><br>"
            "金色航线从这里出发，连向每一处拍摄地；点上方「◎ 重新定位」可更改。")
            .arg(name));
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
