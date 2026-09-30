#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# P_B 落地「光圈开合·镜头唤醒」：CRLF 安全读-替换-断言-写回。
import os, sys

ROOT = os.path.dirname(os.path.abspath(__file__))

def read(p):
    with open(os.path.join(ROOT, p), 'r', encoding='utf-8', newline='') as f:
        return f.read().replace('\r\n', '\n')

def write(p, s):
    s = s.replace('\r\n', '\n').replace('\n', '\r\n')
    with open(os.path.join(ROOT, p), 'w', encoding='utf-8', newline='') as f:
        f.write(s)

def rep(s, old, new, ctx):
    c = s.count(old)
    assert c == 1, f"[{ctx}] expected 1 occurrence, got {c}"
    return s.replace(old, new)

# ---------- Splash.h (full rewrite) ----------
H = r'''#pragma once
// P13 开场动画（P_B 升级为「光圈开合·镜头唤醒」）：
// 全屏 hero 渐变 + 呼吸光晕 + 光圈叶片开合（iris overlay）→ 定时收缩成缝 →
// 随后 MainWindow 从缝中向上下展开（展开由 main.cpp 接管）。
#include <QWidget>
#include <QLabel>
class QGraphicsOpacityEffect;
class QSequentialAnimationGroup;
class QPropertyAnimation;
class QPaintEvent;

class SplashWindow : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal aperture READ aperture WRITE setAperture)
    Q_PROPERTY(qreal bladeAlpha READ bladeAlpha WRITE setBladeAlpha)
public:
    explicit SplashWindow(const QRect& target, QWidget* parent = nullptr);
    // 播放开场：淡入 → 光圈开合露出品牌 + 光晕呼吸 → 收缩成缝 → 发 finished()
    void play();
    // 无障碍（reduced-motion）：静态品牌画面，无开合/呼吸/收缩，短暂停留后发 finished()
    void playStatic();

    qreal aperture()   const { return aperture_; }
    void  setAperture(qreal v) { aperture_ = v; if (iris_) iris_->update(); update(); }
    qreal bladeAlpha() const { return bladeAlpha_; }
    void  setBladeAlpha(qreal v) { bladeAlpha_ = v; if (iris_) iris_->update(); update(); }

signals:
    void finished();

protected:
    void paintEvent(QPaintEvent* e) override;

private:
    class IrisOverlay;
    QRect target_;
    QWidget*                 glow_     = nullptr;   // 呼吸光晕（径向渐变 pixmap）
    QLabel*                  title_    = nullptr;   // 大字「光影集」
    QLabel*                  sub_      = nullptr;   // 金 EN 副标题
    QLabel*                  tag_      = nullptr;   // 一句 tagline
    QGraphicsOpacityEffect*  glowEff_  = nullptr;   // 光晕呼吸
    QSequentialAnimationGroup* breath_ = nullptr;
    QPropertyAnimation*      intro_    = nullptr;   // 整体/标题淡入
    QPropertyAnimation*      contract_ = nullptr;   // 收缩成缝
    IrisOverlay*             iris_     = nullptr;   // 光圈叶片覆盖层（标题之上）
    qreal aperture_   = 0.0;   // 0=闭合 1=全开
    qreal bladeAlpha_ = 1.0;   // 叶片不透明度（随开合淡出）
};
'''
write('src/ui/Splash.h', H)

# ---------- Splash.cpp ----------
s = read('src/ui/Splash.cpp')

# 1) include QPen
s = rep(s,
        '#include <QtMath>\n',
        '#include <QtMath>\n#include <QPen>\n',
        'Splash.cpp include QPen')

# 2) ctor: insert iris_ creation
s = rep(s,
'''    v->addWidget(text, 0, Qt::AlignHCenter);
    // 注：glow_ 已在构造函数上方作为自由子控件创建（不在布局内），
    // 位置在 play()（窗口已显示、尺寸已知）时居中。
}''',
'''    v->addWidget(text, 0, Qt::AlignHCenter);

    // 光圈叶片覆盖层：置于标题之上，随 aperture 张开、bladeAlpha 淡出露出品牌
    iris_ = new IrisOverlay(this, this);
    iris_->setAttribute(Qt::WA_TranslucentBackground, true);
    iris_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    iris_->setGeometry(0, 0, width(), height());
    iris_->raise();

    // 注：glow_ 已在构造函数上方作为自由子控件创建（不在布局内），
    // 位置在 play()（窗口已显示、尺寸已知）时居中。
}''',
        'Splash.cpp ctor iris_')

