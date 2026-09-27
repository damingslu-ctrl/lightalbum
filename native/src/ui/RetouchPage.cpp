#include "RetouchPage.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QProgressBar>
#include <QScrollArea>
#include <QGroupBox>
#include <QFrame>
#include <QFileDialog>
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QApplication>
#include <QPixmap>
#include <algorithm>
#include <cstdio>

namespace {
constexpr int kPreviewW = 460, kPreviewH = 520, kMaxSrc = 1024, kMaxExport = 2400;

// 滑杆定义表
const RtSlider kSliders[] = {
    {"exposure",   "曝光",   -1, 1},
    {"brightness", "亮度",   -1, 1},
    {"contrast",   "对比",   -1, 1},
    {"clarity",    "清晰",   -1, 1},
    {"vibrance",   "自然饱和",-1, 1},
    {"saturation", "饱和度", -1, 1},
    {"temp",       "色温",   -1, 1},
    {"tint",       "色调",   -1, 1},
    {"sharpen",    "锐化",    0, 1},
    {"highlights", "高光",   -1, 1},
    {"shadows",    "阴影",   -1, 1},
    {"vignette",   "暗角",   -1, 1},
};

QString photoLabel(const Photo& ph)
{
    if (!ph.title.isEmpty()) return ph.title;
    if (!ph.rel.isEmpty())   return QFileInfo(ph.rel).fileName();
    return QFileInfo(ph.path).fileName();
}
} // namespace

RetouchParams buildFromMap(const QMap<QString, double>& m)
{
    RetouchParams p;
    auto g = [&](const char* k){ return m.value(QString::fromLatin1(k), 0.0); };
    p.exposure = g("exposure");   p.brightness = g("brightness");
    p.contrast = g("contrast");   p.clarity = g("clarity");
    p.vibrance = g("vibrance");   p.saturation = g("saturation");
    p.temp = g("temp");           p.tint = g("tint");
    p.sharpen = g("sharpen");     p.highlights = g("highlights");
    p.shadows = g("shadows");     p.vignette = g("vignette");
    return p;
}

