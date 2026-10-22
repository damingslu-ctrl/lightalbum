#include "MotionKit.h"
#include <QtMath>
#include <cmath>

namespace Motion {

// 反例钩子：门禁注入「减动效接线断链」形态（全曲线恒终值）。
// 生产默认 false；唯一读取点在 evalCurve。
static bool g_qaForceTerminal = false;
void qaSetForceTerminal(bool on) { g_qaForceTerminal = on; }
bool qaForceTerminal() { return g_qaForceTerminal; }

double evalCurve(Curve c, double t)
{
    if (g_qaForceTerminal) return 1.0;   // 终值语义统一为 1（减动效接线断链注入点）
    if (c == Pulse) {
        // 承接 2.22 骨架契约原式：相位归一 → 三角波 → [0.25,1]，f(0)==f(1)==0.25。
        const double ph = t - std::floor(t);
        const double tri = (ph < 0.5) ? (ph * 2.0) : (2.0 - ph * 2.0);
        return 0.25 + 0.75 * tri;
    }
    if (t <= 0.0) return 0.0;
    if (t >= 1.0) return 1.0;
    switch (c) {
    case Linear:     return t;
    case InQuad:     return t * t;
    case OutQuad:    return t * (2.0 - t);
    case InOutCubic: return t < 0.5 ? 4.0 * t * t * t
                                    : 1.0 - std::pow(-2.0 * t + 2.0, 3) / 2.0;
    case OutCubic: { const double u = 1.0 - t; return 1.0 - u * u * u; }
    case OutExpo:    return 1.0 - std::pow(2.0, -10.0 * t);
    case OutBack: {  // 过冲 ~+10% 后回弹收住
        const double c1 = 1.70158, c3 = c1 + 1.0;
        const double u = t - 1.0;
        return 1.0 + c3 * u * u * u + c1 * u * u;
    }
    case OutElastic: {  // 阻尼振荡，f(1)=1 精确
        const double c4 = 2.0 * M_PI / 3.0;
        return std::pow(2.0, -10.0 * t) * std::sin((t * 10.0 - 0.75) * c4) + 1.0;
    }
    case Spring: {      // 欠阻尼弹簧：轻微过冲一次即收敛，f(1)≈1-ε（UI 端以 to 值收尾）
        const double w = 2.2 * M_PI;
        return 1.0 - std::exp(-3.6 * t) * std::cos(w * t);
    }
    case Pulse: {       // 骨架脉冲：cos 起于峰值 1、周期回 f(0)==f(1)（2.22 契约）
        const double ph = t - std::floor(t);
        return 0.625 + 0.375 * std::cos(2.0 * M_PI * ph);
    }
    default: break;
    }
    return t;
}

double staggerStart(int i, int total, double step)
{
    if (total <= 1 || i <= 0) return 0.0;
    const int last = total - 1;
    const double s = i * step / last;          // 线性错峰 → 天然单调不减
    return s < 0.0 ? 0.0 : (s > 1.0 ? 1.0 : s);
}

QVector<double> evaluate(const Preset& p, double f)
{
    QVector<double> out;
    out.reserve(p.keys.size());
    const bool forceTerm = g_qaForceTerminal;
    // f 允许 >1：主段=[0,1]，等长延中段=[1,2]（has_return 键从 to 落回 return_to）。
    const double g = f <= 0.0 ? 0.0 : (f >= 2.0 ? 2.0 : f);
    for (const Key& k : p.keys) {
        if (forceTerm) {
            out.append(k.has_return ? k.return_to : k.to);   // 终值直给（减动效兜底）
            continue;
        }
        double local;
        if (k.curve == Pulse) {
            local = g;                     // 脉冲曲线自带周期归一
        } else if (k.t1 <= k.t0) {
            local = 1.0;                   // 畸形窗口防御：直接终值
        } else {
            local = (g - k.t0) / (k.t1 - k.t0);
            if (local < 0.0) local = 0.0;
        }
        double v;
        if (local > 1.0 && k.has_return) {
            const double rt = local - 1.0;                   // 延中段 ∈(0,1]
            const double back = evalCurve(k.curve, rt > 1.0 ? 1.0 : rt);
            v = k.to + (k.return_to - k.to) * back;
        } else {
            if (local > 1.0) local = 1.0;
            v = k.from + (k.to - k.from) * evalCurve(k.curve, local);
            if (k.curve == Spring && local >= 1.0) v = k.to;  // 弹簧末帧钉死终值
        }
        if (k.clamp01) {
            if (v < 0.0) v = 0.0;
            if (v > 1.0) v = 1.0;
        }
        out.append(v);
    }
    return out;
}

QPointF heartBurstPos(int idx, int n, double f, qreal radiusPx)
{
    if (n <= 0) return QPointF(0, 0);
    const double ang = (2.0 * M_PI * idx) / n + idx * 2.39996323;   // 均匀 + 黄金角微散
    const double r = evalCurve(OutExpo, f) * radiusPx;
    const double grav = f * f * radiusPx * 0.35;                    // 轻下坠感
    return QPointF(r * std::cos(ang), r * std::sin(ang) - grav);
}

double heartBurstAlpha(int n, double f)
{
    if (n <= 0) return 0.0;
    return 1.0 - evalCurve(InQuad, f <= 0.0 ? 0.0 : (f >= 1.0 ? 1.0 : f));
}

// ---- 预设表：唯一真源，UI 只按名取用 ----
static QVector<Preset> makeTable()
{
    QVector<Preset> t;

    // NavFade：0-35% 淡出（InQuad），30-100% 淡入+上滑 14px（Spring）——窗内子时序。
    {
        Preset p; p.name = kNavFade; p.durationMs = 320;
        p.keys = {
            { kPropOpacity, 1.0, 0.0, InQuad,     0.0, 0.35, true },
            { kPropOpacity, 0.0, 1.0, OutCubic,   0.3, 1.00, true },
            { kPropOffsetY, 14.0, 0.0, Spring,    0.3, 1.00, false },
        };
        t.append(p);
    }
    // LightboxSlide：整段横滑位移 + 微缩放（OutExpo 急起步长尾收）。
    {
        Preset p; p.name = kLightbox; p.durationMs = 380;
        p.keys = {
            { kPropOffsetX, 1.0, 0.0, OutExpo, 0.0, 1.0, false },
            { kPropScale,   0.97, 1.0, OutExpo, 0.0, 1.0, false },
        };
        t.append(p);
    }
    // ChipPop：缩放 0.82→1 过冲回弹 + 淡入 + 字号 12→13.4→12 弹一下归位。
    {
        Preset p; p.name = kChipPop; p.durationMs = 240;
        p.keys = {
            { kPropScale,   0.82, 1.0, OutBack,    0.0, 1.0, false, false, 0.0 },
            { kPropOpacity, 0.0,  1.0, OutQuad,    0.0, 0.6, true,  false, 0.0 },
            { kPropFontPx,  12.0, 13.4, OutBack,   0.0, 1.0, false, true,  12.0 },
            { kPropGlow,     0.0,  1.0, OutBack,   0.0, 1.0, false, true,   0.0 },
        };
        t.append(p);
    }
    // D3 例外白名单（docs/design_studio/2026-10-14/D3_motion_spec.html 原则/禁则两处）：
    // kStarBounce / kHeartBurst 属庆祝动作，允许 overshoot ≤8%，必须吃 reduced-motion。
    // StarBounce：字号 12→过冲 15→回 12（OutElastic 尾振），弹性手感全由曲线给；
    // glow 键供 CSS 固定字号的按钮走「调色板辉光」通道（同曲线同时序）。
    {
        Preset p; p.name = kStarBounce; p.durationMs = 420;
        p.keys = {
            { kPropFontPx, 12.0, 15.0, OutElastic, 0.0, 1.0, false, true, 12.0 },
            { kPropGlow,    0.0,  1.0, OutElastic, 0.0, 1.0, false, true,  0.0 },
        };
        t.append(p);
    }
    // HeartBurst：粒子几何走 heartBurstPos 纯函数；此处关键帧给整体包络。
    {
        Preset p; p.name = kHeartBurst; p.durationMs = 520;
        p.keys = {
            { kPropOpacity, 1.0, 1.0, Linear, 0.0, 1.0, true, false, 0.0 },
        };
        t.append(p);
    }
    // LightFirst slow 640：光先行（glow 先到峰再退），画后进。
    {
        Preset p; p.name = kLightFirst; p.durationMs = 640;
        p.keys = {
            { kPropGlow,    0.0, 1.0, OutCubic, 0.0, 0.35, false, true, 0.0 },  // 光先行包络
            { kPropOpacity, 0.0, 1.0, OutCubic, 0.15, 0.85, true, false, 0.0 },
            { kPropOffsetY, 24.0, 0.0, Spring,  0.15, 1.0, false, false, 0.0 },
        };
        t.append(p);
    }
    // PushDoor standard 360：scale 1.02→1.00（推门）+ 快淡入。
    {
        Preset p; p.name = kPushDoor; p.durationMs = 360;
        p.keys = {
            { kPropScale,   1.02, 1.0, OutCubic, 0.0, 1.0, false, false, 0.0 },
            { kPropOpacity, 0.0, 1.0, OutQuad,  0.0, 0.4, true, false, 0.0 },
        };
        t.append(p);
    }
    // LightsDown standard 360：淡出+微上移（预留键）。
    {
        Preset p; p.name = kLightsDown; p.durationMs = 360;
        p.keys = {
            { kPropOpacity, 1.0, 0.0, InQuad, 0.0, 0.5, true, false, 0.0 },
            { kPropOffsetY, 0.0, -10.0, OutCubic, 0.2, 1.0, false, false, 0.0 },
        };
        t.append(p);
    }
    // ExhibitBreath 2400：策展呼吸（展卡微光 + 凝视逐帧驻足同一档位）。
    // 曲线复用既有 Pulse（spec §2.4「不新造曲线」）：0.25→1→0.25 一整周期，
    // Pulse 自带周期归一 ⇒ f(0)==f(1) 恒等，循环无接缝。
    {
        Preset p; p.name = kExhibitBreath; p.durationMs = 2400;
        p.keys = {
            { kPropGlow, 0.0, 1.0, Pulse, 0.0, 1.0, false, false, 0.0 },
        };
        t.append(p);
    }
    return t;
}

const Preset& preset(const QByteArray& name)
{
    static const QVector<Preset> table = makeTable();
    for (const Preset& p : table)
        if (p.name == name) return p;
    return table.first();   // 未知名回退 NavFade：绝不把 0 时长/空表漏给 UI
}

QVector<QByteArray> presetNames()
{
    static const QVector<Preset> table = makeTable();
    QVector<QByteArray> names;
    names.reserve(table.size());
    for (const Preset& p : table) names.append(p.name);
    return names;
}

} // namespace Motion
