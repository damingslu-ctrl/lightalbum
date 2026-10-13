#include "LumenEngine.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <functional>
#include <random>
#include <QtConcurrent>   // blockingMap（QtConcurrentRun 头不含 map 族，需总括头）
#include <QThread>

// ============================================================ 预设
LumenPreset LumenPreset::byName(const QString& name)
{
    LumenPreset p;
    if (name == QLatin1String("soft"))
        p = {0.5, 0.5, 0.6, 0.6, 0.6, 0.5, 0.6, 0.6, 0.7};
    else if (name == QLatin1String("strong"))
        p = {1.4, 1.3, 1.3, 1.35, 1.3, 1.3, 1.3, 1.3, 1.3};
    return p;   // auto / 未知 → 全 1.0
}

namespace {

constexpr double P_SHADES = 6.0;

// 逐行块并行执行器：把 [0,h) 切成行块用 QtConcurrent::blockingMap 分派。
// body(y0,y1) 负责行区间 [y0,y1)，仅用于"每输出像素只读自身/邻域（只读源）、只写自身像素"的纯映射循环。
// LA_LUMEN_THREADS=1 -> 强制串行（金样门串行/并行逐字节比对基准）；未设/坏值 -> idealThreadCount。
static void lumenParallelRows(int h, const std::function<void(int, int)>& body)
{
    int nt = 0;
    const QByteArray tv = qgetenv("LA_LUMEN_THREADS");
    if (!tv.isEmpty()) nt = tv.toInt();
    if (nt <= 0) nt = QThread::idealThreadCount();
    if (nt <= 1 || h <= 1) { body(0, h); return; }
    const int nchunk = std::max(1, std::min(h, nt * 4));
    const int rows = (h + nchunk - 1) / nchunk;
    QVector<QPair<int, int>> blocks;
    for (int y = 0; y < h; y += rows) blocks.append(qMakePair(y, qMin(y + rows, h)));
    QtConcurrent::blockingMap(blocks, [&](const QPair<int, int>& b) { body(b.first, b.second); });
}

inline int clamp255(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }
inline int chopMul(int a, int b) { return (a * b) / 255; }   // ImageChops.multiply

QImage grayLike(const QImage& a) { return QImage(a.size(), QImage::Format_Grayscale8); }

// Grayscale8 二元操作：0=mul 1=add 2=sub
QImage chopGray(const QImage& a, const QImage& b, int op)
{
    QImage out = grayLike(a);
    out.scanLine(0);                       // 单线程段强制 detach
    lumenParallelRows(a.height(), [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const uchar* pa = a.constScanLine(y);
            const uchar* pb = b.constScanLine(y);
            uchar* po = out.scanLine(y);
            for (int x = 0; x < a.width(); ++x) {
                int r = 0;
                if (op == 0) r = chopMul(pa[x], pb[x]);
                else if (op == 1) r = std::min(255, pa[x] + pb[x]);
                else r = std::max(0, int(pa[x]) - int(pb[x]));
                po[x] = uchar(r);
            }
        }
    });
    return out;
}

QImage invertGray(const QImage& a)
{
    QImage out = grayLike(a);
    out.scanLine(0);                       // 单线程段强制 detach
    lumenParallelRows(a.height(), [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const uchar* pa = a.constScanLine(y);
            uchar* po = out.scanLine(y);
            for (int x = 0; x < a.width(); ++x) po[x] = uchar(255 - pa[x]);
        }
    });
    return out;
}

QImage pointGray(const QImage& a, const int lut[256])
{
    QImage out = grayLike(a);
    out.scanLine(0);                       // 单线程段强制 detach
    lumenParallelRows(a.height(), [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const uchar* pa = a.constScanLine(y);
            uchar* po = out.scanLine(y);
            for (int x = 0; x < a.width(); ++x) po[x] = uchar(lut[pa[x]]);
        }
    });
    return out;
}

// Grayscale8 3×3 中值
QImage median3G(const QImage& src)
{
    QImage out = grayLike(src);
    const int w = src.width(), h = src.height();
    for (int y = 0; y < h; ++y) {
        const uchar* s = src.constScanLine(y);
        uchar* d = out.scanLine(y);
        for (int x = 0; x < w; ++x) {
            int win[9], n = 0;
            for (int dy = -1; dy <= 1; ++dy) {
                const int yy = std::clamp(y + dy, 0, h - 1);
                const uchar* row = src.constScanLine(yy);
                for (int dx = -1; dx <= 1; ++dx)
                    win[n++] = row[std::clamp(x + dx, 0, w - 1)];
            }
            std::sort(win, win + 9);
            d[x] = uchar(win[4]);
        }
    }
    return out;
}

// Grayscale8 SMOOTH 核
QImage smoothG(const QImage& src)
{
    static const int K[3][3] = {{1, 2, 1}, {2, 4, 2}, {1, 2, 1}};
    QImage out = grayLike(src);
    const int w = src.width(), h = src.height();
    for (int y = 0; y < h; ++y) {
        uchar* d = out.scanLine(y);
        for (int x = 0; x < w; ++x) {
            int acc = 0;
            for (int dy = -1; dy <= 1; ++dy) {
                const int yy = std::clamp(y + dy, 0, h - 1);
                const uchar* row = src.constScanLine(yy);
                for (int dx = -1; dx <= 1; ++dx)
                    acc += row[std::clamp(x + dx, 0, w - 1)] * K[dy + 1][dx + 1];
            }
            d[x] = uchar(clamp255(acc / 16));
        }
    }
    return out;
}

QImage pointRgb(const QImage& src, const int lut[256])
{
    QImage img = src.convertToFormat(QImage::Format_RGB32);
    img.scanLine(0);                       // 单线程段强制 detach（就地写）
    lumenParallelRows(img.height(), [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            QRgb* d = reinterpret_cast<QRgb*>(img.scanLine(y));
            for (int x = 0; x < img.width(); ++x)
                d[x] = qRgb(lut[qRed(d[x])], lut[qGreen(d[x])], lut[qBlue(d[x])]);
        }
    });
    return img;
}

QVector<int> hist8(const QImage& g8)
{
    QVector<int> h(256, 0);
    for (int y = 0; y < g8.height(); ++y) {
        const uchar* p = g8.constScanLine(y);
        for (int x = 0; x < g8.width(); ++x) ++h[p[x]];
    }
    return h;
}

struct HpStats { double mean = 0, median = 0; };

HpStats hpStats(const QImage& rgb, double radius)
{
    HpStats out;
    const QImage g = LumenEngine::toL8(rgb);
    const QImage small = LumenEngine::boxDownscale(g, 256);
    const QImage bl = LumenEngine::LumenEngine::gaussianBlur(small, radius);
    const long tot = std::max<long>(1, small.width() * small.height());
    QVector<int> h(256, 0);
    for (int y = 0; y < small.height(); ++y) {
        const uchar* a = small.constScanLine(y);
        const uchar* b = bl.constScanLine(y);
        for (int x = 0; x < small.width(); ++x) ++h[std::abs(int(a[x]) - int(b[x]))];
    }
    double sum = 0;
    long acc = 0;
    for (int i = 0; i < 256; ++i) {
        sum += double(i) * h[i];
        if (out.median == 0) {
            acc += h[i];
            if (acc >= tot / 2.0) out.median = i;
        }
    }
    out.mean = sum / tot / 255.0;
    return out;
}

} // namespace

// ============================================================ PIL 基础设施
int LumenEngine::toL(int r, int g, int b)
{
    return (r * 19595 + g * 38470 + b * 7471 + 0x8000) >> 16;
}

QImage LumenEngine::toL8(const QImage& rgb)
{
    QImage src = rgb.convertToFormat(QImage::Format_RGB32);
    QImage out(src.size(), QImage::Format_Grayscale8);
    out.scanLine(0);                       // 单线程段强制 detach
    lumenParallelRows(src.height(), [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const QRgb* s = reinterpret_cast<const QRgb*>(src.constScanLine(y));
            uchar* d = out.scanLine(y);
            for (int x = 0; x < src.width(); ++x)
                d[x] = uchar(toL(qRed(s[x]), qGreen(s[x]), qBlue(s[x])));
        }
    });
    return out;
}

void LumenEngine::LumenEngine::rgb2hsv(const QImage& rgb, QImage* H, QImage* S, QImage* V)
{
    QImage src = rgb.convertToFormat(QImage::Format_RGB32);
    *H = QImage(src.size(), QImage::Format_Grayscale8);
    *S = QImage(src.size(), QImage::Format_Grayscale8);
    *V = QImage(src.size(), QImage::Format_Grayscale8);
    H->scanLine(0);                        // 单线程段强制 detach
    S->scanLine(0);
    V->scanLine(0);
    lumenParallelRows(src.height(), [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const QRgb* s = reinterpret_cast<const QRgb*>(src.constScanLine(y));
            uchar* ph = H->scanLine(y);
            uchar* ps = S->scanLine(y);
            uchar* pv = V->scanLine(y);
            for (int x = 0; x < src.width(); ++x) {
                const int r = qRed(s[x]), g = qGreen(s[x]), b = qBlue(s[x]);
                const int maxc = std::max({r, g, b});
                const int minc = std::min({r, g, b});
                const int d = maxc - minc;
                int h = 0, sat = 0;
                const int v = maxc;
                if (maxc > 0) sat = (d * 255) / maxc;
                if (d != 0) {
                    if (r == maxc)      h = (43 * (g - b)) / d;
                    else if (g == maxc) h = 85 + (43 * (b - r)) / d;
                    else                h = 170 + (43 * (r - g)) / d;
                    if (h < 0) h += 255;
                    h = clamp255(h);
                }
                ph[x] = uchar(h); ps[x] = uchar(sat); pv[x] = uchar(v);
            }
        }
    });
}

QImage LumenEngine::LumenEngine::hsv2rgb(const QImage& h, const QImage& s, const QImage& v)
{
    QImage out(h.size(), QImage::Format_RGB32);
    out.scanLine(0);                       // 单线程段强制 detach
    lumenParallelRows(h.height(), [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const uchar* ph = h.constScanLine(y);
            const uchar* ps = s.constScanLine(y);
            const uchar* pv = v.constScanLine(y);
            QRgb* d = reinterpret_cast<QRgb*>(out.scanLine(y));
            for (int x = 0; x < h.width(); ++x) {
                if (ps[x] == 0) {
                    const int vv = pv[x];
                    d[x] = qRgb(vv, vv, vv);
                    continue;
                }
                const double H = ph[x], S = ps[x] / 255.0, V = pv[x] / 255.0;
                double h6 = (H / 255.0) * 6.0;
                int i = int(std::floor(h6));
                if (i > 5) i = 5;
                const double f = h6 - i;
                const double p = V * (1 - S);
                const double q = V * (1 - S * f);
                const double t = V * (1 - S * (1 - f));
                double rf = 0, gf = 0, bf = 0;
                switch (i) {
                    case 0: rf = V; gf = t; bf = p; break;
                    case 1: rf = q; gf = V; bf = p; break;
                    case 2: rf = p; gf = V; bf = t; break;
                    case 3: rf = p; gf = q; bf = V; break;
                    case 4: rf = t; gf = p; bf = V; break;
                    default: rf = V; gf = p; bf = q; break;
                }
                d[x] = qRgb(clamp255(int(rf * 255.0 + 0.5)),
                            clamp255(int(gf * 255.0 + 0.5)),
                            clamp255(int(bf * 255.0 + 0.5)));
            }
        }
    });
    return out;
}

QImage LumenEngine::LumenEngine::gaussianBlur(const QImage& src, double radius)
{
    if (radius <= 0 || src.isNull()) return src.copy();
    const bool gray = src.format() == QImage::Format_Grayscale8;
    QImage img = gray ? src.copy() : src.convertToFormat(QImage::Format_RGB32);
    const int w = img.width(), h = img.height();
    const int ch = gray ? 1 : 3;
    const int kr = std::max(1, int(std::ceil(radius * 3.0)));
    QVector<double> kern(2 * kr + 1);
    double ksum = 0;
    for (int i = -kr; i <= kr; ++i) {
        const double e = std::exp(-(double(i) * i) / (2.0 * radius * radius));
        kern[i + kr] = e; ksum += e;
    }
    for (double& k : kern) k /= ksum;

    QImage tmp(img.size(), img.format());
    tmp.scanLine(0);   // 单线程段完成 detach；并行体只写各自行
    img.scanLine(0);
    lumenParallelRows(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const uchar* srow = img.constScanLine(y);
            uchar* drow = tmp.scanLine(y);
            for (int x = 0; x < w; ++x) {
                double acc[3] = {0, 0, 0};
                for (int i = -kr; i <= kr; ++i) {
                    const int xx = std::clamp(x + i, 0, w - 1);
                    const double kw = kern[i + kr];
                    if (ch == 1) acc[0] += srow[xx] * kw;
                    else {
                        const QRgb px = reinterpret_cast<const QRgb*>(srow)[xx];
                        acc[0] += qRed(px) * kw; acc[1] += qGreen(px) * kw; acc[2] += qBlue(px) * kw;
                    }
                }
                if (ch == 1) drow[x] = uchar(clamp255(int(acc[0] + 0.5)));
                else reinterpret_cast<QRgb*>(drow)[x] =
                    qRgb(clamp255(int(acc[0] + 0.5)), clamp255(int(acc[1] + 0.5)), clamp255(int(acc[2] + 0.5)));
            }
        }
    });
    lumenParallelRows(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            uchar* drow = img.scanLine(y);
            for (int x = 0; x < w; ++x) {
                double acc[3] = {0, 0, 0};
                for (int i = -kr; i <= kr; ++i) {
                    const int yy = std::clamp(y + i, 0, h - 1);
                    const double kw = kern[i + kr];
                    const uchar* srow = tmp.constScanLine(yy);
                    if (ch == 1) acc[0] += srow[x] * kw;
                    else {
                        const QRgb px = reinterpret_cast<const QRgb*>(srow)[x];
                        acc[0] += qRed(px) * kw; acc[1] += qGreen(px) * kw; acc[2] += qBlue(px) * kw;
                    }
                }
                if (ch == 1) drow[x] = uchar(clamp255(int(acc[0] + 0.5)));
                else reinterpret_cast<QRgb*>(drow)[x] =
                    qRgb(clamp255(int(acc[0] + 0.5)), clamp255(int(acc[1] + 0.5)), clamp255(int(acc[2] + 0.5)));
            }
        }
    });
    return img;
}

