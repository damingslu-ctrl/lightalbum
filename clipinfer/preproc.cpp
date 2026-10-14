// preproc.cpp —— CLIP 预处理核：stb_image 解码 + Pillow ImagingResample(bicubic,
// Catmull-Rom a=-0.5) 8bpc 定点两趟缩放 verbatim 移植 + CLIP 归一 + HSV 平均饱和度。
// 平价真源：Pillow libImaging/Resample.c（precompute_coeffs / normalize_coeffs_8bpc /
// _ImagingResample{Horizontal,Vertical}_8bpc / ImagingResampleInner）与
// libImaging/Convert.c rgb2hsv_row；oracle 调用形 = classifier_server.py prep()/
// is_monochrome()（PIL Image.resize，reducing_gap=None → 无预滤波，判例见 preproc.h 头注）。
// 铁律29：整数/定点确定性，无 Qt、无随机、无时间因子。
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "preproc.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <windows.h>

namespace preproc {

bool loadRgb(const std::string& path, RgbImage& out)
{
    // UTF-8 路径 → 宽字符直读（stb 内部 fopen 走 ANSI 码页，中文路径会失配；此处自控）。
    std::vector<uint8_t> bytes;
    {
        const int wn = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), int(path.size()), nullptr, 0);
        if (wn <= 0) return false;
        std::wstring wp(size_t(wn), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, path.c_str(), int(path.size()), &wp[0], wn);
        FILE* f = _wfopen(wp.c_str(), L"rb");
        if (!f) return false;
        uint8_t buf[65536];
        size_t r = 0;
        while ((r = std::fread(buf, 1, sizeof(buf), f)) > 0) bytes.insert(bytes.end(), buf, buf + r);
        std::fclose(f);
    }
    if (bytes.empty()) return false;
    int w = 0, h = 0, ch = 0;
    // magic 嗅探解码（R-15）：stb_image 同解 JPEG/PNG/BMP…；PNG 夹具不换格式。
    uint8_t* px = stbi_load_from_memory(bytes.data(), int(bytes.size()), &w, &h, &ch, 3);
    if (!px || w <= 0 || h <= 0) return false;
    out.w = w; out.h = h;
    out.rgb.assign(px, px + size_t(w) * h * 3);
    stbi_image_free(px);
    return true;
}

