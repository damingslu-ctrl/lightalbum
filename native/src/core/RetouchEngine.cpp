#include "RetouchEngine.h"
#include <cmath>
#include <algorithm>

// ----------------------------------------------------------------- 预设表
static const QVector<RetouchPreset>& buildPresets()
{
    static const QVector<RetouchPreset> table = {
        {"original",  QStringLiteral("原图"),        {}},
        {"auto",      QStringLiteral("智能增强"),
            {0.05,0,0.10,0.15,0.20,0,0,0,0.25,0,0,0}},
        {"vivid",     QStringLiteral("鲜艳"),
            {0,0,0.10,0.10,0.20,0.35,0,0,0.25,0,0,0}},
        {"natural",   QStringLiteral("自然"),
            {0,0,0.05,0.05,0.15,0,0,0,0.20,0,0,0}},
        {"bw",       QStringLiteral("黑白"),
            {0,0,0.10,0,0,-1.0,0,0,0.25,0,0,0}},
        {"warm",      QStringLiteral("暖调"),
            {0.05,0,0,0,0.10,0,0.40,0,0.20,0,0,0}},
        {"cool",      QStringLiteral("冷调"),
            {0,0,0,0,0.05,0,-0.40,-0.10,0.20,0,0,0}},
        {"film",      QStringLiteral("胶片"),
            {0,0,0.15,-0.05,0,0,0.15,0,0.20,-0.10,0,0.20}},
        {"vintage",   QStringLiteral("复古"),
            {0.05,0,0.05,-0.15,0,0,0.25,0,0.15,0,0,0.35}},
        {"mono_green",QStringLiteral("墨绿"),
            {0,0,0.10,-0.85,0,0,0.10,0.20,0.15,0,0,0.10}},
        {"portrait",  QStringLiteral("人像"),
            {0,0,0,0.10,0,0,0.10,0.10,0.30,-0.05,0,0}},
        {"landscape", QStringLiteral("风光"),
            {0,0,0.10,0.15,0.30,0.10,0,0,0.25,0,0,0}},
        {"food",     QStringLiteral("美食"),
            {0.05,0,0.05,0.20,0.25,0,0,0,0.25,0,0,0}},
        {"drama",    QStringLiteral("戏剧"),
            {0,0,0.35,0.30,0,0,0,0,0.30,0.15,-0.15,0.30}},
        {"soft",     QStringLiteral("柔光"),
            {0.06,-0.10,0,0,0,0,0,0,0.10,0,0,-0.20}},
        {"hdr",      QStringLiteral("HDR"),
            {0,0,0.20,0.35,0,0.10,0,0,0.30,0.10,-0.10,0}},
    };
    return table;
}

const QVector<RetouchPreset>& RetouchEngine::presets() { return buildPresets(); }

RetouchParams RetouchEngine::preset(const QString& key)
{
    for (const RetouchPreset& pr : buildPresets())
        if (pr.key == key) return pr.p;
    return RetouchParams{};   // 未知 → 身份
}

// ----------------------------------------------------------------- 描述串
QString RetouchEngine::describe(const RetouchParams& p)
{
    QStringList parts;
    auto add = [&](const char* name, double v, double scale, const char* unit){
        if (std::fabs(v) < 1e-6) return;
        parts.append(QStringLiteral("%1 %2%3").arg(QString::fromLatin1(name),
            (v >= 0 ? "+" : ""), QString::number(v * scale, 'f', 2) + QString::fromLatin1(unit)));
    };
    add("曝光", p.exposure, 2.0, "EV");
    add("亮度", p.brightness, 60.0, "");
    add("对比", p.contrast, 0.6, "");
    add("清晰", p.clarity, 1.0, "");
    add("自然饱和", p.vibrance, 1.0, "");
    add("饱和", p.saturation, 1.0, "");
    add("色温", p.temp, 1.0, "");
    add("色调", p.tint, 1.0, "");
    if (p.sharpen > 0)   parts.append(QStringLiteral("锐化 +%1").arg(QString::number(p.sharpen, 'f', 2)));
    add("高光", p.highlights, 1.0, "");
    add("阴影", p.shadows, 1.0, "");
    add("暗角", p.vignette, 1.0, "");
    return parts.isEmpty() ? QStringLiteral("无调整") : parts.join(" · ");
}