QImage LumenEngine::LumenEngine::unsharpMask(const QImage& src, double radius, int percent, int threshold)
{
    QImage img = src.convertToFormat(QImage::Format_RGB32);
    const QImage blur = LumenEngine::gaussianBlur(img, radius);
    const double amt = percent / 100.0;
    img.scanLine(0);                       // 单线程段强制 detach
    lumenParallelRows(img.height(), [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const QRgb* sb = reinterpret_cast<const QRgb*>(blur.constScanLine(y));
            QRgb* d = reinterpret_cast<QRgb*>(img.scanLine(y));
            for (int x = 0; x < img.width(); ++x) {
                int c[3] = {qRed(d[x]), qGreen(d[x]), qBlue(d[x])};
                const int b[3] = {qRed(sb[x]), qGreen(sb[x]), qBlue(sb[x])};
                for (int k = 0; k < 3; ++k) {
                    const int diff = c[k] - b[k];
                    if (std::abs(diff) > threshold)
                        c[k] = clamp255(int(std::lround(c[k] + diff * amt)));
                }
                d[x] = qRgb(c[0], c[1], c[2]);
            }
        }
    });
    return img;
}

QImage LumenEngine::median3(const QImage& src)
{
    QImage img = src.convertToFormat(QImage::Format_RGB32);
    QImage out(img.size(), img.format());
    const int w = img.width(), h = img.height();
    for (int y = 0; y < h; ++y) {
        QRgb* d = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < w; ++x) {
            int r[9], g[9], b[9], n = 0;
            for (int dy = -1; dy <= 1; ++dy) {
                const int yy = std::clamp(y + dy, 0, h - 1);
                const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(yy));
                for (int dx = -1; dx <= 1; ++dx) {
                    const QRgb px = row[std::clamp(x + dx, 0, w - 1)];
                    r[n] = qRed(px); g[n] = qGreen(px); b[n] = qBlue(px); ++n;
                }
            }
            std::sort(r, r + 9); std::sort(g, g + 9); std::sort(b, b + 9);
            d[x] = qRgb(r[4], g[4], b[4]);
        }
    }
    return out;
}

QImage LumenEngine::smoothFilter(const QImage& src)
{
    QImage img = src.convertToFormat(QImage::Format_RGB32);
    QImage out(img.size(), img.format());
    const int w = img.width(), h = img.height();
    static const int K[3][3] = {{1, 2, 1}, {2, 4, 2}, {1, 2, 1}};
    for (int y = 0; y < h; ++y) {
        QRgb* d = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < w; ++x) {
            int acc[3] = {0, 0, 0};
            for (int dy = -1; dy <= 1; ++dy) {
                const int yy = std::clamp(y + dy, 0, h - 1);
                const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(yy));
                for (int dx = -1; dx <= 1; ++dx) {
                    const QRgb px = row[std::clamp(x + dx, 0, w - 1)];
                    const int kw = K[dy + 1][dx + 1];
                    acc[0] += qRed(px) * kw; acc[1] += qGreen(px) * kw; acc[2] += qBlue(px) * kw;
                }
            }
            d[x] = qRgb(clamp255(acc[0] / 16), clamp255(acc[1] / 16), clamp255(acc[2] / 16));
        }
    }
    return out;
}

namespace {
// 精确面积加权重采样到指定尺寸（分数覆盖、保均值；用于缩略统计，避免采样器偏差）
QImage areaResample(const QImage& src, int tw, int th)
{
    const bool gray = src.format() == QImage::Format_Grayscale8;
    const int w = src.width(), h = src.height();
    QImage out(tw, th, src.format());
    const double sx = double(w) / tw, sy = double(h) / th;
    for (int y = 0; y < th; ++y) {
        const double fy0 = y * sy, fy1 = (y + 1) * sy;
        const int iy0 = int(fy0), iy1 = std::min(h, int(std::ceil(fy1)));
        for (int x = 0; x < tw; ++x) {
            const double fx0 = x * sx, fx1 = (x + 1) * sx;
            const int ix0 = int(fx0), ix1 = std::min(w, int(std::ceil(fx1)));
            double acc[3] = {0, 0, 0}, wsum = 0;
            for (int yy = iy0; yy < iy1; ++yy) {
                const double wy = std::min(fy1, yy + 1.0) - std::max(fy0, double(yy));
                if (wy <= 0) continue;
                const uchar* row = src.constScanLine(yy);
                for (int xx = ix0; xx < ix1; ++xx) {
                    const double wx = std::min(fx1, xx + 1.0) - std::max(fx0, double(xx));
                    if (wx <= 0) continue;
                    const double wt = wx * wy;
                    if (gray) acc[0] += row[xx] * wt;
                    else {
                        const QRgb px = reinterpret_cast<const QRgb*>(row)[xx];
                        acc[0] += qRed(px) * wt; acc[1] += qGreen(px) * wt; acc[2] += qBlue(px) * wt;
                    }
                    wsum += wt;
                }
            }
            if (wsum <= 0) wsum = 1;
            if (gray) out.scanLine(y)[x] = uchar(clamp255(int(acc[0] / wsum + 0.5)));
            else reinterpret_cast<QRgb*>(out.scanLine(y))[x] =
                qRgb(clamp255(int(acc[0] / wsum + 0.5)), clamp255(int(acc[1] / wsum + 0.5)),
                     clamp255(int(acc[2] / wsum + 0.5)));
        }
    }
    return out;
}
} // namespace

QImage LumenEngine::boxDownscale(const QImage& src, int maxEdge)
{
    if (src.isNull()) return src;
    QImage img = src.format() == QImage::Format_Grayscale8
                     ? src.copy() : src.convertToFormat(QImage::Format_RGB32);
    const bool gray = img.format() == QImage::Format_Grayscale8;
    const int w = img.width(), h = img.height();
    const int m = std::max(w, h);
    if (m <= maxEdge) return img;
    const int nw = std::max(1, w * maxEdge / m), nh = std::max(1, h * maxEdge / m);
    // 精确面积加权重采样（分数覆盖，无双线性锯齿；对应 PIL thumbnail 的低通角色）
    QImage out(nw, nh, img.format());
    const double sx = double(w) / nw, sy = double(h) / nh;
    for (int y = 0; y < nh; ++y) {
        const double fy0 = y * sy, fy1 = (y + 1) * sy;
        const int iy0 = int(fy0), iy1 = std::min(h, int(std::ceil(fy1)));
        for (int x = 0; x < nw; ++x) {
            const double fx0 = x * sx, fx1 = (x + 1) * sx;
            const int ix0 = int(fx0), ix1 = std::min(w, int(std::ceil(fx1)));
            double acc[3] = {0, 0, 0}, wsum = 0;
            for (int yy = iy0; yy < iy1; ++yy) {
                const double wy = std::min(fy1, yy + 1.0) - std::max(fy0, double(yy));
                if (wy <= 0) continue;
                const uchar* row = img.constScanLine(yy);
                for (int xx = ix0; xx < ix1; ++xx) {
                    const double wx = std::min(fx1, xx + 1.0) - std::max(fx0, double(xx));
                    if (wx <= 0) continue;
                    const double wt = wx * wy;
                    if (gray) { acc[0] += row[xx] * wt; }
                    else {
                        const QRgb px = reinterpret_cast<const QRgb*>(row)[xx];
                        acc[0] += qRed(px) * wt; acc[1] += qGreen(px) * wt; acc[2] += qBlue(px) * wt;
                    }
                    wsum += wt;
                }
            }
            if (wsum <= 0) wsum = 1;
            if (gray) out.scanLine(y)[x] = uchar(clamp255(int(acc[0] / wsum + 0.5)));
            else reinterpret_cast<QRgb*>(out.scanLine(y))[x] =
                qRgb(clamp255(int(acc[0] / wsum + 0.5)), clamp255(int(acc[1] / wsum + 0.5)),
                     clamp255(int(acc[2] / wsum + 0.5)));
        }
    }
    return out;
}

QImage LumenEngine::enhanceBrightness(const QImage& rgb, double f)
{
    QImage img = rgb.convertToFormat(QImage::Format_RGB32);
    img.scanLine(0);                       // 单线程段强制 detach
    lumenParallelRows(img.height(), [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            QRgb* d = reinterpret_cast<QRgb*>(img.scanLine(y));
            for (int x = 0; x < img.width(); ++x)
                d[x] = qRgb(clamp255(int(qRed(d[x]) * f + 0.5)),
                            clamp255(int(qGreen(d[x]) * f + 0.5)),
                            clamp255(int(qBlue(d[x]) * f + 0.5)));
        }
    });
    return img;
}

QImage LumenEngine::enhanceColor(const QImage& rgb, double f)
{
    const QImage gray = toL8(rgb);
    QImage img = rgb.convertToFormat(QImage::Format_RGB32);
    img.scanLine(0);                       // 单线程段强制 detach
    lumenParallelRows(img.height(), [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const uchar* gl = gray.constScanLine(y);
            QRgb* d = reinterpret_cast<QRgb*>(img.scanLine(y));
            for (int x = 0; x < img.width(); ++x) {
                const double g = gl[x];
                d[x] = qRgb(clamp255(int(qRed(d[x]) * f + g * (1 - f) + 0.5)),
                            clamp255(int(qGreen(d[x]) * f + g * (1 - f) + 0.5)),
                            clamp255(int(qBlue(d[x]) * f + g * (1 - f) + 0.5)));
            }
        }
    });
    return img;
}

// ============================================================ 测量
LumenStats LumenEngine::measureStats(const QImage& rgb)
{
    LumenStats st;
    const QImage small = boxDownscale(rgb, 256);
    QImage hI, sI, vI;
    LumenEngine::rgb2hsv(small, &hI, &sI, &vI);
    const QVector<int> vs = hist8(vI);
    const QVector<int> ss = hist8(sI);
    long totv = 0, tots = 0;
    for (int i = 0; i < 256; ++i) { totv += vs[i]; tots += ss[i]; }
    totv = std::max<long>(1, totv); tots = std::max<long>(1, tots);
    double sum = 0, sum2 = 0, sums = 0;
    long hi = 0, lo = 0;
    for (int i = 0; i < 256; ++i) {
        const double v = i / 255.0;
        sum += v * vs[i];
        sum2 += v * v * vs[i];
        sums += v * ss[i];
        if (i >= 250) hi += vs[i];
        if (i <= 5) lo += vs[i];
    }
    st.lum = sum / totv;
    st.lum_std = std::sqrt(std::max(0.0, sum2 / totv - st.lum * st.lum));
    st.sat = sums / tots;
    st.hi = double(hi) / totv;
    st.lo = double(lo) / totv;
    return st;
}

LumenMeasure LumenEngine::measureExtra(const QImage& rgb)
{
    LumenMeasure mx;
    const HpStats hp1 = hpStats(rgb, 1.0);
    const HpStats hp6 = hpStats(rgb, 6.0);
    const QImage small = boxDownscale(rgb, 256);
    QImage hI, sI, vI;
    LumenEngine::rgb2hsv(small, &hI, &sI, &vI);
    const QVector<int> vh = hist8(vI);
    const long tot = std::max<long>(1, small.width() * small.height());
    auto pct = [&](double p) -> double {
        long acc = 0;
        const long need = long(tot * p);
        for (int i = 0; i < 256; ++i) {
            acc += vh[i];
            if (acc >= need) return i / 255.0;
        }
        return 1.0;
    };
    mx.p1 = pct(0.01);
    mx.p99 = pct(0.99);
    mx.dr = std::max(0.0, mx.p99 - mx.p1);
    long ch[3] = {0, 0, 0};
    double sumBs = 0; long nBs = 0;
    for (int y = 0; y < small.height(); ++y) {
        const QRgb* s = reinterpret_cast<const QRgb*>(small.constScanLine(y));
        const uchar* sv = vI.constScanLine(y);
        const uchar* ss = sI.constScanLine(y);
        for (int x = 0; x < small.width(); ++x) {
            ch[0] += qRed(s[x]); ch[1] += qGreen(s[x]); ch[2] += qBlue(s[x]);
            if (sv[x] >= 200) { sumBs += ss[x]; ++nBs; }
        }
    }
    double means[3];
    for (int c = 0; c < 3; ++c) means[c] = double(ch[c]) / tot;
    const double overall = std::max(1e-6, (means[0] + means[1] + means[2]) / 3.0);
    const double meanBs = nBs ? sumBs / nBs / 255.0 : 0.0;
    const double wash = std::max(0.0, (0.12 - meanBs) / 0.12);
    const double flat = std::max(0.0, (0.16 - mx.dr) / 0.16);
    const double bright = std::max(0.0, (overall / 255.0 - 0.52) / 0.40);
    mx.haze = std::clamp(0.5 * wash + 0.3 * flat + 0.3 * bright, 0.0, 1.0);
    mx.lc = hp6.mean;
    mx.noise = hp1.median / 255.0;   // Python: med/255.0（此前漏除 → 255 倍虚高误触发降噪）
    mx.sharp = hp1.mean;
    mx.cast = (std::max({means[0], means[1], means[2]}) - std::min({means[0], means[1], means[2]})) / overall;
    return mx;
}

