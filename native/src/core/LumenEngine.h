#pragma once
// P5 Lumen 引擎 C++ 移植（忠实对齐 Python core/retouch.py 1.9.39 语义，零云端）。
//
// 铁律（平移自 Python 版）：
//   * 测量→规划→执行：幅度由测量值决定（收敛式），全部带 clamp；
//   * steps ↔ 算子一一对应（「改了什么」逐环节记录）；
//   * _audit 输出自检：四门（削顶/死黑/反差塌陷/色偏）不过→轻力度重做→仍不过→回退原图；
//   * 显式重构编辑（dehaze/level/…/film/bw/glow）不受微调审计整张回退（reconstructed 豁免）。
//
// PIL 基础设施复刻（容差内）：convert('L') 权重、HSV 整数域六分区、ImageChops
// multiply=(a*b)/255、Blend、GaussianBlur/UnsharpMask/Median3/SMOOTH、thumbnail 降采样。
// 验收：金样比对（Python 金样 vs C++ 输出，mean-abs-diff 阈值 + steps 触发序列一致）。
#include <QString>
#include <QStringList>
#include <QVector>
#include <QImage>
#include <QRectF>

// 预设幅度倍率（与 Python PRESETS 一致；native 冒烟/金样走 auto）
struct LumenPreset {
    double wb = 1.0, tone = 1.0, levels = 1.0, bright = 1.0, contrast = 1.0,
           clarity = 1.0, vibrance = 1.0, color = 1.0, sharp = 1.0;
    static LumenPreset byName(const QString& name);   // soft/auto/strong
};

struct LumenStats {            // 对齐 _stats()
    double lum = 0, lum_std = 0, sat = 0, hi = 0, lo = 0;
};

struct LumenMeasure {          // 对齐 _measure_extra()
    double lc = 0, noise = 0, sharp = 0, p1 = 0, p99 = 0, dr = 0, cast = 0, haze = 0;
};

struct LumenStep {             // 对齐 steps.append(...)（金样只比 key 序列）
    QString key;
    QString detail;
};

struct LumenReport {
    QVector<LumenStep> steps;
    bool   reverted = false;   // 审计两轮都不过 → 回退原图
    int    denoise = 0;        // 0/1/2
};

class LumenEngine {
public:
    // ---- 测量 ----
    static LumenStats   measureStats(const QImage& rgb);     // ≤256 缩略
    static LumenMeasure measureExtra(const QImage& rgb);     // ≤256 缩略

    // ---- auto 管线（本批：wb→tone→levels→bright→contrast→clarity→vibrance→denoise→sharp + audit）----
    // k 为预设倍率（auto=1.0）。src 任意格式；输出 RGB32。
    static QImage autoPipeline(const QImage& src, const LumenPreset& p, LumenReport* rep);

    // ---- 基础算子（公开静态：单测 / 金样对拍 / UI 复用）----
    static QImage whiteBalance(const QImage& rgb, double k, QVector<double>* gains = nullptr);
    static QImage toneCurve(const QImage& rgb, const LumenStats& st, double k,
                            double* lift = nullptr, double* recover = nullptr);
    static QImage autoLevels(const QImage& rgb, const LumenStats& st, double k, bool* applied = nullptr);
    static QImage brightness(const QImage& rgb, const LumenStats& st, double k, double* f = nullptr);
    static QImage contrast(const QImage& rgb, const LumenStats& st, double k, double* f = nullptr);
    static QImage clarity(const QImage& rgb, const LumenMeasure& mx, double k, double* amt = nullptr);
    static QImage vibrance(const QImage& rgb, const LumenStats& st, double k, double* v = nullptr);
    static QImage denoise(const QImage& rgb, int level);              // 0/1/2（L2 含 median+smooth）
    static QImage sharpen(const QImage& rgb, double k, double noise, bool portrait = false);

