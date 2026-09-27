#pragma once
// P4-2 画笔蒙版画布：QGraphicsScene 重写 web 版 fabric 蒙版部分（零云端）。
// 底图 + 翡翠色可视化叠加层双层 pixmap；笔刷写入 ARGB32 蒙版图（仅用 alpha 通道，
// 255=全强度）；视图 1:1 显示（scene 坐标 == 图像像素坐标），映射零失真。
#include <QGraphicsView>
#include <QImage>
#include <QPointF>

class QGraphicsPixmapItem;

class MaskCanvas : public QGraphicsView {
    Q_OBJECT
public:
    explicit MaskCanvas(QWidget* parent = nullptr);

    void setBase(const QImage& img);        // 换底图（重置蒙版——几何变更会使蒙版失效）
    const QImage& mask() const { return mask_; }
    QImage  baseImage() const { return base_; }
    bool    hasPaint() const { return painted_; }
    void    clearMask();
    void    setBrushSize(int px) { brush_ = px; }

signals:
    void maskPainted();

protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    void drawStroke(const QPointF& from, const QPointF& to);
    void syncOverlay();

    QImage base_;      // 底图（ARGB32）
    QImage mask_;      // 蒙版（ARGB32，仅 alpha 有意义）
    QImage overlay_;   // 翡翠色可视化（与 mask_ 同步绘制）
    QGraphicsPixmapItem* baseItem_ = nullptr;
    QGraphicsPixmapItem* overlayItem_ = nullptr;
    int    brush_ = 24;
    bool   painting_ = false;
    bool   painted_ = false;
    QPointF last_;
};