// ----------------------------------------------------------------- 工具
namespace {
constexpr double clamp01(double v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
constexpr int    clamp255(int v)   { return v < 0 ? 0 : (v > 255 ? 255 : v); }

// 分离式 box blur（RGB32）。radius ≥ 1。返回新图。
QImage boxBlur(const QImage& src, int radius)
{
    if (radius < 1) return src.copy();
    const int w = src.width(), h = src.height();
    QImage tmp(w, h, QImage::Format_RGB32);
    QImage out(w, h, QImage::Format_RGB32);
    const int r = radius;
    const double inv = 1.0 / (2 * r + 1);

    // 水平
    for (int y = 0; y < h; ++y) {
        const QRgb* s = reinterpret_cast<const QRgb*>(src.constScanLine(y));
        QRgb* d = reinterpret_cast<QRgb*>(tmp.scanLine(y));
        double sr = 0, sg = 0, sb = 0;
        for (int i = -r; i <= r; ++i) {
            const int xx = qBound(0, i, w - 1);
            const QRgb p = s[xx];
            sr += qRed(p); sg += qGreen(p); sb += qBlue(p);
        }
        for (int x = 0; x < w; ++x) {
            d[x] = qRgb(int(sr * inv), int(sg * inv), int(sb * inv));
            const int xin  = qBound(0, x + r + 1, w - 1);
            const int xout = qBound(0, x - r,     w - 1);
            const QRgb pin  = s[xin], pout = s[xout];
            sr += qRed(pin)   - qRed(pout);
            sg += qGreen(pin) - qGreen(pout);
            sb += qBlue(pin)  - qBlue(pout);
        }
    }
    // 垂直
    for (int x = 0; x < w; ++x) {
        double sr = 0, sg = 0, sb = 0;
        for (int i = -r; i <= r; ++i) {
            const int yy = qBound(0, i, h - 1);
            const QRgb p = reinterpret_cast<const QRgb*>(tmp.constScanLine(yy))[x];
            sr += qRed(p); sg += qGreen(p); sb += qBlue(p);
        }
        for (int y = 0; y < h; ++y) {
            QRgb* d = reinterpret_cast<QRgb*>(out.scanLine(y));
            d[x] = qRgb(int(sr * inv), int(sg * inv), int(sb * inv));
            const int yin  = qBound(0, y + r + 1, h - 1);
            const int yout = qBound(0, y - r,     h - 1);
            const QRgb pin  = reinterpret_cast<const QRgb*>(tmp.constScanLine(yin))[x];
            const QRgb pout = reinterpret_cast<const QRgb*>(tmp.constScanLine(yout))[x];
            sr += qRed(pin)   - qRed(pout);
            sg += qGreen(pin) - qGreen(pout);
            sb += qBlue(pin)  - qBlue(pout);
        }
    }
    return out;
}

inline double luma(double r, double g, double b) {
    return 0.299 * r + 0.587 * g + 0.114 * b;   // 0..255
}
} // namespace

// ----------------------------------------------------------------- 主修图
QImage RetouchEngine::apply(const QImage& src, const RetouchParams& p)
{
    if (p.isEmpty() || src.isNull()) return src.copy();

    QImage img = src.convertToFormat(QImage::Format_RGB32);
    const int w = img.width(), h = img.height();
    if (w == 0 || h == 0) return img;

    // 预计算增益
    const double expF     = std::pow(2.0, p.exposure * 2.0);   // ±2 EV
    const double contrast = 1.0 + p.contrast * 0.6;
    const double bright   = p.brightness * 60.0;               // 加法
    const double rGain = (1 + p.temp * 0.15) * (1 + p.tint * 0.10);
    const double gGain = (1 - p.tint * 0.10);
    const double bGain = (1 - p.temp * 0.15) * (1 + p.tint * 0.10);

    // ---- Pass A：曝光/亮度/对比/白平衡（逐通道线性） ----
    for (int y = 0; y < h; ++y) {
        QRgb* d = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            double r = qRed(d[x]), g = qGreen(d[x]), b = qBlue(d[x]);
            r = (r * expF * contrast + bright) * rGain;
            g = (g * expF * contrast + bright) * gGain;
            b = (b * expF * contrast + bright) * bGain;
            d[x] = qRgb(clamp255(int(r + 0.5)), clamp255(int(g + 0.5)), clamp255(int(b + 0.5)));
        }
    }

