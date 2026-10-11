#pragma once
#include <QByteArray>
#include <QColor>
#include <QPoint>

class QWidget;
class QObject;

// ============================================================================
// MotionFX —— MotionKit 的 UI 侧唯一封装。UI 不再手配曲线/时长，只给
// 「预设名 + 属性键 + 目标」；animate=false（QA 截图门 env / 系统减动效）
// 时跳过动画直接落终值 → 像素门永远拿到确定性静置态。
// ============================================================================

namespace MotionFx {

// 单属性动画：target 的 Qt 属性 propName 由引擎曲线 from→to 推进
// （经 QVariantAnimation 自驱，曲线值来自 Motion::evalCurve，非 Qt 内建 ease）。
// 返回驱动对象（parent=target，自动回收）；animate=false 返回 nullptr 并落终值。
QObject* runProp(QWidget* target, const char* propName,
                 const QByteArray& presetName, const QByteArray& keyProp,
                 double from, double to, bool animate);

// 字号弹跳（StarBounce 预设的 fontPx 键，from=base→过冲→回 base）。
// 曲线与时长全出自引擎，UI 只提供基准字号。
// 注意：目标控件若有 stylesheet 固定 font-size，CSS 会压过 Qt font —— 那类
// 控件请用 glow（调色板走 CSS 未占用通道）。
void bounceFont(QWidget* target, double basePx, bool animate);

// 辉光回弹：text 色在 [base → peak] 间由预设曲线推进、随后回 base
// （写 palette，适配 CSS 固定字号的按钮）。presetName/keyProp 决定手感曲线。
void glow(QWidget* target, const QColor& base, const QColor& peak,
          const QByteArray& presetName, const QByteArray& keyProp, bool animate);

// 心形粒子爆开：parent 局部坐标 center 处叠瞬时粒子层（WA_TransparentForMouseEvents，
// 播完 deleteLater，绝不常驻）。n 枚粒子轨迹=Motion::heartBurstPos 纯函数。
void heartBurst(QWidget* parent, const QPoint& center, int n = 9, int radiusPx = 42);

} // namespace MotionFx