// ============================================================ 算子
QImage LumenEngine::whiteBalance(const QImage& rgb, double k, QVector<double>* gains)
{
    if (gains) gains->clear();
    QImage img = rgb.convertToFormat(QImage::Format_RGB32);
    const QImage small = boxDownscale(img, 160);
    auto srgb2lin = [](double c) {
        return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    auto lin2srgb = [](double l) {
        return l <= 0.0031308 ? 12.92 * l : 1.055 * std::pow(l, 1.0 / 2.4) - 0.055;
    };
    const int n = std::max(1, small.width() * small.height());
    double ks[3];
    for (int ch = 0; ch < 3; ++ch) {
        double acc = 0;
        for (int y = 0; y < small.height(); ++y) {
            const QRgb* s = reinterpret_cast<const QRgb*>(small.constScanLine(y));
            for (int x = 0; x < small.width(); ++x) {
                const int c = ch == 0 ? qRed(s[x]) : (ch == 1 ? qGreen(s[x]) : qBlue(s[x]));
                acc += std::pow(srgb2lin(c / 255.0), P_SHADES);
            }
        }
        ks[ch] = std::pow(acc / n, 1.0 / P_SHADES);
    }
    const double base = (ks[0] + ks[1] + ks[2]) / 3.0;
    if (base <= 1e-9) return img;
    const double lo = (k <= 1) ? -0.06 * k : -0.08;
    const double hiLim = (k <= 1) ? 0.06 * k : 0.10;
    double gv[3];
    for (int ch = 0; ch < 3; ++ch) {
        if (ks[ch] <= 1e-12) { gv[ch] = 1.0; continue; }
        gv[ch] = 1.0 + std::clamp((base / ks[ch] - 1.0) * k, lo, hiLim);
    }
    if (std::abs(gv[0] - 1.0) < 0.005 && std::abs(gv[1] - 1.0) < 0.005
        && std::abs(gv[2] - 1.0) < 0.005) return img;
    int luts[3][256];
    for (int ch = 0; ch < 3; ++ch)
        for (int v = 0; v < 256; ++v)
            luts[ch][v] = clamp255(int(std::lround(lin2srgb(
                std::clamp(srgb2lin(v / 255.0) * gv[ch], 0.0, 1.0)) * 255.0)));
    QImage out(img.size(), img.format());
    for (int y = 0; y < img.height(); ++y) {
        const QRgb* s = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        QRgb* d = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < img.width(); ++x)
            d[x] = qRgb(luts[0][qRed(s[x])], luts[1][qGreen(s[x])], luts[2][qBlue(s[x])]);
    }
    if (gains) for (int ch = 0; ch < 3; ++ch) gains->append(std::round(gv[ch] * 1000.0) / 1000.0);
    return out;
}

QImage LumenEngine::toneCurve(const QImage& rgb, const LumenStats& st, double k,
                              double* liftOut, double* recoverOut)
{
    if (liftOut) *liftOut = 0;
    if (recoverOut) *recoverOut = 0;
    const double lift = std::min(0.50, std::max(0.0, (st.lo - 0.02) * 4.0)) * k;
    const double rec  = std::min(0.35, std::max(0.0, (st.hi - 0.01) * 5.0)) * k;
    if (lift < 0.02 && rec < 0.02) return rgb.convertToFormat(QImage::Format_RGB32);

    QImage hI, sI, vI;
    LumenEngine::rgb2hsv(rgb, &hI, &sI, &vI);
    const double sc = std::min(1.0, 512.0 / std::max(1, std::max(rgb.width(), rgb.height())));
    QImage smImg = rgb;
    if (sc < 1.0)
        smImg = rgb.scaled(std::max(1, int(rgb.width() * sc)),
                           std::max(1, int(rgb.height() * sc)),
                           Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QImage hsI, ssI, vsI;
    LumenEngine::rgb2hsv(smImg, &hsI, &ssI, &vsI);
    Q_UNUSED(hsI);
    const double rs = std::max(3.0, std::min(smImg.width(), smImg.height()) / 40.0);
    const QImage mm = LumenEngine::gaussianBlur(vsI, rs);

    QImage outV = vI;
    if (lift >= 0.02) {
        int wLut[256], gLut[256], rampLut[256], liftLut[256];
        for (int x = 0; x < 256; ++x) {
            wLut[x] = clamp255(int(std::lround(255.0 * std::pow(std::max(0.0, (130.0 - x) / 130.0), 1.6))));
            gLut[x] = clamp255(int(std::lround(255.0 * (1.0 - 0.5 * (x / 255.0)))));
            rampLut[x] = std::min(255, int(qint64(x) * 255 / 24));
            liftLut[x] = int(x * lift);
        }
        QImage wm = LumenEngine::gaussianBlur(chopGray(pointGray(mm, wLut), pointGray(ssI, gLut), 0), rs);
        if (sc < 1.0)
            wm = wm.scaled(rgb.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        QImage addv = chopGray(chopGray(invertGray(vI), wm, 0), pointGray(vI, rampLut), 0);
        outV = chopGray(outV, pointGray(addv, liftLut), 1);
    }
    if (rec >= 0.02) {
        int w2Lut[256], capLut[256], recLut[256];
        for (int x = 0; x < 256; ++x) {
            w2Lut[x] = clamp255(int(std::lround(255.0 * std::pow(std::max(0.0, (x - 170.0) / 85.0), 1.6))));
            capLut[x] = std::min(x, 40);
            recLut[x] = int(x * rec);
        }
        QImage w2 = LumenEngine::gaussianBlur(pointGray(mm, w2Lut), rs);
        if (sc < 1.0)
            w2 = w2.scaled(rgb.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        QImage capped = pointGray(chopGray(vI, w2, 0), capLut);
        outV = chopGray(outV, pointGray(capped, recLut), 2);
    }
    if (liftOut) *liftOut = std::round(lift * 1000.0) / 1000.0;
    if (recoverOut) *recoverOut = std::round(rec * 1000.0) / 1000.0;
    return LumenEngine::hsv2rgb(hI, sI, outV);
}

QImage LumenEngine::autoLevels(const QImage& rgb, const LumenStats& st, double k, bool* applied)
{
    if (applied) *applied = false;
    if (st.lum_std > 0.17) return rgb.convertToFormat(QImage::Format_RGB32);
    const QVector<int> h = hist8(boxDownscale(toL8(rgb), 256));
    long tot = 0;
    for (int i = 0; i < 256; ++i) tot += h[i];
    tot = std::max<long>(1, tot);
    const int cut = std::max(1, int(tot * 0.005));
    int lo = 0, hi = 255;
    long acc = 0;
    for (int i = 0; i < 256; ++i) { acc += h[i]; if (acc >= cut) { lo = i; break; } }
    acc = 0;
    for (int i = 255; i >= 0; --i) { acc += h[i]; if (acc >= cut) { hi = i; break; } }
    if (hi - lo < 8) return rgb.convertToFormat(QImage::Format_RGB32);
    const double span = double(hi - lo);
    int lut[256];
    for (int v = 0; v < 256; ++v)
        lut[v] = clamp255(int(std::lround(6.0 + (v - lo) * 243.0 / span)));
    if (k < 1.0)
        for (int v = 0; v < 256; ++v) lut[v] = int(std::lround(v + (lut[v] - v) * k));
    if (applied) *applied = true;
    return pointRgb(rgb, lut);
}

QImage LumenEngine::brightness(const QImage& rgb, const LumenStats& st, double k, double* fOut)
{
    if (fOut) *fOut = 0;
    const double d = 0.46 - st.lum;
    if (std::abs(d) < 0.03) return rgb.convertToFormat(QImage::Format_RGB32);
    double f = 1.0 + d * 1.6 * k;
    if (f > 1.0) {
        QImage hI, sI, vI;
        LumenEngine::rgb2hsv(boxDownscale(rgb, 256), &hI, &sI, &vI);
        const QVector<int> vs = hist8(vI);
        long tot = 0;
        for (int i = 0; i < 256; ++i) tot += vs[i];
        tot = std::max<long>(1, tot);
        int p995 = 255;
        long acc = 0;
        for (int i = 0; i < 256; ++i) { acc += vs[i]; if (acc >= tot * 0.995) { p995 = i; break; } }
        f = std::min(f, std::max(1.0, 252.0 / std::max(1.0, double(p995))));
    }
    f = std::clamp(f, 0.60, 1.80);
    if (std::abs(f - 1.0) < 0.005) return rgb.convertToFormat(QImage::Format_RGB32);
    if (fOut) *fOut = std::round(f * 1000.0) / 1000.0;
    return enhanceBrightness(rgb, f);
}

QImage LumenEngine::contrast(const QImage& rgb, const LumenStats& st, double k, double* fOut)
{
    if (fOut) *fOut = 0;
    const double d = 0.19 - st.lum_std;
    if (std::abs(d) < 0.02) return rgb.convertToFormat(QImage::Format_RGB32);
    const double f = std::clamp(1.0 + d * 1.1 * k, 0.92, 1.32);
    const double mid = st.lum * 255.0;
    int lut[256];
    for (int v = 0; v < 256; ++v) lut[v] = clamp255(int((v - mid) * f + mid + 0.5));
    if (fOut) *fOut = std::round(f * 1000.0) / 1000.0;
    return pointRgb(rgb, lut);
}

QImage LumenEngine::clarity(const QImage& rgb, const LumenMeasure& mx, double k, double* amtOut)
{
    if (amtOut) *amtOut = 0;
    if (mx.lc >= 0.045 || mx.lc < 0.006) return rgb.convertToFormat(QImage::Format_RGB32);
    const double amt = std::min(0.45, (0.045 - mx.lc) * 8.0) * k;
    if (amt < 0.05) return rgb.convertToFormat(QImage::Format_RGB32);
    QImage hI, sI, vI;
    LumenEngine::rgb2hsv(rgb, &hI, &sI, &vI);
    // V 通道 USM（借 RGB32 通道等价实现）
    QImage vRgb(vI.size(), QImage::Format_RGB32);
    for (int y = 0; y < vI.height(); ++y) {
        const uchar* g = vI.constScanLine(y);
        QRgb* d = reinterpret_cast<QRgb*>(vRgb.scanLine(y));
        for (int x = 0; x < vI.width(); ++x) d[x] = qRgb(g[x], g[x], g[x]);
    }
    const QImage usm = LumenEngine::unsharpMask(vRgb, 6.0, int(std::lround(amt * 100)), 0);
    QImage v2(vI.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < vI.height(); ++y) {
        const QRgb* s = reinterpret_cast<const QRgb*>(usm.constScanLine(y));
        uchar* d = v2.scanLine(y);
        for (int x = 0; x < vI.width(); ++x) d[x] = uchar(qRed(s[x]));
    }
    if (amtOut) *amtOut = std::round(amt * 1000.0) / 1000.0;
    return LumenEngine::hsv2rgb(hI, sI, v2);
}

QImage LumenEngine::vibrance(const QImage& rgb, const LumenStats& st, double k, double* vOut)
{
    if (vOut) *vOut = 0;
    if (st.sat >= 0.30 || st.sat < 0.02) return rgb.convertToFormat(QImage::Format_RGB32);
    const double v = std::min(0.45, (0.30 - st.sat) * 1.6) * k;
    if (v < 0.05) return rgb.convertToFormat(QImage::Format_RGB32);
    QImage hI, sI, vI;
    LumenEngine::rgb2hsv(rgb, &hI, &sI, &vI);
    int lut[256];
    for (int x = 0; x < 256; ++x)
        lut[x] = std::min(255, int(std::lround(x * (1.0 + v * (1.0 - x / 255.0)))));
    if (vOut) *vOut = std::round(v * 1000.0) / 1000.0;
    return LumenEngine::hsv2rgb(hI, pointGray(sI, lut), vI);
}

QImage LumenEngine::denoise(const QImage& rgb, int level)
{
    if (level <= 0) return rgb.convertToFormat(QImage::Format_RGB32);
    QImage img = rgb.convertToFormat(QImage::Format_RGB32);
    const int w = img.width(), h = img.height();
    QImage yI(w, h, QImage::Format_Grayscale8), cbI(w, h, QImage::Format_Grayscale8),
           crI(w, h, QImage::Format_Grayscale8);
    for (int yy = 0; yy < h; ++yy) {
        const QRgb* s = reinterpret_cast<const QRgb*>(img.constScanLine(yy));
        uchar* py = yI.scanLine(yy);
        uchar* pcb = cbI.scanLine(yy);
        uchar* pcr = crI.scanLine(yy);
        for (int x = 0; x < w; ++x) {
            const long long r = qRed(s[x]), g = qGreen(s[x]), b = qBlue(s[x]);
            py[x]  = uchar(clamp255(int((299 * r + 587 * g + 114 * b) / 1000)));
            pcb[x] = uchar(clamp255(int((-168736 * r - 331264 * g + 500000 * b + 128000000LL) / 1000000)));
            pcr[x] = uchar(clamp255(int((500000 * r - 418688 * g - 81312 * b + 128000000LL) / 1000000)));
        }
    }
    if (level == 1) {
        yI = LumenEngine::gaussianBlur(yI, 1.0);
        cbI = LumenEngine::gaussianBlur(cbI, 1.2);
        crI = LumenEngine::gaussianBlur(crI, 1.2);
    } else {
        yI = smoothG(median3G(yI));
        cbI = LumenEngine::gaussianBlur(median3G(cbI), 1.6);
        crI = LumenEngine::gaussianBlur(median3G(crI), 1.6);
    }
    QImage out(w, h, QImage::Format_RGB32);
    for (int yy = 0; yy < h; ++yy) {
        const uchar* py = yI.constScanLine(yy);
        const uchar* pcb = cbI.constScanLine(yy);
        const uchar* pcr = crI.constScanLine(yy);
        QRgb* d = reinterpret_cast<QRgb*>(out.scanLine(yy));
        for (int x = 0; x < w; ++x) {
            const double Y = py[x], Cb = pcb[x] - 128.0, Cr = pcr[x] - 128.0;
            // PIL ycbcr2rgb 同为截断
            d[x] = qRgb(clamp255(int(Y + 1.402 * Cr)),
                        clamp255(int(Y - 0.344136 * Cb - 0.714136 * Cr)),
                        clamp255(int(Y + 1.772 * Cb)));
        }
    }
    return out;
}

QImage LumenEngine::sharpen(const QImage& rgb, double k, double noise, bool portrait)
{
    const int radius = std::max(rgb.width(), rgb.height()) >= 3000 ? 2 : 1;   // 半径整数量化（冒烟图均 <3000 → 1）
    Q_UNUSED(radius);
    const double r = std::max(rgb.width(), rgb.height()) >= 3000 ? 1.2 : 1.0;
    int percent = int(std::min(140.0, 70.0 * k));
    if (portrait) percent = int(percent * 0.7);
    const int threshold = noise >= 0.035 ? 5 : 2;
    return LumenEngine::unsharpMask(rgb, r, percent, threshold);
}

// ============================================================ PIL 合成基础设施（P5-2）
QImage LumenEngine::blend(const QImage& a, const QImage& b, double f)
{
    QImage ia = a.convertToFormat(QImage::Format_RGB32);
    QImage ib = b.convertToFormat(QImage::Format_RGB32);
    if (ia.size() != ib.size()) return ia;
    ia.scanLine(0);                        // 单线程段强制 detach
    lumenParallelRows(ia.height(), [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const QRgb* pa = reinterpret_cast<const QRgb*>(ia.constScanLine(y));
            const QRgb* pb = reinterpret_cast<const QRgb*>(ib.constScanLine(y));
            QRgb* d = reinterpret_cast<QRgb*>(ia.scanLine(y));
            for (int x = 0; x < ia.width(); ++x)
                d[x] = qRgb(clamp255(int(qRed(pa[x]) * (1 - f) + qRed(pb[x]) * f + 0.5)),
                            clamp255(int(qGreen(pa[x]) * (1 - f) + qGreen(pb[x]) * f + 0.5)),
                            clamp255(int(qBlue(pa[x]) * (1 - f) + qBlue(pb[x]) * f + 0.5)));
        }
    });
    return ia;
}

QImage LumenEngine::screen(const QImage& a, const QImage& b)
{
    QImage ia = a.convertToFormat(QImage::Format_RGB32);
    QImage ib = b.convertToFormat(QImage::Format_RGB32);
    if (ia.size() != ib.size()) return ia;
    ia.scanLine(0);                        // 单线程段强制 detach
    lumenParallelRows(ia.height(), [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const QRgb* pa = reinterpret_cast<const QRgb*>(ia.constScanLine(y));
            const QRgb* pb = reinterpret_cast<const QRgb*>(ib.constScanLine(y));
            QRgb* d = reinterpret_cast<QRgb*>(ia.scanLine(y));
            for (int x = 0; x < ia.width(); ++x)
                d[x] = qRgb(255 - ((255 - qRed(pa[x])) * (255 - qRed(pb[x]))) / 255,
                            255 - ((255 - qGreen(pa[x])) * (255 - qGreen(pb[x]))) / 255,
                            255 - ((255 - qBlue(pa[x])) * (255 - qBlue(pb[x]))) / 255);
        }
    });
    return ia;
}

QImage LumenEngine::composite(const QImage& a, const QImage& b, const QImage& mask)
{
    const QImage ia = a.convertToFormat(QImage::Format_RGB32);
    const QImage ib = b.convertToFormat(QImage::Format_RGB32);
    const QImage m = mask.convertToFormat(QImage::Format_Grayscale8);
    QImage out(ia.size(), QImage::Format_RGB32);
    out.scanLine(0);                       // 单线程段强制 detach
    lumenParallelRows(ia.height(), [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const QRgb* pa = reinterpret_cast<const QRgb*>(ia.constScanLine(y));
            const QRgb* pb = reinterpret_cast<const QRgb*>(ib.constScanLine(y));
            const uchar* pm = m.constScanLine(y);
            QRgb* d = reinterpret_cast<QRgb*>(out.scanLine(y));
            for (int x = 0; x < ia.width(); ++x) {
                const int w = pm[x];
                d[x] = qRgb(clamp255((qRed(pa[x]) * w + qRed(pb[x]) * (255 - w)) / 255),
                            clamp255((qGreen(pa[x]) * w + qGreen(pb[x]) * (255 - w)) / 255),
                            clamp255((qBlue(pa[x]) * w + qBlue(pb[x]) * (255 - w)) / 255));
            }
        }
    });
    return out;
}

