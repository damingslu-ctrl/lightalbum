#pragma once
// P3 修图实验室 — 功能型修图引擎（零云端纯像素运算）
// 算子：曝光 / 亮度 / 对比 / 清晰(clarity) / 自然饱和(vibrance) / 饱和度 /
//        白平衡(色温/色调) / USM 锐化 / 高光-阴影 / 暗角。
// 数据诚实铁律：所有参数默认 0（身份），严禁编造；recommend() 仅按本地像素统计给建议。
// 所有参数统一用归一量：多数 ∈ [-1,1]，锐化/暗角 ∈ [0,1] 或 [-1,1]。
#include <QString>
#include <QStringList>
#include <QVector>
#include <QImage>

struct RetouchParams {
    double exposure   = 0.0;   // [-1,1]  ≈ ±2 EV
    double brightness = 0.0;   // [-1,1]  加法偏移（±60 阶）
    double contrast   = 0.0;   // [-1,1]  乘性对比（±0.6）
    double clarity    = 0.0;   // [-1,1]  局部中调对比（unsharp on luminance）
    double vibrance   = 0.0;   // [-1,1]  自然饱和（保护已饱和色）
    double saturation = 0.0;   // [-1,1]  全局饱和
    double temp      = 0.0;   // [-1,1]  色温（暖正冷负）
    double tint      = 0.0;   // [-1,1]  色调（品红正绿负）
    double sharpen   = 0.0;   // [ 0,1]  USM 锐化量
    double highlights = 0.0;   // [-1,1]  高光
    double shadows   = 0.0;   // [-1,1]  阴影
    double vignette  = 0.0;   // [-1,1]  暗角（正=加暗角）

    bool isEmpty() const {
        return exposure==0 && brightness==0 && contrast==0 && clarity==0 &&
               vibrance==0 && saturation==0 && temp==0 && tint==0 &&
               sharpen==0 && highlights==0 && shadows==0 && vignette==0;
    }
};

struct RetouchPreset {
    QString key;
    QString name;
    RetouchParams p;
};

struct RetouchSuggest {
    QStringList chips;       // 建议芯片文本（可点击一键套用）
    RetouchParams params;     // 一键套用参数
    bool valid = false;      // 是否探测到需修维度（false → 原图已均衡）
};

class RetouchEngine {
public:
    // 16 预设（含原图）。顺序即下拉顺序。
    static const QVector<RetouchPreset>& presets();
    // 按 key 取参；未知 key 返回全 0（身份）。
    static RetouchParams preset(const QString& key);

    // 启发式建议：基于本地像素统计（零云端）。img 可为任意格式。
    static RetouchSuggest recommend(const QImage& img);

    // 主修图：输入原始图，输出修后图（副本）。空参返回原图副本。
    // src 会被 convertToFormat，不修改入参。
    static QImage apply(const QImage& src, const RetouchParams& p);

    // 友好数值：把归一量转成 UI 展示串（如 exposure=0.25 → "+0.50 EV"）
    static QString describe(const RetouchParams& p);
};
