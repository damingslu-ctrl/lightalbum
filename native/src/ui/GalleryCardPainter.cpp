#include "GalleryCardPainter.h"
#include <QPainterPath>
#include <QLinearGradient>

namespace Lumen {

void paintGalleryCard(QPainter* p, const QRectF& card,
                      const QString& title, const QString& sub,
                      const QString& orient, bool miss, int rating,
                      const QPixmap& pm, bool feature, bool hov, bool sel)
{
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);

    // 圆角裁剪：图片铺满卡片（无黑边 / 无 letterbox）
    QRectF c = card;
    QPainterPath clipPath;
    clipPath.addRoundedRect(c, 12, 12);
    p->setClipPath(clipPath);

    // 卡片底（图缺失/未加载时露出；平时被图覆盖，不再露黑边）
    p->setPen(Qt::NoPen);
    p->setBrush(QColor(0x13, 0x1a, 0x15));
    p->drawRoundedRect(c, 12, 12);
    p->setBrush(Qt::NoBrush);

    // 图区：等比裁切铺满（KeepAspectRatioByExpanding → 占满卡片，无黑边/无留白）
    if (!pm.isNull()) {
        QPixmap scaled = pm.scaled(int(c.width()), int(c.height()),
                                   Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        QRect r(int(c.center().x() - scaled.width() / 2.0),
                int(c.center().y() - scaled.height() / 2.0),
                scaled.width(), scaled.height());
        p->drawPixmap(r.topLeft(), scaled);
    } else if (miss) {
        p->setPen(QColor(0x8d, 0xa3, 0x96));
        p->drawText(c, Qt::AlignCenter, QStringLiteral("图片缺失"));
    } else {
        p->setPen(QColor(0x5a, 0x6b, 0x60));
        p->drawText(c, Qt::AlignCenter, QStringLiteral("…"));
    }

    // 横竖徽章（右上角，浮于图上）
    if (orient == QLatin1String("landscape") || orient == QLatin1String("portrait")) {
        const QString b = orient == QLatin1String("landscape")
                              ? QStringLiteral("横") : QStringLiteral("竖");
        QFont f = p->font(); f.setPointSizeF(8.5); f.setBold(true); p->setFont(f);
        QRectF br = c.adjusted(c.width() - 36, 12, -12, -c.height() + 32);
        p->setPen(QColor(0xd9, 0xa9, 0x4e));
        p->setBrush(QColor(0x0c, 0x10, 0x0d, 170));
        p->drawRoundedRect(br, 7, 7);
        p->drawText(br, Qt::AlignCenter, b);
    }

    // 底部渐隐遮罩（文字浮层可读，盖在图上而非卡片底上）
    const qreal scrimH = c.height() * (feature ? 0.32 : 0.42);
    QLinearGradient g(0, c.bottom() - scrimH, 0, c.bottom());
    g.setColorAt(0.0, QColor(6, 10, 8, 0));
    g.setColorAt(0.5, QColor(6, 10, 8, 120));
    g.setColorAt(1.0, QColor(6, 10, 8, 214));
    p->setPen(Qt::NoPen);
    p->setBrush(g);
    p->drawRect(QRectF(c.left(), c.bottom() - scrimH, c.width(), scrimH));

    // 标题 + 副行（浮于图底遮罩上）
    const qreal pad = 12.0;
    p->setPen(QColor(0xf4, 0xf7, 0xf4));
    QFont f = p->font(); f.setPointSizeF(feature ? 14.0 : 10.5); f.setBold(true); p->setFont(f);
    const QRectF tr(c.adjusted(pad, c.height() - 40, -pad, -12));
    p->drawText(tr, Qt::AlignBottom | Qt::AlignLeft,
                p->fontMetrics().elidedText(title, Qt::ElideRight, int(tr.width())));
    if (!sub.isEmpty()) {
        p->setPen(QColor(0xcf, 0xd8, 0xd2));
        QFont f2 = p->font(); f2.setPointSizeF(feature ? 11.0 : 9.0); f2.setBold(false); p->setFont(f2);
        const QRectF sr(c.adjusted(pad, c.height() - 22, -pad, -10));
        p->drawText(sr, Qt::AlignBottom | Qt::AlignLeft,
                    p->fontMetrics().elidedText(sub, Qt::ElideRight, int(sr.width())));
    }

    // 评级星（浮于图底遮罩上，右下角）
    if (rating > 0) {
        const QPointF c0(c.right() - 14 - 4 * 9.0, c.bottom() - 14);
        for (int i = 0; i < 5; ++i) {
            const QPointF cc(c0.x() + i * 9.0, c0.y());
            if (i < rating) { p->setPen(Qt::NoPen); p->setBrush(QColor(0xd9, 0xa9, 0x4e)); }
            else { p->setPen(QPen(QColor(0x5a, 0x6b, 0x60), 1)); p->setBrush(Qt::NoBrush); }
            p->drawEllipse(cc, 2.6, 2.6);
        }
    }

    p->restore();   // 取消圆角裁剪

    // 选中金边 / hover 翡翠（浮于图片上，交互时才出现；默认无描边 → 无黑边）
    if (sel) {
        QPen pen(QColor(0xd9, 0xa9, 0x4e), 2.4);
        p->setPen(pen); p->setBrush(Qt::NoBrush);
        p->drawRoundedRect(c.adjusted(1, 1, -1, -1), 12, 12);
    } else if (hov) {
        QPen pen(QColor(0x4f, 0xc0, 0x8a, 190), 1.6);
        p->setPen(pen); p->setBrush(Qt::NoBrush);
        p->drawRoundedRect(c.adjusted(0.5, 0.5, -0.5, -0.5), 12, 12);
    }
}

} // namespace Lumen
