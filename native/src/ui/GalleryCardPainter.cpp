#include "GalleryCardPainter.h"
#include <QPainterPath>
#include <QLinearGradient>
#include <QtMath>

namespace Lumen {

// 2.22.0 骨架屏（纯函数 + 绘制分离；铁律 20：产品与门禁共用同一实现）
double skeletonPulse(double phase, bool reduceMotion)
{
    // 减动效：直接呈终态，不做脉冲（避免闪烁困扰敏感用户）。
    if (reduceMotion) return 1.0;
    // 相位归一化到 [0,1)，三角波 → 亮度 0..1 平滑往复（比 sin 更"机械"，
    // 更像"正在加载"的指示而非"在呼吸"的装饰）。
    double ph = phase - std::floor(phase);       // 负相位也要归一化
    const double tri = (ph < 0.5) ? (ph * 2.0) : (2.0 - ph * 2.0);   // 0→1→0
    // 抬到 [0.25,1] 区间：底色永远看得见（不会闪成纯卡片底色），峰值到 1。
    return 0.25 + 0.75 * tri;
}

SkeletonState skeletonState(const QPixmap& pm, bool miss)
{
    if (!pm.isNull()) return SkeletonState::Ready;
    return miss ? SkeletonState::Missing : SkeletonState::Loading;
}

void paintSkeleton(QPainter* p, const QRectF& card, double phase, bool reduceMotion)
{
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);

    // 圆角裁剪与卡片底一致（否则脉冲会溢出圆角）
    QPainterPath clipPath;
    clipPath.addRoundedRect(card, 12, 12);
    p->setClipPath(clipPath);

    // ① 脉冲底色：基色随相位在 #141a15 ↔ #1d2720 之间往复（暗绿调，贴合主题）
    const double v = skeletonPulse(phase, reduceMotion);
    const int r = int(0x14 + (0x1d - 0x14) * v);
    const int g = int(0x1a + (0x27 - 0x1a) * v);
    const int b = int(0x15 + (0x20 - 0x15) * v);
    p->setPen(Qt::NoPen);
    p->setBrush(QColor(r, g, b));
    p->drawRoundedRect(card, 12, 12);

    // ② 微光带：45° 斜向扫过时的高光，位置随相位移动（"正在加载"的方向感）
    if (!reduceMotion) {
        const double sweep = phase - std::floor(phase);        // 0..1
        const qreal bandW = card.width() * 0.42;
        const qreal x0 = card.left() - bandW + (card.width() + bandW) * sweep;
        QLinearGradient g(x0, card.top(), x0 + bandW, card.bottom());
        g.setColorAt(0.0, QColor(255, 255, 255, 0));
        g.setColorAt(0.5, QColor(190, 220, 200, 26));           // 极淡，别喧宾夺主
        g.setColorAt(1.0, QColor(255, 255, 255, 0));
        p->setBrush(g);
        p->drawRect(card);
    }

    // ③ 中心极小的"加载中"提示（不做满卡文字，保持干净）
    QFont f = p->font();
    f.setPointSizeF(9.0);
    p->setFont(f);
    p->setPen(QColor(0x6f, 0x83, 0x76));
    p->drawText(card, Qt::AlignCenter, QStringLiteral("载入中…"));

    p->restore();
}

void paintVideoBadge(QPainter* p, const QRectF& rect, double durSec)
{
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    QFont vf = p->font(); vf.setPointSizeF(8.5); vf.setBold(true); p->setFont(vf);
    // 时长文案：<60s 显示秒；否则 分:秒。未知(<=0) 则只画三角不画时长（诚实空值）。
    QString durTxt;
    if (durSec > 0) {
        const int total = int(durSec + 0.5);
        durTxt = (total < 60) ? QStringLiteral("%1s").arg(total)
                              : QStringLiteral("%1:%2").arg(total / 60).arg(total % 60, 2, 10, QLatin1Char('0'));
    }
    const qreal bw = durTxt.isEmpty() ? 22.0 : 22.0 + p->fontMetrics().horizontalAdvance(durTxt) + 8.0;
    const QRectF vb(rect.left() + 12, rect.top() + 12, bw, 20);
    p->setPen(QColor(0xf4, 0xf7, 0xf4));
    p->setBrush(QColor(0x0c, 0x10, 0x0d, 190));
    p->drawRoundedRect(vb, 7, 7);
    // 播放三角（几何画法，不依赖字体字形 → 跨字体稳定）
    QPolygonF tri;
    const qreal ty = vb.center().y();
    const qreal tx = vb.left() + 8.0;
    tri << QPointF(tx - 3.0, ty - 5.0) << QPointF(tx - 3.0, ty + 5.0) << QPointF(tx + 4.5, ty);
    p->setPen(Qt::NoPen);
    p->setBrush(QColor(0xf4, 0xf7, 0xf4));
    p->drawPolygon(tri);
    if (!durTxt.isEmpty()) {
        p->setPen(QColor(0xf4, 0xf7, 0xf4));
        p->drawText(QRectF(vb.left() + 16, vb.top(), vb.width() - 20, vb.height()),
                    Qt::AlignVCenter | Qt::AlignLeft, durTxt);
    }
    p->restore();
}

void paintGalleryCard(QPainter* p, const QRectF& card,
                      const QString& title, const QString& sub,
                      const QString& orient, bool miss, int rating,
                      const QPixmap& pm, bool feature, bool hov, bool sel,
                      double skelPhase, bool reduceMotion,
                      bool isVideo, double durSec)
{
    // 2.22.0：骨架屏优先——没图且未标 missing = 正在加载。
    // 画完骨架即返回（不画图片/横竖徽章/标题：骨架期间无内容可写，
    // 留白比写"载入中"更干净；微光带已在 paintSkeleton 内）。
    // ★注意 save/restore 必须配对：此分支**不要**补 restore()——
    //   本函数开头那次 save() 尚未执行，多一次 restore 会使 QPainter 状态栈
    //   失衡，实测导致退出期 0xC0000005（LA_MAP_CLICK_QA 间歇崩溃）。
    //   paintSkeleton 内部自带 save/restore 配对，自洽即可。
    if (skeletonState(pm, miss) == SkeletonState::Loading) {
        paintSkeleton(p, card, skelPhase, reduceMotion);
        return;
    }

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

    // 2.28.0 / 2.31.0 视频徽标：统一走 Lumen::paintVideoBadge，保证图库页与选片页视觉一致。
    if (isVideo)
        paintVideoBadge(p, c, durSec);

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
