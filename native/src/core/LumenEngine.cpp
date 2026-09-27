#include "LumenEngine.h"
#include <cmath>
#include <cstring>
#include <algorithm>

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

inline int clamp255(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }
inline int chopMul(int a, int b) { return (a * b) / 255; }   // ImageChops.multiply

QImage grayLike(const QImage& a) { return QImage(a.size(), QImage::Format_Grayscale8); }

// Grayscale8 二元操作：0=mul 1=add 2=sub
QImage chopGray(const QImage& a, const QImage& b, int op)
{
    QImage out = grayLike(a);
    for (int y = 0; y < a.height(); ++y) {
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
    return out;
}

QImage invertGray(const QImage& a)
{
    QImage out = grayLike(a);
    for (int y = 0; y < a.height(); ++y) {
        const uchar* pa = a.constScanLine(y);
        uchar* po = out.scanLine(y);
        for (int x = 0; x < a.width(); ++x) po[x] = uchar(255 - pa[x]);
    }
    return out;
}

QImage pointGray(const QImage& a, const int lut[256])
{
    QImage out = grayLike(a);
    for (int y = 0; y < a.height(); ++y) {
        const uchar* pa = a.constScanLine(y);
        uchar* po = out.scanLine(y);
        for (int x = 0; x < a.width(); ++x) po[x] = uchar(lut[pa[x]]);
    }
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
    for (int y = 0; y < img.height(); ++y) {
        QRgb* d = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x)
            d[x] = qRgb(lut[qRed(d[x])], lut[qGreen(d[x])], lut[qBlue(d[x])]);
    }
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
    const QImage bl = LumenEngine::gaussianBlur(small, radius);
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
    for (int y = 0; y < src.height(); ++y) {
        const QRgb* s = reinterpret_cast<const QRgb*>(src.constScanLine(y));
        uchar* d = out.scanLine(y);
        for (int x = 0; x < src.width(); ++x)
            d[x] = uchar(toL(qRed(s[x]), qGreen(s[x]), qBlue(s[x])));
    }
    return out;
}

