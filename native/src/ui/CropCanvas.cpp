#include "CropCanvas.h"
#include "Theme.h"
#include <QMouseEvent>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsRectItem>
#include <QGraphicsLineItem>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <algorithm>

namespace {
constexpr qreal kHandle = 14.0;   // 角手柄命中盒（scene px）
constexpr qreal kMinSize = 16.0;  // 框最小边
} // namespace

CropCanvas::CropCanvas(QWidget* parent)
    : QGraphicsView(parent)
{
    setScene(new QGraphicsScene(this));
    setBackgroundBrush(Lumen::canvasBg());
    setFrameShape(QFrame::NoFrame);
    setRenderHint(QPainter::Antialiasing, true);
    setRenderHint(QPainter::SmoothPixmapTransform, true);
}

void CropCanvas::setBase(const QImage& img)
{
    base_ = img.convertToFormat(QImage::Format_ARGB32);
    scene()->clear();
    baseItem_ = shadeItem_ = nullptr;
    borderItem_ = nullptr;
    v1_ = v2_ = h1_ = h2_ = nullptr;
    if (base_.isNull()) { rect_ = QRectF(); scene()->setSceneRect(0, 0, 0, 0); return; }

    scene()->setSceneRect(0, 0, base_.width(), base_.height());
    baseItem_ = scene()->addPixmap(QPixmap::fromImage(base_));

    shadeItem_ = scene()->addPixmap(QPixmap());
    shadeItem_->setZValue(1);

    borderItem_ = scene()->addRect(QRectF(), QPen(Lumen::focusRingColor(), 2), Qt::NoBrush);
    borderItem_->setZValue(2);

    QColor dashInk = Lumen::ink();
    dashInk.setAlpha(90);
    QPen dash(dashInk, 1, Qt::DashLine);
    v1_ = scene()->addLine(0, 0, 0, 0, dash); v2_ = scene()->addLine(0, 0, 0, 0, dash);
    h1_ = scene()->addLine(0, 0, 0, 0, dash); h2_ = scene()->addLine(0, 0, 0, 0, dash);
    for (QGraphicsLineItem* l : {v1_, v2_, h1_, h2_}) l->setZValue(2);

    // 初始：居中 80% 框
    const qreal w = base_.width() * 0.8, h = base_.height() * 0.8;
    setRect(QRectF((base_.width() - w) / 2, (base_.height() - h) / 2, w, h));
}

void CropCanvas::setAspect(double a)
{
    aspect_ = a;
    if (aspect_ > 0 && !rect_.isNull() && base_.width() > 0) {
        // 以当前宽为准调整高，围绕中心收缩，再夹回场景
        const qreal w = rect_.width();
        qreal h = w / aspect_;
        QPointF c = rect_.center();
        QRectF nr(c.x() - w / 2, c.y() - h / 2, w, h);
        if (nr.top() < 0) nr.moveTop(0);
        if (nr.left() < 0) nr.moveLeft(0);
        if (nr.bottom() > base_.height()) nr.moveBottom(base_.height());
        if (nr.right() > base_.width()) nr.moveRight(base_.width());
        nr.setHeight(std::min(nr.height(), base_.height() - nr.top()));
        setRect(nr);
    }
}

int CropCanvas::hitHandle(const QPointF& p) const
{
    if (rect_.isNull()) return 0;
    const QPointF corners[4] = { rect_.topLeft(), rect_.topRight(),
                                 rect_.bottomRight(), rect_.bottomLeft() };
    for (int i = 0; i < 4; ++i) {
        if (QRectF(corners[i].x() - kHandle / 2, corners[i].y() - kHandle / 2,
                   kHandle, kHandle).contains(p))
            return i + 1;   // 1 TL, 2 TR, 3 BR, 4 BL
    }
    return 0;
}

void CropCanvas::setRect(const QRectF& r)
{
    if (base_.isNull()) return;
    QRectF bounds(0, 0, base_.width(), base_.height());
    QRectF nr = r.intersected(bounds);
    if (nr.width() < kMinSize || nr.height() < kMinSize) return;  // 太小忽略
    rect_ = nr;
    syncOverlay();
    emit rectChanged();
}