RetouchPage::RetouchPage(const QVector<Photo>* photos, const QString& thumbsDir, QWidget* parent)
    : QWidget(parent), photos_(photos), thumbsDir_(thumbsDir)
{
    for (const RtSlider& s : kSliders) cur_[QString::fromLatin1(s.key)] = 0.0;

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    // ---- 操作条：照片选择 + 预设 + 按钮 ----
    auto* bar = new QWidget(this);
    auto* bl = new QHBoxLayout(bar);
    bl->setContentsMargins(0, 0, 0, 0);
    bl->setSpacing(10);

    photoCombo_ = new QComboBox(bar);
    presetCombo_ = new QComboBox(bar);
    if (photos_) {
        for (int i = 0; i < photos_->size() && usableIdx_.size() < 300; ++i) {
            if ((*photos_)[i].thumbFile.isEmpty()) continue;
            usableIdx_.append(i);
            photoCombo_->addItem(photoLabel((*photos_)[i]));
        }
    }
    presetCombo_->addItem(QStringLiteral("— 手动 —"), QStringLiteral("__manual__"));
    for (const RetouchPreset& pr : RetouchEngine::presets())
        presetCombo_->addItem(pr.name, pr.key);

    auto mkBtn = [bar](const QString& t, const char* css){
        auto* b = new QPushButton(t, bar);
        b->setStyleSheet(QString::fromLatin1(css));
        return b;
    };
    autoBtn_   = mkBtn(QStringLiteral("智能增强"),
        "QPushButton{background:#5a8f6b;border:none;border-radius:8px;padding:8px 16px;"
        "color:#f2f5f3;font-weight:bold;} QPushButton:disabled{background:#5a5f5b;}");
    resetBtn_  = mkBtn(QStringLiteral("重置"),
        "QPushButton{background:#181d1a;border:1px solid #2a2f2c;border-radius:8px;"
        "padding:8px 16px;color:#f2f5f3;}");
    exportBtn_ = mkBtn(QStringLiteral("导出副本"),
        "QPushButton{background:#c19a4b;border:none;border-radius:8px;padding:8px 16px;"
        "color:#10130f;font-weight:bold;}");

    bl->addWidget(new QLabel(QStringLiteral("照片"), bar));
    bl->addWidget(photoCombo_);
    bl->addWidget(new QLabel(QStringLiteral("预设"), bar));
    bl->addWidget(presetCombo_);
    bl->addWidget(autoBtn_);
    bl->addWidget(resetBtn_);
    bl->addWidget(exportBtn_);
    bl->addStretch(1);
    root->addWidget(bar);

    // ---- 预览：before / after ----
    auto* pv = new QHBoxLayout;
    pv->setSpacing(12);
    beforeLbl_ = new QLabel(this);
    afterLbl_  = new QLabel(this);
    for (QLabel* l : {beforeLbl_, afterLbl_}) {
        l->setFixedSize(kPreviewW, kPreviewH);
        l->setAlignment(Qt::AlignCenter);
        l->setStyleSheet("QLabel{background:#000;border:1px solid #2a2f2c;border-radius:10px;}");
    }
    auto* bl1 = new QVBoxLayout; bl1->setContentsMargins(0,0,0,0);
    bl1->addWidget(beforeLbl_); bl1->addWidget(new QLabel(QStringLiteral("原图"), this), 0, Qt::AlignHCenter);
    auto* bl2 = new QVBoxLayout; bl2->setContentsMargins(0,0,0,0);
    bl2->addWidget(afterLbl_);  bl2->addWidget(new QLabel(QStringLiteral("修后"), this), 0, Qt::AlignHCenter);
    pv->addStretch(1); pv->addLayout(bl1); pv->addLayout(bl2); pv->addStretch(1);
    root->addLayout(pv);

    // ---- 建议芯片行 ----
    auto* chipWrap = new QWidget(this);
    auto* cl = new QHBoxLayout(chipWrap);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->setSpacing(8);
    chipTitle_ = new QLabel(QStringLiteral("智能建议："), chipWrap);
    chipTitle_->setStyleSheet("color:#9fa8a3;");
    cl->addWidget(chipTitle_);
    chipRow_ = new QWidget(chipWrap);
    chipRow_->setLayout(new QHBoxLayout);
    chipRow_->layout()->setContentsMargins(0,0,0,0);
    chipRow_->layout()->setSpacing(8);
    cl->addWidget(chipRow_, 1);
    root->addWidget(chipWrap);

    // ---- 手动滑杆组（滚动区） ----
    auto* sa = new QScrollArea(this);
    sa->setWidgetResizable(true);
    sa->setStyleSheet("QScrollArea{background:transparent;border:none;}");
    auto* panel = new QWidget(sa);
    auto* gl = new QGridLayout(panel);
    gl->setColumnStretch(1, 1);
    for (const RtSlider& def : kSliders) buildSlider(def, gl);
    sa->setWidget(panel);
    root->addWidget(sa, 1);

    // ---- 进度条 + 徽章 ----
    auto* foot = new QWidget(this);
    auto* fl = new QHBoxLayout(foot);
    fl->setContentsMargins(0, 0, 0, 0);
    prog_ = new QProgressBar(foot);
    prog_->setRange(0, 100); prog_->setValue(0);
    prog_->setTextVisible(false);
    prog_->setStyleSheet("QProgressBar{background:#181d1a;border:1px solid #2a2f2c;"
        "border-radius:6px;height:8px;} QProgressBar::chunk{background:#c19a4b;border-radius:6px;}");
    badge_ = new QLabel(QStringLiteral("未调整 · 零云端"), foot);
    badge_->setStyleSheet("color:#9fa8a3;");
    fl->addWidget(prog_, 1);
    fl->addWidget(badge_);
    root->addWidget(foot);

    // ---- 信号 ----
    connect(photoCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &RetouchPage::onPhotoChanged);
    connect(presetCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &RetouchPage::onPresetChanged);
    connect(autoBtn_, &QPushButton::clicked, this, &RetouchPage::onAutoEnhance);
    connect(resetBtn_, &QPushButton::clicked, this, &RetouchPage::onReset);
    connect(exportBtn_, &QPushButton::clicked, this, &RetouchPage::onExport);

    applyTimer_ = new QTimer(this);
    applyTimer_->setSingleShot(true);
    connect(applyTimer_, &QTimer::timeout, this, &RetouchPage::doApply);

    // 载入首张
    if (!usableIdx_.isEmpty()) {
        loadPhoto(0);
        refreshChips();
        doApply();
    }

    // 自测钩子
    if (!qEnvironmentVariableIsEmpty("LA_RETOUCH_SMOKE")) {
        QTimer::singleShot(0, this, [this]{
            if (usableIdx_.isEmpty()) {
                std::fprintf(stderr, "LA_RETOUCH_SMOKE no_usable_photo\n");
                std::fflush(stderr); QApplication::quit(); return;
            }
            loadPhoto(0);
            RetouchParams p = RetouchEngine::preset(QStringLiteral("auto"));
            RetouchParams s = RetouchEngine::recommend(srcImg_).params;
            QImage out = RetouchEngine::apply(srcImg_, p);
            QImage out2 = RetouchEngine::apply(srcImg_, s);
            std::fprintf(stderr,
                "LA_RETOUCH_SMOKE ok=1 srcW=%d srcH=%d autoOutW=%d sugChips=%d sugOutNull=%d\n",
                srcImg_.width(), srcImg_.height(), out.width(),
                RetouchEngine::recommend(srcImg_).chips.size(), out2.isNull() ? 1 : 0);
            std::fflush(stderr);
            QApplication::quit();
        });
    }
}

