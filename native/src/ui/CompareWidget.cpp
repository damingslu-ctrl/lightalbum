#include "CompareWidget.h"
#include <QPainter>
#include <QMouseEvent>
#include <algorithm>

CompareWidget::CompareWidget(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setCursor(Qt::SplitHCursor);
    setMinimumHeight(360);
}

void CompareWidget::setImages(const QImage& before, const QImage& after)
{
    before_ = before;
    after_ = after;
    update();
}

QRect CompareWidget::dispRect() const
{
    if (before_.isNull() || width() < 2 || height() < 2) return QRect();
    const double ia = double(before_.width()) / double(before_.height());
    const double wa = double(width()) / double(height());
    double w, h;
    if (ia > wa) { w = width() - 2; h = w / ia; }
    else         { h = height() - 2; w = h * ia; }
    return QRect(int((width() - w) / 2), int((height() - h) / 2), int(w), int(h));
}

void CompareWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0x06, 0x08, 0x06));
    if (before_.isNull()) {
        p.setPen(QColor(0x8d, 0xa3, 0x96));
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("无图像"));
        return;
    }
    const QRect d = dispRect();
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    // 右侧：修后（整幅绘制，再被左半覆盖）
    if (!after_.isNull())
        p.drawImage(d, after_);
    else
        p.drawImage(d, before_);
    // 左侧：原图（裁剪到分割线）
    const int splitX = int(d.left() + d.width() * split_);
    p.save();
    p.setClipRect(QRect(d.left(), d.top(), splitX - d.left(), d.height()));
    p.drawImage(d, before_);
    p.restore();
    // 分割线
    p.setPen(QPen(QColor(0xd9, 0xa9, 0x4e), 2));
    p.drawLine(splitX, d.top(), splitX, d.bottom());
    // 角标
    p.setPen(QColor(0xf4, 0xf7, 0xf4));
    QFont f = p.font(); f.setPointSizeF(10); p.setFont(f);
    p.drawText(QRect(d.left() + 8, d.top() + 6, 80, 20), Qt::AlignLeft, QStringLiteral("原图"));
    p.drawText(QRect(d.right() - 88, d.top() + 6, 80, 20), Qt::AlignRight, QStringLiteral("修后"));
}

void CompareWidget::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) { dragging_ = true; e->accept(); return; }
    QWidget::mousePressEvent(e);
}

void CompareWidget::mouseMoveEvent(QMouseEvent* e)
{
    if (dragging_ && width() > 0) {
        split_ = std::clamp(double(e->pos().x()) / double(width()), 0.0, 1.0);
        update();
        e->accept();
        return;
    }
    QWidget::mouseMoveEvent(e);
}

void CompareWidget::mouseReleaseEvent(QMouseEvent* e)
{
    dragging_ = false;
    QWidget::mouseReleaseEvent(e);
}
