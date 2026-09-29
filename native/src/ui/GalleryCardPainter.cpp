#include "GalleryCardPainter.h"

namespace Lumen {

void paintGalleryCard(QPainter* p, const QRectF& card,
                      const QString& title, const QString& sub,
                      const QString& orient, bool miss, int rating,
                      const QPixmap& pm, bool feature, bool hov, bool sel)
{
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);

    // 卡片底（P12：圆角 + hover 翡翠描边 + 选中金边）
    QRectF c = card;
    p->setPen(Qt::NoPen);
    p->setBrush(QColor(0x13, 0x1a, 0x15));
    p->drawRoundedRect(c, 12, 12);
    p->setBrush(Qt::NoBrush);
    if (sel) {
        QPen pen(QColor(0xd9, 0xa9, 0x4e), 2.2);
        p->setPen(pen);
        p->drawRoundedRect(c.adjusted(1, 1, -1, -1), 12, 12);
    } else if (hov) {
        QPen pen(QColor(0x4f, 0xc0, 0x8a, 170), 1.4);
        p->setPen(pen);
        p->drawRoundedRect(c.adjusted(0.5, 0.5, -0.5, -0.5), 12, 12);
    } else {
        QPen pen(QColor(0x26, 0x31, 0x2a), 1);
        p->setPen(pen);
        p->drawRoundedRect(c.adjusted(0.5, 0.5, -0.5, -0.5), 12, 12);
    }

    // 图区：等比居中；缺失/未加载显示占位
    QRectF img = c.adjusted(10, 10, -10, -46);
    if (!pm.isNull()) {
        QPixmap scaled = pm.scaled(int(img.width()), int(img.height()),
                                   Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QRectF r(QPointF(img.center().x() - scaled.width() / 2.0,
                         img.center().y() - scaled.height() / 2.0),
                 scaled.size());
        p->drawPixmap(r.toRect(), scaled);
    } else if (miss) {
        p->setPen(QColor(0x8d, 0xa3, 0x96));
        p->drawText(img, Qt::AlignCenter, QStringLiteral("图片缺失"));
    } else {
        p->setPen(QColor(0x5a, 0x6b, 0x60));
        p->drawText(img, Qt::AlignCenter, QStringLiteral("…"));
    }

    // 横竖徽章（右上角小胶囊，金描边）
    if (orient == QLatin1String("landscape") || orient == QLatin1String("portrait")) {
        const QString b = orient == QLatin1String("landscape")
                              ? QStringLiteral("横") : QStringLiteral("竖");
        QFont f = p->font(); f.setPointSizeF(8.5); f.setBold(true); p->setFont(f);
        QRectF br = c.adjusted(c.width() - 38, 14, -10, -c.height() + 34);
        p->setPen(QColor(0xd9, 0xa9, 0x4e));
        p->setBrush(QColor(0x0c, 0x10, 0x0d));
        p->drawRoundedRect(br, 7, 7);
        p->drawText(br, Qt::AlignCenter, b);
    }

    // 评级星（数据诚实：rating=0 不画）
    if (rating > 0) {
        const QPointF c0(c.right() - 14 - 4 * 9.0, c.bottom() - 44);
        for (int i = 0; i < 5; ++i) {
            const QPointF cc(c0.x() + i * 9.0, c0.y());
            if (i < rating) { p->setPen(Qt::NoPen); p->setBrush(QColor(0xd9, 0xa9, 0x4e)); }
            else { p->setPen(QPen(QColor(0x5a, 0x6b, 0x60), 1)); p->setBrush(Qt::NoBrush); }
            p->drawEllipse(cc, 2.6, 2.6);
        }
    }

    // 标题 + 副行（feature 放大字号）
    // 注意 QRectF::adjusted 的 dy 是「加在原边坐标上」：标题带 = 底边上方 39~22px。
    // （修复 P12 时代遗留 bug：原 dy2 误写 c.height()-22 → 矩形底边落到卡片外，标题从未渲染）
    p->setPen(QColor(0xf4, 0xf7, 0xf4));
    QFont f = p->font(); f.setPointSizeF(feature ? 13.0 : 9.5); f.setBold(false); p->setFont(f);
    const QRectF tr(c.adjusted(10, c.height() - 40, -10, -22));
    p->drawText(tr, Qt::AlignVCenter | Qt::AlignLeft,
                p->fontMetrics().elidedText(title, Qt::ElideRight, int(tr.width())));
    if (!sub.isEmpty()) {
        p->setPen(QColor(0x8d, 0xa3, 0x96));
        QFont f2 = p->font(); f2.setPointSizeF(feature ? 11.0 : 8.5); p->setFont(f2);
        const QRectF sr(c.adjusted(10, c.height() - 22, -10, -8));
        p->drawText(sr, Qt::AlignVCenter | Qt::AlignLeft,
                    p->fontMetrics().elidedText(sub, Qt::ElideRight, int(sr.width())));
    }
    p->restore();
}

} // namespace Lumen
