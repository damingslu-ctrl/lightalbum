#pragma once
// P13 开场动画：全屏 hero 渐变 + 呼吸光晕，定时后上下收缩进一条缝，
// 随后 MainWindow 从缝中向上下展开（展开由 main.cpp 接管）。
#include <QWidget>
#include <QLabel>
class QGraphicsOpacityEffect;
class QSequentialAnimationGroup;
class QPropertyAnimation;

class SplashWindow : public QWidget {
    Q_OBJECT
public:
    explicit SplashWindow(const QRect& target, QWidget* parent = nullptr);
    // 播放开场：整体淡入 → 光晕呼吸 → 定时后收缩成缝 → 发 finished()
    void play();
    // 无障碍（reduced-motion）：静态品牌画面，无呼吸/收缩动效，短暂停留后发 finished()
    void playStatic();

signals:
    void finished();

protected:
    void paintEvent(QPaintEvent* e) override;

private:
    QRect target_;
    QWidget*               glow_    = nullptr;   // 呼吸光晕（径向渐变 pixmap）
    QLabel*                title_   = nullptr;   // 大字「光影集」
    QLabel*                sub_     = nullptr;   // 金 EN 副标题
    QLabel*                tag_     = nullptr;   // 一句 tagline
    QGraphicsOpacityEffect* glowEff_ = nullptr;  // 光晕呼吸
    QSequentialAnimationGroup* breath_ = nullptr;
    QPropertyAnimation*     intro_   = nullptr;  // 整体/标题淡入
    QPropertyAnimation*     contract_ = nullptr; // 收缩成缝
};
