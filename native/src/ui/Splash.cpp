#include "Splash.h"
#include "Theme.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QBrush>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QSequentialAnimationGroup>
#include <QTimer>
#include <QtMath>

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
    // 注：glow_ 已在构造函数上方作为自由子控件创建（不在布局内），
    // 位置在 play()（窗口已显示、尺寸已知）时居中。
}

void SplashWindow::paintEvent(QPaintEvent* e)
{
    Q_UNUSED(e);
    QPainter p(this);
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
    intro_->setDuration(360);
    intro_->setStartValue(0.0);
    intro_->setEndValue(1.0);
    intro_->setEasingCurve(QEasingCurve::OutCubic);
    intro_->start(QAbstractAnimation::DeleteWhenStopped);

    // 定时后收缩成缝
    QTimer::singleShot(1500, this, [this] {
        if (breath_) { breath_->stop(); }
        const QRect slit(target_.x(), target_.center().y() - 1,
                         target_.width(), 2);
        contract_ = new QPropertyAnimation(this, "geometry");
        contract_->setDuration(680);
        contract_->setStartValue(geometry());
        contract_->setEndValue(slit);
        contract_->setEasingCurve(QEasingCurve::InOutCubic);
        connect(contract_, &QPropertyAnimation::finished, this, [this] {
            emit finished();
        });
        contract_->start(QAbstractAnimation::DeleteWhenStopped);
    });
}