namespace {

// ---------- libImaging Resample.c verbatim ----------
constexpr int kPrecisionBits = 32 - 8 - 2;           // 22
constexpr double kFilterSupport = 2.0;               // BICUBIC.support

// bicubic_filter（Resample.c:48，a=-0.5 Catmull-Rom）逐式照抄。
inline double bicubicFilter(double x)
{
    if (x < 0.0) x = -x;
    if (x < 1.0) return ((-0.5 + 2.0) * x - (-0.5 + 3.0)) * x * x + 1.0;
    if (x < 2.0) return (((x - 5.0) * x + 8.0) * x - 4.0) * -0.5;
    return 0.0;
}

// precompute_coeffs + normalize_coeffs_8bpc 合并版：double 权 → int32×2^22 就地。
struct Coeffs {
    int ksize = 0;
    std::vector<int> bounds;                          // outSize*2 (xmin, xmax)
    std::vector<int32_t> kk;                          // outSize*ksize 定点
};

// in0/in1 语义 = box 边界（本工程恒全幅 0..inSize，float 精确覆盖 ≤2^24 整数）。
Coeffs precompute(int inSize, double in0, double in1, int outSize)
{
    Coeffs c;
    double filterscale = (in1 - in0) / outSize;
    const double scale = filterscale;
    if (filterscale < 1.0) filterscale = 1.0;
    const double support = kFilterSupport * filterscale;
    const int ksize = int(std::ceil(support)) * 2 + 1;
    c.ksize = ksize;
    c.bounds.assign(size_t(outSize) * 2, 0);
    std::vector<double> pre(size_t(outSize) * ksize, 0.0);
    const double inv = 1.0 / filterscale;
    for (int xx = 0; xx < outSize; ++xx) {
        const double center = in0 + (xx + 0.5) * scale;
        double ww = 0.0;
        int xmin = int(center - support + 0.5);       // C 截尾 = PIL (int) 语义
        if (xmin < 0) xmin = 0;
        int xmax = int(center + support + 0.5);
        if (xmax > inSize) xmax = inSize;
        xmax -= xmin;
        double* k = &pre[size_t(xx) * ksize];
        int x = 0;
        for (; x < xmax; ++x) {
            const double w = bicubicFilter((x + xmin - center + 0.5) * inv);
            k[x] = w;
            ww += w;
        }
        if (ww != 0.0)
            for (int i = 0; i < xmax; ++i) k[i] /= ww;
        for (; x < ksize; ++x) k[x] = 0.0;
        c.bounds[xx * 2 + 0] = xmin;
        c.bounds[xx * 2 + 1] = xmax;
    }
    // normalize_coeffs_8bpc：同号 ±0.5 再截尾 = round-half-away 的定点化（照抄 :277-283）。
    c.kk.resize(size_t(outSize) * ksize);
    for (size_t i = 0; i < pre.size(); ++i) {
        c.kk[i] = (pre[i] < 0.0) ? int(-0.5 + pre[i] * double(1 << kPrecisionBits))
                                 : int(0.5 + pre[i] * double(1 << kPrecisionBits));
    }
    return c;
}

// clip8 = clip8_lookups[in >> 22]，查表即 clamp 0..255（±640 护栏外亦收敛同义）。
inline uint8_t clip8(int v)
{
    const int q = v >> kPrecisionBits;
    return uint8_t(q < 0 ? 0 : (q > 255 ? 255 : q));
}

// _ImagingResampleHorizontal_8bpc（bands==3 支路）：dst dw×rows，读 src 行 yy+offset。
void horizontalPass(const RgbImage& src, int offset, int dw, int rows, const Coeffs& c,
                    RgbImage& dst)
{
    dst.w = dw; dst.h = rows;
    dst.rgb.assign(size_t(dw) * rows * 3, 0);
    for (int yy = 0; yy < rows; ++yy) {
        const uint8_t* lin = src.rgb.data() + size_t(yy + offset) * src.w * 3;
        uint8_t* lout = dst.rgb.data() + size_t(yy) * dw * 3;
        for (int xx = 0; xx < dw; ++xx) {
            const int xmin = c.bounds[xx * 2 + 0];
            const int xmax = c.bounds[xx * 2 + 1];
            const int32_t* k = &c.kk[size_t(xx) * c.ksize];
            // 与 PIL 四累加器展开等价（整数加法对通道独立、顺序无关；int32 回绕行为一致）。
            int ss0 = 1 << (kPrecisionBits - 1), ss1 = ss0, ss2 = ss0;
            for (int x = 0; x < xmax; ++x) {
                const int kv = k[x];
                const uint8_t* p = lin + size_t(x + xmin) * 3;
                ss0 += p[0] * kv;
                ss1 += p[1] * kv;
                ss2 += p[2] * kv;
            }
            lout[xx * 3 + 0] = clip8(ss0);
            lout[xx * 3 + 1] = clip8(ss1);
            lout[xx * 3 + 2] = clip8(ss2);
        }
    }
}

// _ImagingResampleVertical_8bpc（bands==3 支路）：dst dw×dh，读 src 行 x+xmin+offset 列 xx。
void verticalPass(const RgbImage& src, int offset, const Coeffs& c, RgbImage& dst)
{
    const int dw = dst.w;
    const int dh = dst.h;
    dst.rgb.assign(size_t(dw) * dh * 3, 0);
    for (int yy = 0; yy < dh; ++yy) {
        const int xmin = c.bounds[yy * 2 + 0];
        const int xmax = c.bounds[yy * 2 + 1];
        const int32_t* k = &c.kk[size_t(yy) * c.ksize];
        for (int xx = 0; xx < dw; ++xx) {
            int ss0 = 1 << (kPrecisionBits - 1), ss1 = ss0, ss2 = ss0;
            for (int x = 0; x < xmax; ++x) {
                const int kv = k[x];
                const uint8_t* p = src.rgb.data()
                                 + (size_t(x + xmin + offset) * src.w + xx) * 3;
                ss0 += p[0] * kv;
                ss1 += p[1] * kv;
                ss2 += p[2] * kv;
            }
            dst.rgb[(size_t(yy) * dw + xx) * 3 + 0] = clip8(ss0);
            dst.rgb[(size_t(yy) * dw + xx) * 3 + 1] = clip8(ss1);
            dst.rgb[(size_t(yy) * dw + xx) * 3 + 2] = clip8(ss2);
        }
    }
}

// ImagingResampleInner 全幅 box 支路（box=[0,0,sw,sh]，reducing_gap=None）：
// need_* 判定、竖系数先算全高、横趟后 bounds_vert 左移 ybox_first —— 序 verbatim。
void resampleFull(const RgbImage& src, int dw, int dh, RgbImage& dst)
{
    const bool needH = dw != src.w;                  // xsize!=imIn->xsize || box 非全幅
    const bool needV = dh != src.h;
    Coeffs vert = precompute(src.h, 0.0, double(src.h), dh);
    const int yboxFirst = vert.bounds[0];
    const int yboxLast = vert.bounds[dh * 2 - 2] + vert.bounds[dh * 2 - 1];
    if (!needH && !needV) { dst = src; return; }     // 两趟皆免 → ImagingCopy
    if (needH) {
        Coeffs horiz = precompute(src.w, 0.0, double(src.w), dw);
        for (int i = 0; i < dh; ++i) vert.bounds[i * 2] -= yboxFirst;
        RgbImage temp;
        horizontalPass(src, yboxFirst, dw, yboxLast - yboxFirst, horiz, temp);
        if (needV) {
            dst.w = dw; dst.h = dh;
            verticalPass(temp, 0, vert, dst);
        } else {
            dst = temp;                              // temp 高度 == dh
        }
    } else {
        dst.w = dw; dst.h = dh;
        verticalPass(src, 0, vert, dst);             // 免横趟：竖 bounds 不移位
    }
}

} // namespace

