#include "BeautyPage.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QProgressBar>
#include <QScrollArea>
#include <QFileDialog>
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QApplication>
#include <QPixmap>
#include <QIcon>
#include <algorithm>
#include <cstdio>

namespace {
constexpr int kPreviewW = 460, kPreviewH = 520, kMaxSrc = 1024, kMaxExport = 2400;
constexpr int kCardCols = 4, kCardIcon = 140;

QString photoLabel(const Photo& ph)
{
    if (!ph.title.isEmpty()) return ph.title;
    if (!ph.rel.isEmpty())   return QFileInfo(ph.rel).fileName();
    return QFileInfo(ph.path).fileName();
}
} // namespace

BeautyPage::BeautyPage(const QVector<Photo>* photos, const QString& thumbsDir, QWidget* parent)
    : QWidget(parent), photos_(photos), thumbsDir_(thumbsDir)
{
    presets_ = RetouchEngine::presets();
    active_  = presets_.isEmpty() ? RetouchParams{} : presets_.first().p;

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    // ---- 顶栏：照片 + 智能增强 + 导出 ----
    auto* bar = new QWidget(this);
    auto* bl = new QHBoxLayout(bar);
    bl->setContentsMargins(0, 0, 0, 0);
    bl->setSpacing(10);
    photoCombo_ = new QComboBox(bar);
    if (photos_) {
        for (int i = 0; i < photos_->size() && usableIdx_.size() < 300; ++i) {
            if ((*photos_)[i].thumbFile.isEmpty()) continue;
            usableIdx_.append(i);
            photoCombo_->addItem(photoLabel((*photos_)[i]));
        }
    }
    auto mkBtn = [bar](const QString& t, const char* css){
        auto* b = new QPushButton(t, bar);
        b->setStyleSheet(QString::fromLatin1(css));
        return b;
    };
    autoBtn_ = mkBtn(QStringLiteral("智能增强"),
        "QPushButton{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #6adba6, stop:1 #2f7d57);"
        "border:none;border-radius:9px;padding:8px 16px;"
        "color:#06130c;font-weight:bold;}"
        "QPushButton:disabled{background:#2a332d;color:#5a6b60;}");
    exportBtn_ = mkBtn(QStringLiteral("导出副本"),
        "QPushButton{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #f0d08a, stop:1 #b0863a);"
        "border:none;border-radius:9px;padding:8px 16px;"
        "color:#1a1204;font-weight:bold;}"
        "QPushButton:disabled{background:#2a332d;color:#5a6b60;}");
    bl->addWidget(new QLabel(QStringLiteral("照片"), bar));
    bl->addWidget(photoCombo_);
    bl->addWidget(autoBtn_);
    bl->addWidget(exportBtn_);
    bl->addStretch(1);
    root->addWidget(bar);

    // ---- 大图 before/after ----
    auto* pv = new QHBoxLayout; pv->setSpacing(12);
    beforeLbl_ = new QLabel(this); afterLbl_ = new QLabel(this);
    for (QLabel* l : {beforeLbl_, afterLbl_}) {
        l->setFixedSize(kPreviewW, kPreviewH);
        l->setAlignment(Qt::AlignCenter);
        l->setStyleSheet("QLabel{background:#000;border:1px solid #26312a;border-radius:12px;}");
    }
    selName_ = new QLabel(QStringLiteral("原图"), this);
    selName_->setStyleSheet("color:#f4f7f4;font-weight:bold;");
    auto* bl1 = new QVBoxLayout; bl1->setContentsMargins(0,0,0,0);
    bl1->addWidget(beforeLbl_); bl1->addWidget(new QLabel(QStringLiteral("原图"), this), 0, Qt::AlignHCenter);
    auto* bl2 = new QVBoxLayout; bl2->setContentsMargins(0,0,0,0);
    bl2->addWidget(afterLbl_);  bl2->addWidget(selName_, 0, Qt::AlignHCenter);
    pv->addStretch(1); pv->addLayout(bl1); pv->addLayout(bl2); pv->addStretch(1);
    root->addLayout(pv);

    // ---- 预设画廊（滚动） ----
    auto* sa = new QScrollArea(this);
    sa->setWidgetResizable(true);
    sa->setStyleSheet("QScrollArea{background:transparent;border:none;}");
    gallery_ = new QWidget(sa);
    auto* gl = new QGridLayout(gallery_);
    gl->setSpacing(10);
    for (int i = 0; i < presets_.size(); ++i) {
        auto* card = new QToolButton(gallery_);
        card->setFixedSize(kCardIcon + 16, kCardIcon + 36);
        card->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        card->setIconSize(QSize(kCardIcon, kCardIcon));
        card->setText(presets_[i].name);
        card->setStyleSheet("QToolButton{background:#131a15;border:2px solid #26312a;"
            "border-radius:12px;color:#f4f7f4;padding:4px;}");
        connect(card, &QToolButton::clicked, this, [this, i]{ onCardClicked(i); });
        gl->addWidget(card, i / kCardCols, i % kCardCols);
        cards_.append(card);
    }
    sa->setWidget(gallery_);
    root->addWidget(sa, 1);

    // ---- 徽章 + 进度 ----
    auto* foot = new QWidget(this);
    auto* fl = new QHBoxLayout(foot);
    fl->setContentsMargins(0, 0, 0, 0);
    prog_ = new QProgressBar(foot);
    prog_->setRange(0, 100); prog_->setValue(0);
    prog_->setTextVisible(false);
    prog_->setStyleSheet("QProgressBar{background:#0e1410;border:1px solid #26312a;"
        "border-radius:6px;height:8px;} QProgressBar::chunk{border-radius:6px;"
        "background:qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #4fc08a, stop:1 #6adba6);}");
    badge_ = new QLabel(QStringLiteral("选择下方风格卡 · 零云端"), foot);
    badge_->setStyleSheet("color:#8da396;");
    fl->addWidget(prog_, 1);
    fl->addWidget(badge_);
    root->addWidget(foot);

    connect(photoCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &BeautyPage::onPhotoChanged);
    connect(autoBtn_, &QPushButton::clicked, this, &BeautyPage::onAutoEnhance);
    connect(exportBtn_, &QPushButton::clicked, this, &BeautyPage::onExport);

    if (!usableIdx_.isEmpty()) {
        loadPhoto(0);
        onCardClicked(0);
    }

    if (!qEnvironmentVariableIsEmpty("LA_BEAUTY_SMOKE")) {
        QTimer::singleShot(0, this, [this]{
            if (usableIdx_.isEmpty()) {
                std::fprintf(stderr, "LA_BEAUTY_SMOKE no_usable_photo\n");
                std::fflush(stderr); QApplication::quit(); return;
            }
            loadPhoto(0);
            int nullOut = 0;
            for (const RetouchPreset& pr : presets_) {
                const QImage o = RetouchEngine::apply(srcImg_, pr.p);
                if (o.isNull()) ++nullOut;
            }
            std::fprintf(stderr,
                "LA_BEAUTY_SMOKE ok=1 presets=%d thumbs=%d nullOut=%d\n",
                presets_.size(), cards_.size(), nullOut);
            std::fflush(stderr);
            QApplication::quit();
        });
    }
}

