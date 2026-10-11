#pragma once
#include <QPointF>
#include <QString>
#include <QVector>

// ============================================================================
// MotionKit —— 客户端动效引擎唯一真源（2.34.0，方案 A）。
// 曲线族 / 关键帧时间线 / 预设表 / 粒子轨迹全部是**纯函数**（铁律 20：
// 产品与门禁共用同一实现，可断言值域/端点/单调/减动效终态）。
// MotionDriver 是唯一的 Qt 侧驱动器：读预设 → 推进时间 → 逐属性回调数值，
// UI 不再各自散写 QPropertyAnimation 的曲线/时长。
// ============================================================================

namespace Motion {

// ---- 曲线族：输入归一化时间 t∈[0,1]，输出进度值 ----
enum Curve {
    Linear = 0,      // 恒等
    InQuad,          // 缓入（原 MainWindow 淡出用 InQuad）
    OutQuad,
    InOutCubic,
    OutCubic,        // 原工程事实标准
    OutExpo,         // 急速起步、长尾收住（灯箱横滑）
    OutBack,         // 过冲回弹（chip/星级弹出）
    OutElastic,      // 阻尼振荡（弹簧，星级弹跳）
    Spring,          // 临界阻尼偏振荡，位置/缩放主用
    Pulse,           // 骨架脉冲：cos → [0.25,1]，f(0)==f(1)（承接 2.22 契约）
    kCurveCount      // 必须保持最后
};

double evalCurve(Curve c, double t);

// 错峰：第 i 个元素相对整组的起始时刻偏移 [0,1)；step 为组内归一化步长。
// 单调不减（断言点）；total<=1 时恒 0。
double staggerStart(int i, int total, double step);

// ---- 关键帧与预设 ----
struct Key {
    QByteArray prop;      // 属性名（见 kProp* 常量）
    double from = 0.0;
    double to = 1.0;
    Curve curve = OutCubic;
    double t0 = 0.0;      // 本关键帧在整段中的起点（归一化）
    double t1 = 1.0;      // 终点；t∈[t0,t1] 之外钳位到端值
    bool clamp01 = false; // opacity 类属性必须钳位；位移/缩放可过冲
    // 过冲回归段：has_return 时，[t1, t1+(t1-t0)] 内以同曲线从 to 落回 return_to
    // ——「弹一下再归位」（星级/chip 手感），过冲峰值完全由曲线决定，无手工双段。
    bool has_return = false;
    double return_to = 0.0;
};

struct Preset {
    QByteArray name;
    int durationMs = 260;
    QVector<Key> keys;
};

// 预设表唯一真源（未知名回退 NavFade，绝不返回空表 → UI 永不拿到 0 时长）。
const Preset& preset(const QByteArray& name);
QVector<QByteArray> presetNames();

// 整段求值：f∈[0,1] → 每个 Key 的数值（顺序与 keys 一致）。
QVector<double> evaluate(const Preset& p, double f);

// ---- 预设名（数据驱动，UI 只按名取用）----
inline const QByteArray kNavFade    { "NavFade" };     // 页面转场：淡出→淡入+上滑
inline const QByteArray kLightbox   { "LightboxSlide" };// 灯箱横滑
inline const QByteArray kChipPop    { "ChipPop" };      // 故事 chip 入场弹跳
inline const QByteArray kStarBounce { "StarBounce" };   // 批量星级按钮弹跳
inline const QByteArray kHeartBurst { "HeartBurst" };   // 喜欢标记心形粒子爆开

// ---- 属性名（evaluate 回调与 UI 约定）----
inline const QByteArray kPropOpacity { "opacity" };
inline const QByteArray kPropOffsetY { "offsetY" };   // px，入场=由下向上
inline const QByteArray kPropOffsetX { "offsetX" };   // px，横滑位移
inline const QByteArray kPropScale   { "scale" };     // 1.0 基准，可过冲
inline const QByteArray kPropFontPx  { "fontPx" };    // 星级按钮字号弹跳
inline const QByteArray kPropGlow    { "glow" };      // 0→1→0 辉光包络（色/尺寸混合因子）

// 粒子轨迹纯函数：第 idx 枚（共 n 枚）在整段进度 f 时的平面位置与衰减不透明度。
// 角度 = idx/n 均匀分布 + 黄金角微散（确定性，无随机数 → 铁律 29 主机无关）。
QPointF heartBurstPos(int idx, int n, double f, qreal radiusPx);
double    heartBurstAlpha(int n, double f);   // 整体淡出包络

// ---- QA 钩子（仅供门禁反例，绝不影响生产路径默认值）----
void  qaSetForceTerminal(bool on);   // 强制全曲线终值（模拟减动效接线断链）
bool  qaForceTerminal();

} // namespace Motion