    // ---- P5-2 显式算子（**仅显式预设触发，绝不 auto**；Python 同语义）----
    static QImage filmStyle(const QImage& rgb, bool withGrain = false,
                            double lift = 0.055, double rolloff = 0.42);   // 影调+分离色调+饱和0.92(+颗粒)
    static QImage blackWhite(const QImage& rgb, double contrast = 1.05);    // 通道混合 + 轻微加对比
    static QImage glowStyle(const QImage& rgb, double amount = 0.5);        // Orton 柔焦
    static QImage redeyeRemove(const QImage& rgb, int* pixels = nullptr);   // 保守启发式（面积闸门）
    static QImage vignetteCorrect(const QImage& rgb, double strength = 1.0, double* kOut = nullptr);
    static QImage toneMapHdr(const QImage& rgb, double amount = 1.0);       // 局部光照引导 + 高光软膝
    static QImage structure(const QImage& rgb, double amount = 1.0);        // 明度细尺度 USM + 中调掩膜
    static QImage dodge(const QImage& rgb, double amount = 1.0);            // 局部暗部提亮

    // ---- P5-3 显式算子（第三批）----
    static QImage dehaze(const QImage& rgb, double strength = 1.0);         // 暗通道先验（He et al.）
    static QImage chromaticCorrect(const QImage& rgb, double amount = 1.0); // 横向色差 / 紫边
    static QImage portraitBeautify(const QImage& rgb, double strength = 1.0,
                                   double* skinFrac = nullptr);             // 肤色掩膜磨皮 + 美白

    // ---- PIL 合成基础设施（P5-2）----
    static QImage blend(const QImage& a, const QImage& b, double f);        // a*(1-f)+b*f
    static QImage screen(const QImage& a, const QImage& b);                 // chop screen
    static QImage composite(const QImage& a, const QImage& b, const QImage& mask);
    static QImage channelGain(const QImage& rgb, double gr, double gg, double gb);
    static QImage enhanceContrastPil(const QImage& rgb, double f);          // 以 L 均值为轴心（PIL Contrast）
    static QImage addGrain(const QImage& rgb, double amount);               // PIL effect_noise 近似（PRNG）

    // ---- PIL 基础设施（P5-3）----
    static QImage minFilter(const QImage& src, int k);          // K×K 最小值滤波（Grayscale8 / RGB32）
    static QImage medianFilter(const QImage& rgb, int size);    // size×size 中值（size 奇数）
    static void   ycbcrSplit(const QImage& rgb, QImage* y, QImage* cb, QImage* cr);
    static QImage skinMask(const QImage& rgb);                  // YCbCr 规则 + 亮度门 + 羽化

    // 降噪档位（EXIF 缺失时按画面噪声估计；与 Python _denoise_level_noise 一致）
    static int denoiseLevelByNoise(double noise) { return noise >= 0.070 ? 2 : (noise >= 0.045 ? 1 : 0); }

    // ---- 审计（四门；与 _audit 阈值一致）----
    static QStringList audit(const LumenStats& st0, const LumenMeasure& mx0, const QImage& out);

    // ---- PIL 兼容基础设施（供算子内部与单测）----
    static int  toL(int r, int g, int b);                 // PIL convert('L') 整数权重
    static QImage  toL8(const QImage& rgb);               // → Format_Grayscale8
    static void rgb2hsv(const QImage& rgb, QImage* h, QImage* s, QImage* v);  // 8bit 六分区分量
    static QImage hsv2rgb(const QImage& h, const QImage& s, const QImage& v);
    static QImage gaussianBlur(const QImage& src, double radius);   // 分离高斯（σ=radius）
    static QImage unsharpMask(const QImage& src, double radius, int percent, int threshold);
    static QImage median3(const QImage& src);             // 3×3 中值（逐通道）
    static QImage smoothFilter(const QImage& src);        // PIL SMOOTH 核 [1,2,1;2,4,2;1,2,1]/16
    static QImage boxDownscale(const QImage& src, int maxEdge);     // thumbnail 近似（box 均值）
    static QImage enhanceBrightness(const QImage& rgb, double f);   // blend(black, img, f)
    static QImage enhanceColor(const QImage& rgb, double f);        // blend(gray, img, f)
};