void RetouchPage::buildSlider(const RtSlider& def, QGridLayout* gl)
{
    const int row = gl->rowCount();
    auto* lab = new QLabel(QString::fromLatin1(def.label), this);
    lab->setStyleSheet("color:#f2f5f3;");
    auto* sl = new QSlider(Qt::Horizontal, this);
    sl->setRange(int(def.min * 100), int(def.max * 100));
    sl->setValue(0);
    sl->setStyleSheet("QSlider::groove:horizontal{background:#181d1a;height:4px;border-radius:2px;}"
        "QSlider::handle:horizontal{background:#c19a4b;width:14px;border-radius:7px;}");
    auto* val = new QLabel(QStringLiteral("0.00"), this);
    val->setStyleSheet("color:#9fa8a3;min-width:48px;");
    val->setAlignment(Qt::AlignRight);
    gl->addWidget(lab, row, 0);
    gl->addWidget(sl,  row, 1);
    gl->addWidget(val, row, 2);
    sliders_.append(sl);
    sliderVals_.append(val);
    sliderDefs_.append(&def);
    connect(sl, &QSlider::valueChanged, this, &RetouchPage::onSliderChanged);
}

void RetouchPage::onSliderChanged()
{
    // 哪个滑杆动了就同步到 cur_，并把预设下拉切回手动
    for (int i = 0; i < sliders_.size(); ++i) {
        const RtSlider* d = sliderDefs_[i];
        cur_[QString::fromLatin1(d->key)] = sliders_[i]->value() / 100.0;
        sliderVals_[i]->setText(QString::number(sliders_[i]->value() / 100.0, 'f', 2));
    }
    if (presetCombo_->currentData().toString() != QStringLiteral("__manual__"))
        presetCombo_->setCurrentIndex(0);  // “— 手动 —”
    applyTimer_->start(40);
}

void RetouchPage::onPhotoChanged(int idx)
{
    Q_UNUSED(idx);
    if (photoCombo_->currentIndex() < 0) return;
    loadPhoto(photoCombo_->currentIndex());
    refreshChips();
    doApply();
}

void RetouchPage::onPresetChanged(int idx)
{
    Q_UNUSED(idx);
    onApplyPreset();
}

void RetouchPage::onApplyPreset()   // rlRun
{
    const QString key = presetCombo_->currentData().toString();
    if (key == QStringLiteral("__manual__")) return;
    setParams(RetouchEngine::preset(key));
    doApply();
}

void RetouchPage::onAutoEnhance()   // rlAi
{
    refreshChips();
    setParams(RetouchEngine::recommend(srcImg_).params);
    doApply();
}

void RetouchPage::onReset()
{
    setParams(RetouchParams{});
    presetCombo_->setCurrentIndex(0);
    doApply();
}