QImage LumenEngine::LumenEngine::channelGain(const QImage& rgb, double gr, double gg, double gb)
{
    QImage img = rgb.convertToFormat(QImage::Format_RGB32);
    img.scanLine(0);                       // 单线程段强制 detach
    lumenParallelRows(img.height(), [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            QRgb* d = reinterpret_cast<QRgb*>(img.scanLine(y));
            for (int x = 0; x < img.width(); ++x)
                d[x] = qRgb(clamp255(int(qRed(d[x]) * gr + 0.5)),
                            clamp255(int(qGreen(d[x]) * gg + 0.5)),
                            clamp255(int(qBlue(d[x]) * gb + 0.5)));
        }
    });
    return img;
}

QImage LumenEngine::enhanceContrastPil(const QImage& rgb, double f)
{
    // PIL Contrast：blend(均值灰, img, f) → mean + (v-mean)*f（mean 取 L 通道均值）
    const QImage gray = toL8(rgb);
    long sum = 0, n = 0;
    for (int y = 0; y < gray.height(); ++y) {
        const uchar* p = gray.constScanLine(y);
        for (int x = 0; x < gray.width(); ++x) { sum += p[x]; ++n; }
    }
    const double mean = n ? double(sum) / n : 128.0;
    QImage img = rgb.convertToFormat(QImage::Format_RGB32);
    for (int y = 0; y < img.height(); ++y) {
        QRgb* d = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x)
            d[x] = qRgb(clamp255(int(mean + (qRed(d[x]) - mean) * f + 0.5)),
                        clamp255(int(mean + (qGreen(d[x]) - mean) * f + 0.5)),
                        clamp255(int(mean + (qBlue(d[x]) - mean) * f + 0.5)));
    }
    return img;
}

QImage LumenEngine::addGrain(const QImage& rgb, double amount)
{
    // PIL effect_noise(sigma=32) 近似：均值 128、σ=32 的高斯噪；随亮度耦合（三角窗）
    if (amount <= 0) return rgb.convertToFormat(QImage::Format_RGB32);
    QImage img = rgb.convertToFormat(QImage::Format_RGB32);
    const QImage gray = toL8(img);
    const double k = std::min(3.0, amount * 1.6);
    for (int y = 0; y < img.height(); ++y) {
        const uchar* g = gray.constScanLine(y);
        QRgb* d = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            // 高斯噪（Box-Muller，σ=32）
            static thread_local std::mt19937 gen(1234567);
            std::normal_distribution<double> nd(128.0, 32.0);
            const double nv = 128.0 + (nd(gen) - 128.0) * k;
            const double lw = 255.0 * (1.0 - std::abs(g[x] - 128) / 128.0);
            const double scaled = (nv * lw + (255.0 - lw) * 128.0) / 255.0;
            const int delta = int(scaled - 128.0 + 0.5);
            d[x] = qRgb(clamp255(qRed(d[x]) + delta),
                        clamp255(qGreen(d[x]) + delta),
                        clamp255(qBlue(d[x]) + delta));
        }
    }
    return img;
}

// ============================================================ P5-2 显式算子
QImage LumenEngine::filmStyle(const QImage& rgb, bool withGrain, double lift, double rolloff)
{
    QImage img = rgb.convertToFormat(QImage::Format_RGB32);
    // 1) 胶片影调 LUT：高光肩部滚降 + 抬黑场
    const double knee = 200.0;
    int lut[256];
    for (int v = 0; v < 256; ++v) {
        double v1 = double(v);
        if (v > knee) {
            const double t = (v - knee) / (255.0 - knee);
            const double y = t - rolloff * t * (1.0 - t);
            v1 = knee + y * (255.0 - knee);
        }
        const double lo = lift * 255.0;
        const double v2 = lo + v1 * (255.0 - lo) / 255.0;
        lut[v] = clamp255(int(std::lround(v2)));
    }
    img = pointRgb(img, lut);
    // 2) 分离色调：阴影冷(0.96,1.0,1.05) / 高光暖(1.05,1.0,0.95)，amount=0.55，balance=0.5
    const QImage l = toL8(img);
    const int bp = int(std::lround(0.5 * 255));   // 128
    int swLut[256], hwLut[256];
    for (int v = 0; v < 256; ++v) {
        swLut[v] = clamp255(255 - std::min(255, int(255.0 * v / bp)));
        hwLut[v] = clamp255(std::max(0, std::min(255, int(255.0 * (v - bp) / (255 - bp)))));
    }
    const double amount = 0.55;
    int amLut[256];
    for (int v = 0; v < 256; ++v) amLut[v] = int(v * amount);
    QImage sw = pointGray(l, swLut);
    QImage hw = pointGray(l, hwLut);
    sw = pointGray(sw, amLut);
    hw = pointGray(hw, amLut);
    img = composite(LumenEngine::channelGain(img, 0.96, 1.0, 1.05), img, sw);
    img = composite(LumenEngine::channelGain(img, 1.05, 1.0, 0.95), img, hw);
    // 3) 饱和度略降
    img = enhanceColor(img, 0.92);
    // 4) 颗粒（PRNG，金样比对不参与）
    if (withGrain) img = addGrain(img, 0.55);
    return img;
}

QImage LumenEngine::blackWhite(const QImage& rgb, double contrast)
{
    QImage img = rgb.convertToFormat(QImage::Format_RGB32);
    img.scanLine(0);                       // 单线程段强制 detach
    lumenParallelRows(img.height(), [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            QRgb* d = reinterpret_cast<QRgb*>(img.scanLine(y));
            for (int x = 0; x < img.width(); ++x) {
                const int g = clamp255(int(std::lround(qRed(d[x]) * 0.30
                                                      + qGreen(d[x]) * 0.59
                                                      + qBlue(d[x]) * 0.11)));
                d[x] = qRgb(g, g, g);
            }
        }
    });
    return enhanceContrastPil(img, contrast);
}

QImage LumenEngine::glowStyle(const QImage& rgb, double amount)
{
    const double rr = std::max(6.0, std::min(rgb.width(), rgb.height()) / 45.0);
    const QImage blur = enhanceBrightness(LumenEngine::gaussianBlur(rgb, rr), 1.12);
    const QImage scr = screen(rgb, blur);
    return blend(rgb, scr, std::max(0.0, std::min(0.9, amount)));
}

QImage LumenEngine::redeyeRemove(const QImage& rgb, int* pixels)
{
    if (pixels) *pixels = 0;
    QImage img = rgb.convertToFormat(QImage::Format_RGB32);
    const int w = img.width(), h = img.height();
    if (w < 32 || h < 32) return img;
    QVector<uchar> hit(w * h, 0);
    int cnt = 0;
    for (int y = 0; y < h; ++y) {
        const QRgb* s = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            const int r = qRed(s[x]), g = qGreen(s[x]), b = qBlue(s[x]);
            if (r < 120 || g > 110 || b > 110) continue;
            if (r < 2.2 * g || r < 2.2 * b) continue;
            hit[y * w + x] = 1; ++cnt;
        }
    }
    const long area = long(w) * h;
    if (cnt < 20 || cnt > 0.06 * area) return img;   // 面积闸门：太少无红眼 / 太多疑似整片红
    for (int y = 0; y < h; ++y) {
        QRgb* d = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            if (!hit[y * w + x]) continue;
            const int g = qGreen(d[x]), b = qBlue(d[x]);
            const int v = int(g * 0.35);
            d[x] = qRgb(v, v, int(b * 0.35));
        }
    }
    if (pixels) *pixels = cnt;
    return img;
}

