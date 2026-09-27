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
#include <QTransform>
#include <algorithm>
#include <cmath>
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

// ----------------------------------------------------------------- P4-1 几何纯函数
// 固定顺序：拉直 → 翻转 → 90°旋转 → 居中裁剪（确定性流水线，冒烟可断言）。
QImage RetouchPage::applyGeometry(const QImage& src, int rot, bool flipH, bool flipV,
                                  double angleDeg, double aspect)
{
    if (src.isNull()) return src;
    QImage img = src;

    if (std::fabs(angleDeg) > 1e-6) {
        QTransform t; t.rotate(angleDeg);
        img = img.transformed(t, Qt::SmoothTransformation);
    }
    if (flipH || flipV) {
        QTransform f; f.scale(flipH ? -1.0 : 1.0, flipV ? -1.0 : 1.0);
        img = img.transformed(f);
    }
    const int r = ((rot % 4) + 4) % 4;
    if (r) {
        QTransform q; q.rotate(r * 90.0);
        img = img.transformed(q, Qt::FastTransformation);
    }
    if (aspect > 0) {
        const int w = img.width(), h = img.height();
        const double cur = double(w) / double(h);
        int cw = w, ch = h;
        if (cur > aspect) cw = int(h * aspect + 0.5);
        else              ch = int(w / aspect + 0.5);
        img = img.copy((w - cw) / 2, (h - ch) / 2, cw, ch);
    }
    return img;
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
    const char* goldCss =
        "QPushButton{background:#c19a4b;border:none;border-radius:8px;padding:8px 16px;"
        "color:#10130f;font-weight:bold;}";
    autoBtn_   = mkBtn(QStringLiteral("智能增强"),
        "QPushButton{background:#5a8f6b;border:none;border-radius:8px;padding:8px 16px;"
        "color:#f2f5f3;font-weight:bold;} QPushButton:disabled{background:#5a5f5b;}");
    resetBtn_  = mkBtn(QStringLiteral("重置"),
        "QPushButton{background:#181d1a;border:1px solid #2a2f2c;border-radius:8px;"
        "padding:8px 16px;color:#f2f5f3;}");
    exportBtn_ = mkBtn(QStringLiteral("导出副本"), goldCss);

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

    // ---- P4-1 几何编辑条 ----
    auto* geo = new QWidget(this);
    auto* gl2 = new QHBoxLayout(geo);
    gl2->setContentsMargins(0, 0, 0, 0);
    gl2->setSpacing(8);
    const char* geoCss =
        "QPushButton{background:#181d1a;border:1px solid #2a2f2c;border-radius:8px;"
        "padding:6px 12px;color:#f2f5f3;}";
    auto gbtn = [geo, geoCss](const QString& t){
        auto* b = new QPushButton(t, geo);
        b->setStyleSheet(QString::fromLatin1(geoCss));
        return b;
    };
    geoRotLBtn_ = gbtn(QStringLiteral("旋转⟲"));
    geoRotRBtn_ = gbtn(QStringLiteral("旋转⟳"));
    geoFlipHBtn_ = gbtn(QStringLiteral("⇋ 水平"));
    geoFlipVBtn_ = gbtn(QStringLiteral("⇅ 垂直"));
    straighten_ = new QSlider(Qt::Horizontal, geo);
    straighten_->setRange(-100, 100); straighten_->setValue(0);
    straighten_->setFixedWidth(160);
    straighten_->setToolTip(QStringLiteral("拉直 ±10°"));
    straighten_->setStyleSheet("QSlider::groove:horizontal{background:#181d1a;height:4px;border-radius:2px;}"
        "QSlider::handle:horizontal{background:#c19a4b;width:12px;border-radius:6px;}");
    aspectCombo_ = new QComboBox(geo);
    aspectCombo_->addItem(QStringLiteral("自由"), 0.0);
    aspectCombo_->addItem(QStringLiteral("1:1"),  1.0);
    aspectCombo_->addItem(QStringLiteral("4:3"),  4.0 / 3.0);
    aspectCombo_->addItem(QStringLiteral("3:2"),  1.5);
    aspectCombo_->addItem(QStringLiteral("16:9"), 16.0 / 9.0);
    geoCropBtn_ = gbtn(QStringLiteral("应用裁剪"));
    geoResetBtn_ = gbtn(QStringLiteral("重置几何"));
    gl2->addWidget(geoRotLBtn_); gl2->addWidget(geoRotRBtn_);
    gl2->addWidget(geoFlipHBtn_); gl2->addWidget(geoFlipVBtn_);
    gl2->addWidget(new QLabel(QStringLiteral("拉直"), geo));
    gl2->addWidget(straighten_);
    gl2->addWidget(new QLabel(QStringLiteral("裁剪"), geo));
    gl2->addWidget(aspectCombo_);
    gl2->addWidget(geoCropBtn_);
    gl2->addWidget(geoResetBtn_);
    gl2->addStretch(1);
    root->addWidget(geo);

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
    // P4-1 几何
    connect(geoRotLBtn_, &QPushButton::clicked, this, [this]{ onGeoRotate(true); });
    connect(geoRotRBtn_, &QPushButton::clicked, this, [this]{ onGeoRotate(false); });
    connect(geoFlipHBtn_, &QPushButton::clicked, this, [this]{ onGeoFlip(true); });
    connect(geoFlipVBtn_, &QPushButton::clicked, this, [this]{ onGeoFlip(false); });
    connect(straighten_, &QSlider::valueChanged, this, &RetouchPage::onGeoStraighten);
    connect(geoCropBtn_, &QPushButton::clicked, this, &RetouchPage::onGeoCropApply);
    connect(geoResetBtn_, &QPushButton::clicked, this, &RetouchPage::onGeoReset);

    applyTimer_ = new QTimer(this);
    applyTimer_->setSingleShot(true);
    connect(applyTimer_, &QTimer::timeout, this, &RetouchPage::doApply);

    // 载入首张（rebuildGeo 内已做首次 doApply）
    if (!usableIdx_.isEmpty()) {
        loadPhoto(0);
        refreshChips();
    }

    // 自测钩子：修调 + 几何断言
    if (!qEnvironmentVariableIsEmpty("LA_RETOUCH_SMOKE")) {
        QTimer::singleShot(0, this, [this]{
            if (usableIdx_.isEmpty()) {
                std::fprintf(stderr, "LA_RETOUCH_SMOKE no_usable_photo\n");
                std::fflush(stderr); QApplication::quit(); return;
            }
            loadPhoto(0);
            const QImage rot1 = applyGeometry(geoSrc_, 1, false, false, 0, 0);
            const QImage sq   = applyGeometry(geoSrc_, 0, false, false, 0, 1.0);
            const QImage flip = applyGeometry(geoSrc_, 0, true, false, 0, 0);
            RetouchParams p = RetouchEngine::preset(QStringLiteral("auto"));
            RetouchParams s = RetouchEngine::recommend(geoImg_).params;
            QImage out = RetouchEngine::apply(geoImg_, p);
            QImage out2 = RetouchEngine::apply(geoImg_, s);
            std::fprintf(stderr,
                "LA_RETOUCH_SMOKE ok=1 srcW=%d srcH=%d rotSwap=%d sqOk=%d flipOk=%d "
                "autoOutW=%d sugChips=%d sugOutNull=%d\n",
                geoSrc_.width(), geoSrc_.height(),
                (rot1.width() == geoSrc_.height() && rot1.height() == geoSrc_.width()) ? 1 : 0,
                (sq.width() == sq.height()) ? 1 : 0,
                (flip.size() == geoSrc_.size()) ? 1 : 0,
                out.width(),
                RetouchEngine::recommend(geoImg_).chips.size(), out2.isNull() ? 1 : 0);
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

// ----------------------------------------------------------------- P4-1 几何槽
void RetouchPage::rebuildGeo()
{
    geoImg_ = applyGeometry(geoSrc_, rot_, flipH_, flipV_, angle_, aspect_);
    if (geoImg_.isNull()) {
        beforeLbl_->setText(QStringLiteral("无缩略图"));
        return;
    }
    srcPm_ = QPixmap::fromImage(geoImg_);
    beforeLbl_->setPixmap(srcPm_.scaled(kPreviewW, kPreviewH, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    doApply();
}

void RetouchPage::onGeoRotate(bool left)
{
    rot_ = (rot_ + (left ? 3 : 1)) % 4;
    rebuildGeo();
}

void RetouchPage::onGeoFlip(bool horizontal)
{
    if (horizontal) flipH_ = !flipH_; else flipV_ = !flipV_;
    rebuildGeo();
}

void RetouchPage::onGeoStraighten(int v)
{
    angle_ = v / 10.0;               // -10..10°
    rebuildGeo();
}

void RetouchPage::onGeoCropApply()
{
    aspect_ = aspectCombo_->currentData().toDouble();   // 0 = 自由
    rebuildGeo();
}

void RetouchPage::onGeoReset()
{
    rot_ = 0; flipH_ = flipV_ = false; angle_ = 0.0; aspect_ = 0.0;
    straighten_->blockSignals(true);
    straighten_->setValue(0);
    straighten_->blockSignals(false);
    aspectCombo_->setCurrentIndex(0);
    rebuildGeo();
}

// ----------------------------------------------------------------- 页面槽
void RetouchPage::onPhotoChanged(int)
{
    if (photoCombo_->currentIndex() < 0) return;
    loadPhoto(photoCombo_->currentIndex());
    refreshChips();
}

void RetouchPage::onPresetChanged(int)
{
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
    setParams(RetouchEngine::recommend(geoImg_).params);
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
    if (geoSrc_.isNull()) return;
    const Photo& ph = (*photos_)[usableIdx_[photoCombo_->currentIndex()]];
    // 优先用原始全分辨率文件（仍零云端，纯本地）：先几何后修调，与预览同序
    QImage full = QImage(ph.path);
    QImage work = full.isNull() ? geoSrc_ : full;
    if (work.width() > kMaxExport || work.height() > kMaxExport)
        work = work.scaled(kMaxExport, kMaxExport, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    work = applyGeometry(work, rot_, flipH_, flipV_, angle_, aspect_);
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
        geoSrc_ = QImage(); geoImg_ = QImage();
        beforeLbl_->setText(QStringLiteral("无缩略图"));
        return;
    }
    if (img.width() > kMaxSrc || img.height() > kMaxSrc)
        img = img.scaled(kMaxSrc, kMaxSrc, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    geoSrc_ = img;
    rebuildGeo();
}

void RetouchPage::doApply()
{
    if (geoImg_.isNull()) { afterLbl_->setText(QStringLiteral("无缩略图")); return; }
    const QImage out = RetouchEngine::apply(geoImg_, buildFromMap(cur_));
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
    const RetouchSuggest s = RetouchEngine::recommend(geoImg_);
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