void RetouchPage::onExport()
{
    if (srcImg_.isNull()) return;
    const Photo& ph = (*photos_)[usableIdx_[photoCombo_->currentIndex()]];
    // 优先用原始全分辨率文件（仍零云端，纯本地）
    QImage full = QImage(ph.path);
    QImage work = full.isNull() ? srcImg_ : full;
    if (work.width() > kMaxExport || work.height() > kMaxExport)
        work = work.scaled(kMaxExport, kMaxExport, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const QImage out = RetouchEngine::apply(work, buildFromMap(cur_));

    const QString fname = QFileDialog::getSaveFileName(this, QStringLiteral("导出修后副本"),
        QDir::homePath() + "/" + QFileInfo(ph.path).baseName() + "_retouched.jpg",
        QStringLiteral("JPEG (*.jpg);;PNG (*.png)"));
    if (fname.isEmpty()) return;
    if (out.save(fname)) {
        badge_->setText(QStringLiteral("已导出 · %1 · 零云端").arg(QFileInfo(fname).fileName()));
        prog_->setValue(100);
    } else {
        badge_->setText(QStringLiteral("导出失败"));
    }
}

void RetouchPage::setParams(const RetouchParams& p)
{
    // 写回 cur_
    cur_["exposure"]=p.exposure;   cur_["brightness"]=p.brightness;
    cur_["contrast"]=p.contrast;   cur_["clarity"]=p.clarity;
    cur_["vibrance"]=p.vibrance;   cur_["saturation"]=p.saturation;
    cur_["temp"]=p.temp;           cur_["tint"]=p.tint;
    cur_["sharpen"]=p.sharpen;     cur_["highlights"]=p.highlights;
    cur_["shadows"]=p.shadows;     cur_["vignette"]=p.vignette;
    // 同步滑杆 + 数值
    for (int i = 0; i < sliders_.size(); ++i) {
        const RtSlider* d = sliderDefs_[i];
        const double v = cur_[QString::fromLatin1(d->key)];
        sliders_[i]->blockSignals(true);
        sliders_[i]->setValue(int(v * 100));
        sliders_[i]->blockSignals(false);
        sliderVals_[i]->setText(QString::number(v, 'f', 2));
    }
}

void RetouchPage::loadPhoto(int idx)
{
    if (idx < 0 || idx >= usableIdx_.size()) return;
    const Photo& ph = (*photos_)[usableIdx_[idx]];
    const QString path = QDir(thumbsDir_).filePath(ph.thumbFile);
    QImage img(path);
    if (img.isNull()) {
        srcImg_ = QImage();
        beforeLbl_->setText(QStringLiteral("无缩略图"));
        return;
    }
    if (img.width() > kMaxSrc || img.height() > kMaxSrc)
        img = img.scaled(kMaxSrc, kMaxSrc, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    srcImg_ = img;
    srcPm_ = QPixmap::fromImage(img);
    beforeLbl_->setPixmap(srcPm_.scaled(kPreviewW, kPreviewH, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void RetouchPage::doApply()
{
    if (srcImg_.isNull()) { afterLbl_->setText(QStringLiteral("无缩略图")); return; }
    const QImage out = RetouchEngine::apply(srcImg_, buildFromMap(cur_));
    afterLbl_->setPixmap(QPixmap::fromImage(out).scaled(kPreviewW, kPreviewH,
        Qt::KeepAspectRatio, Qt::SmoothTransformation));
    updateBadge();
    prog_->setValue(100);
}

void RetouchPage::updateBadge()
{
    badge_->setText(QStringLiteral("%1 · 零云端").arg(RetouchEngine::describe(buildFromMap(cur_))));
}

void RetouchPage::refreshChips()
{
    auto* hl = qobject_cast<QHBoxLayout*>(chipRow_->layout());
    if (!hl) return;
    while (hl->count()) { QLayoutItem* it = hl->takeAt(0); delete it->widget(); delete it; }
    const RetouchSuggest s = RetouchEngine::recommend(srcImg_);
    if (!s.valid || s.chips.isEmpty()) {
        auto* t = new QLabel(QStringLiteral("曝光均衡，无需修正"), chipRow_);
        t->setStyleSheet("color:#9fa8a3;");
        hl->addWidget(t);
        return;
    }
    for (const QString& c : s.chips) {
        auto* b = new QPushButton(c, chipRow_);
        b->setStyleSheet("QPushButton{background:#181d1a;border:1px solid #5a8f6b;border-radius:14px;"
            "padding:4px 12px;color:#cfe3d6;font-size:12px;}");
        connect(b, &QPushButton::clicked, this, &RetouchPage::onAutoEnhance);
        hl->addWidget(b);
    }
    hl->addStretch(1);
}
