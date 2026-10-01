#pragma once
// P13 开场动画（P_B 升级为「光圈开合·镜头唤醒」）：
// 全屏 hero 渐变 + 呼吸光晕 + 光圈叶片开合（iris overlay）→ 定时收缩成缝 →
// 随后 MainWindow 从缝中向上下展开（展开由 main.cpp 接管）。
#include <QWidget>
#include <QLabel>
#include <QPixmap>
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
    // QA：单独抓取光圈叶片覆盖层（验证 Option B 渲染）
    QPixmap grabIris() const;

    qreal aperture()   const { return aperture_; }
    void  setAperture(qreal v);
    qreal bladeAlpha() const { return bladeAlpha_; }
    void  setBladeAlpha(qreal v);

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
    QPixmap boardPix_;         // 静态底板（渐变+seam）一次性渲染，逐帧 blit，避免每帧重绘渐变
    void renderBoard();        // 懒渲染底板到 boardPix_
};