void resizeBicubic(const RgbImage& src, int dw, int dh, RgbImage& dst)
{
    resampleFull(src, dw, dh, dst);
}

// torchvision Resize(int=224) 老式短边换算（open_clip image_transform 底层）：
// int(224*long/short) 截尾；CenterCrop 偏移 int(round(...)) —— Python round 是
// half-to-even，std::round（half-away）在 (dim-224) 为奇数时差 1，必须换 nearbyint 语义。
namespace {
inline int roundHalfToEven(double v)
{
    // 精确复刻 Python round()：0.5 落点取偶数。
    const double f = std::floor(v);
    const double d = v - f;
    if (d > 0.5) return int(f) + 1;
    if (d < 0.5) return int(f);
    const long li = long(f);
    return int(li + (li % 2 == 0 ? 0 : 1));
}
} // namespace

void clipTensor(const RgbImage& src, std::vector<float>& nchw)
{
    int nw, nh;
    if (src.h < src.w) { nw = int(224.0 * src.w / src.h); nh = 224; }
    else               { nw = 224; nh = int(224.0 * src.h / src.w); }
    RgbImage rs;
    resizeBicubic(src, nw, nh, rs);
    const int top = roundHalfToEven((nh - 224) / 2.0);
    const int left = roundHalfToEven((nw - 224) / 2.0);
    static const float kMean[3] = {0.48145466f, 0.4578275f, 0.40821073f};
    static const float kStd[3]  = {0.26862954f, 0.26130258f, 0.27577711f};
    nchw.assign(size_t(3) * 224 * 224, 0.f);
    for (int c = 0; c < 3; ++c) {
        float* plane = nchw.data() + size_t(c) * 224 * 224;
        for (int y = 0; y < 224; ++y) {
            const uint8_t* srow = rs.rgb.data() + (size_t(y + top) * nw + left) * 3;
            float* orow = plane + size_t(y) * 224;
            for (int x = 0; x < 224; ++x) {
                const float v = float(srow[x * 3 + c]) / 255.0f;   // ToTensor float32
                orow[x] = (v - kMean[c]) / kStd[c];                // Normalize float32
            }
        }
    }
}

// is_monochrome verbatim：resize((48,48)) 默认 bicubic → rgb2hsv_row S 通道求和/2304。
// Convert.c rgb2hsv：s=(float)(maxc-minc)/(float)maxc → (int)(s*255.0) 截尾 → CLIP8。
double meanSaturation(const RgbImage& src)
{
    RgbImage t;
    resizeBicubic(src, 48, 48, t);
    double sum = 0.0;
    for (size_t i = 0; i < size_t(t.w) * t.h; ++i) {
        const uint8_t* p = t.rgb.data() + i * 3;
        const int maxc = p[0] > p[1] ? (p[0] > p[2] ? p[0] : p[2]) : (p[1] > p[2] ? p[1] : p[2]);
        const int minc = p[0] < p[1] ? (p[0] < p[2] ? p[0] : p[2]) : (p[1] < p[2] ? p[1] : p[2]);
        int us = 0;
        if (maxc != minc) {
            const double s = double(maxc - minc) / double(maxc);
            const int v = int(s * 255.0);
            us = v < 0 ? 0 : (v > 255 ? 255 : v);
        }
        sum += double(us);
    }
    return sum / 2304.0;
}

} // namespace preproc
