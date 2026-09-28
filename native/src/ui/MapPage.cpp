#include "MapPage.h"
#include "Theme.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

MapPage::MapPage(const QVector<Photo>* photos, QWidget* parent) : QWidget(parent), photos_(photos)
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
    ll->addWidget(bar);

    view_ = new MapView(left);
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
    sl->addStretch(1);
    lay->addWidget(stats_);

    connect(view_, &MapView::breadcrumbChanged, crumb_, &QLabel::setText);
    connect(back_, &QPushButton::clicked, view_, &MapView::goUp);
    connect(view_, &MapView::breadcrumbChanged, this, [this](const QString& t) {
        Q_UNUSED(t);
        back_->setEnabled(view_->level() != MapView::World);
    });
    connect(view_, &MapView::picked, this, &MapPage::onPicked);
    connect(viewBtn_, &QPushButton::clicked, this, &MapPage::onViewInGallery);
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