void LumenEngine::rgb2hsv(const QImage& rgb, QImage* H, QImage* S, QImage* V)
{
    QImage src = rgb.convertToFormat(QImage::Format_RGB32);
    *H = QImage(src.size(), QImage::Format_Grayscale8);
    *S = QImage(src.size(), QImage::Format_Grayscale8);
    *V = QImage(src.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < src.height(); ++y) {
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
}

QImage LumenEngine::hsv2rgb(const QImage& h, const QImage& s, const QImage& v)
{
    QImage out(h.size(), QImage::Format_RGB32);
    for (int y = 0; y < h.height(); ++y) {
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
    return out;
}

QImage LumenEngine::gaussianBlur(const QImage& src, double radius)
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
    for (int y = 0; y < h; ++y) {
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
    for (int y = 0; y < h; ++y) {
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
    return img;
}

QImage LumenEngine::unsharpMask(const QImage& src, double radius, int percent, int threshold)
{
    QImage img = src.convertToFormat(QImage::Format_RGB32);
    const QImage blur = gaussianBlur(img, radius);
    const double amt = percent / 100.0;
    for (int y = 0; y < img.height(); ++y) {
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
    for (int y = 0; y < img.height(); ++y) {
        QRgb* d = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x)
            d[x] = qRgb(clamp255(int(qRed(d[x]) * f + 0.5)),
                        clamp255(int(qGreen(d[x]) * f + 0.5)),
                        clamp255(int(qBlue(d[x]) * f + 0.5)));
    }
    return img;
}

QImage LumenEngine::enhanceColor(const QImage& rgb, double f)
{
    const QImage gray = toL8(rgb);
    QImage img = rgb.convertToFormat(QImage::Format_RGB32);
    for (int y = 0; y < img.height(); ++y) {
        const uchar* gl = gray.constScanLine(y);
        QRgb* d = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const double g = gl[x];
            d[x] = qRgb(clamp255(int(qRed(d[x]) * f + g * (1 - f) + 0.5)),
                        clamp255(int(qGreen(d[x]) * f + g * (1 - f) + 0.5)),
                        clamp255(int(qBlue(d[x]) * f + g * (1 - f) + 0.5)));
        }
    }
    return img;
}

// ============================================================ 测量
LumenStats LumenEngine::measureStats(const QImage& rgb)
{
    LumenStats st;
    const QImage small = boxDownscale(rgb, 256);
    QImage hI, sI, vI;
    rgb2hsv(small, &hI, &sI, &vI);
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
    rgb2hsv(small, &hI, &sI, &vI);
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
    rgb2hsv(rgb, &hI, &sI, &vI);
    const double sc = std::min(1.0, 512.0 / std::max(1, std::max(rgb.width(), rgb.height())));
    QImage smImg = rgb;
    if (sc < 1.0)
        smImg = rgb.scaled(std::max(1, int(rgb.width() * sc)),
                           std::max(1, int(rgb.height() * sc)),
                           Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QImage hsI, ssI, vsI;
    rgb2hsv(smImg, &hsI, &ssI, &vsI);
    Q_UNUSED(hsI);
    const double rs = std::max(3.0, std::min(smImg.width(), smImg.height()) / 40.0);
    const QImage mm = gaussianBlur(vsI, rs);

    QImage outV = vI;
    if (lift >= 0.02) {
        int wLut[256], gLut[256], rampLut[256], liftLut[256];
        for (int x = 0; x < 256; ++x) {
            wLut[x] = clamp255(int(std::lround(255.0 * std::pow(std::max(0.0, (130.0 - x) / 130.0), 1.6))));
            gLut[x] = clamp255(int(std::lround(255.0 * (1.0 - 0.5 * (x / 255.0)))));
            rampLut[x] = std::min(255, int(qint64(x) * 255 / 24));
            liftLut[x] = int(x * lift);
        }
        QImage wm = gaussianBlur(chopGray(pointGray(mm, wLut), pointGray(ssI, gLut), 0), rs);
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
        QImage w2 = gaussianBlur(pointGray(mm, w2Lut), rs);
        if (sc < 1.0)
            w2 = w2.scaled(rgb.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        QImage capped = pointGray(chopGray(vI, w2, 0), capLut);
        outV = chopGray(outV, pointGray(capped, recLut), 2);
    }
    if (liftOut) *liftOut = std::round(lift * 1000.0) / 1000.0;
    if (recoverOut) *recoverOut = std::round(rec * 1000.0) / 1000.0;
    return hsv2rgb(hI, sI, outV);
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
        rgb2hsv(boxDownscale(rgb, 256), &hI, &sI, &vI);
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
    rgb2hsv(rgb, &hI, &sI, &vI);
    // V 通道 USM（借 RGB32 通道等价实现）
    QImage vRgb(vI.size(), QImage::Format_RGB32);
    for (int y = 0; y < vI.height(); ++y) {
        const uchar* g = vI.constScanLine(y);
        QRgb* d = reinterpret_cast<QRgb*>(vRgb.scanLine(y));
        for (int x = 0; x < vI.width(); ++x) d[x] = qRgb(g[x], g[x], g[x]);
    }
    const QImage usm = unsharpMask(vRgb, 6.0, int(std::lround(amt * 100)), 0);
    QImage v2(vI.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < vI.height(); ++y) {
        const QRgb* s = reinterpret_cast<const QRgb*>(usm.constScanLine(y));
        uchar* d = v2.scanLine(y);
        for (int x = 0; x < vI.width(); ++x) d[x] = uchar(qRed(s[x]));
    }
    if (amtOut) *amtOut = std::round(amt * 1000.0) / 1000.0;
    return hsv2rgb(hI, sI, v2);
}

QImage LumenEngine::vibrance(const QImage& rgb, const LumenStats& st, double k, double* vOut)
{
    if (vOut) *vOut = 0;
    if (st.sat >= 0.30 || st.sat < 0.02) return rgb.convertToFormat(QImage::Format_RGB32);
    const double v = std::min(0.45, (0.30 - st.sat) * 1.6) * k;
    if (v < 0.05) return rgb.convertToFormat(QImage::Format_RGB32);
    QImage hI, sI, vI;
    rgb2hsv(rgb, &hI, &sI, &vI);
    int lut[256];
    for (int x = 0; x < 256; ++x)
        lut[x] = std::min(255, int(std::lround(x * (1.0 + v * (1.0 - x / 255.0)))));
    if (vOut) *vOut = std::round(v * 1000.0) / 1000.0;
    return hsv2rgb(hI, pointGray(sI, lut), vI);
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
            const double r = qRed(s[x]), g = qGreen(s[x]), b = qBlue(s[x]);
            py[x]  = uchar(clamp255(int(0.299 * r + 0.587 * g + 0.114 * b + 0.5)));
            pcb[x] = uchar(clamp255(int(-0.168736 * r - 0.331264 * g + 0.5 * b + 128.5)));
            pcr[x] = uchar(clamp255(int(0.5 * r - 0.418688 * g - 0.081312 * b + 128.5)));
        }
    }
    if (level == 1) {
        yI = gaussianBlur(yI, 1.0);
        cbI = gaussianBlur(cbI, 1.2);
        crI = gaussianBlur(crI, 1.2);
    } else {
        yI = smoothG(median3G(yI));
        cbI = gaussianBlur(median3G(cbI), 1.6);
        crI = gaussianBlur(median3G(crI), 1.6);
    }
    QImage out(w, h, QImage::Format_RGB32);
    for (int yy = 0; yy < h; ++yy) {
        const uchar* py = yI.constScanLine(yy);
        const uchar* pcb = cbI.constScanLine(yy);
        const uchar* pcr = crI.constScanLine(yy);
        QRgb* d = reinterpret_cast<QRgb*>(out.scanLine(yy));
        for (int x = 0; x < w; ++x) {
            const double Y = py[x], Cb = pcb[x] - 128.0, Cr = pcr[x] - 128.0;
            d[x] = qRgb(clamp255(int(Y + 1.402 * Cr + 0.5)),
                        clamp255(int(Y - 0.344136 * Cb - 0.714136 * Cr + 0.5)),
                        clamp255(int(Y + 1.772 * Cb + 0.5)));
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
    return unsharpMask(rgb, r, percent, threshold);
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
