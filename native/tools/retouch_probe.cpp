// 临时探针：实测 12 个 RetouchEngine 算子的真实像素 delta，用于标定门禁阈值。
// 用法：见下方 main()，编译为独立 exe 链接 RetouchEngine.cpp。
#include <QCoreApplication>
#include <QImage>
#include <QPainter>
#include <QVector>
#include <cmath>
#include <cstdio>
#include "core/RetouchEngine.h"

static double lumaOf(QRgb c) { return 0.299 * qRed(c) + 0.587 * qGreen(c) + 0.114 * qBlue(c); }

static double meanLuma(const QImage& im) {
    if (im.isNull()) return -1;
    double s = 0; long n = 0;
    for (int y = 0; y < im.height(); ++y) {
        const QRgb* d = reinterpret_cast<const QRgb*>(im.constScanLine(y));
        for (int x = 0; x < im.width(); ++x) { s += lumaOf(d[x]); ++n; }
    }
    return n ? s / n : -1;
}

static double stdLuma(const QImage& im) {
    if (im.isNull()) return -1;
    double s = 0, s2 = 0; long n = 0;
    for (int y = 0; y < im.height(); ++y) {
        const QRgb* d = reinterpret_cast<const QRgb*>(im.constScanLine(y));
        for (int x = 0; x < im.width(); ++x) { const double L = lumaOf(d[x]); s += L; s2 += L * L; ++n; }
    }
    if (!n) return -1;
    const double m = s / n;
    return std::sqrt(std::max(0.0, s2 / n - m * m));
}

static double meanSat(const QImage& im) {
    if (im.isNull()) return -1;
    double s = 0; long n = 0;
    for (int y = 0; y < im.height(); ++y) {
        const QRgb* d = reinterpret_cast<const QRgb*>(im.constScanLine(y));
        for (int x = 0; x < im.width(); ++x) {
            const int mx = std::max({qRed(d[x]), qGreen(d[x]), qBlue(d[x])});
            const int mn = std::min({qRed(d[x]), qGreen(d[x]), qBlue(d[x])});
            s += (mx - mn) / 255.0; ++n;
        }
    }
    return n ? s / n : -1;
}

// 局部对比度代理：|拉普拉斯| 均值（clarity/sharpen 的方向判别器）
static double localContrast(const QImage& im) {
    if (im.isNull() || im.width() < 3 || im.height() < 3) return -1;
    QImage q = im.convertToFormat(QImage::Format_RGB32);
    double s = 0; long n = 0;
    for (int y = 1; y < q.height() - 1; ++y) {
        const QRgb* d = reinterpret_cast<const QRgb*>(q.constScanLine(y));
        const QRgb* u = reinterpret_cast<const QRgb*>(q.constScanLine(y - 1));
        const QRgb* v = reinterpret_cast<const QRgb*>(q.constScanLine(y + 1));
        for (int x = 1; x < q.width() - 1; ++x) {
            const double lap = 4.0 * lumaOf(d[x]) - lumaOf(d[x - 1]) - lumaOf(d[x + 1])
                             - lumaOf(u[x]) - lumaOf(v[x]);
            s += std::fabs(lap); ++n;
        }
    }
    return n ? s / n : -1;
}

