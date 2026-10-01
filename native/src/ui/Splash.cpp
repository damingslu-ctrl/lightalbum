#include "Splash.h"
#include "Theme.h"
#include "../core/MotionPref.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QPixmap>
#include <QBrush>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QSequentialAnimationGroup>
#include <QTimer>
#include <QtMath>
#include <QPen>

// ---- P_B 光圈叶片覆盖层：随 aperture 张开、bladeAlpha 淡出，露出墨韵底 + 呼吸光晕 + 标题 ----
// 流畅性：叶片圆盘与中央唤醒光点均「一次性渲染成 pixmap」，逐帧只 blit（8×径向渐变/帧 → 8×blit/帧），
// 避免每帧重建径向渐变（Qt 光栅引擎每帧 8 个径向渐变填充是开合卡顿主因）。
static const double kIrisPi = 3.141592653589793;

class SplashWindow::IrisOverlay : public QWidget {
public:
    explicit IrisOverlay(SplashWindow* host, QWidget* parent = nullptr)
        : QWidget(parent), host_(host) {}

    void paintEvent(QPaintEvent*) override {
        const qreal ba = host_->bladeAlpha_;
        if (ba <= 0.002) return;                 // 完全淡出，无需绘制
        if (width() <= 0 || height() <= 0) return;
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

        // 叶片圆盘 pixmap 懒渲染（尺寸随 Rb 变化，尺寸不符即重渲一次）
        if (bladePix_.isNull() || bladePix_.width() != int(2 * Rb) + 2)
            renderBlade(Rb);
        // 中央唤醒光点 pixmap 懒渲染
        const qreal gr = maxR * 0.24;
        if (glowPix_.isNull() || glowPix_.width() != int(2 * gr) + 2)
            renderGlow(gr);

        p.setOpacity(ba);
        for (int i = 0; i < N; ++i) {
            const qreal ang = i * 2.0 * kIrisPi / N;
            const qreal bx = cx + off * qCos(ang);
            const qreal by = cy + off * qSin(ang);
            p.drawPixmap(QPointF(bx - Rb, by - Rb), bladePix_);
        }
        // 中央「唤醒光点」：随开合淡出，似镜头上电点亮
        p.drawPixmap(QPointF(cx - gr, cy - gr), glowPix_);
    }

private:
    void renderBlade(qreal Rb) {
        const int S = int(2 * Rb) + 2;
        bladePix_ = QPixmap(S, S);
        bladePix_.fill(Qt::transparent);
        QPainter p(&bladePix_);
        p.setRenderHint(QPainter::Antialiasing, true);
        QRadialGradient g(Rb, Rb, Rb);
        g.setColorAt(0.0,  QColor(0x18, 0x22, 0x1c));
        g.setColorAt(0.72, QColor(0x0e, 0x16, 0x11));
        g.setColorAt(1.0,  QColor(0x07, 0x0d, 0x09));
        p.setBrush(g);
        p.setPen(QPen(QColor(0x4f, 0xc0, 0x8a, 80), 1.1));
        p.drawEllipse(QRectF(0, 0, 2 * Rb, 2 * Rb));
    }
    void renderGlow(qreal r) {
        const int S = int(2 * r) + 2;
        glowPix_ = QPixmap(S, S);
        glowPix_.fill(Qt::transparent);
        QPainter p(&glowPix_);
        p.setRenderHint(QPainter::Antialiasing, true);
        QRadialGradient cg(r, r, r);
        cg.setColorAt(0.0, QColor(0x7f, 0xe6, 0xac, 235));
        cg.setColorAt(1.0, QColor(0x7f, 0xe6, 0xac, 0));
        p.setBrush(cg);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QRectF(0, 0, 2 * r, 2 * r));
    }
    SplashWindow* host_;
    QPixmap bladePix_;
    QPixmap glowPix_;
};


