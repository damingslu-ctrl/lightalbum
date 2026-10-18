#include "RetouchPage.h"
#include "Theme.h"          // Task 4：Lumen token + 组件工厂（btnViolet/btnGhost/slider/progress/input）
#include "../core/ImageLoad.h"   // 2.29.0 统一原图加载（照片/RAW/视频）
#include "../core/VideoDecode.h"// 2.31.0 视频选帧（探测时长）
#include "../core/qaExit.h"   // 2.7.0 门禁自退契约（禁裸调 quit）
#include "MaskCanvas.h"
#include "CropCanvas.h"
#include "CompareWidget.h"
#include "../core/EraseBridge.h"
#include "../core/LumenEngine.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QStackedLayout>
#include <QStackedWidget>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QProgressBar>
#include <QToolButton>
#include <QIcon>
#include <QScrollArea>
#include <QGroupBox>
#include <QFrame>
#include <QFileDialog>
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QApplication>
#include <QPixmap>
#include <QPainter>
#include <QTransform>
#include <QLineEdit>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <QtConcurrent/QtConcurrentRun>
#include <QFutureWatcher>
#include <QShortcut>

namespace {
constexpr int kPreviewW = 400, kPreviewH = 470, kMaxSrc = 1024, kMaxExport = 2400;

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

// 2.38.0 R1：一键增强 13 算子（组内平铺；key 与 LumenEngine 方法严格同名）
struct EnhOp { const char* key; const char* label; int group; };   // group: 0 影调 / 1 人像 / 2 校正
const EnhOp kEnhOps[] = {
    {"filmStyle",       "胶片", 0}, {"blackWhite",  "黑白", 0}, {"toneMapHdr",      "HDR",  0},
    {"structure",       "结构", 0}, {"dodge",       "减淡", 0}, {"glowStyle",       "柔光", 0},
    {"redeyeRemove",    "红眼", 1}, {"portraitBeautify", "美颜", 1},
    {"levelCorrect",    "水平", 2}, {"lensCorrect",  "镜头", 2}, {"vignetteCorrect", "暗角", 2},
    {"dehaze",          "去雾", 2}, {"chromaticCorrect", "色差", 2},
};

// 算子执行结果：img=null 表示未改动（诚实 no-op）；note 为诚实反馈（空=用默认文案）
struct EnhOutcome { QImage img; QString note; };

EnhOutcome runEnhOp(const QImage& src, const QString& key)
{
    EnhOutcome o;
    if (src.isNull()) return o;
    if (key == QStringLiteral("filmStyle"))            o.img = LumenEngine::filmStyle(src);
    else if (key == QStringLiteral("blackWhite"))      o.img = LumenEngine::blackWhite(src);
    else if (key == QStringLiteral("glowStyle"))       o.img = LumenEngine::glowStyle(src);
    else if (key == QStringLiteral("toneMapHdr"))      o.img = LumenEngine::toneMapHdr(src);
    else if (key == QStringLiteral("structure"))       o.img = LumenEngine::structure(src);
    else if (key == QStringLiteral("dodge"))           o.img = LumenEngine::dodge(src);
    else if (key == QStringLiteral("dehaze"))          o.img = LumenEngine::dehaze(src);
    else if (key == QStringLiteral("chromaticCorrect"))o.img = LumenEngine::chromaticCorrect(src);
    else if (key == QStringLiteral("lensCorrect"))     o.img = LumenEngine::lensCorrect(src);
    else if (key == QStringLiteral("redeyeRemove")) {
        int pixels = 0;
        const QImage hit = LumenEngine::redeyeRemove(src, &pixels);
        if (pixels == 0) {
            o.note = QCoreApplication::translate("LightAlbum", "未检测到红眼 · 未改动");
            return o;                                   // 诚实 no-op：不追加 ops_、不进历史（引擎 0 命中仍返回副本，须丢弃）
        }
        o.img = hit;
        o.note = QCoreApplication::translate("LightAlbum", "红眼已修 %1 点").arg(pixels);
    } else if (key == QStringLiteral("portraitBeautify")) {
        double frac = -1.0;
        const QImage hit = LumenEngine::portraitBeautify(src, 1.0, &frac);
        if (frac < 0.015) {                       // 引擎无肤色→返回原图副本：诚实 no-op，不追加 ops_/历史
            o.note = QCoreApplication::translate("LightAlbum", "未检测到肤色区域 · 未改动");
            return o;
        }
        o.img = hit;
        o.note = QCoreApplication::translate("LightAlbum", "美颜 · 肤色占比 %1%")
                     .arg(frac * 100.0, 0, 'f', 1);
    } else if (key == QStringLiteral("vignetteCorrect")) {
        double k = 0.0;
        const QImage hit = LumenEngine::vignetteCorrect(src, 1.0, &k);
        if (k <= 0.0) {                            // 引擎 k<0.05→返回原图副本：诚实 no-op，不追加 ops_/历史
            o.note = QCoreApplication::translate("LightAlbum", "未见明显暗角 · 未改动");
            return o;
        }
        o.img = hit;
        o.note = QCoreApplication::translate("LightAlbum", "暗角校正 · 强度 %1%")
                     .arg(k * 100.0, 0, 'f', 1);
    } else if (key == QStringLiteral("levelCorrect")) {
        const double ang = LumenEngine::levelDetectAngle(src);
        if (std::fabs(ang) < 0.5) {                // 引擎 |ang|<0.5→返回原图副本：与引擎阈值对齐，非 ==0
            o.note = QCoreApplication::translate("LightAlbum", "倾斜过小(<0.5°) · 未改动");
            return o;
        }
        o.img = LumenEngine::levelCorrect(src);
        o.note = QCoreApplication::translate("LightAlbum", "水平校正 %1°").arg(ang, 0, 'f', 2);
    }
    return o;
}

QString photoLabel(const Photo& ph)
{
    if (!ph.title.isEmpty()) return ph.title;
    if (!ph.rel.isEmpty())   return QFileInfo(ph.rel).fileName();
    return QFileInfo(ph.path).fileName();
}

QString rtCardStyle(bool sel)
{
    // Task 4 归一：色字面量→Lumen token（spec §1.7 回家映射，色值不变）
    return sel
      ? QString::fromUtf8("QToolButton{background:%1;border:1px solid %2;border-radius:%4px;color:%3;padding:4px;}")
          .arg(Lumen::hex(Lumen::cardHi()), Lumen::hex(Lumen::emerald()), Lumen::hex(Lumen::ink()))
          .arg(Lumen::Radius::Card)    // T9 C3: 12->Radius::Card 14（%4）；2px 描边同步归 1px（§3.2）
      : QString::fromUtf8("QToolButton{background:%1;border:1px solid %2;border-radius:%4px;color:%3;padding:4px;}")
          .arg(Lumen::hex(Lumen::card()), Lumen::hex(Lumen::line()), Lumen::hex(Lumen::ink()))
          .arg(Lumen::Radius::Card);   // T9 C3: 12->Radius::Card 14（%4）；2px 描边同步归 1px（§3.2）
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
// 固定顺序：拉直 → 翻转 → 90°旋转 → 交互裁剪(cropNorm 归一化) → 比例居中裁剪（确定性流水线）。
QImage RetouchPage::applyGeometry(const QImage& src, int rot, bool flipH, bool flipV,
                                  double angleDeg, double aspect, const QRectF& cropNorm)
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
    // P4-3：交互裁剪框（归一化 0..1，null = 未裁）
    if (cropNorm.isValid() && cropNorm.width() > 0 && cropNorm.height() > 0) {
        const int w = img.width(), h = img.height();
        int cw = int(cropNorm.width() * w + 0.5);
        int ch = int(cropNorm.height() * h + 0.5);
        int cx = int(cropNorm.left() * w + 0.5);
        int cy = int(cropNorm.top() * h + 0.5);
        cw = std::min(cw, w - cx); ch = std::min(ch, h - cy);
        if (cw >= 1 && ch >= 1) img = img.copy(cx, cy, cw, ch);
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

// ----------------------------------------------------------------- P4-2 蒙版混合纯函数
// out = base*(1-f) + toned*f，f = mask alpha/255；toned/mask 尺寸可异，自动缩放到 base。
QImage RetouchPage::blendMask(const QImage& base, const QImage& toned, const QImage& mask)
{
    if (base.isNull() || toned.isNull() || mask.isNull()) return QImage();
    QImage b = base.convertToFormat(QImage::Format_ARGB32);
    QImage t = toned.convertToFormat(QImage::Format_ARGB32);
    QImage m = mask.convertToFormat(QImage::Format_ARGB32);
    if (t.size() != b.size()) t = t.scaled(b.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    if (m.size() != b.size()) m = m.scaled(b.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QImage out(b.size(), QImage::Format_ARGB32);
    for (int y = 0; y < b.height(); ++y) {
        const QRgb* pb = reinterpret_cast<const QRgb*>(b.constScanLine(y));
        const QRgb* pt = reinterpret_cast<const QRgb*>(t.constScanLine(y));
        const QRgb* pm = reinterpret_cast<const QRgb*>(m.constScanLine(y));
        QRgb* po = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < b.width(); ++x) {
            const double f = qAlpha(pm[x]) / 255.0;
            const int r  = int(qRed(pb[x])   * (1 - f) + qRed(pt[x])   * f + 0.5);
            const int g  = int(qGreen(pb[x]) * (1 - f) + qGreen(pt[x]) * f + 0.5);
            const int bl = int(qBlue(pb[x])  * (1 - f) + qBlue(pt[x])  * f + 0.5);
            po[x] = qRgba(r, g, bl, 255);
        }
    }
    return out;
}

RetouchPage::RetouchPage(const QVector<Photo>* photos, const QString& thumbsDir, QWidget* parent)
    : QWidget(parent), photos_(photos), thumbsDir_(thumbsDir)
{
    for (const RtSlider& s : kSliders) cur_[QString::fromLatin1(s.key)] = 0.0;
    presets_ = RetouchEngine::presets();

    auto* outH = new QHBoxLayout(this);
    outH->setContentsMargins(0, 0, 0, 0);
    outH->setSpacing(0);

    // ===== 左 模式轨（预设 / 调整 / 几何 / 蒙版 / 对比 / AI）=====
    toolrail_ = new QWidget(this);
    toolrail_->setFixedWidth(116);
    toolrail_->setStyleSheet(QString::fromUtf8("QWidget{background:%1;border-right:1px solid %2;}")
        .arg(Lumen::hex(Lumen::panel()), Lumen::hex(Lumen::line())));
    auto* trl = new QVBoxLayout(toolrail_);
    trl->setContentsMargins(Lumen::Space::M, Lumen::Space::L, Lumen::Space::M, Lumen::Space::L);
    trl->setSpacing(8);
    railCss_   = (QString::fromUtf8(
        "QPushButton{background:transparent;border:none;border-radius:%2px;color:%1;padding:9px 8px;")
        + Lumen::Type::css(Lumen::Type::Tier::Small)   // T8：12px=T5 原位
        + QString::fromUtf8(
        "letter-spacing:.06em;text-align:left;}"
        "QPushButton:hover{background:rgba(232,228,218,0.06);}")
        )
        .arg(Lumen::hex(Lumen::muted())).arg(Lumen::Radius::Ctl);   // T9 C3: 9->Radius::Ctl 10（%2 按号绑定）旧 9fa8a3 → MUTED（spec §1.7）
    railOnCss_ = (QString::fromUtf8(
        "QPushButton{background:rgba(217,169,78,0.16);border:none;border-radius:%2px;color:%1;"
        "font-weight:bold;padding:9px 8px;")   // T8 §2.4：600 归一 bold 三档
        + Lumen::Type::css(Lumen::Type::Tier::Small)   // T8：12px=T5 原位
        + QString::fromUtf8(
        "letter-spacing:.06em;text-align:left;}"))
        .arg(Lumen::hex(Lumen::gold())).arg(Lumen::Radius::Ctl);   // T9 C3: 9->Radius::Ctl 10（railOn %2）
    static const char* kModeNames[] = {"预设", "调整", "几何", "蒙版", "对比", "AI"};
    for (int m = 0; m < 6; ++m) {
        auto* b = new QPushButton(QString::fromUtf8(kModeNames[m]), toolrail_);
        b->setStyleSheet(railCss_);
        trl->addWidget(b);
        modeBtns_.append(b);
        connect(b, &QPushButton::clicked, this, [this, m]{ setMode(m); });
    }
    trl->addStretch(1);
    outH->addWidget(toolrail_);

    // ===== 中间工作区 =====
    auto* centerCol = new QWidget(this);
    centerCol->setStyleSheet(QString::fromUtf8("QWidget{background:%1;}").arg(Lumen::hex(Lumen::inset())));
    auto* root = new QVBoxLayout(centerCol);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);
    outH->addWidget(centerCol, 1);

    // ---- 操作条：照片选择 + 预设下拉 ----
    auto* bar = new QWidget(centerCol);
    auto* bl = new QHBoxLayout(bar);
    bl->setContentsMargins(0, 0, 0, 0);
    bl->setSpacing(Lumen::Space::M);
    photoCombo_ = new QComboBox(bar);
    presetCombo_ = new QComboBox(bar);
    if (photos_) {
        for (int i = 0; i < photos_->size() && usableIdx_.size() < 300; ++i) {
            if ((*photos_)[i].thumbFile.isEmpty()) continue;
            usableIdx_.append(i);
            photoCombo_->addItem(photoLabel((*photos_)[i]));
        }
    }
    presetCombo_->addItem(QCoreApplication::translate("LightAlbum", "— 手动 —"), QStringLiteral("__manual__"));
    for (const RetouchPreset& pr : presets_)
        presetCombo_->addItem(pr.name, pr.key);
    bl->addWidget(new QLabel(QCoreApplication::translate("LightAlbum", "照片"), bar));
    bl->addWidget(photoCombo_);
    bl->addWidget(new QLabel(QCoreApplication::translate("LightAlbum", "预设"), bar));
    bl->addWidget(presetCombo_);
    bl->addStretch(1);
    root->addWidget(bar);

    // ---- 预览：before / after（堆叠 0 预览 / 1 蒙版 / 2 裁剪 / 3 对比）----
    auto* pv = new QHBoxLayout;
    pv->setSpacing(12);
    beforeLbl_ = new QLabel(this);
    afterLbl_  = new QLabel(this);
    for (QLabel* l : {beforeLbl_, afterLbl_}) {
        l->setFixedSize(kPreviewW, kPreviewH);
        l->setAlignment(Qt::AlignCenter);
        l->setStyleSheet(QString::fromUtf8("QLabel{background:#000;border:1px solid %1;border-radius:%2px;}")
            .arg(Lumen::hex(Lumen::line())).arg(Lumen::Radius::Card));   // T9 C3: 12->Radius::Card 14
    }
    auto* bl1 = new QVBoxLayout; bl1->setContentsMargins(0,0,0,0);
    bl1->addWidget(beforeLbl_); bl1->addWidget(new QLabel(QCoreApplication::translate("LightAlbum", "原图"), this), 0, Qt::AlignHCenter);
    auto* bl2 = new QVBoxLayout; bl2->setContentsMargins(0,0,0,0);
    bl2->addWidget(afterLbl_);  bl2->addWidget(new QLabel(QCoreApplication::translate("LightAlbum", "修后"), this), 0, Qt::AlignHCenter);
    pv->addStretch(1); pv->addLayout(bl1); pv->addLayout(bl2); pv->addStretch(1);
    auto* pvWrap = new QWidget(this);
    pvWrap->setLayout(pv);
    canvas_ = new MaskCanvas(this);
    canvas_->setMinimumHeight(360);
    cropCanvas_ = new CropCanvas(this);
    cropCanvas_->setMinimumHeight(360);
    compareW_ = new CompareWidget(this);
    midStack_ = new QStackedLayout;
    midStack_->addWidget(pvWrap);       // 0
    midStack_->addWidget(canvas_);      // 1
    midStack_->addWidget(cropCanvas_);  // 2
    midStack_->addWidget(compareW_);    // 3
    midStack_->setCurrentIndex(0);
    root->addLayout(midStack_, 1);

    // ---- 建议芯片行 ----
    auto* chipWrap = new QWidget(centerCol);
    auto* cl = new QHBoxLayout(chipWrap);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->setSpacing(8);
    chipTitle_ = new QLabel(QCoreApplication::translate("LightAlbum", "智能建议："), chipWrap);
    chipTitle_->setStyleSheet(QString::fromUtf8("color:%1;").arg(Lumen::hex(Lumen::muted())));
    cl->addWidget(chipTitle_);
    chipRow_ = new QWidget(chipWrap);
    chipRow_->setLayout(new QHBoxLayout);
    chipRow_->layout()->setContentsMargins(0,0,0,0);
    chipRow_->layout()->setSpacing(8);
    cl->addWidget(chipRow_, 1);
    root->addWidget(chipWrap);

    // ---- 进度条 + 徽章 ----
    auto* foot = new QWidget(centerCol);
    auto* fl = new QHBoxLayout(foot);
    fl->setContentsMargins(0, 0, 0, 0);
    prog_ = new QProgressBar(foot);
    prog_->setRange(0, 100); prog_->setValue(0);
    prog_->setTextVisible(false);
    prog_->setStyleSheet(Lumen::progress());   // 手写进度条 → 共享工厂（同 INSET/LINE/EMERALD→EMERALD_HI 配方）
    badge_ = new QLabel(QCoreApplication::translate("LightAlbum", "未调整 · 零云端"), foot);
    badge_->setStyleSheet(QString::fromUtf8("color:%1;").arg(Lumen::hex(Lumen::muted())));
    fl->addWidget(prog_, 1);
    fl->addWidget(badge_);

    // ---- 2.31.0 视频选帧控件（默认隐藏，仅视频时显示；铁律30：不碰既有命中区）----
    m_vBadge = new QLabel(QCoreApplication::translate("LightAlbum",
        "正在编辑视频的一帧 · 导出为图片"), foot);
    m_vBadge->setStyleSheet(QString::fromUtf8("color:%1;")
        .arg(Lumen::hex(Lumen::gold())));   // 暖金淡字 c8a86a→GOLD（GOLD 家族微亮饱和，§1.3 荣誉标记归板）
    m_vBadge->setVisible(false);
    fl->addWidget(m_vBadge);

    auto* vrow = new QWidget(foot);
    vrow->setObjectName(QStringLiteral("videoFrameRow"));
    auto* vl = new QHBoxLayout(vrow);
    vl->setContentsMargins(0, 0, 0, 0);
    m_vPrev = new QPushButton(QStringLiteral("◀"), vrow);
    m_vPrev->setToolTip(QCoreApplication::translate("LightAlbum", "上一帧"));
    m_vPrev->setFixedSize(28, 24);
    m_vSeek = new QSlider(Qt::Horizontal, vrow);
    m_vSeek->setRange(0, 0);
    m_vLabel = new QLabel(QStringLiteral("-"), vrow);
    m_vNext = new QPushButton(QStringLiteral("▶"), vrow);
    m_vNext->setToolTip(QCoreApplication::translate("LightAlbum", "下一帧"));
    m_vNext->setFixedSize(28, 24);
    vl->addWidget(m_vPrev);
    vl->addWidget(m_vSeek, 1);
    vl->addWidget(m_vLabel);
    vl->addWidget(m_vNext);
    vrow->setVisible(false);
    fl->addWidget(vrow);
    //★滑块在拖动过程中连续抽帧会狂拉ffmpeg，故仅在松手时触发（sliderReleased）。
    connect(m_vSeek, &QSlider::sliderReleased, this, [this] {
        onVideoFrameChanged(m_vSeek->value());
    });
    connect(m_vPrev, &QPushButton::clicked, this, [this] {
        onVideoFrameChanged(qMax(0, (m_vSeek ? m_vSeek->value() : 0) - 1));
    });
    connect(m_vNext, &QPushButton::clicked, this, [this] {
        onVideoFrameChanged(m_vSeek ? m_vSeek->value() + 1 : 0);
    });
    root->addWidget(foot);

    // ---- 动作条（智能增强 / AI / 重置 / 导出）----
    auto* actbar = new QWidget(centerCol);
    auto* abl = new QHBoxLayout(actbar);
    abl->setContentsMargins(0, 0, 0, 0);
    abl->setSpacing(8);
    // Task 4 组件归厂：四段手写渐变/幽灵 CSS → Theme.h 共享工厂
    // （btnPrimary=翡翠执行、btnGold=金品牌导出、btnViolet=紫 AI 专属、btnGhost(INK)=中性幽灵，
    //  文字色语义不变；渐变/圆角/内距按工厂统一，新增 hover 态为工厂配方自带。）
    auto mkAct = [actbar](const QString& t, const QString& css){
        auto* b = new QPushButton(t, actbar);
        b->setStyleSheet(css);
        return b;
    };
    autoBtn_   = mkAct(QCoreApplication::translate("LightAlbum", "智能增强"), Lumen::btnPrimary());
    agentBtn_  = mkAct(QCoreApplication::translate("LightAlbum", "AI 智能体"), Lumen::btnViolet());
    resetBtn_  = mkAct(QCoreApplication::translate("LightAlbum", "重置"), Lumen::btnGhost(Lumen::ink()));
    undoBtn_   = mkAct(QCoreApplication::translate("LightAlbum", "↶ 撤销"), Lumen::btnGhost(Lumen::ink()));
    redoBtn_   = mkAct(QCoreApplication::translate("LightAlbum", "↷ 重做"), Lumen::btnGhost(Lumen::ink()));
    exportBtn_ = mkAct(QCoreApplication::translate("LightAlbum", "导出副本"), Lumen::btnGold());
    abl->addWidget(autoBtn_);
    abl->addWidget(agentBtn_);
    abl->addWidget(resetBtn_);
    abl->addWidget(undoBtn_);
    abl->addWidget(redoBtn_);
    abl->addStretch(1);
    abl->addWidget(exportBtn_);
    root->addWidget(actbar);

    // ===== 右栈：6 模式面板 =====
    rightStack_ = new QStackedWidget(this);
    rightStack_->setFixedWidth(248);
    rightStack_->setStyleSheet(QString::fromUtf8("QStackedWidget{background:%1;}")
        .arg(Lumen::hex(Lumen::inset())));   // 私有深阶 10150f→INSET（ΔRGB≤2 不可辨归格，右栈=控件井语义）

    // page 0 调整：分组滑杆
    {
        auto* adj = new QWidget(rightStack_);
        auto* al0 = new QVBoxLayout(adj);
        al0->setContentsMargins(Lumen::Space::L, Lumen::Space::L, Lumen::Space::L, Lumen::Space::L);
        al0->setSpacing(Lumen::Space::M);
        auto* sa = new QScrollArea(adj);
        sa->setWidgetResizable(true);
        sa->setStyleSheet("QScrollArea{background:transparent;border:none;}");
        auto* panel = new QWidget(sa);
        auto* gl = new QGridLayout(panel);
        gl->setColumnStretch(1, 1);
        buildEnhOps(gl);                                     // 2.38.0 R1：一键增强 13 入口（三组）
        for (const RtSlider& def : kSliders) buildSlider(def, gl);
        sa->setWidget(panel);
        al0->addWidget(sa, 1);
        rightStack_->addWidget(adj);
    }
    // page 1 预设：galleryPanel_
    {
        galleryPanel_ = new QWidget(rightStack_);
        auto* gv = new QVBoxLayout(galleryPanel_);
        gv->setContentsMargins(Lumen::Space::M, Lumen::Space::L, Lumen::Space::M, Lumen::Space::L);
        gv->setSpacing(8);
        auto* gt = new QLabel(QCoreApplication::translate("LightAlbum", "滤镜预设"), galleryPanel_);
        gt->setStyleSheet(QString::fromUtf8("color:%1;font-weight:bold;")
            .arg(Lumen::hex(Lumen::ink()))
            + Lumen::Type::css(Lumen::Type::Tier::Body));   // T8：13px=T4 原位（粗体留串）
        gv->addWidget(gt);
        auto* gsa = new QScrollArea(galleryPanel_);
        gsa->setWidgetResizable(true);
        gsa->setStyleSheet(QString::fromUtf8("QScrollArea{background:%1;border:1px solid %2;border-radius:%3px;}")
            .arg(Lumen::hex(Lumen::inset()), Lumen::hex(Lumen::line()))
            .arg(Lumen::Radius::Card));   // T9 C3: 12->Radius::Card 14（%3 按号绑定）
        auto* gw = new QWidget(gsa);
        constexpr int kCardCols = 2, kCardIcon = 86;
        auto* gl = new QGridLayout(gw);
        gl->setSpacing(Lumen::Space::M);
        for (int i = 0; i < presets_.size(); ++i) {
            auto* card = new QToolButton(gw);
            card->setFixedSize(kCardIcon + 14, kCardIcon + 32);
            card->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            card->setIconSize(QSize(kCardIcon, kCardIcon));
            card->setText(presets_[i].name);
            card->setStyleSheet(rtCardStyle(false));
            connect(card, &QToolButton::clicked, this, [this, i]{ onCardClicked(i); });
            gl->addWidget(card, i / kCardCols, i % kCardCols);
            cards_.append(card);
        }
        gsa->setWidget(gw);
        gv->addWidget(gsa, 1);
        rightStack_->addWidget(galleryPanel_);
    }
    // page 2 几何
    {
        auto* gp = new QWidget(rightStack_);
        auto* gl2 = new QVBoxLayout(gp);
        gl2->setContentsMargins(Lumen::Space::L, Lumen::Space::L, Lumen::Space::L, Lumen::Space::L);
        gl2->setSpacing(8);
        auto gbtn = [gp](const QString& t){
            auto* b = new QPushButton(t, gp);
            b->setStyleSheet(Lumen::btnGhost(Lumen::ink()));   // 手写幽灵 CSS → 共享工厂（INK 文字语义不变）
            return b;
        };
        auto* hgeo = new QLabel(QCoreApplication::translate("LightAlbum", "几何编辑"), gp);
        hgeo->setStyleSheet(QString::fromUtf8("color:%1;font-weight:bold;")
            .arg(Lumen::hex(Lumen::ink()))
            + Lumen::Type::css(Lumen::Type::Tier::Small));   // T8：12px=T5 原位（粗体留串）
        gl2->addWidget(hgeo);
        geoRotLBtn_ = gbtn(QCoreApplication::translate("LightAlbum", "旋转⟲"));
        geoRotRBtn_ = gbtn(QCoreApplication::translate("LightAlbum", "旋转⟳"));
        geoFlipHBtn_ = gbtn(QCoreApplication::translate("LightAlbum", "⇋ 水平"));
        geoFlipVBtn_ = gbtn(QCoreApplication::translate("LightAlbum", "⇅ 垂直"));
        geoRotLBtn_->setToolTip(QCoreApplication::translate("LightAlbum", "向左旋转 90°"));
        geoRotRBtn_->setToolTip(QCoreApplication::translate("LightAlbum", "向右旋转 90°"));
        geoFlipHBtn_->setToolTip(QCoreApplication::translate("LightAlbum", "水平翻转（镜像）"));
        geoFlipVBtn_->setToolTip(QCoreApplication::translate("LightAlbum", "垂直翻转（镜像）"));
        auto* sep = new QFrame(gp); sep->setFrameShape(QFrame::VLine); sep->setFixedHeight(20);
        sep->setStyleSheet(QString::fromUtf8("color:%1;").arg(Lumen::hex(Lumen::line())));
        straighten_ = new QSlider(Qt::Horizontal, gp);
        straighten_->setRange(-100, 100); straighten_->setValue(0);
        straighten_->setFixedWidth(160);
        straighten_->setToolTip(QCoreApplication::translate("LightAlbum", "拉直 ±10°"));
        straighten_->setStyleSheet(Lumen::slider(Lumen::gold()));   // 手写滑块 → 工厂，保留 GOLD 强调语义
        aspectCombo_ = new QComboBox(gp);
        aspectCombo_->addItem(QCoreApplication::translate("LightAlbum", "自由"), 0.0);
        aspectCombo_->addItem(QStringLiteral("1:1"), 1.0);
        aspectCombo_->addItem(QStringLiteral("4:3"), 4.0 / 3.0);
        aspectCombo_->addItem(QStringLiteral("3:2"), 1.5);
        aspectCombo_->addItem(QStringLiteral("16:9"), 16.0 / 9.0);
        geoCropBtn_ = gbtn(QCoreApplication::translate("LightAlbum", "应用裁剪"));
        geoResetBtn_ = gbtn(QCoreApplication::translate("LightAlbum", "重置几何"));
        cropBtn_ = gbtn(QCoreApplication::translate("LightAlbum", "✂ 裁剪"));
        cropBtn_->setCheckable(true);
        cropBtn_->setStyleSheet(Lumen::btnGhost(Lumen::ink())
            + QString::fromUtf8("QPushButton:checked{border:2px solid %1;background:%2;}")
                  .arg(Lumen::hex(Lumen::gold()), Lumen::hex(Lumen::GOLD_WASH)));   // 深金底 wash 收编新 token GOLD_WASH（色不变；§1 erratum 提案）
        gl2->addWidget(geoRotLBtn_); gl2->addWidget(geoRotRBtn_);
        gl2->addWidget(geoFlipHBtn_); gl2->addWidget(geoFlipVBtn_);
        gl2->addWidget(sep);
        gl2->addWidget(new QLabel(QCoreApplication::translate("LightAlbum", "拉直"), gp)); gl2->addWidget(straighten_);
        gl2->addWidget(new QLabel(QCoreApplication::translate("LightAlbum", "裁剪"), gp)); gl2->addWidget(aspectCombo_);
        gl2->addWidget(geoCropBtn_); gl2->addWidget(geoResetBtn_);
        gl2->addWidget(cropBtn_);
        gl2->addStretch(1);
        rightStack_->addWidget(gp);
    }
    // page 3 蒙版
    {
        auto* mp = new QWidget(rightStack_);
        auto* ml = new QVBoxLayout(mp);
        ml->setContentsMargins(Lumen::Space::L, Lumen::Space::L, Lumen::Space::L, Lumen::Space::L);
        ml->setSpacing(8);
        auto mbtn = [mp](const QString& t, const QString& css){
            auto* b = new QPushButton(t, mp);
            b->setStyleSheet(css);
            return b;
        };
        maskBtn_ = mbtn(QCoreApplication::translate("LightAlbum", "🖌 蒙版模式"),
            Lumen::btnGhost(Lumen::ink())
            + QString::fromUtf8("QPushButton:checked{border:2px solid %1;background:%2;}")
                  .arg(Lumen::hex(Lumen::emerald()), Lumen::hex(Lumen::cardHi())));   // 深翡翠底 12241c→CARD_HI（ΔRGB≤8 不可辨；active 底语义合法）
        maskBtn_->setCheckable(true);
        maskClearBtn_ = mbtn(QCoreApplication::translate("LightAlbum", "清除蒙版"), Lumen::btnGhost(Lumen::ink()));
        maskApplyBtn_ = mbtn(QCoreApplication::translate("LightAlbum", "局部应用"),
            QString::fromUtf8("QPushButton{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 %1, stop:1 %2);"
                "border:none;border-radius:%4px;padding:6px 14px;color:%3;font-weight:bold;}")   // T9 C3: 9->%4 Radius::Ctl
                .arg(Lumen::hex(Lumen::EMERALD_HI), Lumen::hex(Lumen::EMERALD_DK), Lumen::hex(Lumen::ink()))
                .arg(Lumen::Radius::Ctl));
        brushSlider_ = new QSlider(Qt::Horizontal, mp);
        brushSlider_->setRange(4, 80); brushSlider_->setValue(24);
        brushSlider_->setFixedWidth(120);
        brushSlider_->setToolTip(QCoreApplication::translate("LightAlbum", "笔刷大小（像素）"));
        brushSlider_->setStyleSheet(Lumen::slider(Lumen::emerald()));   // 手写滑块 → 工厂，保留 EMERALD 强调语义
        eraseBridge_ = new EraseBridge(this);
        eraseBridge_->setModelSlot(qEnvironmentVariable("LA_INPAINT_MODEL"));
        eraseBtn_ = mbtn(QCoreApplication::translate("LightAlbum", "🧹 AI消除"), Lumen::btnGhost(Lumen::ink()));
        const QString slot = eraseBridge_->modelPath().isEmpty() ? QCoreApplication::translate("LightAlbum", "未配置") : eraseBridge_->modelPath();
        eraseSlotLbl_ = new QLabel(QCoreApplication::translate("LightAlbum", "模型槽位：%1 · 执行器未接入 · 零云端").arg(slot), mp);
        eraseSlotLbl_->setStyleSheet(QString::fromUtf8("color:%1;").arg(Lumen::hex(Lumen::muted())));
        ml->addWidget(maskBtn_);
        ml->addWidget(new QLabel(QCoreApplication::translate("LightAlbum", "笔刷"), mp)); ml->addWidget(brushSlider_);
        ml->addWidget(maskClearBtn_); ml->addWidget(maskApplyBtn_);
        ml->addWidget(eraseBtn_); ml->addWidget(eraseSlotLbl_);
        eraseCancelBtn_ = new QPushButton(QCoreApplication::translate("LightAlbum", "取消任务"), mp);
        eraseCancelBtn_->setObjectName(QStringLiteral("eraseCancelBtn"));
        eraseCancelBtn_->setStyleSheet(Lumen::btnGhost(Lumen::ink()));
        eraseCancelBtn_->hide();                       // 有 queued/running 任务才出现
        ml->addWidget(eraseCancelBtn_);
        ml->addStretch(1);
        rightStack_->addWidget(mp);
    }
    // page 4 对比
    {
        auto* cp = new QWidget(rightStack_);
        auto* cl4 = new QVBoxLayout(cp);
        cl4->setContentsMargins(Lumen::Space::L, Lumen::Space::L, Lumen::Space::L, Lumen::Space::L);
        cl4->setSpacing(8);
        compareBtn_ = new QPushButton(QCoreApplication::translate("LightAlbum", "◐ 对比"), cp);
        compareBtn_->setCheckable(true);
        compareBtn_->setStyleSheet(Lumen::btnGhost(Lumen::ink())
            + QString::fromUtf8("QPushButton:checked{border:2px solid %1;background:%2;}")
                  .arg(Lumen::hex(Lumen::gold()), Lumen::hex(Lumen::GOLD_WASH)));   // 同 cropBtn_：GOLD_WASH 收编（色不变）
        auto* cnote = new QLabel(QCoreApplication::translate("LightAlbum", "拖动金色分割线对比原图与修后 · 零云端"), cp);
        cnote->setStyleSheet(QString::fromUtf8("color:%1;").arg(Lumen::hex(Lumen::muted())));
        cl4->addWidget(compareBtn_);
        cl4->addWidget(cnote);
        cl4->addStretch(1);
        rightStack_->addWidget(cp);
    }
    // page 5 AI
    {
        auto* ap = new QWidget(rightStack_);
        auto* al5 = new QVBoxLayout(ap);
        al5->setContentsMargins(Lumen::Space::L, Lumen::Space::L, Lumen::Space::L, Lumen::Space::L);
        al5->setSpacing(Lumen::Space::M);
        auto* ah = new QLabel(QCoreApplication::translate("LightAlbum", "AI 智能体（Lumen × 本地 LLM）"), ap);
        ah->setStyleSheet(QString::fromUtf8("color:%1;font-weight:bold;")
            .arg(Lumen::hex(Lumen::VIOLET))
            + Lumen::Type::css(Lumen::Type::Tier::Small));   // T8：12px=T5 原位（粗体留串）
        goalEdit_ = new QLineEdit(ap);
        goalEdit_->setPlaceholderText(QCoreApplication::translate("LightAlbum", "目标（可选）：如 风光通透 / 胶片感"));
        goalEdit_->setStyleSheet(Lumen::input()
            + QString::fromUtf8("QLineEdit:focus{border-color:%1;}").arg(Lumen::hex(Lumen::VIOLET)));
            // input() 焦点环是 EMERALD（全局语义）；AI 面板专属 VIOLET 焦点为工厂不可表达的
            // per-site 例外（VIOLET=AI 职责锁死，spec §1.3），故追加覆盖。
        auto* anote = new QLabel(QCoreApplication::translate("LightAlbum", "描述目标，本地测量 → 本地 LLM 决策 → 所见即所得导出。"), ap);
        anote->setStyleSheet(QString::fromUtf8("color:%1;").arg(Lumen::hex(Lumen::muted())));
        al5->addWidget(ah);
        al5->addWidget(goalEdit_);
        al5->addWidget(anote);
        al5->addStretch(1);
        rightStack_->addWidget(ap);
    }
    outH->addWidget(rightStack_);

    // ---- 信号 ----
    connect(photoCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &RetouchPage::onPhotoChanged);
    connect(presetCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &RetouchPage::onPresetChanged);
    connect(autoBtn_, &QPushButton::clicked, this, &RetouchPage::onAutoEnhance);
    connect(agentBtn_, &QPushButton::clicked, this, &RetouchPage::onAgentRun);
    connect(resetBtn_, &QPushButton::clicked, this, &RetouchPage::onReset);
    connect(exportBtn_, &QPushButton::clicked, this, &RetouchPage::onExport);
    connect(undoBtn_, &QPushButton::clicked, this, &RetouchPage::onUndo);
    connect(redoBtn_, &QPushButton::clicked, this, &RetouchPage::onRedo);
    connect(geoRotLBtn_, &QPushButton::clicked, this, [this]{ onGeoRotate(true); });
    connect(geoRotRBtn_, &QPushButton::clicked, this, [this]{ onGeoRotate(false); });
    connect(geoFlipHBtn_, &QPushButton::clicked, this, [this]{ onGeoFlip(true); });
    connect(geoFlipVBtn_, &QPushButton::clicked, this, [this]{ onGeoFlip(false); });
    connect(straighten_, &QSlider::valueChanged, this, &RetouchPage::onGeoStraighten);
    connect(geoCropBtn_, &QPushButton::clicked, this, &RetouchPage::onGeoCropApply);
    connect(geoResetBtn_, &QPushButton::clicked, this, &RetouchPage::onGeoReset);
    connect(maskBtn_, &QPushButton::toggled, this, &RetouchPage::onMaskToggle);
    connect(maskClearBtn_, &QPushButton::clicked, this, &RetouchPage::onMaskClear);
    connect(maskApplyBtn_, &QPushButton::clicked, this, &RetouchPage::onMaskApply);
    connect(brushSlider_, &QSlider::valueChanged, canvas_, &MaskCanvas::setBrushSize);
    connect(cropBtn_, &QPushButton::toggled, this, &RetouchPage::onCropToggle);
    connect(compareBtn_, &QPushButton::toggled, this, &RetouchPage::onCompareToggle);
    connect(eraseBtn_, &QPushButton::clicked, this, &RetouchPage::onEraseClicked);
    connect(eraseCancelBtn_, &QPushButton::clicked, this, &RetouchPage::onEraseCancel);
    connect(eraseBridge_, &EraseBridge::taskUpdated, this, &RetouchPage::onEraseTaskUpdated);
    applyTimer_ = new QTimer(this);
    applyTimer_->setSingleShot(true);
    connect(applyTimer_, &QTimer::timeout, this, &RetouchPage::doApply);

    // 撤销/重做快捷键：Ctrl+Z = 撤销；Ctrl+Shift+Z / Ctrl+Y = 重做
    auto* scUndo = new QShortcut(QKeySequence::Undo, this);
    connect(scUndo, &QShortcut::activated, this, &RetouchPage::onUndo);
    auto* scRedo = new QShortcut(QKeySequence::Redo, this);
    connect(scRedo, &QShortcut::activated, this, &RetouchPage::onRedo);

    if (!usableIdx_.isEmpty()) {
        loadPhoto(0);
        refreshChips();
    } else {
        updateUndoRedoButtons();   // 无可用照片时基线：无可撤销/重做
    }
    setMode(1);   // 默认 调整 页（分组滑杆）

    // 自测钩子：修调 + 几何断言
    if (!qEnvironmentVariableIsEmpty("LA_RETOUCH_SMOKE")) {
        QTimer::singleShot(0, this, [this]{
            if (usableIdx_.isEmpty()) {
                std::fprintf(stderr, "LA_RETOUCH_SMOKE no_usable_photo\n");
                std::fflush(stderr); QaExit::requestExit(0); return;   // 2.7.0 契约
            }
            loadPhoto(0);
            const QImage rot1 = applyGeometry(geoSrc_, 1, false, false, 0, 0);
            const QImage sq   = applyGeometry(geoSrc_, 0, false, false, 0, 1.0);
            const QImage flip = applyGeometry(geoSrc_, 0, true, false, 0, 0);
            RetouchParams p = RetouchEngine::preset(QStringLiteral("auto"));
            RetouchParams s = RetouchEngine::recommend(geoImg_).params;
            QImage out = RetouchEngine::apply(geoImg_, p);
            QImage out2 = RetouchEngine::apply(geoImg_, s);
            QImage mb(32, 32, QImage::Format_RGB32); mb.fill(qRgb(120, 120, 120));
            RetouchParams bp; bp.brightness = -0.5;
            QImage mt = RetouchEngine::apply(mb, bp);
            QImage mm(32, 32, QImage::Format_ARGB32); mm.fill(Qt::transparent);
            QPainter mmp(&mm);
            mmp.fillRect(0, 0, 16, 32, QColor(255, 255, 255, 255));
            mmp.end();
            QImage mbl = blendMask(mb, mt, mm);
            const bool maskBlend = qRed(mbl.pixel(8, 16)) < 120 && qRed(mbl.pixel(24, 16)) == 120;
            EraseBridge eb;
            eb.setModelSlot(QStringLiteral("D:/lightalbum_ai/inpaint"));
            QImage m8(8, 8, QImage::Format_ARGB32); m8.fill(QColor(255, 255, 255, 255));
            const int tid = eb.submit(geoSrc_, m8);
            const bool blockedOk = eb.task(tid)
                && eb.task(tid)->status == QStringLiteral("blocked_no_model")
                && eb.task(tid)->result.isNull();
            QImage res(geoSrc_.size(), QImage::Format_RGB32);
            res.fill(qRgb(7, 8, 9));
            const bool backfillOk = eb.applyResult(tid, res)
                && eb.task(tid)->status == QStringLiteral("done")
                && !eb.task(tid)->result.isNull();
            const QImage cn = applyGeometry(geoSrc_, 0, false, false, 0, 0, QRectF(0, 0, 0.5, 0.5));
            const bool cropNormOk = cn.width() == geoSrc_.width() / 2
                                 && cn.height() == geoSrc_.height() / 2;
            std::fprintf(stderr,
                "LA_RETOUCH_SMOKE ok=1 srcW=%d srcH=%d rotSwap=%d sqOk=%d flipOk=%d "
                "autoOutW=%d sugChips=%d sugOutNull=%d maskBlend=%d cropNormOk=%d eraseBridge=%d\n",
                geoSrc_.width(), geoSrc_.height(),
                (rot1.width() == geoSrc_.height() && rot1.height() == geoSrc_.width()) ? 1 : 0,
                (sq.width() == sq.height()) ? 1 : 0,
                (flip.size() == geoSrc_.size()) ? 1 : 0,
                out.width(),
                RetouchEngine::recommend(geoImg_).chips.size(), out2.isNull() ? 1 : 0,
                maskBlend ? 1 : 0, cropNormOk ? 1 : 0,
                (blockedOk && backfillOk) ? 1 : 0);
            std::fflush(stderr);
            QaExit::requestExit(0);   // 2.7.0：收敛到自退契约
        });
    }
}

void RetouchPage::setMode(int m)
{
    for (int i = 0; i < modeBtns_.size(); ++i)
        modeBtns_[i]->setStyleSheet(i == m ? railOnCss_ : railCss_);
    // 模式轨序（预设0/调整1/…）与右栈物理页序（0=调整滑杆，1=预设画廊）前两项互换：
    static const int kModeToPage[] = {1, 0, 2, 3, 4, 5};
    if (rightStack_) rightStack_->setCurrentIndex(kModeToPage[m]);
    if (m != 3 && maskBtn_ && maskBtn_->isChecked())   maskBtn_->setChecked(false);
    if (m != 4 && compareBtn_ && compareBtn_->isChecked()) compareBtn_->setChecked(false);
    if (m != 2 && cropBtn_ && cropBtn_->isChecked())   cropBtn_->setChecked(false);
    if (m == 3 && maskBtn_ && !maskBtn_->isChecked())   maskBtn_->setChecked(true);
    if (m == 4 && compareBtn_ && !compareBtn_->isChecked()) compareBtn_->setChecked(true);
    if (m == 0 || m == 1 || m == 5) {
        if (midStack_ && midStack_->currentIndex() != 0) midStack_->setCurrentIndex(0);
    }
}

void RetouchPage::qaShootA(const QString& png)
{
    setMode(0);
    const int pgPreset = rightStack_ ? rightStack_->currentIndex() : -1; // 期望 1（预设画廊页）
    setMode(1);
    const int pgAdj    = rightStack_ ? rightStack_->currentIndex() : -1; // 期望 0（调整滑杆页）
    QCoreApplication::processEvents();

    const int W = 1240, H = 820;
    auto renderTo = [this, W, H]() -> QImage {
        QImage img(W, H, QImage::Format_ARGB32);
        img.fill(qRgb(14, 18, 15));
        QPainter rp(&img);
        this->render(&rp, QPoint(0, 0), QRegion(this->rect()), QWidget::DrawWindowBackground | QWidget::DrawChildren);
        rp.end();
        return img;
    };
    // 偏离页面背景（≈14,18,15）即计为内容；返回（计数, 最右内容x）
    auto bandStats = [](const QImage& img) -> QPair<int,int> {
        int n = 0, ext = -1;
        for (int y = 0; y < img.height(); ++y) {
            const QRgb* ln = reinterpret_cast<const QRgb*>(img.constScanLine(y));
            for (int x = 0; x < img.width(); ++x) {
                const int r = qRed(ln[x]), g = qGreen(ln[x]), b = qBlue(ln[x]);
                if (qAbs(r - 14) + qAbs(g - 18) + qAbs(b - 15) > 40) { ++n; if (x > ext) ext = x; }
            }
        }
        return qMakePair(n, ext);
    };

    // 调整页（mode 1）
    setMode(1); QCoreApplication::processEvents();
    QImage img1 = renderTo();
    int gold = 0;
    for (int y = 0; y < img1.height(); ++y) {
        const QRgb* ln = reinterpret_cast<const QRgb*>(img1.constScanLine(y));
        for (int x = 0; x < img1.width(); ++x)
            if (qAbs(qRed(ln[x]) - 0xd9) < 40 && qAbs(qGreen(ln[x]) - 0xa9) < 40 && qAbs(qBlue(ln[x]) - 0x4e) < 40) ++gold;
    }
    const QPair<int,int> st1 = bandStats(img1);

    // 预设页（mode 0）
    setMode(0); QCoreApplication::processEvents();
    QImage img0 = renderTo();
    const QPair<int,int> st0 = bandStats(img0);

    // ext>1180 证明右栈整栏未被裁切（否则右栏内容会止于 ≈1120）；两页右栏均有内容。
    // 2.38.0 R1：一键增强 13 入口存在性（objectName opBtn_<key>；构造即建，无需点击）。
    int enhOps = 0;
    for (auto* b : findChildren<QPushButton*>())
        if (b->objectName().startsWith(QStringLiteral("opBtn_"))) ++enhOps;
    // 2.38.0 R3：消除任务取消按钮存在性（objectName 契约 eraseCancelBtn）。
    const bool hasEraseCancel = findChild<QPushButton*>(QStringLiteral("eraseCancelBtn")) != nullptr;
    const bool ok = rightStack_->count() == 6 && sliders_.size() == 12 && modeBtns_.size() == 6
                    && pgPreset == 1 && pgAdj == 0 && gold > 0
                    && st1.second > 1180 && st0.second > 1180
                    && st1.first > 500 && st0.first > 500
                    && enhOps == 13 && hasEraseCancel;
    std::fprintf(stderr, "LA_RETOUCH_A_QA pages=%d sliders=%d modes=%d map=%d/%d "
                          "adjExt=%d presetExt=%d gold=%d enhOps=%d eraseCancel=%d ok=%d png=%s\n",
                 rightStack_->count(), sliders_.size(), modeBtns_.size(), pgPreset, pgAdj,
                 st1.second, st0.second, gold, enhOps, hasEraseCancel ? 1 : 0, ok ? 1 : 0, qPrintable(png));
    std::fprintf(stderr, "LA_RETOUCH_A_QA %s ok=%d enhOps=%d eraseCancel=%d\n", ok ? "PASS" : "FAIL", ok ? 1 : 0, enhOps, hasEraseCancel ? 1 : 0);
    std::fflush(stderr);
    img1.save(png);
    const QString presetPng = QString(png).section(QLatin1Char('.'), 0, 0) + QStringLiteral("_preset.png");
    img0.save(presetPng);
    QaExit::requestExit(ok ? 0 : 1);   // 2.38.0 R1：诚实判定（旧版恒 0 = 空心门，已实测证实）
}
void RetouchPage::clearCardSel()
{
    selCard_ = -1;
    for (auto* c : cards_) c->setStyleSheet(rtCardStyle(false));
}

void RetouchPage::onCardClicked(int i)
{
    if (i < 0 || i >= presets_.size() || i >= cards_.size()) return;
    selCard_ = i;
    // 同步预设下拉（触发 onPresetChanged → onApplyPreset → setParams + doApply）
    for (int k = 0; k < presetCombo_->count(); ++k) {
        if (presetCombo_->itemData(k).toString() == presets_[i].key) {
            presetCombo_->setCurrentIndex(k);
            break;
        }
    }
    for (int k = 0; k < cards_.size(); ++k)
        cards_[k]->setStyleSheet(rtCardStyle(k == selCard_));
    badge_->setText(QCoreApplication::translate("LightAlbum", "%1 · 零云端").arg(presets_[i].name));
}

void RetouchPage::refreshGallery()
{
    if (geoImg_.isNull()) { for (auto* c : cards_) c->setIcon(QIcon()); return; }
    const QImage gsrc = geoImg_.scaled(220, 220, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    for (int i = 0; i < presets_.size() && i < cards_.size(); ++i) {
        const QImage thumb = RetouchEngine::apply(gsrc, presets_[i].p);
        cards_[i]->setIcon(QIcon(QPixmap::fromImage(thumb)));
    }
}

void RetouchPage::buildSlider(const RtSlider& def, QGridLayout* gl)
{
    const int row = gl->rowCount();
    auto* lab = new QLabel(QString::fromUtf8(def.label), this);  // P11 修乱码：label 是 UTF-8 中文，Latin-1 必豆腐
    lab->setStyleSheet(QString::fromUtf8("color:%1;").arg(Lumen::hex(Lumen::ink())));
    auto* sl = new QSlider(Qt::Horizontal, this);
    sl->setRange(int(def.min * 100), int(def.max * 100));
    sl->setValue(0);
    sl->setStyleSheet(Lumen::slider(Lumen::emerald()));   // 手写滑块 → 工厂（把手统一 12px，sanctioned）
    auto* val = new QLabel(QStringLiteral("0.00"), this);
    val->setStyleSheet(QString::fromUtf8("color:%1;min-width:48px;").arg(Lumen::hex(Lumen::muted())));
    val->setAlignment(Qt::AlignRight);
    gl->addWidget(lab, row, 0);
    gl->addWidget(sl,  row, 1);
    gl->addWidget(val, row, 2);
    sliders_.append(sl);
    sliderVals_.append(val);
    sliderDefs_.append(&def);
    connect(sl, &QSlider::valueChanged, this, &RetouchPage::onSliderChanged);
}

// 2.38.0 R1：一键增强工具区（标题 + 三组九宫格；插在滑杆行之前，buildSlider 依 rowCount 自动顺延）
void RetouchPage::buildEnhOps(QGridLayout* gl)
{
    if (!gl) return;
    int row = 0;
    auto* ht = new QLabel(QCoreApplication::translate("LightAlbum", "一键增强"), this);
    ht->setStyleSheet(QString::fromUtf8("color:%1;font-weight:bold;").arg(Lumen::hex(Lumen::ink()))
                      + Lumen::Type::css(Lumen::Type::Tier::Body));
    gl->addWidget(ht, row++, 0, 1, 3);
    const QStringList kGroups = { QCoreApplication::translate("LightAlbum", "影调"),
                                  QCoreApplication::translate("LightAlbum", "人像"),
                                  QCoreApplication::translate("LightAlbum", "校正") };
    for (int g = 0; g < kGroups.size(); ++g) {
        auto* gt = new QLabel(kGroups[g], this);
        gt->setStyleSheet(QString::fromUtf8("color:%1;font-weight:bold;").arg(Lumen::hex(Lumen::muted()))
                          + Lumen::Type::css(Lumen::Type::Tier::Small));
        gl->addWidget(gt, row++, 0, 1, 3);
        int col = 0;
        for (const EnhOp& op : kEnhOps) {
            if (op.group != g) continue;
            auto* b = new QPushButton(QString::fromUtf8(op.label), this);
            b->setObjectName(QStringLiteral("opBtn_") + QString::fromLatin1(op.key));
            b->setStyleSheet(Lumen::btnGhost(Lumen::ink()));
            b->setCursor(Qt::PointingHandCursor);
            gl->addWidget(b, row, col % 3);
            if (++col % 3 == 0) ++row;
            opBtns_.append(b);
            const QString key = QString::fromLatin1(op.key);
            connect(b, &QPushButton::clicked, this, [this, key]() { onEnhOpClicked(key); });
        }
        if (col % 3 != 0) ++row;
    }
}

void RetouchPage::onSliderChanged()
{
    exitAgentMode();   // 手动干预即接管（智能体计划失效）
    clearCardSel();    // 手动滑杆 ≠ 预设卡片
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

// ----------------------------------------------------------------- P9 AI 修图智能体
void RetouchPage::exitAgentMode()
{
    agentMode_ = false;
    agentPlan_.clear();
    agentNote_.clear();
}

void RetouchPage::syncSlidersFromCur()
{
    for (int i = 0; i < sliders_.size(); ++i) {
        const RtSlider* d = sliderDefs_[i];
        const double v = cur_[QString::fromLatin1(d->key)];
        sliders_[i]->blockSignals(true);
        sliders_[i]->setValue(int(std::max(double(d->min),
                                 std::min(double(d->max), v)) * 100));
        sliders_[i]->blockSignals(false);
        sliderVals_[i]->setText(QString::number(v, 'f', 2));
    }
}

void RetouchPage::onAgentRun()
{
    if (geoImg_.isNull() || agentBtn_->isEnabled() == false) return;
    clearCardSel();    // 进入智能体模式 ≠ 预设卡片
    agentBtn_->setEnabled(false);
    prog_->setValue(15);
    badge_->setText(QCoreApplication::translate("LightAlbum", "AI 智能体决策中…（本地测量 → 本地 LLM）"));

    const QImage img = geoImg_;
    int iso = 0;
    double shutter = 0;
    const int combo = photoCombo_->currentIndex();
    if (photos_ && combo >= 0 && combo < usableIdx_.size()) {
        const Photo& ph = (*photos_)[usableIdx_[combo]];
        iso = ph.iso;
        shutter = LumenAgent::parseShutterSeconds(ph.sh);
    }
    const QString goal = goalEdit_ ? goalEdit_->text() : QString();

    auto* watcher = new QFutureWatcher<AgentPlan>(this);
    connect(watcher, &QFutureWatcher<AgentPlan>::finished, this,
            [this, watcher]() {
                watcher->deleteLater();
                agentBtn_->setEnabled(true);
                const AgentPlan plan = watcher->result();
                if (!plan.ok || plan.params.isEmpty()) {
                    badge_->setText(QCoreApplication::translate("LightAlbum", "智能体决策失败：%1").arg(plan.reason));
                    return;
                }
                agentPlan_ = plan.params;
                agentMode_ = true;
                // 计划中与滑杆同名的键回填显示（亮度/对比/高光/阴影/自然饱和/饱和/清晰/色调/色温/锐化）
                cur_[QStringLiteral("brightness")] = agentPlan_.value(QStringLiteral("brightness"), 0.0);
                cur_[QStringLiteral("contrast")]   = agentPlan_.value(QStringLiteral("contrast"), 0.0);
                cur_[QStringLiteral("highlights")] = agentPlan_.value(QStringLiteral("highlights"), 0.0);
                cur_[QStringLiteral("shadows")]    = agentPlan_.value(QStringLiteral("shadows"), 0.0);
                cur_[QStringLiteral("vibrance")]   = agentPlan_.value(QStringLiteral("vibrance"), 0.0);
                cur_[QStringLiteral("saturation")] = agentPlan_.value(QStringLiteral("saturation"), 0.0);
                cur_[QStringLiteral("clarity")]    = agentPlan_.value(QStringLiteral("clarity"), 0.0);
                cur_[QStringLiteral("tint")]       = agentPlan_.value(QStringLiteral("tint"), 0.0);
                cur_[QStringLiteral("temp")]       = agentPlan_.value(QStringLiteral("warmth"), 0.0);
                cur_[QStringLiteral("sharpen")]    = std::max(0.0, agentPlan_.value(QStringLiteral("sharpness"), 0.0));
                syncSlidersFromCur();
                if (presetCombo_->currentData().toString() != QStringLiteral("__manual__"))
                    presetCombo_->setCurrentIndex(0);
                agentNote_ = (plan.source == QStringLiteral("ollama"))
                    ? QCoreApplication::translate("LightAlbum", "AI 智能体 · %1 · %2 · 零云端").arg(plan.model, plan.intent)
                    : QCoreApplication::translate("LightAlbum", "AI 智能体 · 本地规则兜底 · %1 · 零云端").arg(plan.intent);
                badge_->setText(QCoreApplication::translate("LightAlbum", "%1\n依据：%2").arg(agentNote_, plan.reason));
                prog_->setValue(0);  // P12: 完成即回零，进度条只在 AI 决策期间可见
                doApply();
            });
    watcher->setFuture(QtConcurrent::run([img, iso, shutter, goal]() {
        return LumenAgent::decide(LumenAgent::contextFrom(img, iso, shutter, goal));
    }));
}

// 2.38.0 R1：点击一键增强入口——异步执行（QtConcurrent）+ 写回编辑态 + 走撤销通道。
// 诚实反馈：no-op（如 levelCorrect 无倾斜）不追加 ops_、不产生历史点。
void RetouchPage::onEnhOpClicked(const QString& key)
{
    if (opBusy_ || geoSrc_.isNull()) return;
    exitAgentMode();                     // 手动动作即退出智能体模式
    clearCardSel();
    opBusy_ = true;
    const int gen = opGen_;
    for (auto* b : opBtns_) b->setEnabled(false);
    prog_->setRange(0, 0);               // 忙碌态（不确定进度；完成回落）
    badge_->setText(QCoreApplication::translate("LightAlbum", "一键增强处理中…（本地计算，零云端）"));
    const QImage src = enhImg_.isNull() ? geoSrc_ : enhImg_;   // 增强层叠在增强结果上，绝不取 geoImg_（会把已烘焙几何二次叠加，预览≠导出）
    auto* watcher = new QFutureWatcher<EnhOutcome>(this);
    connect(watcher, &QFutureWatcher<EnhOutcome>::finished, this, [this, watcher, key, gen]() {
        watcher->deleteLater();
        opBusy_ = false;
        prog_->setRange(0, 100);
        prog_->setValue(0);
        for (auto* b : opBtns_) b->setEnabled(true);
        if (gen != opGen_) return;       // 换图/重置后抵达的飞行结果：丢弃（不跨图套用）
        const EnhOutcome oc = watcher->result();
        if (oc.img.isNull()) {
            badge_->setText(oc.note.isEmpty()
                ? QCoreApplication::translate("LightAlbum", "该操作未产生改动")
                : oc.note);
            return;
        }
        enhImg_ = oc.img;
        ops_.append(key);
        rebuildGeo();                    // 几何管线基图 = enhImg_（doApply 末尾提交撤销快照）
        badge_->setText(oc.note.isEmpty()
            ? QCoreApplication::translate("LightAlbum", "一键增强 · %1 · 零云端").arg(key)
            : oc.note);                  // 诚实反馈优先于默认文案（rebuildGeo 后设置，防被 updateBadge 覆盖）
    });
    watcher->setFuture(QtConcurrent::run([src, key]() { return runEnhOp(src, key); }));
}

// ----------------------------------------------------------------- P4-1 几何槽
void RetouchPage::rebuildGeo()
{
    const QImage& base = enhImg_.isNull() ? geoSrc_ : enhImg_;   // 2.38.0 R1：几何在增强结果之上
    geoImg_ = applyGeometry(base, rot_, flipH_, flipV_, angle_, aspect_, cropNorm_);
    if (geoImg_.isNull()) {
        beforeLbl_->setText(QCoreApplication::translate("LightAlbum", "无缩略图"));
        return;
    }
    srcPm_ = QPixmap::fromImage(geoImg_);
    beforeLbl_->setPixmap(srcPm_.scaled(kPreviewW, kPreviewH, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    if (canvas_) canvas_->setBase(geoImg_);   // 几何变更使旧蒙版失效（蒙版按图像坐标存储）
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
    if (cropMode_) {
        // P4-3：提交交互裁剪框（归一化，几何无关，导出缩放自动成立）
        const QRectF r = cropCanvas_->cropRect();
        const QImage b = cropCanvas_->baseImage();
        if (r.isValid() && !b.isNull() && r.width() > 1 && r.height() > 1) {
            cropNorm_ = QRectF(r.left() / b.width(), r.top() / b.height(),
                               r.width() / b.width(), r.height() / b.height());
        }
        cropBtn_->setChecked(false);   // 退出裁剪模式回预览
    } else {
        aspect_ = aspectCombo_->currentData().toDouble();   // 0 = 自由
    }
    rebuildGeo();
}

void RetouchPage::onGeoReset()
{
    rot_ = 0; flipH_ = flipV_ = false; angle_ = 0.0; aspect_ = 0.0;
    cropNorm_ = QRectF();   // P4-3：清除交互裁剪
    straighten_->blockSignals(true);
    straighten_->setValue(0);
    straighten_->blockSignals(false);
    aspectCombo_->setCurrentIndex(0);
    rebuildGeo();
}

// ----------------------------------------------------------------- P4-2 蒙版槽
void RetouchPage::onMaskToggle(bool on)
{
    maskMode_ = on;
    if (on) {
        canvas_->setBase(geoImg_);   // 进入画布：以当前几何图重置蒙版
        if (cropBtn_->isChecked())    cropBtn_->setChecked(false);     // 三模式互斥
        if (compareBtn_->isChecked()) compareBtn_->setChecked(false);
    }
    midStack_->setCurrentIndex(on ? 1 : 0);
    badge_->setText(on ? QCoreApplication::translate("LightAlbum", "涂抹要调整的区域 · 零云端")
                       : QCoreApplication::translate("LightAlbum", "退出蒙版 · 蒙版保留可再局部应用"));
}

void RetouchPage::onMaskClear()
{
    canvas_->clearMask();
    badge_->setText(QCoreApplication::translate("LightAlbum", "蒙版已清除 · 零云端"));
}

void RetouchPage::onMaskApply()
{
    if (!canvas_->hasPaint()) {
        badge_->setText(QCoreApplication::translate("LightAlbum", "先在蒙版模式涂抹要调整的区域"));
        return;
    }
    const QImage base = geoImg_;
    const QImage toned = RetouchEngine::apply(base, buildFromMap(cur_));
    localOut_ = blendMask(base, toned, canvas_->mask());
    if (localOut_.isNull()) { badge_->setText(QCoreApplication::translate("LightAlbum", "局部应用失败")); return; }
    if (maskMode_) {
        maskBtn_->setChecked(false);   // 触发 onMaskToggle(false)：退回预览、蒙版保留
    }
    afterLbl_->setPixmap(QPixmap::fromImage(localOut_).scaled(kPreviewW, kPreviewH,
        Qt::KeepAspectRatio, Qt::SmoothTransformation));
    afterImg_ = localOut_;                                   // P4-3：供对比视图
    if (compareMode_) compareW_->setImages(geoImg_, afterImg_);
    badge_->setText(QCoreApplication::translate("LightAlbum", "局部调整已应用 · 零云端"));
    prog_->setValue(0);  // P12: 完成即回零，进度条只在 AI 决策期间可见
}

// ----------------------------------------------------------------- P4-3 裁剪 / 对比槽
void RetouchPage::onCropToggle(bool on)
{
    cropMode_ = on;
    if (on) {
        if (maskBtn_->isChecked())    maskBtn_->setChecked(false);   // 互斥（同步触发其 false 槽）
        if (compareBtn_->isChecked()) compareBtn_->setChecked(false);
        cropCanvas_->setBase(geoImg_);
        cropCanvas_->setAspect(aspect_);
        badge_->setText(QCoreApplication::translate("LightAlbum", "拖角缩放 / 框内拖移，「应用裁剪」提交 · 零云端"));
    }
    midStack_->setCurrentIndex(on ? 2 : 0);
}

void RetouchPage::onCompareToggle(bool on)
{
    compareMode_ = on;
    if (on) {
        if (maskBtn_->isChecked()) maskBtn_->setChecked(false);
        if (cropBtn_->isChecked()) cropBtn_->setChecked(false);
        compareW_->setImages(geoImg_, afterImg_);
        badge_->setText(QCoreApplication::translate("LightAlbum", "拖动金色分割线对比 · 零云端"));
    }
    midStack_->setCurrentIndex(on ? 3 : 0);
}

// ----------------------------------------------------------------- P4-4 AI 消除桥
void RetouchPage::onEraseClicked()
{
    if (!canvas_->hasPaint()) {
        badge_->setText(QCoreApplication::translate("LightAlbum", "先用蒙版模式涂抹要消除的区域"));
        return;
    }
    eraseLastTaskId_ = eraseBridge_->submit(geoImg_, canvas_->mask());
    // 状态展示在 onEraseTaskUpdated 统一更新（submit 会同步触发信号）
}

void RetouchPage::onEraseTaskUpdated(int id)
{
    const EraseTask* t = eraseBridge_->task(id);
    if (!t) return;
    if (eraseCancelBtn_) {   // 2.38.0 R3：进行中任务才显示取消（按钮零 emoji，C5 棘轮不动）
        const bool active = t->status == QStringLiteral("queued") || t->status == QStringLiteral("running");
        eraseCancelBtn_->setVisible(active);
        eraseCancelBtn_->setEnabled(active);
    }
    eraseSlotLbl_->setText(
        QCoreApplication::translate("LightAlbum", "任务#%1 %2 · %3").arg(id).arg(t->status, t->note));
    if (t->status == QStringLiteral("done") && !t->result.isNull()) {
        // 结果回填通道：模型接入后消除结果进 after 预览
        localOut_ = t->result;
        afterImg_ = t->result;
        afterLbl_->setPixmap(QPixmap::fromImage(t->result).scaled(kPreviewW, kPreviewH,
            Qt::KeepAspectRatio, Qt::SmoothTransformation));
        if (maskMode_) maskBtn_->setChecked(false);
        badge_->setText(QCoreApplication::translate("LightAlbum", "AI消除结果已回填 · 零云端"));
        prog_->setValue(0);  // P12: 完成即回零，进度条只在 AI 决策期间可见
    }
}

void RetouchPage::onEraseCancel()
{
    if (eraseLastTaskId_ < 0) return;
    if (eraseBridge_->cancel(eraseLastTaskId_))
        badge_->setText(QCoreApplication::translate("LightAlbum", "取消已受理 · 本地执行器正在复位"));
}

// ----------------------------------------------------------------- 页面槽
void RetouchPage::onPhotoChanged(int)
{
    if (photoCombo_->currentIndex() < 0) return;
    loadPhoto(usableIdx_[photoCombo_->currentIndex()]);
    refreshChips();
}

void RetouchPage::onPresetChanged(int)
{
    clearCardSel();    // 下拉切预设 ≠ 右侧卡片高亮（卡片点击会再次高亮）
    onApplyPreset();
}

void RetouchPage::onApplyPreset()   // rlRun
{
    const QString key = presetCombo_->currentData().toString();
    if (key == QStringLiteral("__manual__")) return;
    exitAgentMode();
    setParams(RetouchEngine::preset(key));
    doApply();
}

void RetouchPage::onAutoEnhance()   // rlAi
{
    exitAgentMode();
    clearCardSel();    // AI 增强 ≠ 预设卡片
    refreshChips();
    setParams(RetouchEngine::recommend(geoImg_).params);
    doApply();
}

void RetouchPage::onReset()
{
    exitAgentMode();
    clearCardSel();    // 重置后无激活预设卡片
    enhImg_ = QImage(); ops_.clear(); ++opGen_;   // 重置=全量回基线（含增强层）；doApply 末尾自然提交撤销点
    setParams(RetouchParams{});
    presetCombo_->setCurrentIndex(0);
    doApply();
}

void RetouchPage::onExport()
{
    if (geoSrc_.isNull()) return;
    const Photo& ph = (*photos_)[usableIdx_[photoCombo_->currentIndex()]];
    // 优先用原始全分辨率文件（仍零云端，纯本地）：先几何后修调，与预览同序
    // 2.29.0：统一走 ImageLoad::loadForEdit（照片/RAW/视频三条路径收口）。
    // 旧代码用 QImage(ph.path)，实测对 RAW 返回 **0x0 null=1** → RAW 无法进入修图页。
    // ★2.31.0视频必须透传**当前选中的秒数**——否则导出的是默认第2秒，
    //   而画布是用户选的那一帧 → 预览所见≠导出所得（选帧器的必备配套）。
    QImage full = ImageLoad::loadForEdit(ph.path, m_isVideo ? m_videoSec : 2.0);
    QImage work = full.isNull() ? geoSrc_ : full;
    if (work.width() > kMaxExport || work.height() > kMaxExport)
        work = work.scaled(kMaxExport, kMaxExport, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    // 2.38.0 R1：一键增强序列在全分辨率上如实重放（预览所见 = 导出所得；顺序与预览一致：先增强后几何）
    for (const QString& k : ops_) {
        const EnhOutcome oc = runEnhOp(work, k);
        if (!oc.img.isNull()) work = oc.img;
    }
    work = applyGeometry(work, rot_, flipH_, flipV_, angle_, aspect_, cropNorm_);
    // P9：智能体模式导出走同一执行路径（retouchWithPlan），预览所见即导出所得
    QImage out = agentMode_
        ? LumenEngine::retouchWithPlan(work, agentPlan_)
        : RetouchEngine::apply(work, buildFromMap(cur_));
    if (!agentMode_ && canvas_->hasPaint())      // P4-2：导出与预览同序合成蒙版
        out = blendMask(work, out, canvas_->mask());

    const QString fname = QFileDialog::getSaveFileName(this, QCoreApplication::translate("LightAlbum", "导出修后副本"),
        QDir::homePath() + "/" + QFileInfo(ph.path).baseName() + "_retouched.jpg",
        QStringLiteral("JPEG (*.jpg);;PNG (*.png)"));
    if (fname.isEmpty()) return;
    if (out.save(fname)) {
        badge_->setText(QCoreApplication::translate("LightAlbum", "已导出 · %1 · 零云端").arg(QFileInfo(fname).fileName()));
        prog_->setValue(0);  // P12: 完成即回零，进度条只在 AI 决策期间可见
    } else {
        badge_->setText(QCoreApplication::translate("LightAlbum", "导出失败"));
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

void RetouchPage::setupVideoFrameBar(const Photo& ph)
{
    // 由 mediaType 驱动（不靠扩展名二次判定，铁律6）；老行/照片恒为image。
    m_isVideo = (ph.mediaType == QLatin1String("video"));
    if (!m_isVideo) {
        if (m_vBadge) m_vBadge->setVisible(false);
        if (m_vSeek)  m_vSeek->parentWidget()->setVisible(false);
        return;
    }
    // 时长：库里已落库则直接用，否则现探（诚实空值兜底）。
    m_videoDur = ph.durSec > 0.0 ? ph.durSec
                                 : VideoDecode::probe(ph.path).durationSec;
    m_videoSec = 2.0;
    if (m_videoDur > 3.0 && m_videoSec > m_videoDur - 0.05)
        m_videoSec = qMax(0.0, m_videoDur - 1.0);   // 短片别落在片尾外

    if (m_vBadge) m_vBadge->setVisible(true);
    if (m_vSeek) {
        m_vSeek->parentWidget()->setVisible(true);
        m_vSeek->setRange(0, int(qMax(0.0, m_videoDur)));
        m_vSeek->setValue(int(m_videoSec));
    }
    if (m_vLabel) {
        m_vLabel->setText(m_videoDur > 0.0
            ? QCoreApplication::translate("LightAlbum", "第 %1 秒 / 共 %2 秒")
                  .arg(m_videoSec, 0, 'f', 1).arg(m_videoDur, 0, 'f', 1)
            : QStringLiteral("-"));
    }
}

void RetouchPage::onVideoFrameChanged(int sec)
{
    if (!m_isVideo || !photos_) return;
    const int idx = usableIdx_.value(photoCombo_->currentIndex(), -1);
    if (idx < 0 || idx >= photos_->size()) return;
    const Photo& ph = (*photos_)[idx];
    const double target = qBound(0.0, double(sec), qMax(0.0, m_videoDur));
    if (qAbs(target - m_videoSec) < 0.01) return;      // 未变则不重抽（省一次 ffmpeg）

    //按新时刻重抽帧并重载画布。
    m_videoSec = target;
    const QImage frame = ImageLoad::loadForEdit(ph.path, m_videoSec);
    if (frame.isNull()) return;                        // 抽帧失败：保持当前帧，不清空画布
    QImage img = frame;
    if (img.width() > kMaxSrc || img.height() > kMaxSrc)
        img = img.scaled(kMaxSrc, kMaxSrc, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    restoring_ = true;                // 换帧重建不计入撤销历史（不是「编辑动作」）
    geoSrc_ = img;
    enhImg_ = QImage(); ops_.clear(); ++opGen_;
    rebuildGeo();
    refreshGallery();
    restoring_ = false;
    resetHistory();
    updateUndoRedoButtons();
    if (m_vLabel) {
        m_vLabel->setText(m_videoDur > 0.0
            ? QCoreApplication::translate("LightAlbum", "第 %1 秒 / 共 %2 秒")
                  .arg(m_videoSec, 0, 'f', 1).arg(m_videoDur, 0, 'f', 1)
            : QStringLiteral("-"));
    }
}

void RetouchPage::loadPhoto(int idx)
{
    if (idx < 0 || idx >= photos_->size()) return;
    const Photo& ph = (*photos_)[idx];
    setupVideoFrameBar(ph);          // 2.31.0：按媒体类型显示/隐藏选帧控件
    const QString path = QDir(thumbsDir_).filePath(ph.thumbFile);
    QImage img(path);
    if (img.isNull()) {
        geoSrc_ = QImage(); geoImg_ = QImage();
        enhImg_ = QImage(); ops_.clear(); ++opGen_;
        beforeLbl_->setText(QCoreApplication::translate("LightAlbum", "无缩略图"));
        resetHistory();              // 空基线：无可撤销
        updateUndoRedoButtons();
        return;
    }
    if (img.width() > kMaxSrc || img.height() > kMaxSrc)
        img = img.scaled(kMaxSrc, kMaxSrc, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    restoring_ = true;               // 载入重建不计入历史
    geoSrc_ = img;
    enhImg_ = QImage(); ops_.clear(); ++opGen_;
    rebuildGeo();
    refreshGallery();
    restoring_ = false;
    resetHistory();                  // 以载入后状态为基线
    updateUndoRedoButtons();
}

// ----------------------------------------------------------------- 方向 B：撤销/重做栈
void RetouchPage::commitEdit()
{
    hist_.push(captureState());
    updateUndoRedoButtons();
}

void RetouchPage::resetHistory()
{
    hist_.clear(captureState());
    updateUndoRedoButtons();
}

RtEditState RetouchPage::captureState() const
{
    RtEditState s;
    s.cur = cur_;
    s.agentPlan = agentPlan_;
    s.agentMode = agentMode_;
    s.rot = rot_; s.flipH = flipH_; s.flipV = flipV_;
    s.angle = angle_; s.aspect = aspect_; s.cropNorm = cropNorm_;
    s.enh = enhImg_; s.ops = ops_;
    return s;
}

void RetouchPage::applyState(const RtEditState& s)
{
    cur_ = s.cur;
    agentPlan_ = s.agentPlan;
    agentMode_ = s.agentMode;
    rot_ = s.rot; flipH_ = s.flipH; flipV_ = s.flipV;
    angle_ = s.angle; aspect_ = s.aspect; cropNorm_ = s.cropNorm;
    enhImg_ = s.enh; ops_ = s.ops;
    syncSlidersFromCur();                       // 滑杆显示同步
    if (straighten_) {
        straighten_->blockSignals(true);
        straighten_->setValue(int(std::round(angle_ * 10)));
        straighten_->blockSignals(false);
    }
    if (aspectCombo_) {
        const int ai = aspectCombo_->findData(s.aspect);
        if (ai >= 0) aspectCombo_->setCurrentIndex(ai);
    }
    rebuildGeo();                               // 重算几何 + 修调（restoring_=true 已设，不重入 commit）
    updateUndoRedoButtons();
}

void RetouchPage::onUndo()
{
    if (opBusy_) return;
    if (!hist_.canUndo()) return;
    restoring_ = true;
    applyState(hist_.undo());
    restoring_ = false;
}

void RetouchPage::onRedo()
{
    if (opBusy_) return;
    if (!hist_.canRedo()) return;
    restoring_ = true;
    applyState(hist_.redo());
    restoring_ = false;
}

void RetouchPage::updateUndoRedoButtons()
{
    if (undoBtn_) undoBtn_->setEnabled(hist_.canUndo());
    if (redoBtn_) redoBtn_->setEnabled(hist_.canRedo());
}

void RetouchPage::openPhoto(const QString& path)
{
    if (!photos_ || path.isEmpty()) return;
    int g = -1;
    for (int i = 0; i < photos_->size(); ++i) {
        if ((*photos_)[i].path == path) { g = i; break; }
    }
    if (g < 0) return;
    const int u = usableIdx_.indexOf(g);
    if (u >= 0) photoCombo_->setCurrentIndex(u);   // 在 300 限额内 → 同步下拉
    loadPhoto(g);
    refreshChips();
}

void RetouchPage::doApply()
{
    if (geoImg_.isNull()) { afterLbl_->setText(QCoreApplication::translate("LightAlbum", "无缩略图")); return; }
    // P9：智能体模式走 Lumen 13 项增量管线；手动/预设走 RetouchEngine
    const QImage out = agentMode_
        ? LumenEngine::retouchWithPlan(geoImg_, agentPlan_)
        : RetouchEngine::apply(geoImg_, buildFromMap(cur_));
    afterImg_ = out;                                         // P4-3：供对比视图
    afterLbl_->setPixmap(QPixmap::fromImage(out).scaled(kPreviewW, kPreviewH,
        Qt::KeepAspectRatio, Qt::SmoothTransformation));
    if (compareMode_) compareW_->setImages(geoImg_, afterImg_);
    updateBadge();
    prog_->setValue(0);  // P12: 完成即回零，进度条只在 AI 决策期间可见
    if (!restoring_) commitEdit();   // 每次实际出图后提交快照（驱动撤销/重做栈）
}

void RetouchPage::updateBadge()
{
    if (agentMode_ && !agentNote_.isEmpty()) { badge_->setText(agentNote_); return; }
    badge_->setText(QCoreApplication::translate("LightAlbum", "%1 · 零云端").arg(RetouchEngine::describe(buildFromMap(cur_))));
}

void RetouchPage::refreshChips()
{
    auto* hl = qobject_cast<QHBoxLayout*>(chipRow_->layout());
    if (!hl) return;
    while (hl->count()) { QLayoutItem* it = hl->takeAt(0); delete it->widget(); delete it; }
    const RetouchSuggest s = RetouchEngine::recommend(geoImg_);
    if (!s.valid || s.chips.isEmpty()) {
        auto* t = new QLabel(QCoreApplication::translate("LightAlbum", "曝光均衡，无需修正"), chipRow_);
        t->setStyleSheet(QString::fromUtf8("color:%1;").arg(Lumen::hex(Lumen::muted())));
        hl->addWidget(t);
        return;
    }
    for (const QString& c : s.chips) {
        auto* b = new QPushButton(c, chipRow_);
        b->setStyleSheet(QString::fromUtf8(
            "QPushButton{background:%1;border:1px solid %2;border-radius:14px;"
            "padding:4px 12px;color:%3;%4}")
            .arg(Lumen::hex(Lumen::card()), Lumen::hex(Lumen::emerald()), Lumen::hex(Lumen::INK_IMG))
            .arg(Lumen::Type::css(Lumen::Type::Tier::Small)));   // T8：12px=T5 原位
            // 旧 d2f0e0 → INK_IMG：建议芯片浮于影像工作区之上，属影像叠加层（spec §1.7）
        connect(b, &QPushButton::clicked, this, &RetouchPage::onAutoEnhance);
        hl->addWidget(b);
    }
    hl->addStretch(1);
}
