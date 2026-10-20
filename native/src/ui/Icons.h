#pragma once
// 线条风图标工厂：QPainter 矢量自绘（对应 web 版内联 SVG，零外部资源、零 emoji）。
#include <cmath>
#include <QIcon>
#include <QPixmap>
#include <QPainter>
#include <QPen>
#include <QString>

namespace Icons {

// 描边宽度体系（V4 §四）：旧控件组 1.8；2.42.0 馆长图标组 1.5 —— 更细的线，
// 才压得住 E4 开馆层那 1px 金描边（两层同时粗就成「框里有框」）。唯一真源，
// makePixmap 只按这张表取宽度（门禁 V_icons_stroke_curator_1_5 断两组不互串）。
inline qreal strokeWidth(const QString& kind)
{
    return (kind == QLatin1String("curate") || kind == QLatin1String("star")
            || kind == QLatin1String("reject") || kind == QLatin1String("swap")) ? 1.5 : 1.8;
}

inline QPixmap makePixmap(const QString& kind, const QColor& color, int size = 22)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, strokeWidth(kind));
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    const qreal m = size * 0.14;                 // 边距
    const QRectF r(m, m, size - 2 * m, size - 2 * m);

    if (kind == QLatin1String("home")) {         // 房子
        QPointF top(r.center().x(), r.top() + r.height() * 0.12);
        p.drawPolyline(QPolygonF{{r.left(), r.center().y()}, top, {r.right(), r.center().y()}});
        p.drawRect(QRectF(r.left() + r.width() * 0.14, r.center().y(),
                          r.width() * 0.72, r.height() * 0.42));
    } else if (kind == QLatin1String("map")) {   // 定位钉（足迹）
        QPointF c(r.center().x(), r.top() + r.height() * 0.36);
        p.drawEllipse(c, r.width() * 0.26, r.width() * 0.26);
        p.drawPolyline(QPolygonF{{c.x() - r.width() * 0.24, c.y() + r.width() * 0.20},
                        {r.center().x(), r.bottom()},
                        {c.x() + r.width() * 0.24, c.y() + r.width() * 0.20}});
    } else if (kind == QLatin1String("gallery")) { // 2x2 网格
        const qreal g = r.width() * 0.07, cw = (r.width() - g) / 2, ch = (r.height() - g) / 2;
        p.drawRoundedRect(QRectF(r.left(), r.top(), cw, ch), 2, 2);
        p.drawRoundedRect(QRectF(r.right() - cw, r.top(), cw, ch), 2, 2);
        p.drawRoundedRect(QRectF(r.left(), r.bottom() - ch, cw, ch), 2, 2);
        p.drawRoundedRect(QRectF(r.right() - cw, r.bottom() - ch, cw, ch), 2, 2);
    } else if (kind == QLatin1String("cull")) {  // 漏斗
        p.drawPolyline(QPolygonF{{r.left(), r.top()}, {r.right(), r.top()},
                        {r.center().x() + r.width() * 0.12, r.center().y() + r.height() * 0.1},
                        {r.center().x() + r.width() * 0.12, r.bottom()},
                        {r.center().x() - r.width() * 0.12, r.bottom() - r.height() * 0.12},
                        {r.center().x() - r.width() * 0.12, r.center().y() + r.height() * 0.1},
                        {r.left(), r.top()}});
    } else if (kind == QLatin1String("retouch")) { // 三条调节滑杆
        for (int i = 0; i < 3; ++i) {
            const qreal y = r.top() + r.height() * (0.2 + 0.3 * i);
            p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
            const qreal kx = r.left() + r.width() * (i == 1 ? 0.32 : 0.68);
            p.setBrush(color);
            p.drawEllipse(QPointF(kx, y), size * 0.055, size * 0.055);
            p.setBrush(Qt::NoBrush);
        }
    } else if (kind == QLatin1String("enhance")) { // 四角星光
        QPointF c(r.center());
        const qreal R = r.width() * 0.46, ri = r.width() * 0.10;
        QPolygonF star;
        for (int i = 0; i < 8; ++i) {
            const qreal ang = -M_PI_2 + i * M_PI_4;
            const qreal rad = (i % 2 == 0) ? R : ri;
            star << QPointF(c.x() + rad * std::cos(ang), c.y() + rad * std::sin(ang));
        }
        p.drawPolyline(star);
        p.drawLine(QPointF(c.x() + R * 1.25, c.y() - R * 0.75),
                   QPointF(c.x() + R * 1.45, c.y() - R * 0.95));
    } else if (kind == QLatin1String("log")) {   // 柱状图
        p.drawLine(QPointF(r.left(), r.bottom()), QPointF(r.right(), r.bottom()));
        const qreal bw = r.width() * 0.16;
        for (int i = 0; i < 3; ++i) {
            const qreal x = r.left() + r.width() * (0.12 + 0.3 * i);
            const qreal h = r.height() * (i == 1 ? 0.78 : (i == 0 ? 0.45 : 0.6));
            p.drawLine(QPointF(x + bw / 2, r.bottom()), QPointF(x + bw / 2, r.bottom() - h));
        }
    } else if (kind == QLatin1String("lobby")) {   // 2.41.0 前厅：拱门
        p.drawArc(QRectF(r.left(), r.top() + r.height() * 0.3,
                         r.width(), r.height() * 0.9), 0, 180 * 16);
        p.drawLine(QPointF(r.center().x(), r.center().y()), QPointF(r.center().x(), r.bottom()));
    } else if (kind == QLatin1String("people")) {  // 2.41.0 人物馆：双人像
        p.drawEllipse(QPointF(r.left() + r.width() * 0.32, r.top() + r.height() * 0.3),
                      r.width() * 0.16, r.width() * 0.16);
        p.drawArc(QRectF(r.left(), r.top() + r.height() * 0.45, r.width() * 0.62, r.height() * 0.7),
                  0, 180 * 16);
        p.drawEllipse(QPointF(r.right() - r.width() * 0.28, r.top() + r.height() * 0.34),
                      r.width() * 0.13, r.width() * 0.13);
        p.drawArc(QRectF(r.right() - r.width() * 0.56, r.top() + r.height() * 0.52,
                         r.width() * 0.56, r.height() * 0.6), 0, 180 * 16);
    } else if (kind == QLatin1String("lamp")) {    // 2.41.0 切灯钮：台灯
        p.drawArc(QRectF(r.center().x() - r.width() * 0.32, r.top(),
                         r.width() * 0.64, r.height() * 0.6), 0, 180 * 16);
        p.drawLine(QPointF(r.left() + r.width() * 0.12, r.top() + r.height() * 0.3),
                   QPointF(r.right() - r.width() * 0.12, r.top() + r.height() * 0.3));
        p.drawLine(QPointF(r.center().x(), r.top() + r.height() * 0.3),
                   QPointF(r.center().x(), r.bottom() - r.height() * 0.18));
        p.drawLine(QPointF(r.center().x() - r.width() * 0.18, r.bottom() - r.height() * 0.18),
                   QPointF(r.center().x() + r.width() * 0.18, r.bottom() - r.height() * 0.18));
    } else if (kind == QLatin1String("curate")) {   // 2.42.0 馆长：展台上的装裱画（策展入口）
        const qreal fw = r.width() * 0.60, fh = r.height() * 0.56;
        const QRectF frame(r.center().x() - fw / 2, r.top(), fw, fh);
        p.drawRoundedRect(frame, 2, 2);
        p.drawPolyline(QPolygonF{{frame.left() + fw * 0.14, frame.bottom() - fh * 0.18},
                                 {frame.left() + fw * 0.40, frame.top() + fh * 0.42},
                                 {frame.left() + fw * 0.62, frame.bottom() - fh * 0.30},
                                 {frame.right() - fw * 0.12, frame.top() + fh * 0.26}});
        p.drawLine(QPointF(r.left(), r.bottom()), QPointF(r.right(), r.bottom()));
        p.drawLine(QPointF(frame.left() + fw * 0.10, r.bottom()),
                   QPointF(frame.center().x() - fw * 0.14, frame.bottom()));
        p.drawLine(QPointF(frame.right() - fw * 0.10, r.bottom()),
                   QPointF(frame.center().x() + fw * 0.14, frame.bottom()));
    } else if (kind == QLatin1String("star")) {      // 馆长组：收藏此展（闭合五角星轮廓）
        QPointF c(r.center());
        const qreal R = qMin(r.width(), r.height()) * 0.5, ri = R * 0.42;
        QPolygonF st;
        for (int i = 0; i < 10; ++i) {
            const qreal ang = -M_PI_2 + i * (M_PI / 5.0);   // 五角星 10 顶点，相邻 36°
            const qreal rad = (i % 2 == 0) ? R : ri;
            st << QPointF(c.x() + rad * std::cos(ang), c.y() + rad * std::sin(ang));
        }
        st << st.first();
        p.drawPolyline(st);
    } else if (kind == QLatin1String("reject")) {    // 馆长组：别再这类 / 退藏（圆内斜杠）
        const qreal rad = qMin(r.width(), r.height()) * 0.46;
        const qreal k = rad * 0.7071;
        p.drawEllipse(r.center(), rad, rad);
        p.drawLine(QPointF(r.center().x() - k, r.center().y() - k),
                   QPointF(r.center().x() + k, r.center().y() + k));
    } else if (kind == QLatin1String("swap")) {      // 馆长组：换一展（上下反向双箭头）
        const qreal up = r.center().y() - r.height() * 0.22;
        const qreal dn = r.center().y() + r.height() * 0.22;
        const qreal hs = r.height() * 0.20, hl = r.width() * 0.20;
        p.drawLine(QPointF(r.left(), up), QPointF(r.right(), up));
        p.drawPolyline(QPolygonF{{r.right() - hl, up - hs}, {r.right(), up}, {r.right() - hl, up + hs}});
        p.drawLine(QPointF(r.left(), dn), QPointF(r.right(), dn));
        p.drawPolyline(QPolygonF{{r.left() + hl, dn - hs}, {r.left(), dn}, {r.left() + hl, dn + hs}});
    } else {                                     // about：人像
        p.drawEllipse(QPointF(r.center().x(), r.top() + r.height() * 0.32), r.width() * 0.24, r.width() * 0.24);
        p.drawArc(QRectF(r.left(), r.top() + r.height() * 0.5, r.width(), r.height() * 0.9),
                  180 * 16, 180 * 16);
    }
    return pm;
}

// 金色变体纪律（V4 §四）：**仅**馆长图标组（curate/star/reject/swap）允许以
// Lumen::gold() 着色；其余图标跟控件墨色，金只在「开馆」语义处出现。
inline QIcon make(const QString& kind, const QColor& color, int size = 22)
{
    return QIcon(makePixmap(kind, color, size));
}

} // namespace Icons