# 3) play() full replacement
s = rep(s,
'''void SplashWindow::play()
{
    show();
    raise();
    // 窗口尺寸已知后居中呼吸光晕（柔翡翠径向光，压在标题后方）
    glow_->setGeometry((width() - 520) / 2, (height() - 520) / 2, 520, 520);
    // 光晕呼吸：0.42 ↔ 0.7，循环
    auto* a1 = new QPropertyAnimation(glowEff_, "opacity");
    a1->setDuration(2200);
    a1->setStartValue(0.42);
    a1->setEndValue(0.72);
    auto* a2 = new QPropertyAnimation(glowEff_, "opacity");
    a2->setDuration(2200);
    a2->setStartValue(0.72);
    a2->setEndValue(0.42);
    breath_ = new QSequentialAnimationGroup(this);
    breath_->addAnimation(a1);
    breath_->addAnimation(a2);
    breath_->setLoopCount(-1);
    breath_->start();

    // 整体淡入
    setWindowOpacity(0.0);
    intro_ = new QPropertyAnimation(this, "windowOpacity");
    intro_->setDuration(Lumen::Motion::kSplashIntroMs);
    intro_->setStartValue(0.0);
    intro_->setEndValue(1.0);
    intro_->setEasingCurve(QEasingCurve::OutCubic);
    intro_->start(QAbstractAnimation::DeleteWhenStopped);

    // 定时后收缩成缝
    QTimer::singleShot(Lumen::Motion::kSplashHoldMs, this, [this] {
        if (breath_) { breath_->stop(); }
        const QRect slit(target_.x(), target_.center().y() - 1,
                         target_.width(), 2);
        contract_ = new QPropertyAnimation(this, "geometry");
        contract_->setDuration(Lumen::Motion::kSplashContractMs);
        contract_->setStartValue(geometry());
        contract_->setEndValue(slit);
        contract_->setEasingCurve(QEasingCurve::InOutCubic);
        connect(contract_, &QPropertyAnimation::finished, this, [this] {
            emit finished();
        });
        contract_->start(QAbstractAnimation::DeleteWhenStopped);
    });
}''',
'''void SplashWindow::play()
{
    show();
    raise();
    // 窗口尺寸已知后居中呼吸光晕（柔翡翠径向光，压在标题后方）
    glow_->setGeometry((width() - 520) / 2, (height() - 520) / 2, 520, 520);
    if (iris_) { iris_->setGeometry(0, 0, width(), height()); iris_->raise(); }

    // 初始：光圈闭合、叶片不透明
    setAperture(0.0);
    setBladeAlpha(1.0);

    // 光圈开合：叶片随 aperture 外移张开，并随 bladeAlpha 淡出，露出墨韵底 + 光晕 + 标题
    auto* ap = new QPropertyAnimation(this, "aperture");
    ap->setDuration(Lumen::Motion::kSplashApertureMs);
    ap->setStartValue(0.0);
    ap->setEndValue(1.0);
    ap->setEasingCurve(QEasingCurve::OutCubic);
    ap->start(QAbstractAnimation::DeleteWhenStopped);

    auto* ba = new QPropertyAnimation(this, "bladeAlpha");
    ba->setDuration(Lumen::Motion::kSplashApertureMs);
    ba->setStartValue(1.0);
    ba->setEndValue(0.0);
    ba->setEasingCurve(QEasingCurve::OutCubic);
    ba->start(QAbstractAnimation::DeleteWhenStopped);

    // 光晕呼吸：0.42 ↔ 0.7，循环（被逐步张开的光圈显露）
    auto* a1 = new QPropertyAnimation(glowEff_, "opacity");
    a1->setDuration(2200);
    a1->setStartValue(0.42);
    a1->setEndValue(0.72);
    auto* a2 = new QPropertyAnimation(glowEff_, "opacity");
    a2->setDuration(2200);
    a2->setStartValue(0.72);
    a2->setEndValue(0.42);
    breath_ = new QSequentialAnimationGroup(this);
    breath_->addAnimation(a1);
    breath_->addAnimation(a2);
    breath_->setLoopCount(-1);
    breath_->start();

    // 整体淡入
    setWindowOpacity(0.0);
    intro_ = new QPropertyAnimation(this, "windowOpacity");
    intro_->setDuration(Lumen::Motion::kSplashIntroMs);
    intro_->setStartValue(0.0);
    intro_->setEndValue(1.0);
    intro_->setEasingCurve(QEasingCurve::OutCubic);
    intro_->start(QAbstractAnimation::DeleteWhenStopped);

    // 开合 + 停留 后收缩成缝（沿用现有「缝 → 客户端展开」衔接）
    QTimer::singleShot(Lumen::Motion::kSplashApertureMs + Lumen::Motion::kSplashHoldMs,
                       this, [this] {
        if (breath_) { breath_->stop(); }
        const QRect slit(target_.x(), target_.center().y() - 1,
                         target_.width(), 2);
        contract_ = new QPropertyAnimation(this, "geometry");
        contract_->setDuration(Lumen::Motion::kSplashContractMs);
        contract_->setStartValue(geometry());
        contract_->setEndValue(slit);
        contract_->setEasingCurve(QEasingCurve::InOutCubic);
        connect(contract_, &QPropertyAnimation::finished, this, [this] {
            emit finished();
        });
        contract_->start(QAbstractAnimation::DeleteWhenStopped);
    });
}''',
        'Splash.cpp play()')

