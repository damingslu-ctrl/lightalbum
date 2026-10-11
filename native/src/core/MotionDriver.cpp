#include "MotionDriver.h"
#include <QVariantAnimation>

namespace Motion {

Driver::Driver(const QByteArray& presetName, bool reduceMotion, QObject* parent)
    : QObject(parent), presetName_(presetName), reduceMotion_(reduceMotion)
{
}

const Preset& Driver::preset() const { return Motion::preset(presetName_); }
bool Driver::finished() const { return done_; }

void Driver::start()
{
    const Preset& p = Motion::preset(presetName_);
    if (reduceMotion_ || qaForceTerminal()) {
        // 减动效单点兜底：直接发终帧，不创建计时器、不空转（引擎一处收口）。
        const QVector<double> v = evaluate(p, 1.0);
        for (int i = 0; i < p.keys.size(); ++i)
            emit valueFor(p.keys[i].prop, v[i]);
        done_ = true;
        emit finishedSignal();
        return;
    }
    anim_ = new QVariantAnimation(this);
    anim_->setDuration(p.durationMs);
    anim_->setStartValue(0.0);
    anim_->setEndValue(1.0);
    connect(anim_, &QVariantAnimation::valueChanged, this, [this, p](const QVariant& v) {
        const QVector<double> vals = evaluate(p, v.toDouble());
        for (int i = 0; i < p.keys.size(); ++i)
            emit valueFor(p.keys[i].prop, vals[i]);
    });
    connect(anim_, &QVariantAnimation::finished, this, [this] {
        done_ = true;
        emit finishedSignal();
    });
    anim_->start();
}

} // namespace Motion