QImage LumenEngine::vignetteCorrect(const QImage& rgb, double strength, double* kOut)
{
    if (kOut) *kOut = 0;
    // 1) 估计径向压暗 k（稳健环带 + r² 最小二乘）
    const QImage small = boxDownscale(toL8(rgb), 256);
    const int sw = small.width(), sh = small.height();
    if (sw < 16 || sh < 16) return rgb.convertToFormat(QImage::Format_RGB32);
    const double cx = (sw - 1) / 2.0, cy = (sh - 1) / 2.0;
    const double rmax = std::hypot(cx, cy);
    if (rmax < 1.0) return rgb.convertToFormat(QImage::Format_RGB32);
    const int bins = 12;
    QVector<double> sums(bins, 0.0);
    QVector<long> cnts(bins, 0);
    for (int y = 0; y < sh; ++y) {
        const uchar* p = small.constScanLine(y);
        const double dy = y - cy;
        for (int x = 0; x < sw; ++x) {
            const double r = std::hypot(x - cx, dy) / rmax;
            const int bi = (r >= 1.0) ? bins - 1 : int(r * bins);
            sums[bi] += p[x]; ++cnts[bi];
        }
    }
    QVector<double> prof, rr;
    for (int b = 0; b < bins; ++b)
        if (cnts[b] >= 8) { prof.append(sums[b] / cnts[b]); rr.append(std::pow((b + 0.5) / bins, 2)); }
    if (prof.size() < 4) return rgb.convertToFormat(QImage::Format_RGB32);
    const double ref = *std::max_element(prof.constBegin(), prof.constEnd());
    if (ref <= 1e-6) return rgb.convertToFormat(QImage::Format_RGB32);
    double num = 0, den = 0;
    for (int i = 0; i < prof.size(); ++i) {
        const double rel = prof[i] / ref;
        num += rr[i] * (1.0 - rel);
        den += rr[i] * rr[i];
    }
    double k = std::max(0.0, num / std::max(1e-9, den));
    if (k < 0.05) return rgb.convertToFormat(QImage::Format_RGB32);   // 无明显暗角不乱修
    k = std::min(0.60, k * strength);
    const double gmax = std::min(1.8, 1.0 / std::max(0.35, 1.0 - k));
    if (kOut) *kOut = std::round(k * 1000.0) / 1000.0;
    // 2) 全分辨率径向增益（解析式，等价低分辨率掩膜双线性放大）
    QImage img = rgb.convertToFormat(QImage::Format_RGB32);
    const int w = img.width(), h = img.height();
    const double cx2 = (w - 1) / 2.0, cy2 = (h - 1) / 2.0;
    const double rmax2 = cx2 * cx2 + cy2 * cy2;
    if (rmax2 < 1.0) return img;
    for (int y = 0; y < h; ++y) {
        QRgb* d = reinterpret_cast<QRgb*>(img.scanLine(y));
        const double dy = y - cy2;
        for (int x = 0; x < w; ++x) {
            const double dx = x - cx2;
            const double r2 = (dx * dx + dy * dy) / rmax2;
            const double inv = std::max(0.35, 1.0 - k * r2);
            const double gain = std::min(gmax, 1.0 / inv);
            d[x] = qRgb(clamp255(int(qRed(d[x]) * gain + 0.5)),
                        clamp255(int(qGreen(d[x]) * gain + 0.5)),
                        clamp255(int(qBlue(d[x]) * gain + 0.5)));
        }
    }
    return img;
}

QImage LumenEngine::toneMapHdr(const QImage& rgb, double amount)
{
    QImage img = rgb.convertToFormat(QImage::Format_RGB32);
    const int w = img.width(), h = img.height();
    if (w < 32 || h < 32 || amount <= 0) return img;
    QImage hI, sI, vI;
    LumenEngine::rgb2hsv(img, &hI, &sI, &vI);
    const double R = std::max(4.0, std::min(w, h) / 10.0);
    const QImage m = LumenEngine::gaussianBlur(vI, R);
    const double HI = 1.55;
    int gl[256], hiLut[256], kneeLut[256];
    for (int x = 0; x < 256; ++x) {
        const double g = std::clamp(std::pow(128.0 / (x + 1.0), 0.25), 0.62, HI);
        gl[x] = clamp255(int(std::lround(255.0 * g / HI)));
        hiLut[x] = std::min(255, int(std::lround(x * HI)));
        if (x <= 196) kneeLut[x] = x;
        else {
            const double t = (x - 196.0) / 59.0;
            const double y = 1.0 - std::pow(1.0 - t, 1.6);
            kneeLut[x] = clamp255(int(std::lround(196.0 + y * 59.0 * 0.95)));
        }
    }
    QImage v1 = pointGray(chopGray(vI, pointGray(m, gl), 0), hiLut);
    v1 = pointGray(v1, kneeLut);
    // 灰度 blend(vv, v1, amount)
    QImage outV(vI.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < vI.height(); ++y) {
        const uchar* pa = vI.constScanLine(y);
        const uchar* pb = v1.constScanLine(y);
        uchar* d = outV.scanLine(y);
        for (int x = 0; x < vI.width(); ++x)
            d[x] = uchar(clamp255(int(pa[x] * (1 - amount) + pb[x] * amount + 0.5)));
    }
    return LumenEngine::hsv2rgb(hI, sI, outV);
}

QImage LumenEngine::structure(const QImage& rgb, double amount)
{
    QImage img = rgb.convertToFormat(QImage::Format_RGB32);
    if (std::min(img.width(), img.height()) < 24 || amount <= 0) return img;
    QImage hI, sI, vI;
    LumenEngine::rgb2hsv(img, &hI, &sI, &vI);
    const double R = std::max(1.0, std::min(img.width(), img.height()) / 150.0);
    const int pct = int(std::lround(std::min(0.55, 0.25 + 0.35 * amount) * 100));
    // 明度通道细尺度 USM（借 RGB32 等价实现）
    QImage vRgb(vI.size(), QImage::Format_RGB32);
    for (int y = 0; y < vI.height(); ++y) {
        const uchar* g = vI.constScanLine(y);
        QRgb* d = reinterpret_cast<QRgb*>(vRgb.scanLine(y));
        for (int x = 0; x < vI.width(); ++x) d[x] = qRgb(g[x], g[x], g[x]);
    }
    const QImage usm = LumenEngine::unsharpMask(vRgb, std::max(1.0, std::round(R)), pct, 3);
    QImage vSharp(vI.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < vI.height(); ++y) {
        const QRgb* s = reinterpret_cast<const QRgb*>(usm.constScanLine(y));
        uchar* d = vSharp.scanLine(y);
        for (int x = 0; x < vI.width(); ++x) d[x] = uchar(qRed(s[x]));
    }
    // 中调掩膜：V≈128 权重 1，两端趋 0
    int maskLut[256];
    for (int x = 0; x < 256; ++x)
        maskLut[x] = clamp255(int(255 * std::pow(std::max(0.0, 1.0 - std::abs(x - 128) / 128.0), 0.8)));
    const QImage mask = pointGray(vI, maskLut);
    // 灰度 composite(vSharp, vv, mask)
    QImage outV(vI.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < vI.height(); ++y) {
        const uchar* pa = vSharp.constScanLine(y);
        const uchar* pb = vI.constScanLine(y);
        const uchar* pm = mask.constScanLine(y);
        uchar* d = outV.scanLine(y);
        for (int x = 0; x < vI.width(); ++x)
            d[x] = uchar(clamp255((pa[x] * pm[x] + pb[x] * (255 - pm[x])) / 255));
    }
    return LumenEngine::hsv2rgb(hI, sI, outV);
}

QImage LumenEngine::dodge(const QImage& rgb, double amount)
{
    QImage img = rgb.convertToFormat(QImage::Format_RGB32);
    if (img.width() < 32 || img.height() < 32 || amount <= 0) return img;
    QImage hI, sI, vI;
    LumenEngine::rgb2hsv(img, &hI, &sI, &vI);
    const double R = std::max(4.0, std::min(img.width(), img.height()) / 8.0);
    const QImage m = LumenEngine::gaussianBlur(vI, R);
    const double lift = std::clamp(amount, 0.0, 1.0) * 0.28;
    int wlLut[256], rampLut[256], capLut[256];
    for (int x = 0; x < 256; ++x) {
        wlLut[x] = clamp255(int(std::lround(255.0 * lift * std::pow(1.0 - x / 255.0, 1.5))));
        rampLut[x] = std::min(255, int(qint64(x) * 255 / 24));
        capLut[x] = std::min(x, 80);
    }
    QImage add = chopGray(chopGray(invertGray(vI), pointGray(m, wlLut), 0),
                          pointGray(vI, rampLut), 0);
    add = pointGray(add, capLut);
    const QImage outV = chopGray(vI, add, 1);
    return LumenEngine::hsv2rgb(hI, sI, outV);
}

// ============================================================ PIL 基础设施（P5-3）
QImage LumenEngine::minFilter(const QImage& src, int k)
{
    if (k < 1 || src.isNull()) return src.copy();
    const bool gray = src.format() == QImage::Format_Grayscale8;
    QImage img = gray ? src.copy() : src.convertToFormat(QImage::Format_RGB32);
    const int w = img.width(), h = img.height(), r = k / 2;
    QImage out(img.size(), img.format());
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int mn[3] = {255, 255, 255};
            for (int dy = -r; dy <= r; ++dy) {
                const int yy = std::clamp(y + dy, 0, h - 1);
                const uchar* row = img.constScanLine(yy);
                for (int dx = -r; dx <= r; ++dx) {
                    const int xx = std::clamp(x + dx, 0, w - 1);
                    if (gray) mn[0] = std::min(mn[0], int(row[xx]));
                    else {
                        const QRgb px = reinterpret_cast<const QRgb*>(row)[xx];
                        mn[0] = std::min(mn[0], qRed(px));
                        mn[1] = std::min(mn[1], qGreen(px));
                        mn[2] = std::min(mn[2], qBlue(px));
                    }
                }
            }
            if (gray) out.scanLine(y)[x] = uchar(mn[0]);
            else reinterpret_cast<QRgb*>(out.scanLine(y))[x] = qRgb(mn[0], mn[1], mn[2]);
        }
    }
    return out;
}

QImage LumenEngine::medianFilter(const QImage& rgb, int size)
{
    if (size < 3 || rgb.isNull()) return rgb.convertToFormat(QImage::Format_RGB32);
    if (size == 3) return median3(rgb);
    QImage img = rgb.convertToFormat(QImage::Format_RGB32);
    QImage out(img.size(), img.format());
    const int w = img.width(), h = img.height(), r = size / 2;
    const int nn = size * size;
    QVector<int> vr(nn), vg(nn), vb(nn);
    for (int y = 0; y < h; ++y) {
        QRgb* d = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < w; ++x) {
            int n = 0;
            for (int dy = -r; dy <= r; ++dy) {
                const int yy = std::clamp(y + dy, 0, h - 1);
                const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(yy));
                for (int dx = -r; dx <= r; ++dx) {
                    const QRgb px = row[std::clamp(x + dx, 0, w - 1)];
                    vr[n] = qRed(px); vg[n] = qGreen(px); vb[n] = qBlue(px); ++n;
                }
            }
            std::sort(vr.begin(), vr.begin() + n);
            std::sort(vg.begin(), vg.begin() + n);
            std::sort(vb.begin(), vb.begin() + n);
            d[x] = qRgb(vr[n / 2], vg[n / 2], vb[n / 2]);
        }
    }
    return out;
}

void LumenEngine::ycbcrSplit(const QImage& rgb, QImage* y, QImage* cb, QImage* cr)
{
    QImage img = rgb.convertToFormat(QImage::Format_RGB32);
    *y = QImage(img.size(), QImage::Format_Grayscale8);
    *cb = QImage(img.size(), QImage::Format_Grayscale8);
    *cr = QImage(img.size(), QImage::Format_Grayscale8);
    for (int yy = 0; yy < img.height(); ++yy) {
        const QRgb* s = reinterpret_cast<const QRgb*>(img.constScanLine(yy));
        uchar* py = y->scanLine(yy);
        uchar* pcb = cb->scanLine(yy);
        uchar* pcr = cr->scanLine(yy);
        for (int x = 0; x < img.width(); ++x) {
            // PIL 整数定点公式（截断，非四舍五入）——肤色阈值边界对偏移极敏感
            const long long r = qRed(s[x]), g = qGreen(s[x]), b = qBlue(s[x]);
            py[x]  = uchar(clamp255(int((299 * r + 587 * g + 114 * b) / 1000)));
            pcb[x] = uchar(clamp255(int((-168736 * r - 331264 * g + 500000 * b + 128000000LL) / 1000000)));
            pcr[x] = uchar(clamp255(int((500000 * r - 418688 * g - 81312 * b + 128000000LL) / 1000000)));
        }
    }
}

