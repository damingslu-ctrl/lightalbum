#pragma once
#include "MotionKit.h"
#include <QObject>
#include <QByteArray>

class QVariantAnimation;

namespace Motion {

// 唯一 Qt 驱动器：按预设名推进时间，逐属性发数值信号。
// reduceMotion=true（或 qaSetForceTerminal）→ 起步即发终帧、不挂计时器（不空转）。
class Driver : public QObject {
    Q_OBJECT
public:
    explicit Driver(const QByteArray& presetName, bool reduceMotion,
                    QObject* parent = nullptr);

    // 目标帧数（>1 时驱动分段回调；默认一次整段 from→to）。
    void start();

    const Preset& preset() const;
    bool finished() const;

signals:
    void valueFor(const QByteArray& prop, double value);
    void finishedSignal();

private:
    QVariantAnimation* anim_ = nullptr;
    QByteArray presetName_;
    bool reduceMotion_ = false;
    bool done_ = false;
};

} // namespace Motion