    // ---- Pass B：饱和度 / 自然饱和（保亮度） ----
    if (p.saturation != 0 || p.vibrance != 0) {
        const double satF = std::max(0.0, 1.0 + p.saturation);   // -1 → 全灰(0)，+1 → 2x
        for (int y = 0; y < h; ++y) {
            QRgb* d = reinterpret_cast<QRgb*>(img.scanLine(y));
            for (int x = 0; x < w; ++x) {
                double r = qRed(d[x]), g = qGreen(d[x]), b = qBlue(d[x]);
                const double L = luma(r, g, b);
                // vibrance：弱饱和色多提，强饱和色少提
                const double mx = std::max({r, g, b}), mn = std::min({r, g, b});
                const double sat = (mx - mn) / 255.0;
                const double vF  = 1.0 + p.vibrance * (1.0 - sat) * 0.8;
                const double f   = satF * vF;
                r = L + (r - L) * f;
                g = L + (g - L) * f;
                b = L + (b - L) * f;
                d[x] = qRgb(clamp255(int(r + 0.5)), clamp255(int(g + 0.5)), clamp255(int(b + 0.5)));
            }
        }
    }

    // 基准副本（用于 blurred 参考，保证 clarity/sharpen 在修色后图上做）
    QImage base = img.copy();

    // ---- Pass C：高光 / 阴影（S 曲线，保中间调） ----
    if (p.highlights != 0 || p.shadows != 0) {
        for (int y = 0; y < h; ++y) {
            QRgb* d = reinterpret_cast<QRgb*>(img.scanLine(y));
            for (int x = 0; x < w; ++x) {
                double r = qRed(d[x]), g = qGreen(d[x]), b = qBlue(d[x]);
                const double L = luma(r, g, b) / 255.0;
                double t = 0;                                  // 每通道位移（0..1 域）
                if (L > 0.5) t = -p.highlights * (L - 0.5) * 2.0;   // 压高光
                else          t =  p.shadows    * (0.5 - L) * 2.0;   // 提阴影
                r = r + t * 255.0; g = g + t * 255.0; b = b + t * 255.0;
                d[x] = qRgb(clamp255(int(r + 0.5)), clamp255(int(g + 0.5)), clamp255(int(b + 0.5)));
            }
        }
    }

    // ---- Pass D：清晰（luminance unsharp） ----
    if (p.clarity != 0) {
        QImage blur = boxBlur(base, 4);
        const double k = p.clarity * 0.5;
        for (int y = 0; y < h; ++y) {
            QRgb* d = reinterpret_cast<QRgb*>(img.scanLine(y));
            const QRgb* sb = reinterpret_cast<const QRgb*>(base.constScanLine(y));
            const QRgb* bl = reinterpret_cast<const QRgb*>(blur.constScanLine(y));
            for (int x = 0; x < w; ++x) {
                const double Lb = luma(qRed(sb[x]), qGreen(sb[x]), qBlue(sb[x]));
                const double Ls = luma(qRed(bl[x]), qGreen(bl[x]), qBlue(bl[x]));
                const double delta = (Lb - Ls) * k;
                double r = qRed(d[x]) + delta, g = qGreen(d[x]) + delta, b = qBlue(d[x]) + delta;
                d[x] = qRgb(clamp255(int(r + 0.5)), clamp255(int(g + 0.5)), clamp255(int(b + 0.5)));
            }
        }
    }

