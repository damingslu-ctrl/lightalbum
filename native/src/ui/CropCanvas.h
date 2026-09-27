#pragma once
// P4-3 交互式裁剪画布：QGraphicsScene 重写 web 版 fabric 裁剪部分（零云端）。
// 暗色遮罩盖住裁剪框外区域 + 金边框 + 三分线；四角手柄缩放 / 框内拖移 / 空白处新建；
// aspect>0 时拖拽锁定宽高比。scene 坐标 == 图像像素坐标（1:1，mapToScene 零失真）。
#include <QGraphicsView>
#include <QImage>
#include <QRectF>
#include <QPointF>

class QGraphicsPixmapItem;
class QGraphicsRectItem;
class QGraphicsLineItem;

class CropCanvas : public QGraphicsView {
    Q_OBJECT
public:
    explicit CropCanvas(QWidget* parent = nullptr);

    void setBase(const QImage& img);        // 换底图（重置为居中 80% 框）
    void setAspect(double a);               // 0=自由；>0 拖拽锁定宽高比
    QRectF cropRect() const { return rect_; }
    QImage baseImage() const { return base_; }

signals:
    void rectChanged();

protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    enum Mode { None, New, Move, ResTL, ResTR, ResBR, ResBL };
    void setRect(const QRectF& r);
    void syncOverlay();
    int hitHandle(const QPointF& p) const;  // 0 无, 1 TL, 2 TR, 3 BR, 4 BL

    QImage base_;
    QGraphicsPixmapItem* baseItem_ = nullptr;
    QGraphicsPixmapItem* shadeItem_ = nullptr;
    QGraphicsRectItem*  borderItem_ = nullptr;
    QGraphicsLineItem* v1_ = nullptr;
    QGraphicsLineItem* v2_ = nullptr;
    QGraphicsLineItem* h1_ = nullptr;
    QGraphicsLineItem* h2_ = nullptr;
    QRectF rect_;
    double aspect_ = 0;
    int    mode_ = None;
    QPointF press_;        // 按下时 scene 坐标
    QRectF  pressRect_;    // 按下时框
    QPointF resizeAnchor_; // 缩放时的对角锚点
};
