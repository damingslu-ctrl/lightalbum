#include "MotionFX.h"
#include "MotionKit.h"
#include "MotionDriver.h"

#include <QColor>
#include <QElapsedTimer>
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>
#include <QTimer>
#include <QVariantAnimation>
#include <QWidget>

namespace MotionFx {

namespace {

void applyFontPx(QWidget* target, double px)
{
    if (!target || px <= 0.0) return;
    QFont f = target->font();
    f.setPixelSize(qMax(1, int(px + 0.5)));
    target->setFont(f);
}

// 心形粒子瞬时层：paintEvent 只做「按纯函数摆点」，不含任何独立数学。
class BurstOverlay : public QWidget {
public:
    BurstOverlay(QWidget* parent, const QPoint& center, int n, int radiusPx)
        : QWidget(parent), center_(center), n_(n), radius_(radiusPx)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_DeleteOnClose);
        raise();
        const Motion::Preset& p = Motion::preset(Motion::kHeartBurst);
        clock_.setInterval(16);
        QObject::connect(&clock_, &QTimer::timeout, this, [this, dur = p.durationMs] {
            const double f = double(elapsed_.elapsed()) / dur;   // 允许 >1（延中段无碍）
            update();
            if (f >= 1.0) { clock_.stop(); close(); }   // close→deleteLater 自毁
        });
    }
    void begin() { elapsed_.start(); clock_.start(); update(); }

protected:
    void paintEvent(QPaintEvent*) override
    {
        const Motion::Preset& p = Motion::preset(Motion::kHeartBurst);
        const double f = qMin(1.6, double(elapsed_.elapsed()) / p.durationMs);
        QPainter g(this);
        g.setRenderHint(QPainter::Antialiasing);
        const double a = Motion::heartBurstAlpha(n_, f);
        if (a <= 0.0) return;
        for (int i = 0; i < n_; ++i) {
            const QPointF off = Motion::heartBurstPos(i, n_, f, radius_);
            const QPointF c = center_ + off;
            const double sz = 7.0 * (1.0 - 0.55 * f);
            QPainterPath heart;   // 参数心形线，尺寸随进度收缩
            for (int s = 0; s <= 24; ++s) {
                const double t = 2.0 * M_PI * s / 24.0;
                const double x = 16 * std::pow(std::sin(t), 3);
                const double y = 13 * std::cos(t) - 5 * std::cos(2 * t)
                                 - 2 * std::cos(3 * t) - std::cos(4 * t);
                const QPointF pt(c.x() + x * sz / 16.0, c.y() - y * sz / 16.0);
                if (s == 0) heart.moveTo(pt); else heart.lineTo(pt);
            }
            heart.closeSubpath();
            g.fillPath(heart, QColor(0x4f, 0xc0, 0x8a, int(255 * a)));
        }
    }

private:
    QPoint center_;
    int n_, radius_;
    QElapsedTimer elapsed_;
    QTimer clock_;
};

} // namespace

QObject* runProp(QWidget* target, const char* propName,
                 const QByteArray& presetName, const QByteArray& keyProp,
                 double from, double to, bool animate)
{
    if (!target) return nullptr;
    const Motion::Preset& p = Motion::preset(presetName);
    int ix = -1;
    for (int i = 0; i < p.keys.size(); ++i)
        if (p.keys[i].prop == keyProp) { ix = i; break; }
    const double kFrom = from, kTo = to;
    const auto assign = [target, propName](double v) {
        target->setProperty(propName, v);
    };
    if (!animate || Motion::qaForceTerminal()) {
        assign(kTo);                                   // 静置终值：像素门确定性
        return nullptr;
    }
    auto* va = new QVariantAnimation(target);
    va->setDuration(p.durationMs);
    va->setStartValue(0.0);
    va->setEndValue(1.0);
    QObject::connect(va, &QVariantAnimation::valueChanged, target,
                     [p, ix, kFrom, kTo, assign](const QVariant& v) {
        double cv = (ix >= 0)
            ? Motion::evalCurve(p.keys[ix].curve, v.toDouble())
            : v.toDouble();
        if (cv > 1.0) cv = 1.0;                        // 本入口走主段，无回归延中
        assign(kFrom + (kTo - kFrom) * cv);
    });
    QObject::connect(va, &QVariantAnimation::finished, target, [assign, kTo] { assign(kTo); });
    va->start();
    return va;
}

