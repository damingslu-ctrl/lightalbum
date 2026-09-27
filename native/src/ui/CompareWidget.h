#pragma once
// P4-3 深度对比视图：单幅区域内 before/after 金色分割线对比（拖拽分割线）。
// 左侧显原图（几何后）、右侧显修后；角标文字；零云端纯绘制。
#include <QWidget>
#include <QImage>

class CompareWidget : public QWidget {
    Q_OBJECT
public:
    explicit CompareWidget(QWidget* parent = nullptr);

    void setImages(const QImage& before, const QImage& after);

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    QRect dispRect() const;   // 等比显示区（两图同尺寸）

    QImage before_, after_;
    double split_ = 0.5;      // 0..1（widget 宽比例）
    bool   dragging_ = false;
};