SplashWindow::SplashWindow(const QRect& target, QWidget* parent)
    : QWidget(parent), target_(target)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setGeometry(target_);

    // ---- 呼吸光晕（径向翡翠光，置于标题后方）----
    glow_ = new QWidget(this);
    glow_->setAttribute(Qt::WA_TransparentForMouseEvents);
    glow_->setFixedSize(520, 520);
    {
        QPixmap gp(520, 520);
        gp.fill(Qt::transparent);
        QPainter p(&gp);
        p.setRenderHint(QPainter::Antialiasing, true);
        QRadialGradient rg(260, 260, 260);
        rg.setColorAt(0.0, QColor(0x4f, 0xc0, 0x8a, 150));
        rg.setColorAt(0.45, QColor(0x4f, 0xc0, 0x8a, 60));
        rg.setColorAt(1.0, QColor(0x4f, 0xc0, 0x8a, 0));
        p.setBrush(rg);
        p.setPen(Qt::NoPen);
        p.drawEllipse(0, 0, 520, 520);
        glow_->setAutoFillBackground(false);
        // 用 QLabel 承载 pixmap 更稳；这里直接 setPixmap 需要 QLabel，改用子 QLabel
        auto* gpLbl = new QLabel(glow_);
        gpLbl->setPixmap(gp);
        gpLbl->setGeometry(0, 0, 520, 520);
    }
    glowEff_ = new QGraphicsOpacityEffect(glow_);
    glowEff_->setOpacity(0.42);
    glow_->setGraphicsEffect(glowEff_);

    // ---- 标题 / 副标题 / tagline ----
    title_ = new QLabel(QStringLiteral("光影集"), this);
    QFont tf = title_->font(); tf.setPointSizeF(46); tf.setBold(true);
    tf.setLetterSpacing(QFont::AbsoluteSpacing, 14.0);
    title_->setFont(tf);
    title_->setStyleSheet("color:#f4f7f4;background:transparent;");

    sub_ = new QLabel(QStringLiteral("L I G H T A L B U M"), this);
    QFont sf = sub_->font(); sf.setPointSizeF(12); sf.setBold(true);
    sf.setLetterSpacing(QFont::AbsoluteSpacing, 6.0);
    sub_->setFont(sf);
    sub_->setStyleSheet("color:#d9a94e;background:transparent;");

    tag_ = new QLabel(QStringLiteral("私人影像馆 · 零云端 · 本地优先"), this);
    tag_->setStyleSheet("color:#8da396;font-size:13px;background:transparent;");

    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(14);
    v->setAlignment(Qt::AlignCenter);

    auto* text = new QWidget(this);
    auto* tl = new QVBoxLayout(text);
    tl->setContentsMargins(0, 0, 0, 0);
    tl->setSpacing(12);
    tl->setAlignment(Qt::AlignCenter);
    tl->addWidget(title_, 0, Qt::AlignHCenter);
    tl->addWidget(sub_, 0, Qt::AlignHCenter);
    tl->addWidget(tag_, 0, Qt::AlignHCenter);
    v->addWidget(text, 0, Qt::AlignHCenter);

    // 光圈叶片覆盖层：置于标题之上，随 aperture 张开、bladeAlpha 淡出露出品牌
    iris_ = new IrisOverlay(this, this);
    iris_->setAttribute(Qt::WA_TranslucentBackground, true);
    iris_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    iris_->setGeometry(0, 0, width(), height());
    iris_->raise();

    // 注：glow_ 已在构造函数上方作为自由子控件创建（不在布局内），
    // 位置在 play()（窗口已显示、尺寸已知）时居中。
}

void SplashWindow::paintEvent(QPaintEvent* e)
{
    Q_UNUSED(e);
    // 底板是静态渐变+seam，仅在尺寸变化时重渲一次，逐帧只 blit（避免每帧重画渐变 → 开合更顺）。
    if (boardPix_.isNull() || boardPix_.size() != size())
        renderBoard();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.drawPixmap(0, 0, boardPix_);
}

void SplashWindow::renderBoard()
{
    boardPix_ = QPixmap(size());
    boardPix_.fill(Qt::transparent);
    QPainter p(&boardPix_);
    p.setRenderHint(QPainter::Antialiasing, true);
    // 圆角底板：对角翡翠→曜石→金低语渐变
    QPainterPath path;
    const qreal r = 22.0;
    path.addRoundedRect(0, 0, width(), height(), r, r);
    p.setClipPath(path);
    QLinearGradient g(0, 0, width(), height());
    g.setColorAt(0.0, QColor(0x10, 0x1c, 0x14));
    g.setColorAt(0.5, QColor(0x07, 0x0c, 0x08));
    g.setColorAt(1.0, QColor(0x18, 0x12, 0x06));
    p.fillRect(0, 0, width(), height(), g);
    // 顶部一条翡翠→金发丝线（与客户端 seam 呼应）
    QLinearGradient seam(0, 0, width(), 0);
    seam.setColorAt(0.0, QColor(0x4f, 0xc0, 0x8a));
    seam.setColorAt(0.55, QColor(0xd9, 0xa9, 0x4e));
    seam.setColorAt(1.0, QColor(0xd9, 0xa9, 0x4e, 0));
    p.fillRect(0, 0, width(), 3, seam);
}

void SplashWindow::play()
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
}

// 无障碍：静态品牌画面（无呼吸/收缩），短暂停留后直接发 finished()，
// 由 main.cpp 以「无展开动效」方式切入客户端。
void SplashWindow::playStatic()
{
    show();
    raise();
    // 静态态：光圈直接全开、叶片透明，避免覆盖品牌（reduced-motion 无开合动效）
    setAperture(1.0);
    setBladeAlpha(0.0);
    glow_->setGeometry((width() - 520) / 2, (height() - 520) / 2, 520, 520);
    setWindowOpacity(1.0);
    QTimer::singleShot(600, this, [this] {
        emit finished();
    });
}

void SplashWindow::setAperture(qreal v)
{
    aperture_ = v;
    // 底板是静态缓存，随 aperture 不变 → 只重绘 iris 覆盖层，不再 update() 整窗（减少每帧重绘开销）
    if (iris_) iris_->update();
}

void SplashWindow::setBladeAlpha(qreal v)
{
    bladeAlpha_ = v;
    if (iris_) iris_->update();
}

QPixmap SplashWindow::grabIris() const
{
    return iris_ ? iris_->grab() : QPixmap();
}
