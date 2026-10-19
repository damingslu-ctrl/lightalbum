#pragma once
// 线条风图标工厂：QPainter 矢量自绘（对应 web 版内联 SVG，零外部资源、零 emoji）。
#include <cmath>
#include <QIcon>
#include <QPixmap>
#include <QPainter>
#include <QPen>
#include <QString>

namespace Icons {

inline QIcon make(const QString& kind, const QColor& color, int size = 22)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, 1.8);
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
    } else {                                     // about：人像
        p.drawEllipse(QPointF(r.center().x(), r.top() + r.height() * 0.32), r.width() * 0.24, r.width() * 0.24);
        p.drawArc(QRectF(r.left(), r.top() + r.height() * 0.5, r.width(), r.height() * 0.9),
                  180 * 16, 180 * 16);
    }
    return QIcon(pm);
}

} // namespace Icons