QImage LumenEngine::skinMask(const QImage& rgb)
{
    QImage y, cb, cr;
    ycbcrSplit(rgb, &y, &cb, &cr);
    int cbLut[256], crLut[256], yLut[256];
    auto fill = [](int lut[256], double lo0, double lo1, double hi0, double hi1) {
        for (int v = 0; v < 256; ++v) {
            if (v < lo0 || v > hi1) lut[v] = 0;
            else if (v < lo1) lut[v] = int(std::lround(255.0 * (v - lo0) / std::max(1.0, lo1 - lo0)));
            else if (v <= hi0) lut[v] = 255;
            else lut[v] = int(std::lround(255.0 * (hi1 - v) / std::max(1.0, hi1 - hi0)));
        }
    };
    fill(cbLut, 69, 77, 127, 135);     // Chai & Ngan 核心 77..127
    fill(crLut, 125, 133, 173, 181);   // 核心 133..173
    fill(yLut, 30, 45, 245, 253);      // 亮度门
    QImage m = chopGray(pointGray(cb, cbLut), pointGray(cr, crLut), 0);
    m = chopGray(m, pointGray(y, yLut), 0);
    return LumenEngine::gaussianBlur(m, std::max(1.5, std::min(rgb.width(), rgb.height()) / 120.0));
}