    // ---- Pass E：USM 锐化 ----
    if (p.sharpen > 0) {
        QImage blur = boxBlur(base, 1);
        const double k = p.sharpen * 0.8;
        for (int y = 0; y < h; ++y) {
            QRgb* d = reinterpret_cast<QRgb*>(img.scanLine(y));
            const QRgb* sb = reinterpret_cast<const QRgb*>(base.constScanLine(y));
            const QRgb* bl = reinterpret_cast<const QRgb*>(blur.constScanLine(y));
            for (int x = 0; x < w; ++x) {
                double r = qRed(sb[x])     + (qRed(sb[x])     - qRed(bl[x]))     * k;
                double g = qGreen(sb[x])   + (qGreen(sb[x])   - qGreen(bl[x]))   * k;
                double b = qBlue(sb[x])    + (qBlue(sb[x])    - qBlue(bl[x]))    * k;
                d[x] = qRgb(clamp255(int(r + 0.5)), clamp255(int(g + 0.5)), clamp255(int(b + 0.5)));
            }
        }
    }

    // ---- Pass F：暗角 ----
    if (p.vignette != 0) {
        const double cx = (w - 1) * 0.5, cy = (h - 1) * 0.5;
        const double maxD = std::sqrt(cx * cx + cy * cy);
        const double v = p.vignette;
        for (int y = 0; y < h; ++y) {
            QRgb* d = reinterpret_cast<QRgb*>(img.scanLine(y));
            for (int x = 0; x < w; ++x) {
                const double dx = x - cx, dy = y - cy;
                const double nd = std::sqrt(dx * dx + dy * dy) / maxD;   // 0 中 → 1 角
                const double fall = (1.0 - nd);
                const double factor = 1.0 + v * 0.6 * (fall * fall - 1.0); // 正→角变暗
                double r = qRed(d[x]) * factor, g = qGreen(d[x]) * factor, b = qBlue(d[x]) * factor;
                d[x] = qRgb(clamp255(int(r + 0.5)), clamp255(int(g + 0.5)), clamp255(int(b + 0.5)));
            }
        }
    }

    return img;
}

// ----------------------------------------------------------------- 建议
RetouchSuggest RetouchEngine::recommend(const QImage& img)
{
    RetouchSuggest s;
    if (img.isNull()) return s;

    const QImage q = img.convertToFormat(QImage::Format_RGB32);
    const int w = q.width(), h = q.height();
    if (w == 0 || h == 0) return s;

    // 抽样统计（步进取大，限制 ~50k 像素）
    const int total = w * h;
    const int step = std::max(1, int(std::sqrt(double(total) / 40000.0)));
    double sumL = 0, sumSat = 0, sumL2 = 0;
    long n = 0;
    for (int y = 0; y < h; y += step) {
        const QRgb* d = reinterpret_cast<const QRgb*>(q.constScanLine(y));
        for (int x = 0; x < w; x += step) {
            const double r = qRed(d[x]), g = qGreen(d[x]), b = qBlue(d[x]);
            const double L = luma(r, g, b) / 255.0;
            const double mx = std::max({r, g, b}), mn = std::min({r, g, b});
            const double sat = (mx - mn) / 255.0;
            sumL += L; sumL2 += L * L; sumSat += sat; ++n;
        }
    }
    if (n == 0) return s;
    const double avgL = sumL / n;
    const double avgSat = sumSat / n;
    const double var = sumL2 / n - avgL * avgL;
    const double stdL = std::sqrt(std::max(0.0, var));

    RetouchParams& p = s.params;
    if (avgL < 0.40) {
        s.chips.append(QStringLiteral("偏暗 · 提亮"));
        p.brightness += 0.25; p.exposure += 0.10; p.shadows += 0.30;
    } else if (avgL > 0.62) {
        s.chips.append(QStringLiteral("偏亮 · 压高光"));
        p.highlights -= 0.30; p.exposure -= 0.10;
    }
    if (avgSat < 0.06) {
        s.chips.append(QStringLiteral("色彩平淡 · 加自然饱和"));
        p.vibrance += 0.40;
    } else if (avgSat > 0.22) {
        s.chips.append(QStringLiteral("色彩浓烈 · 略降饱和"));
        p.saturation -= 0.15;
    }
    if (stdL < 0.12) {
        s.chips.append(QStringLiteral("对比偏弱 · 加清晰"));
        p.clarity += 0.30; p.contrast += 0.10;
    }
    if (p.isEmpty()) {
        s.chips.append(QStringLiteral("曝光均衡 · 仅锐化"));
        p.sharpen = 0.30;
    } else {
        p.sharpen = 0.30;   // 任何建议都附带轻度锐化
    }
    s.valid = true;
    return s;
}