void BeautyPage::loadPhoto(int idx)
{
    if (idx < 0 || idx >= usableIdx_.size()) return;
    const Photo& ph = (*photos_)[usableIdx_[idx]];
    const QString path = QDir(thumbsDir_).filePath(ph.thumbFile);
    QImage img(path);
    if (img.isNull()) { srcImg_ = QImage(); beforeLbl_->setText(QStringLiteral("无缩略图")); return; }
    if (img.width() > kMaxSrc || img.height() > kMaxSrc)
        img = img.scaled(kMaxSrc, kMaxSrc, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    srcImg_ = img;
    srcPm_ = QPixmap::fromImage(img);
    beforeLbl_->setPixmap(srcPm_.scaled(kPreviewW, kPreviewH, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    refreshGallery();
}

void BeautyPage::refreshGallery()
{
    if (srcImg_.isNull()) { for (auto* c : cards_) c->setIcon(QIcon()); return; }
    const QImage gsrc = srcImg_.scaled(220, 220, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    for (int i = 0; i < presets_.size() && i < cards_.size(); ++i) {
        const QImage thumb = RetouchEngine::apply(gsrc, presets_[i].p);
        cards_[i]->setIcon(QIcon(QPixmap::fromImage(thumb)));
    }
}

void BeautyPage::onCardClicked(int i)
{
    if (i < 0 || i >= presets_.size()) return;
    sel_ = i;
    active_ = presets_[i].p;
    for (int k = 0; k < cards_.size(); ++k) {
        cards_[k]->setStyleSheet(QString::fromLatin1(
            k == sel_
            ? "QPushButton{background:#1a231d;border:2px solid #4fc08a;border-radius:12px;color:#f4f7f4;padding:4px;}"
            : "QPushButton{background:#131a15;border:2px solid #26312a;border-radius:12px;color:#f4f7f4;padding:4px;}"));
    }
    selName_->setText(presets_[i].name);
    updatePreview();
}

void BeautyPage::onAutoEnhance()   // rlAi
{
    if (srcImg_.isNull()) return;
    active_ = RetouchEngine::recommend(srcImg_).params;
    selName_->setText(QStringLiteral("智能增强"));
    // 取消卡片高亮（AI 非预设）
    for (auto* c : cards_)
        c->setStyleSheet("QPushButton{background:#131a15;border:2px solid #26312a;"
            "border-radius:12px;color:#f4f7f4;padding:4px;}");
    updatePreview();
}

void BeautyPage::onExport()
{
    if (srcImg_.isNull()) return;
    const Photo& ph = (*photos_)[usableIdx_[photoCombo_->currentIndex()]];
    QImage full = QImage(ph.path);
    QImage work = full.isNull() ? srcImg_ : full;
    if (work.width() > kMaxExport || work.height() > kMaxExport)
        work = work.scaled(kMaxExport, kMaxExport, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const QImage out = RetouchEngine::apply(work, active_);
    const QString fname = QFileDialog::getSaveFileName(this, QStringLiteral("导出风格化副本"),
        QDir::homePath() + "/" + QFileInfo(ph.path).baseName() + "_style.jpg",
        QStringLiteral("JPEG (*.jpg);;PNG (*.png)"));
    if (fname.isEmpty()) return;
    if (out.save(fname)) {
        badge_->setText(QStringLiteral("已导出 · %1 · 零云端").arg(QFileInfo(fname).fileName()));
        prog_->setValue(100);
    } else {
        badge_->setText(QStringLiteral("导出失败"));
    }
}

void BeautyPage::updatePreview()
{
    if (srcImg_.isNull()) { afterLbl_->setText(QStringLiteral("无缩略图")); return; }
    const QImage out = RetouchEngine::apply(srcImg_, active_);
    afterLbl_->setPixmap(QPixmap::fromImage(out).scaled(kPreviewW, kPreviewH,
        Qt::KeepAspectRatio, Qt::SmoothTransformation));
    badge_->setText(QStringLiteral("%1 · 零云端").arg(selName_->text()));
    prog_->setValue(100);
}

void BeautyPage::onPhotoChanged(int)   // 忽略 idx，用 combo 当前值
{
    if (photoCombo_->currentIndex() < 0) return;
    loadPhoto(photoCombo_->currentIndex());
    onCardClicked(sel_);   // 重新套用当前风格
}