void bounceFont(QWidget* target, double basePx, bool animate)
{
    if (!target) return;
    const Motion::Preset& p = Motion::preset(Motion::kStarBounce);
    int ix = -1;
    for (int i = 0; i < p.keys.size(); ++i)
        if (p.keys[i].prop == Motion::kPropFontPx) { ix = i; break; }
    const double terminal = (ix >= 0 && p.keys[ix].has_return)
        ? p.keys[ix].return_to : basePx;
    if (!animate || Motion::qaForceTerminal()) { applyFontPx(target, terminal); return; }

    // 整段 [0,2]：主段 from→峰值（曲线过冲感由 OutElastic 尾振给），延中落回 base。
    applyFontPx(target, p.keys.value(ix).from);
    auto* va = new QVariantAnimation(target);
    va->setDuration(p.durationMs * 2);                 // 主段+延中段等长
    va->setStartValue(0.0);
    va->setEndValue(2.0);
    QObject::connect(va, &QVariantAnimation::valueChanged, target,
                     [target, p, ix, terminal](const QVariant& v) {
        const QVector<double> vals = Motion::evaluate(p, v.toDouble());
        applyFontPx(target, vals.value(ix, terminal));
    });
    QObject::connect(va, &QVariantAnimation::finished, target, [target, terminal] {
        applyFontPx(target, terminal);
    });
    va->start();
}

void glow(QWidget* target, const QColor& base, const QColor& peak,
          const QByteArray& presetName, const QByteArray& keyProp, bool animate)
{
    if (!target) return;
    const Motion::Preset& p = Motion::preset(presetName);
    int ix = -1;
    for (int i = 0; i < p.keys.size(); ++i)
        if (p.keys[i].prop == keyProp) { ix = i; break; }
    const auto mix = [target, base, peak](double f) {
        if (f < 0.0) f = 0.0;
        if (f > 1.0) f = 1.0;                       // 颜色通道钳位（过冲感由曲线时间轴给）
        QPalette pal = target->palette();
        pal.setColor(QPalette::ButtonText,
                     QColor(int(base.red()   + (peak.red()   - base.red())   * f),
                            int(base.green() + (peak.green() - base.green()) * f),
                            int(base.blue()  + (peak.blue()  - base.blue())  * f)));
        target->setPalette(pal);
    };
    if (!animate || Motion::qaForceTerminal()) { mix(0.0); return; }   // 终值=base 色
    auto* va = new QVariantAnimation(target);
    va->setDuration(p.durationMs * 2);             // 主段 + 等长回归段（glow 键有 return）
    va->setStartValue(0.0);
    va->setEndValue(2.0);
    QObject::connect(va, &QVariantAnimation::valueChanged, target,
                     [p, ix, mix](const QVariant& v) {
        const QVector<double> vals = Motion::evaluate(p, v.toDouble());
        mix(ix >= 0 ? vals.value(ix, 0.0) : v.toDouble());
    });
    QObject::connect(va, &QVariantAnimation::finished, target, [mix] { mix(0.0); });
    va->start();
}

void heartBurst(QWidget* parent, const QPoint& center, int n, int radiusPx)
{
    if (!parent || n <= 0) return;
    if (Motion::qaForceTerminal()) return;   // 门禁截图态：不放瞬时层
    auto* ov = new BurstOverlay(parent, center, n, radiusPx);
    ov->setGeometry(parent->rect());
    ov->begin();
    ov->show();
}

} // namespace MotionFx
