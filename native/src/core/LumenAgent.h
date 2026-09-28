#pragma once
// P9 Lumen × Ollama AI 修图智能体（决策层）—— brain.py 的 C++ 移植。
//
// 架构（对齐 LocalLLMBrain / LocalBrain 三层兜底）：
//   decide()   = Ollama 本地 LLM（think:false + format:json，实测 0.5s 级）
//   decideLocal() = 纯规则兜底（移植 LocalBrain.decide），永远可用
// 铁律（平移自 Python 版）：
//   * 输入是引擎的**客观测量**，不是原图——不传 base64，又快又稳；
//   * 只允许回环地址（零云端铁律，is_local_url 同款校验）；
//   * LLM 输出 13 参数白名单 + clamp [-1,1]，未知键丢弃（不信模型乱给）；
//   * 任何失败都不抛断链路——decide 失败自动落到 decideLocal（source 注明）。
// 执行：plan 直接喂 LumenEngine::retouchWithPlan（13 项增量管线，P5-4 金样全绿）。
#include <QString>
#include <QMap>
#include <QImage>

struct AgentContext {
    double lum = 0, lum_std = 0, sat = 0;      // 亮度 / 离散 / 饱和（LumenStats）
    double hi = 0, lo = 0;                     // 高光/阴影占比
    double cast = 0, haze = 0, noise = 0;      // 色偏 / 雾度 / 噪声（LumenMeasure）
    int    iso = 0;                            // EXIF（Photo.iso；无 → 0）
    double shutter = 0;                        // EXIF 快门秒数（"1/125"→0.008；无 → 0）
    QString goal;                              // 用户目标（可空）
};

struct AgentPlan {
    QMap<QString, double> params;   // 13 项增量（retouchWithPlan 契约键）
    QString intent;                 // 意图（LLM 的 _intent / 规则推导）
    QString reason;                 // 依据（展示用）
    QString source;                 // "ollama" / "local"
    QString model;                  // 模型名（source=ollama 时有值）
    bool   ok = false;
};

class LumenAgent {
public:
    // 从图像 + EXIF + 目标构造客观测量上下文（缩略 ≤256，与 LumenEngine 测量同源）
    static AgentContext contextFrom(const QImage& rgb, int iso, double shutter,
                                    const QString& goal);

    // 决策入口：Ollama 在线且输出合法 → llm 计划；否则 → 规则兜底（ok 恒 true）
    static AgentPlan decide(const AgentContext& ctx);

    // 纯规则决策（brain.LocalBrain.decide 忠实移植；零依赖零网络）
    static AgentPlan decideLocal(const AgentContext& ctx);

    // 13 参数白名单 + clamp [-1,1]（多余键丢弃）
    static QMap<QString, double> clampPlan(const QMap<QString, double>& in);

    // "1/125" → 0.008；"15s" → 15.0；"2.5s" → 2.5；无法解析 → 0
    static double parseShutterSeconds(const QString& sh);
};