void CropCanvas::syncOverlay()
{
    if (base_.isNull() || !shadeItem_) return;
    QImage shade(base_.size(), QImage::Format_ARGB32);
    QColor shadeMask = Lumen::maskColor();
    shadeMask.setAlpha(150);
    shade.fill(shadeMask);
    if (!rect_.isNull()) {
        QPainter p(&shade);
        p.setCompositionMode(QPainter::CompositionMode_Clear);
        p.fillRect(rect_, Qt::black);
        p.end();
    }
    shadeItem_->setPixmap(QPixmap::fromImage(shade));
    if (borderItem_) borderItem_->setRect(rect_);
    if (v1_ && v2_ && h1_ && h2_) {
        const qreal x1 = rect_.left() + rect_.width() / 3.0;
        const qreal x2 = rect_.left() + rect_.width() * 2.0 / 3.0;
        const qreal y1 = rect_.top() + rect_.height() / 3.0;
        const qreal y2 = rect_.top() + rect_.height() * 2.0 / 3.0;
        v1_->setLine(x1, rect_.top(), x1, rect_.bottom());
        v2_->setLine(x2, rect_.top(), x2, rect_.bottom());
        h1_->setLine(rect_.left(), y1, rect_.right(), y1);
        h2_->setLine(rect_.left(), y2, rect_.right(), y2);
    }
}

void CropCanvas::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && !base_.isNull()) {
        const QPointF p = mapToScene(e->pos());
        const int hh = hitHandle(p);
        if (hh) {
            mode_ = hh;   // ResTL..ResBL
            switch (hh) {
                case 1: resizeAnchor_ = rect_.bottomRight(); break;
                case 2: resizeAnchor_ = rect_.bottomLeft();  break;
                case 3: resizeAnchor_ = rect_.topLeft();     break;
                default: resizeAnchor_ = rect_.topRight();   break;
            }
        } else if (rect_.contains(p)) {
            mode_ = Move;
        } else {
            mode_ = New;
        }
        press_ = p;
        pressRect_ = rect_;
        e->accept();
        return;
    }
    QGraphicsView::mousePressEvent(e);
}

void CropCanvas::mouseMoveEvent(QMouseEvent* e)
{
    if (mode_ == None || base_.isNull()) { QGraphicsView::mouseMoveEvent(e); return; }
    const QPointF p = mapToScene(e->pos());
    const QRectF bounds(0, 0, base_.width(), base_.height());

    if (mode_ == New) {
        setRect(QRectF(press_, p).normalized());
    } else if (mode_ == Move) {
        QPointF delta = p - press_;
        QRectF nr = pressRect_.translated(delta);
        // 夹回场景
        if (nr.left() < 0) nr.translate(-nr.left(), 0);
        if (nr.top() < 0)  nr.translate(0, -nr.top());
        if (nr.right() > bounds.right())  nr.translate(bounds.right() - nr.right(), 0);
        if (nr.bottom() > bounds.bottom()) nr.translate(0, bounds.bottom() - nr.bottom());
        rect_ = nr;
        syncOverlay();
        emit rectChanged();
    } else {
        // 角缩放：resizeAnchor_ 固定，p 为活动角
        QRectF nr(resizeAnchor_, p);
        if (aspect_ > 0) {
            // 以宽定高，按活动角相对锚点的方向伸长
            const qreal w = nr.width();
            const qreal sy = (p.y() >= resizeAnchor_.y()) ? 1.0 : -1.0;
            nr.setHeight(w / aspect_ * sy);
        }
        nr = nr.normalized();
        if (nr.width() >= kMinSize && nr.height() >= kMinSize) setRect(nr);
    }
    e->accept();
}

void CropCanvas::mouseReleaseEvent(QMouseEvent* e)
{
    if (mode_ == New && (rect_.width() < kMinSize || rect_.height() < kMinSize)
        && !base_.isNull()) {
        const qreal w = base_.width() * 0.8, h = base_.height() * 0.8;
        setRect(QRectF((base_.width() - w) / 2, (base_.height() - h) / 2, w, h));
    }
    mode_ = None;
    QGraphicsView::mouseReleaseEvent(e);
}