static QImage makeTestImage() {
    // 128x96 合成图：左半中灰、右上有亮块、左下有暗块、中心一个方块（给锐化/清晰边缘）
    QImage im(128, 96, QImage::Format_RGB32);
    im.fill(qRgb(128, 128, 128));
    QPainter p(&im);
    p.fillRect(80, 8, 40, 36, qRgb(225, 225, 225));    // 亮块（L>0.5 → 高光区）
    p.fillRect(8, 56, 40, 32, qRgb(38, 38, 38));      // 暗块（L<0.5 → 阴影区）
    p.fillRect(52, 34, 24, 24, qRgb(200, 60, 60));    // 有色块（给饱和/白平衡）
    p.fillRect(30, 20, 20, 20, qRgb(245, 245, 245));  // 硬边缘（给锐化/清晰）
    p.end();
    return im;
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QImage src = makeTestImage();

    struct Probe { const char* key; const char* name; RetouchParams p; };
    QVector<Probe> probes = {
        { "exposure+",   "exposure=+0.5",   [] { RetouchParams p; p.exposure   =  0.5; return p; }() },
        { "exposure-",   "exposure=-0.5",   [] { RetouchParams p; p.exposure   = -0.5; return p; }() },
        { "brightness+", "brightness=+0.5", [] { RetouchParams p; p.brightness =  0.5; return p; }() },
        { "brightness-", "brightness=-0.5", [] { RetouchParams p; p.brightness = -0.5; return p; }() },
        { "contrast+",   "contrast=+0.5",   [] { RetouchParams p; p.contrast   =  0.5; return p; }() },
        { "contrast-",   "contrast=-0.5",   [] { RetouchParams p; p.contrast   = -0.5; return p; }() },
        { "clarity+",    "clarity=+0.5",    [] { RetouchParams p; p.clarity    =  0.5; return p; }() },
        { "clarity-",    "clarity=-0.5",    [] { RetouchParams p; p.clarity    = -0.5; return p; }() },
        { "vibrance+",   "vibrance=+0.6",   [] { RetouchParams p; p.vibrance   =  0.6; return p; }() },
        { "vibrance-",   "vibrance=-0.6",   [] { RetouchParams p; p.vibrance   = -0.6; return p; }() },
        { "saturation+", "saturation=+0.6", [] { RetouchParams p; p.saturation =  0.6; return p; }() },
        { "saturation-", "saturation=-1.0", [] { RetouchParams p; p.saturation = -1.0; return p; }() },
        { "temp+",       "temp=+0.8",       [] { RetouchParams p; p.temp       =  0.8; return p; }() },
        { "temp-",       "temp=-0.8",       [] { RetouchParams p; p.temp       = -0.8; return p; }() },
        { "tint+",       "tint=+0.8",       [] { RetouchParams p; p.tint       =  0.8; return p; }() },
        { "tint-",       "tint=-0.8",       [] { RetouchParams p; p.tint       = -0.8; return p; }() },
        { "sharpen+",    "sharpen=0.8",     [] { RetouchParams p; p.sharpen    =  0.8; return p; }() },
        { "highlights-", "highlights=-0.8", [] { RetouchParams p; p.highlights = -0.8; return p; }() },
        { "shadows+",    "shadows=+0.8",    [] { RetouchParams p; p.shadows    =  0.8; return p; }() },
        { "vignette+",   "vignette=+0.8",   [] { RetouchParams p; p.vignette   =  0.8; return p; }() },
    };

    // identity 契约：空参必须逐位等同原图
    const RetouchParams id;
    const QImage idOut = RetouchEngine::apply(src, id);
    bool bitIdentical = (idOut.size() == src.size());
    long diffPx = 0;
    if (bitIdentical) {
        for (int y = 0; y < src.height() && diffPx == 0; ++y) {
            const QRgb* a = reinterpret_cast<const QRgb*>(src.constScanLine(y));
            const QRgb* b = reinterpret_cast<const QRgb*>(idOut.constScanLine(y));
            for (int x = 0; x < src.width(); ++x) {
                if (a[x] != b[x]) { diffPx = 1; break; }
            }
        }
    }
    std::printf("IDENTITY size=%d bitIdentical=%d\n",
                int(idOut.size() == src.size()), int(bitIdentical && diffPx == 0));

    std::printf("%-14s %10s %10s %10s %10s %10s\n",
                "op", "dLuma", "dStd", "dSat", "dLocal", "dRGBsum");
    for (const Probe& pr : probes) {
        const QImage out = RetouchEngine::apply(src, pr.p);
        std::printf("%-14s %10.3f %10.3f %10.5f %10.3f  ",
                    pr.name,
                    meanLuma(out) - meanLuma(src),
                    stdLuma(out) - stdLuma(src),
                    meanSat(out) - meanSat(src),
                    localContrast(out) - localContrast(src));
        // RGB 三通道和（判定色温/色调方向）
        long sr = 0, sg = 0, sb = 0;
        for (int y = 0; y < out.height(); ++y) {
            const QRgb* d = reinterpret_cast<const QRgb*>(out.constScanLine(y));
            for (int x = 0; x < out.width(); ++x) { sr += qRed(d[x]); sg += qGreen(d[x]); sb += qBlue(d[x]); }
        }
        long br = 0, bg = 0, bb = 0;
        for (int y = 0; y < src.height(); ++y) {
            const QRgb* d = reinterpret_cast<const QRgb*>(src.constScanLine(y));
            for (int x = 0; x < src.width(); ++x) { br += qRed(d[x]); bg += qGreen(d[x]); bb += qBlue(d[x]); }
        }
        std::printf("dR=%ld dG=%ld dB=%ld\n", sr - br, sg - bg, sb - bb);
    }

    // 暗角空间分布：中心 vs 四角
    {
        RetouchParams p; p.vignette = 0.8;
        const QImage out = RetouchEngine::apply(src, p);
        const int cx = out.width() / 2, cy = out.height() / 2;
        std::printf("VIGNETTE center=%d corner=%d srcCenter=%d srcCorner=%d\n",
                    qRed(out.pixel(cx, cy)), qRed(out.pixel(1, 1)),
                    qRed(src.pixel(cx, cy)), qRed(src.pixel(1, 1)));
    }
    // 高光/阴影空间分布：亮块 vs 暗块
    {
        RetouchParams p; p.highlights = -0.8;
        const QImage out = RetouchEngine::apply(src, p);
        std::printf("HIGHLIGHTS brightBlock src=%d out=%d darkBlock src=%d out=%d\n",
                    qRed(src.pixel(100, 25)), qRed(out.pixel(100, 25)),
                    qRed(src.pixel(25, 70)), qRed(out.pixel(25, 70)));
        RetouchParams p2; p2.shadows = 0.8;
        const QImage out2 = RetouchEngine::apply(src, p2);
        std::printf("SHADOWS    darkBlock src=%d out=%d brightBlock src=%d out=%d\n",
                    qRed(src.pixel(25, 70)), qRed(out2.pixel(25, 70)),
                    qRed(src.pixel(100, 25)), qRed(out2.pixel(100, 25)));
    }
    return 0;
}
