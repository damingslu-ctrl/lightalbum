#include "MaskCanvas.h"
#include "Theme.h"
#include <QMouseEvent>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QPainter>
#include <QPen>
#include <QPixmap>

MaskCanvas::MaskCanvas(QWidget* parent)
    : QGraphicsView(parent)
{
    setScene(new QGraphicsScene(this));
    setBackgroundBrush(Lumen::canvasBg());
    setFrameShape(QFrame::NoFrame);
    setRenderHint(QPainter::Antialiasing, true);
    setRenderHint(QPainter::SmoothPixmapTransform, true);
    setDragMode(QGraphicsView::NoDrag);
    setTransformationAnchor(AnchorUnderMouse);
}

void MaskCanvas::setBase(const QImage& img)
{
    base_ = img.convertToFormat(QImage::Format_ARGB32);
    mask_ = QImage(base_.size(), QImage::Format_ARGB32);
    mask_.fill(Qt::transparent);
    overlay_ = QImage(base_.size(), QImage::Format_ARGB32);
    overlay_.fill(Qt::transparent);
    painted_ = false;
    painting_ = false;

    scene()->clear();
    baseItem_ = nullptr; overlayItem_ = nullptr;
    if (base_.isNull()) { scene()->setSceneRect(0, 0, 0, 0); return; }
    scene()->setSceneRect(0, 0, base_.width(), base_.height());
    baseItem_ = scene()->addPixmap(QPixmap::fromImage(base_));
    overlayItem_ = scene()->addPixmap(QPixmap::fromImage(overlay_));
    overlayItem_->setZValue(1);
}

void MaskCanvas::clearMask()
{
    mask_.fill(Qt::transparent);
    overlay_.fill(Qt::transparent);
    painted_ = false;
    syncOverlay();
}

void MaskCanvas::drawStroke(const QPointF& from, const QPointF& to)
{
    if (mask_.isNull()) return;
    {
        QPainter pm(&mask_);
        pm.setRenderHint(QPainter::Antialiasing, true);
        QColor strokeInk = Lumen::ink();
        strokeInk.setAlpha(210);
        QPen pen(strokeInk, brush_, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        pm.setPen(pen);
        pm.drawLine(from, to);
    }
    {
        QPainter pm(&overlay_);
        pm.setRenderHint(QPainter::Antialiasing, true);
        QColor overlayMask = Lumen::maskColor();
        overlayMask.setAlpha(130);
        QPen pen(overlayMask,
                 brush_, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        pm.setPen(pen);
        pm.drawLine(from, to);
    }
    painted_ = true;
    syncOverlay();
}

void MaskCanvas::syncOverlay()
{
    if (overlayItem_)
        overlayItem_->setPixmap(QPixmap::fromImage(overlay_));
    emit maskPainted();
}

void MaskCanvas::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && !base_.isNull()) {
        painting_ = true;
        last_ = mapToScene(e->pos());
        drawStroke(last_, last_);   // RoundCap：同点画线即圆点
        e->accept();
        return;
    }
    QGraphicsView::mousePressEvent(e);
}

void MaskCanvas::mouseMoveEvent(QMouseEvent* e)
{
    if (painting_) {
        const QPointF p = mapToScene(e->pos());
        drawStroke(last_, p);
        last_ = p;
        e->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(e);
}

void MaskCanvas::mouseReleaseEvent(QMouseEvent* e)
{
    painting_ = false;
    QGraphicsView::mouseReleaseEvent(e);
}