# 4) playStatic(): reset iris to open+transparent
s = rep(s,
'''void SplashWindow::playStatic()
{
    show();
    raise();
    glow_->setGeometry((width() - 520) / 2, (height() - 520) / 2, 520, 520);
    setWindowOpacity(1.0);''',
'''void SplashWindow::playStatic()
{
    show();
    raise();
    // 静态态：光圈直接全开、叶片透明，避免覆盖品牌（reduced-motion 无开合动效）
    setAperture(1.0);
    setBladeAlpha(0.0);
    glow_->setGeometry((width() - 520) / 2, (height() - 520) / 2, 520, 520);
    setWindowOpacity(1.0);''',
        'Splash.cpp playStatic()')

# 5) append IrisOverlay class + kPi
IRIS = r'''

// ---- P_B 光圈叶片覆盖层：随 aperture 张开、bladeAlpha 淡出，露出墨韵底 + 呼吸光晕 + 标题 ----
static const double kIrisPi = 3.141592653589793;

class SplashWindow::IrisOverlay : public QWidget {
public:
    explicit IrisOverlay(SplashWindow* host, QWidget* parent = nullptr)
        : QWidget(parent), host_(host) {}
    void paintEvent(QPaintEvent*) override {
        const qreal ba = host_->bladeAlpha_;
        if (ba <= 0.002) return;                 // 完全淡出，无需绘制
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        QPainterPath clip;
        clip.addRoundedRect(0, 0, width(), height(), 22.0, 22.0);
        p.setClipPath(clip);

        const qreal cx = width() / 2.0, cy = height() / 2.0;
        const qreal maxR = qMin(width(), height()) * 0.62;
        const int   N  = Lumen::Motion::kSplashBladeCount;
        const qreal Rb = maxR * 1.05;
        const qreal ap = host_->aperture_;
        const qreal off = Rb + ap * maxR;         // 叶片中心外移 → 中央孔张开
        p.setOpacity(ba);

        for (int i = 0; i < N; ++i) {
            const qreal ang = i * 2.0 * kIrisPi / N;
            const qreal bx = cx + off * qCos(ang);
            const qreal by = cy + off * qSin(ang);
            QRadialGradient g(bx, by, Rb);
            g.setColorAt(0.0,  QColor(0x18, 0x22, 0x1c));
            g.setColorAt(0.72, QColor(0x0e, 0x16, 0x11));
            g.setColorAt(1.0,  QColor(0x07, 0x0d, 0x09));
            p.setBrush(g);
            p.setPen(QPen(QColor(0x4f, 0xc0, 0x8a, 80), 1.1));
            p.drawEllipse(QRectF(bx - Rb, by - Rb, 2 * Rb, 2 * Rb));
        }
        // 中央「唤醒光点」：随开合淡出，似镜头上电点亮
        QRadialGradient cg(cx, cy, maxR * 0.24);
        cg.setColorAt(0.0, QColor(0x7f, 0xe6, 0xac, 235));
        cg.setColorAt(1.0, QColor(0x7f, 0xe6, 0xac, 0));
        p.setBrush(cg);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QRectF(cx - maxR * 0.24, cy - maxR * 0.24,
                            2 * maxR * 0.24, 2 * maxR * 0.24));
    }
    SplashWindow* host_;
};
'''
s = s.rstrip('\n') + '\n' + IRIS
write('src/ui/Splash.cpp', s)

# ---------- MotionPref.h ----------
m = read('src/core/MotionPref.h')
m = rep(m,
'''inline constexpr int    kSplashIntroMs    = 420;   // 开场整体淡入（原 360）
inline constexpr int    kSplashHoldMs     = 1300;  // 呼吸停留（原 1500）
inline constexpr int    kSplashContractMs = 720;   // 收缩进缝（原 680）''',
'''inline constexpr int    kSplashIntroMs    = 420;   // 开场整体淡入（原 360）
inline constexpr int    kSplashHoldMs     = 1300;  // 呼吸停留（原 1500）
inline constexpr int    kSplashContractMs = 720;   // 收缩进缝（原 680）
inline constexpr int    kSplashApertureMs = 900;  // P_B 光圈开合时长
inline constexpr int    kSplashBladeCount = 8;     // P_B 光圈叶片数''',
        'MotionPref.h splash consts')

# ---------- main.cpp ----------
mc = read('src/main.cpp')
mc = rep(mc,
'''    // P13 开场动画探针：LA_SPLASH_SHOT=<dir> 时抓 3 帧（完整/收缩中/成缝）存 PNG 后退出。''',
'''    // P13 开场动画探针：LA_SPLASH_SHOT=<dir> 时抓 3 帧（开合/停留/成缝）存 PNG 后退出。''',
        'main.cpp splash comment')
mc = rep(mc,
'''        QTimer::singleShot(1850, &app, [&sp, &dir] {
            sp.grab().save(QDir(dir).filePath(QStringLiteral("splash-2-contract.png")));
            std::fprintf(stderr, "LA_SPLASH_SHOT contract saved\\n"); std::fflush(stderr);
        });''',
'''        QTimer::singleShot(1600, &app, [&sp, &dir] {
            sp.grab().save(QDir(dir).filePath(QStringLiteral("splash-2-hold.png")));
            std::fprintf(stderr, "LA_SPLASH_SHOT hold saved\\n"); std::fflush(stderr);
        });''',
        'main.cpp splash middle grab')

write('src/main.cpp', mc)
print("PATCH_OK")