// ============================================================ P5-3 显式算子（第三批）
QImage LumenEngine::dehaze(const QImage& rgb, double strength)
{
    QImage img = rgb.convertToFormat(QImage::Format_RGB32);
    const int W = img.width(), H = img.height();
    if (W < 8 || H < 8) return img;
    // 工作分辨率：长边 ≤1280（雾是低频效应，降采样几乎无损）
    const double scale = std::min(1.0, 1280.0 / std::max(1, std::max(W, H)));
    QImage work = img;
    if (scale < 1.0)
        work = img.scaled(std::max(1, int(W * scale)), std::max(1, int(H * scale)),
                          Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    const int ww = work.width(), wh = work.height();
    // 单通道分离
    QImage R(ww, wh, QImage::Format_Grayscale8), G(ww, wh, QImage::Format_Grayscale8),
           B(ww, wh, QImage::Format_Grayscale8);
    for (int y = 0; y < wh; ++y) {
        const QRgb* s = reinterpret_cast<const QRgb*>(work.constScanLine(y));
        uchar* pr = R.scanLine(y); uchar* pg = G.scanLine(y); uchar* pb = B.scanLine(y);
        for (int x = 0; x < ww; ++x) { pr[x] = uchar(qRed(s[x])); pg[x] = uchar(qGreen(s[x])); pb[x] = uchar(qBlue(s[x])); }
    }
    const QImage rmin = minFilter(R, 15), gmin = minFilter(G, 15), bmin = minFilter(B, 15);
    QImage dark(ww, wh, QImage::Format_Grayscale8);
    for (int y = 0; y < wh; ++y) {
        const uchar* a = rmin.constScanLine(y);
        const uchar* b = gmin.constScanLine(y);
        const uchar* c = bmin.constScanLine(y);
        uchar* d = dark.scanLine(y);
        for (int x = 0; x < ww; ++x) d[x] = uchar(std::min({a[x], b[x], c[x]}));
    }
    // 大气光 A：暗通道最亮 0.1% 像素中，取原图该位置最亮通道最大的那个
    const int n = ww * wh;
    const int ntop = std::max(1, int(n * 0.001));
    QVector<int> idx(n);
    for (int i = 0; i < n; ++i) idx[i] = i;
    const uchar* ddata = nullptr;
    QVector<uchar> dflat;
    dflat.reserve(n);
    for (int y = 0; y < wh; ++y) {
        const uchar* p = dark.constScanLine(y);
        for (int x = 0; x < ww; ++x) dflat.append(p[x]);
    }
    ddata = dflat.constData();
    std::partial_sort(idx.begin(), idx.begin() + ntop, idx.end(),
                      [ddata](int a, int b) { return ddata[a] > ddata[b]; });
    int A[3] = {1, 1, 1};
    int best = -1;
    QVector<uchar> rf, gf, bf;
    rf.reserve(n); gf.reserve(n); bf.reserve(n);
    for (int y = 0; y < wh; ++y) {
        const uchar* a = R.constScanLine(y);
        const uchar* b = G.constScanLine(y);
        const uchar* c = B.constScanLine(y);
        for (int x = 0; x < ww; ++x) { rf.append(a[x]); gf.append(b[x]); bf.append(c[x]); }
    }
    for (int t = 0; t < ntop; ++t) {
        const int i = idx[t];
        const int mx = std::max({int(rf[i]), int(gf[i]), int(bf[i])});
        if (mx > best) { best = mx; A[0] = rf[i]; A[1] = gf[i]; A[2] = bf[i]; }
    }
    for (int c = 0; c < 3; ++c) A[c] = std::max(1, A[c]);
    const double omega = 0.95, t0 = 0.1;
    auto divc = [](const QImage& ch, int a) {
        int lut[256];
        for (int v = 0; v < 256; ++v) lut[v] = std::min(255, int(std::lround(v / double(a) * 255)));
        return pointGray(ch, lut);
    };
    const QImage nr = divc(R, A[0]), ng = divc(G, A[1]), nb = divc(B, A[2]);
    QImage nd(ww, wh, QImage::Format_Grayscale8);
    for (int y = 0; y < wh; ++y) {
        const uchar* a = nr.constScanLine(y);
        const uchar* b = ng.constScanLine(y);
        const uchar* c = nb.constScanLine(y);
        uchar* d = nd.scanLine(y);
        for (int x = 0; x < ww; ++x) d[x] = uchar(std::min({a[x], b[x], c[x]}));
    }
    int tLut[256], t0Lut[256];
    for (int v = 0; v < 256; ++v) {
        tLut[v] = std::min(255, int(std::lround((1.0 - omega * (v / 255.0)) * 255)));
        t0Lut[v] = std::max(int(t0 * 255), v);
    }
    QImage t = LumenEngine::gaussianBlur(pointGray(pointGray(nd, tLut), t0Lut),
                            std::max(3.0, std::min(ww, wh) / 120.0));
    QImage res(ww, wh, QImage::Format_RGB32);
    for (int y = 0; y < wh; ++y) {
        const uchar* pt = t.constScanLine(y);
        QRgb* d = reinterpret_cast<QRgb*>(res.scanLine(y));
        const int base = y * ww;
        for (int x = 0; x < ww; ++x) {
            const int i = base + x;
            const double ti = std::max(t0, pt[x] / 255.0);
            const int p0 = clamp255(int(std::lround((rf[i] - A[0]) / ti + A[0])));
            const int p1 = clamp255(int(std::lround((gf[i] - A[1]) / ti + A[1])));
            const int p2 = clamp255(int(std::lround((bf[i] - A[2]) / ti + A[2])));
            d[x] = qRgb(p0, p1, p2);
        }
    }
    if (strength < 1.0) res = blend(work, res, strength);
    if (scale < 1.0)
        res = res.scaled(W, H, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    return res;
}

QImage LumenEngine::chromaticCorrect(const QImage& rgb, double amount)
{
    QImage img = rgb.convertToFormat(QImage::Format_RGB32);
    const int w = img.width(), h = img.height();
    if (w < 96 || h < 96 || amount <= 0) return img;
    double k = 0.006 * amount;
    k = std::min(k, 0.02);
    // R：放大(1+k) 后裁中心 → 收回外扩红边
    QImage rCh(w, h, QImage::Format_Grayscale8), gCh(w, h, QImage::Format_Grayscale8),
           bCh(w, h, QImage::Format_Grayscale8);
    for (int y = 0; y < h; ++y) {
        const QRgb* s = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        uchar* pr = rCh.scanLine(y); uchar* pg = gCh.scanLine(y); uchar* pb = bCh.scanLine(y);
        for (int x = 0; x < w; ++x) { pr[x] = uchar(qRed(s[x])); pg[x] = uchar(qGreen(s[x])); pb[x] = uchar(qBlue(s[x])); }
    }
    const int rw = std::max(2, int(std::lround(w * (1 + k))));
    const int rh = std::max(2, int(std::lround(h * (1 + k))));
    const QImage rBig = rCh.scaled(rw, rh, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    const int rx = (rw - w) / 2, ry = (rh - h) / 2;
    const QImage rCorr = rBig.copy(rx, ry, w, h);
    // B：缩小(1-k) 后镜像扩边归位 → 收回内缩蓝边
    const int bw = std::max(2, int(std::lround(w * (1 - k))));
    const int bh = std::max(2, int(std::lround(h * (1 - k))));
    const QImage bSmall = bCh.scaled(bw, bh, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QImage bCorr(w, h, QImage::Format_Grayscale8);
    bCorr.fill(0);
    const int ox = (w - bw) / 2, oy = (h - bh) / 2;
    for (int y = 0; y < bh; ++y)
        std::memcpy(bCorr.scanLine(oy + y) + ox, bSmall.constScanLine(y), size_t(bw));
    // 四边镜像拉伸填充（无黑边）
    for (int y = 0; y < h; ++y) {
        uchar* row = bCorr.scanLine(y);
        for (int x = 0; x < ox && ox > 0; ++x) row[x] = row[ox];
        for (int x = ox + bw; x < w && (ox + bw) < w; ++x) row[x] = row[ox + bw - 1];
    }
    if (oy > 0) for (int y = 0; y < oy; ++y)
        std::memcpy(bCorr.scanLine(y), bCorr.constScanLine(oy), size_t(w));
    if (oy + bh < h) for (int y = oy + bh; y < h; ++y)
        std::memcpy(bCorr.scanLine(y), bCorr.constScanLine(oy + bh - 1), size_t(w));
    for (int y = 0; y < h; ++y) {
        QRgb* d = reinterpret_cast<QRgb*>(img.scanLine(y));
        const uchar* pr = rCorr.constScanLine(y);
        const uchar* pg = gCh.constScanLine(y);
        const uchar* pb = bCorr.constScanLine(y);
        for (int x = 0; x < w; ++x) d[x] = qRgb(pr[x], pg[x], pb[x]);
    }
    return img;
}

QImage LumenEngine::portraitBeautify(const QImage& rgb, double strength, double* skinFrac)
{
    if (skinFrac) *skinFrac = 0;
    QImage img = rgb.convertToFormat(QImage::Format_RGB32);
    const QImage mask = skinMask(img);
    long tot = 0, ge60 = 0;
    for (int y = 0; y < mask.height(); ++y) {
        const uchar* p = mask.constScanLine(y);
        for (int x = 0; x < mask.width(); ++x) { ++tot; if (p[x] >= 60) ++ge60; }
    }
    const double frac = tot ? double(ge60) / tot : 0.0;
    if (skinFrac) *skinFrac = std::round(frac * 1000.0) / 1000.0;
    if (frac < 0.015) return img;   // 无肤色 → 不乱修
    const double r = std::max(3.0, std::min(img.width(), img.height()) / 150.0);
    const QImage med = medianFilter(img, 5);
    const QImage gau = LumenEngine::gaussianBlur(img, r);
    const QImage smooth = blend(med, gau, 0.5);
    const double softAmt = std::min(1.0, 0.80 * strength);
    QImage out = composite(blend(img, smooth, softAmt), img, mask);
    // 美白：肤色偏暗才轻提亮（宁欠勿爆）
    QImage hI, sI, vI;
    LumenEngine::rgb2hsv(img, &hI, &sI, &vI);
    // 美白门控(skin_lum<0.62)是**二值**判定且样本常贴边界 → 必须用保均值的面积重采样
    // （Qt 平滑采样器均值偏差会翻转判定，导致全图 ±5% 亮度差）
    const QImage vs = areaResample(vI, 64, 64);
    const QImage ms = areaResample(mask, 64, 64);
    double wsum = 0, msum = 0;
    for (int y = 0; y < 64; ++y) {
        const uchar* pv = vs.constScanLine(y);
        const uchar* pm = ms.constScanLine(y);
        for (int x = 0; x < 64; ++x) { wsum += pv[x] * pm[x]; msum += pm[x]; }
    }
    const double skinLum = msum ? (wsum / msum) / 255.0 : 1.0;
    if (skinLum < 0.62) {
        const double gain = 1.0 + 0.05 * strength;
        out = composite(enhanceBrightness(out, gain), out, mask);
    }
    return out;
}

// ============================================================ P5-4 水平校正（Hough 地平线）
// 对齐 _detect_horizon + _straighten：降采样→Sobel→单阈值边缘→Hough(θ∈[78°,102°])→最高票。
namespace {
struct HorizonDet { double angle = 0; int votes = 0; int total = 0; bool ok = false; };

// 单值 3×3 线性求解：A·x=B，A 的每行 [dx,dy,1]（高斯消元带主元）
bool solve3(double d0x, double d0y, double d1x, double d1y, double d2x, double d2y,
            double b0, double b1, double b2, double out[3])
{
    double M[3][4] = {
        {d0x, d0y, 1, b0},
        {d1x, d1y, 1, b1},
        {d2x, d2y, 1, b2},
    };
    for (int i = 0; i < 3; ++i) {
        int p = i;
        for (int r = i + 1; r < 3; ++r)
            if (std::fabs(M[r][i]) > std::fabs(M[p][i])) p = r;
        if (p != i) for (int c = 0; c < 4; ++c) std::swap(M[i][c], M[p][c]);
        const double piv = M[i][i] != 0 ? M[i][i] : 1e-12;
        for (int c = i; c < 4; ++c) M[i][c] /= piv;
        for (int r = 0; r < 3; ++r) {
            if (r == i) continue;
            const double f = M[r][i];
            for (int c = i; c < 4; ++c) M[r][c] -= f * M[i][c];
        }
    }
    out[0] = M[0][3]; out[1] = M[1][3]; out[2] = M[2][3];
    return true;
}

HorizonDet detectHorizon(const QImage& rgb)
{
    HorizonDet res;
    QImage img = rgb.convertToFormat(QImage::Format_RGB32);
    const int W = img.width(), H = img.height();
    // 降采样到长边 ≤480（BILINEAR 近似用 Qt 平滑），转灰度
    const double sc = std::min(1.0, 480.0 / std::max(1, std::max(W, H)));
    int sw, sh;
    if (sc < 1.0) {
        sw = std::max(1, int(std::lround(W * sc)));
        sh = std::max(1, int(std::lround(H * sc)));
        img = img.scaled(sw, sh, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    } else { sw = W; sh = H; }
    const QImage gray = LumenEngine::toL8(img);
    // Sobel（整数）
    QVector<int> gx(sw * sh), gy(sw * sh);
    auto px = [&](int x, int y) -> int {
        return gray.constScanLine(std::clamp(y, 0, sh - 1))[std::clamp(x, 0, sw - 1)];
    };
    for (int y = 1; y < sh - 1; ++y)
        for (int x = 1; x < sw - 1; ++x) {
            const int up = (y - 1) * sw, cur = y * sw, dn = (y + 1) * sw;
            gx[y * sw + x] = (px(x + 1, y - 1) + 2 * px(x + 1, y) + px(x + 1, y + 1)
                              - px(x - 1, y - 1) - 2 * px(x - 1, y) - px(x - 1, y + 1));
            gy[y * sw + x] = (px(x - 1, y - 1) + 2 * px(x, y - 1) + px(x + 1, y - 1)
                              - px(x - 1, y + 1) - 2 * px(x, y + 1) - px(x + 1, y + 1));
        }
    // 固定低阈值边缘图
    const int hi = 55;
    QVector<uchar> edges(sw * sh, 0);
    int total = 0;
    for (int y = 0; y < sh; ++y)
        for (int x = 0; x < sw; ++x) {
            const int i = y * sw + x;
            if (std::hypot(double(gx[i]), double(gy[i])) >= hi) { edges[i] = 1; ++total; }
        }
    // Hough：θ∈[78°,102°]（近水平直线法线角≈90°），预计算 sin/cos
    const int loT = 78, hiT = 102;
    const int nt = hiT - loT + 1;
    QVector<double> cT(nt), sT(nt);
    for (int i = 0; i < nt; ++i) {
        const double r = (loT + i) * 3.14159265358979323846 / 180.0;
        cT[i] = std::cos(r); sT[i] = std::sin(r);
    }
    // 票数表：key=(round(rho), deg) → votes
    QMap<QPair<int,int>, int> acc;
    for (int y = 0; y < sh; ++y)
        for (int x = 0; x < sw; ++x) {
            if (!edges[y * sw + x]) continue;
            for (int i = 0; i < nt; ++i) {
                const int rho = int(std::lround(x * cT[i] + y * sT[i]));
                const QPair<int,int> key(rho, loT + i);
                acc[key] += 1;
            }
        }
    if (acc.isEmpty()) return res;
    int bestVotes = 0; QPair<int,int> bestKey;
    for (auto it = acc.constBegin(); it != acc.constEnd(); ++it)
        if (it.value() > bestVotes) { bestVotes = it.value(); bestKey = it.key(); }
    const int votes = bestVotes;
    if (votes < std::max(30, int(0.3 * sw)) || votes < 0.03 * total) return res;
    res.angle = bestKey.second - 90.0;   // 拉平所需旋转角 = deg_normal - 90
    res.votes = votes; res.total = total; res.ok = true;
    return res;
}
} // namespace

QImage LumenEngine::levelCorrect(const QImage& rgb)
{
    HorizonDet det = detectHorizon(rgb);
    if (!det.ok || std::fabs(det.angle) < 0.5)
        return rgb.copy();
    const double angle = det.angle;
    const int w = rgb.width(), h = rgb.height();
    // 旋转（PIL 为逆时针，Qt 为顺时针 → 取负角对齐视觉）
    const QTransform tf = QTransform().rotate(-angle);
    QImage rot = rgb.convertToFormat(QImage::Format_RGB32)
                     .transformed(tf, Qt::SmoothTransformation);
    const int nw = rot.width(), nh = rot.height();
    const double c = std::cos(angle * 3.14159265358979323846 / 180.0),
                 s = std::sin(angle * 3.14159265358979323846 / 180.0);
    const double iw = w * c - h * s;
    const double ih = h * c - w * s;
    if (iw > w * 0.5 && ih > h * 0.5) {
        const int left = int(std::lround((nw - iw) / 2));
        const int top  = int(std::lround((nh - ih) / 2));
        rot = rot.copy(left, top, int(std::lround(iw)), int(std::lround(ih)));
    }
    return rot.convertToFormat(QImage::Format_RGB32);
}

double LumenEngine::levelDetectAngle(const QImage& rgb)
{
    const HorizonDet d = detectHorizon(rgb);
    return d.ok ? d.angle : 0.0;
}
int LumenEngine::levelDetectVotes(const QImage& rgb)
{
    const HorizonDet d = detectHorizon(rgb);
    return d.ok ? d.votes : 0;
}

// ============================================================ P5-4 镜头畸变校正（网格 AFFINE）
namespace {
// 径向反畸变源坐标（对齐 _lens_src）
void lensSrc(double px, double py, double cx, double cy, double R, double k, double* sx, double* sy)
{
    const double vx = px - cx, vy = py - cy;
    const double rn = std::hypot(vx, vy) / R;
    const double f = 1.0 + k * rn * rn;
    *sx = cx + vx * f;
    *sy = cy + vy * f;
}

// 镜像扩边（对齐 _mirror_expand）
QImage mirrorExpand(const QImage& img, int m)
{
    if (m <= 0) return img.copy();
    QImage src = img.convertToFormat(QImage::Format_RGB32);
    const int w = src.width(), h = src.height();
    QImage out(w + 2 * m, h + 2 * m, QImage::Format_RGB32);
    out.fill(0);
    // 中心
    {
        const QImage c = src.copy();
        for (int y = 0; y < h; ++y)
            std::memcpy(out.scanLine(m + y) + m * 4, c.constScanLine(y), size_t(w) * 4);
    }
    // 左/右条（左右镜像）
    {
        QImage lc = src.copy(0, 0, 1, h).scaled(m, h).mirrored(true, false);
        for (int y = 0; y < h; ++y)
            std::memcpy(out.scanLine(m + y), lc.constScanLine(y), size_t(m) * 4);
        QImage rc = src.copy(w - 1, 0, 1, h).scaled(m, h).mirrored(true, false);
        for (int y = 0; y < h; ++y)
            std::memcpy(out.scanLine(m + y) + (m + w) * 4, rc.constScanLine(y), size_t(m) * 4);
    }
    // 上/下条（上下镜像）—— 注意：须逐行写入 m 行厚边，而非只写一行
    {
        QImage tr = src.copy(0, 0, w, 1).scaled(w, m).mirrored(false, true);
        for (int y = 0; y < m; ++y)
            std::memcpy(out.scanLine(y) + m * 4, tr.constScanLine(y), size_t(w) * 4);
        QImage br = src.copy(0, h - 1, w, 1).scaled(w, m).mirrored(false, true);
        for (int y = 0; y < m; ++y)
            std::memcpy(out.scanLine(m + h + y) + m * 4, br.constScanLine(y), size_t(w) * 4);
    }
    // 四角（双向镜像）
    auto corner = [&](int sx0, int sy0, int dx, int dy) {
        QImage cn = src.copy(sx0, sy0, 1, 1).scaled(m, m).mirrored(true, true);
        for (int y = 0; y < m; ++y)
            std::memcpy(out.scanLine(dy + y) + dx * 4, cn.constScanLine(y), size_t(m) * 4);
    };
    corner(0, 0, 0, 0);
    corner(w - 1, 0, m + w, 0);
    corner(0, h - 1, 0, m + h);
    corner(w - 1, h - 1, m + w, m + h);
    return out;
}

// 双线性采样（越界返回黑，对齐 PIL fill=0）
inline QRgb bilinearSample(const QImage& img, double x, double y)
{
    const int w = img.width(), h = img.height();
    if (x < -0.5 || y < -0.5 || x > w - 0.5 || y > h - 0.5) return qRgb(0, 0, 0);
    const int x0 = int(std::floor(x)), y0 = int(std::floor(y));
    const double fx = x - x0, fy = y - y0;
    const int x1 = std::min(w - 1, x0 + 1), y1 = std::min(h - 1, y0 + 1);
    auto at = [&](int xx, int yy) -> QRgb {
        if (xx < 0 || yy < 0 || xx >= w || yy >= h) return qRgb(0, 0, 0);
        return reinterpret_cast<const QRgb*>(img.constScanLine(yy))[xx];
    };
    const QRgb p00 = at(x0, y0), p10 = at(x1, y0), p01 = at(x0, y1), p11 = at(x1, y1);
    auto ch = [&](int a, int b, int c, int d) -> int {
        const double top = a * (1 - fx) + b * fx;
        const double bot = c * (1 - fx) + d * fx;
        return clamp255(int(std::lround(top * (1 - fy) + bot * fy)));
    };
    return qRgb(ch(qRed(p00), qRed(p10), qRed(p01), qRed(p11)),
                ch(qGreen(p00), qGreen(p10), qGreen(p01), qGreen(p11)),
                ch(qBlue(p00), qBlue(p10), qBlue(p01), qBlue(p11)));
}
} // namespace

QImage LumenEngine::lensCorrect(const QImage& rgb, double amount)
{
    const int w = rgb.width(), h = rgb.height();
    if (w < 64 || h < 64 || amount <= 0) return rgb.copy();
    double k = 0.10 * amount;
    k = std::min(k, 0.30);
    const double R = 0.5 * std::hypot(double(w), double(h));
    int margin = int(k * R) + 2;
    if (margin < 2) return rgb.copy();
    const QImage pad = mirrorExpand(rgb, margin);
    const int pw = pad.width(), ph = pad.height();
    const double cx = pw / 2.0, cy = ph / 2.0;
    const int N = std::max(8, std::min(32, w / 60));
    QImage out(pw, ph, QImage::Format_RGB32);
    out.fill(0);
    for (int iy = 0; iy < N; ++iy)
        for (int ix = 0; ix < N; ++ix) {
            int x0 = int(ix * pw / N), x1 = int((ix + 1) * pw / N);
            int y0 = int(iy * ph / N), y1 = int((iy + 1) * ph / N);
            if (x1 <= x0) x1 = x0 + 1;
            if (y1 <= y0) y1 = y0 + 1;
            const int tw = x1 - x0, th = y1 - y0;
            // 局部瓦片坐标(0..tw)→全局源坐标仿射（对齐 _affine_from）
            double s00[2], sT0[2], sTH[2];
            lensSrc(x0, y0, cx, cy, R, k, &s00[0], &s00[1]);
            lensSrc(x0 + tw, y0, cx, cy, R, k, &sT0[0], &sT0[1]);
            lensSrc(x0 + tw, y0 + th, cx, cy, R, k, &sTH[0], &sTH[1]);
            double M[6];
            solve3(0, 0, tw, 0, tw, th, s00[0], sT0[0], sTH[0], &M[0]);
            solve3(0, 0, tw, 0, tw, th, s00[1], sT0[1], sTH[1], &M[3]);
            for (int ly = 0; ly < th; ++ly)
                for (int lx = 0; lx < tw; ++lx) {
                    const double sx = M[0] * lx + M[1] * ly + M[2];
                    const double sy = M[3] * lx + M[4] * ly + M[5];
                    reinterpret_cast<QRgb*>(out.scanLine(y0 + ly))[x0 + lx] =
                        bilinearSample(pad, sx, sy);
                }
        }
    return out.copy(margin, margin, w, h).convertToFormat(QImage::Format_RGB32);
}

// ============================================================ P5-4 参数管线 retouch_with_plan
namespace {
// 以实测均值 mid 为轴心的对比度 LUT（对齐 plan 的 contrast）
QImage contrastMeanAxis(const QImage& rgb, double mid, double f)
{
    QImage img = rgb.convertToFormat(QImage::Format_RGB32);
    int lut[256];
    for (int v = 0; v < 256; ++v)
        lut[v] = clamp255(int(std::lround((v - mid) * f + mid)));
    for (int y = 0; y < img.height(); ++y) {
        QRgb* d = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x)
            d[x] = qRgb(lut[qRed(d[x])], lut[qGreen(d[x])], lut[qBlue(d[x])]);
    }
    return img;
}

// 自然饱和度（S 通道 LUT，对齐 plan vibrance）
QImage planVibrance(const QImage& rgb, double vb2)
{
    QImage h, s, v;
    LumenEngine::rgb2hsv(rgb, &h, &s, &v);
    const double vv2 = std::min(0.45, std::fabs(vb2) * 0.45) * (vb2 > 0 ? 1.0 : -1.0);
    int lut[256];
    for (int x = 0; x < 256; ++x)
        lut[x] = clamp255(int(std::lround(x * (1.0 + vv2 * (1.0 - x / 255.0)))));
    QImage s2(s.size(), QImage::Format_Grayscale8);
    const uchar* ps = s.constScanLine(0);
    uchar* p2 = s2.scanLine(0);
    for (int i = 0; i < s.width() * s.height(); ++i) p2[i] = uchar(lut[ps[i]]);
    return LumenEngine::hsv2rgb(h, s2, v);
}

// 清晰度（V 通道 USM / 模糊，对齐 plan clarity）
QImage planClarity(const QImage& rgb, double cld)
{
    QImage h, s, v;
    LumenEngine::rgb2hsv(rgb, &h, &s, &v);
    QImage v2;
    if (cld > 0) {
        const int pct = int(std::min(60.0, cld * 55.0));
        v2 = LumenEngine::unsharpMask(v, 6.0, pct, 0);
    } else {
        const double rr = 0.8 + 1.6 * (-cld);
        v2 = LumenEngine::gaussianBlur(v, rr);
    }
    // unsharpMask 返回 RGB32，gaussianBlur 返回 Grayscale8；hsv2rgb 的 V 必须是
    // Grayscale8（逐字节读取），故统一转回灰度，避免把 RGB32 的字节当灰阶读 → 过曝。
    const QImage vGray = v2.convertToFormat(QImage::Format_Grayscale8);
    return LumenEngine::hsv2rgb(h, s, vGray);
}

// 色调（绿↔品红，通道增益，对齐 plan tint）
QImage planTint(const QImage& rgb, double td)
{
    double fr, fg, fb;
    if (td > 0) { fr = 1 + td * 0.05; fg = 1 - td * 0.04; fb = 1 + td * 0.05; }
    else        { const double m = -td; fr = 1 - m * 0.04; fg = 1 + m * 0.05; fb = 1 - m * 0.04; }
    return LumenEngine::channelGain(rgb, fr, fg, fb);
}

// 局部影调（对齐 _param_tone：highlights/shadows/whites/blacks）
QImage paramTone(const QImage& rgb, double hl, double sh, double wh, double bl)
{
    if (!(std::fabs(hl) >= 0.01 || std::fabs(sh) >= 0.01
          || std::fabs(wh) >= 0.01 || std::fabs(bl) >= 0.01))
        return rgb.copy();
    QImage h, s, v;
    LumenEngine::rgb2hsv(rgb, &h, &s, &v);
    const int W = v.width(), H = v.height();
    const double sc = std::min(1.0, 512.0 / std::max(1, std::max(W, H)));
    QImage small = (sc < 1.0)
        ? v.scaled(int(W * sc), int(H * sc), Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
        : v.copy();
    const double rs = std::max(3.0, double(std::min(small.width(), small.height())) / 40.0);
    const QImage m = LumenEngine::gaussianBlur(small, rs);   // 局部光照（缩略图算、原图用）
    QImage out = v.copy();

    auto upscaleLut = [&](const QImage& g) -> QImage {
        QImage r = (sc < 1.0)
            ? g.scaled(W, H, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
            : g.copy();
        return r;
    };
    auto pointGrayLut = [](const QImage& g, const std::function<int(int)>& fn) -> QImage {
        QImage r(g.size(), QImage::Format_Grayscale8);
        const uchar* pg = g.constScanLine(0);
        uchar* pr = r.scanLine(0);
        for (int i = 0; i < g.width() * g.height(); ++i) pr[i] = uchar(clamp255(fn(pg[i])));
        return r;
    };

    if (std::fabs(sh) >= 0.01) {
        const double T = 130.0;
        QImage w = pointGrayLut(m, [T](int x) {
            return int(255 * std::pow(std::max(0.0, (T - x) / T), 1.6)); });
        w = upscaleLut(w);
        if (sh > 0) {
            QImage ramp = pointGrayLut(v, [](int x) { return std::min(255, int(x * 255.0 / 24.0)); });
            QImage add = chopGray(chopGray(invertGray(v), w, 0), ramp, 0);
            out = chopGray(out, pointGrayLut(add, [sh](int x) {
                return int(x * 0.55 * sh); }), 1);
        } else {
            QImage sub = chopGray(v, w, 0);
            out = chopGray(out, pointGrayLut(sub, [sh](int x) {
                return int(x * 0.35 * (-sh)); }), 2);
        }
    }
    if (std::fabs(hl) >= 0.01) {
        const double T2 = 170.0;
        QImage w2 = pointGrayLut(m, [T2](int x) {
            return int(255 * std::pow(std::max(0.0, (x - T2) / (255.0 - T2)), 1.6)); });
        w2 = upscaleLut(w2);
        if (hl < 0) {
            QImage cap = pointGrayLut(chopGray(v, w2, 0), [](int x) { return std::min(x, 60); });
            out = chopGray(out, pointGrayLut(cap, [hl](int x) {
                return int(x * 0.6 * (-hl)); }), 2);
        } else {
            QImage addh = chopGray(invertGray(v), w2, 0);
            out = chopGray(out, pointGrayLut(addh, [hl](int x) {
                return int(x * 0.5 * hl); }), 1);
        }
    }
    if (std::fabs(bl) >= 0.01 || std::fabs(wh) >= 0.01) {
        int lut[256];
        for (int x = 0; x < 256; ++x) {
            double y = double(x);
            if (bl >= 0) y += bl * 42.0 * std::pow(1.0 - y / 255.0, 2);
            else         y -= (-bl) * 42.0 * std::pow(y / 255.0, 2);
            if (wh >= 0) y += wh * 42.0 * std::pow(y / 255.0, 2);
            else         y -= (-wh) * 42.0 * std::pow(1.0 - y / 255.0, 2);
            lut[x] = clamp255(int(std::lround(y)));
        }
        QImage o2(v.size(), QImage::Format_Grayscale8);
        const uchar* po = out.constScanLine(0);
        uchar* p2 = o2.scanLine(0);
        for (int i = 0; i < v.width() * v.height(); ++i) p2[i] = uchar(lut[po[i]]);
        out = o2;
    }
    if (out.constBits() == v.constBits()) return rgb.copy();
    return LumenEngine::hsv2rgb(h, s, out);
}
} // namespace

QImage LumenEngine::retouchWithPlan(const QImage& rgb, const QMap<QString,double>& params)
{
    auto D = [&](const char* k) -> double {
        auto it = params.find(QLatin1String(k));
        return it == params.end() ? 0.0 : it.value();
    };
    QImage img = rgb.convertToFormat(QImage::Format_RGB32);
    const LumenStats st = measureStats(img);

    // 1. 色温
    const double wm = D("warmth");
    if (std::fabs(wm) >= 0.01)
        img = LumenEngine::channelGain(img, 1 + wm * 0.05, 1.0, 1 - wm * 0.05);
    // 2. 亮度
    const double bd = D("brightness");
    if (std::fabs(bd) >= 0.01) {
        const double f = std::max(0.60, std::min(1.60, 1 + bd * 0.55));
        img = enhanceBrightness(img, f);
    }
    // 3. 对比度（以实测均值轴心）
    const double cd = D("contrast");
    if (std::fabs(cd) >= 0.01) {
        const double f = std::max(0.80, std::min(1.50, 1 + cd * 0.60));
        img = contrastMeanAxis(img, st.lum * 255.0, f);
    }
    // 3b. 局部影调
    img = paramTone(img, D("highlights"), D("shadows"), D("whites"), D("blacks"));
    // 4. 饱和度
    const double sd = D("saturation");
    if (std::fabs(sd) >= 0.01) {
        const double f = std::max(0.0, std::min(2.0, 1 + sd * 0.50));
        img = enhanceColor(img, f);
    }
    // 4b. 自然饱和度
    const double vb2 = D("vibrance");
    if (std::fabs(vb2) >= 0.01) img = planVibrance(img, vb2);
    // 4c. 清晰度
    const double cld = D("clarity");
    if (std::fabs(cld) >= 0.01) img = planClarity(img, cld);
    // 4d. 色调
    const double td = D("tint");
    if (std::fabs(td) >= 0.01) img = planTint(img, td);
    // 5. 降噪
    const double nd = D("denoise");
    const int lvl = nd >= 0.35 ? 2 : (nd >= 0.12 ? 1 : 0);
    if (lvl) img = denoise(img, lvl);
    // 6. 锐化
    const double shd = D("sharpness");
    const int percent = int(std::max(30.0, std::min(140.0, 70 + shd * 70.0)));
    const double radius = (std::max(img.width(), img.height()) >= 3000) ? 1.2 : 1.0;
    img = LumenEngine::unsharpMask(img, radius, percent, 2);
    return img.convertToFormat(QImage::Format_RGB32);
}

// ============================================================ 审计
QStringList LumenEngine::audit(const LumenStats& st0, const LumenMeasure& mx0, const QImage& out)
{
    const LumenStats b_st = measureStats(out);
    const LumenMeasure b_mx = measureExtra(out);
    QStringList issues;
    if (b_st.hi > st0.hi + 0.02)
        issues.append(QStringLiteral("高光削顶 %.1f%%→%.1f%%").arg(st0.hi * 100).arg(b_st.hi * 100));
    if (b_st.lo > st0.lo + 0.03)
        issues.append(QStringLiteral("暗部死黑 %.1f%%→%.1f%%").arg(st0.lo * 100).arg(b_st.lo * 100));
    if (st0.lum_std > 0.10 && b_st.lum_std < std::min(0.12, st0.lum_std * 0.65))
        issues.append(QStringLiteral("反差塌陷 %.3f→%.3f").arg(st0.lum_std).arg(b_st.lum_std));
    if (b_mx.cast > mx0.cast + 0.02)
        issues.append(QStringLiteral("色偏加重 %.3f→%.3f").arg(mx0.cast).arg(b_mx.cast));
    return issues;
}

// ============================================================ auto 管线
QImage LumenEngine::autoPipeline(const QImage& src, const LumenPreset& p, LumenReport* rep)
{
    LumenReport local;
    LumenReport& R = rep ? *rep : local;
    R.steps.clear(); R.reverted = false; R.denoise = 0;

    QImage img = src.convertToFormat(QImage::Format_RGB32);
    if (img.isNull()) return img;
    const QImage orig = img.copy();
    const LumenStats st0 = measureStats(img);
    const LumenMeasure mx0 = measureExtra(img);

    auto addStep = [&R](const QString& key, const QString& detail) {
        R.steps.append({key, detail});
    };

    // wb 有增益才记步骤（与 Python steps 语义一致）
    {
        QVector<double> gains;
        QImage wbImg = whiteBalance(img, p.wb, &gains);
        if (!gains.isEmpty()) {
            addStep(QStringLiteral("wb"),
                    QStringLiteral("R×%1 G×%2 B×%3").arg(gains[0]).arg(gains[1]).arg(gains[2]));
        }
        img = wbImg;   // 无增益时 wb 返回原图
    }

    double lift = 0, rec = 0;
    img = toneCurve(img, st0, p.tone, &lift, &rec);
    if (lift >= 0.02 || rec >= 0.02)
        addStep(QStringLiteral("tone"),
                QStringLiteral("抬阴影 +%1 · 回收高光 +%2").arg(lift).arg(rec));

    bool lv = false;
    img = autoLevels(img, st0, p.levels, &lv);
    if (lv) addStep(QStringLiteral("levels"), QStringLiteral("有界拉伸 6..249"));

    const LumenStats st = measureStats(img);   // 色阶/阴影后重测（Python 同）

    double bf = 0;
    img = brightness(img, st, p.bright, &bf);
    if (bf != 0) addStep(QStringLiteral("bright"), QStringLiteral("增益 ×%1").arg(bf));

    double cf = 0;
    img = contrast(img, st, p.contrast, &cf);
    if (cf != 0) addStep(QStringLiteral("contrast"), QStringLiteral("展开 ×%1").arg(cf));

    double amt = 0;
    img = clarity(img, mx0, p.clarity, &amt);
    if (amt != 0) addStep(QStringLiteral("clarity"), QStringLiteral("明度 USM ×%1").arg(int(amt * 100)));

    double vb = 0;
    img = vibrance(img, st, p.vibrance, &vb);
    if (vb != 0) addStep(QStringLiteral("vibrance"), QStringLiteral("强度 %1").arg(vb));

    const int dl = denoiseLevelByNoise(mx0.noise);
    if (dl) {
        img = denoise(img, dl);
        R.denoise = dl;
        addStep(QStringLiteral("denoise"), QStringLiteral("档位 %1（噪声估计 %2）").arg(dl).arg(mx0.noise));
    }

    img = sharpen(img, p.sharp, mx0.noise);
    addStep(QStringLiteral("sharp"), QStringLiteral("UnsharpMask（噪声 %1）").arg(mx0.noise));

    // ---- 输出自检 + 回退/降级 ----
    const QStringList issues = audit(st0, mx0, img);
    if (issues.isEmpty()) {
        addStep(QStringLiteral("audit"), QStringLiteral("通过"));
    } else {
        // 轻力度重做：仅 wb + tone
        QVector<double> gains;
        QImage g = whiteBalance(orig, p.wb, &gains);
        double l2 = 0, r2 = 0;
        g = toneCurve(g, st0, p.tone, &l2, &r2);
        QStringList stepsG;
        if (!gains.isEmpty()) stepsG.append(QStringLiteral("wb"));
        if (l2 >= 0.02 || r2 >= 0.02) stepsG.append(QStringLiteral("tone"));
        if (!audit(st0, mx0, g).isEmpty()) {
            img = orig;
            R.steps.clear();
            R.steps.append({QStringLiteral("audit"),
                            QStringLiteral("回退原图（%1）").arg(issues.join(';'))});
            R.reverted = true;
        } else {
            img = g;
            R.steps = {};
            for (const QString& kk : stepsG) R.steps.append({kk, QStringLiteral("轻力度")});
            R.steps.append({QStringLiteral("audit"),
                            QStringLiteral("降为轻力度（%1）").arg(issues.join(';'))});
        }
    }
    return img;
}
