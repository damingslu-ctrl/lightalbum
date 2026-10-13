// imgkit.cpp —— 见 imgkit.h 契约注释。kernel 语义 = inpaint/CMakeLists 判例（inpaint.cpp
// resizeGrayU8 同族半像素中心钳位双线性），但此处不做 /255 归一：uint8→uint8 就近取整
// （cv2.resize uint8 往返同构；0.5 进位由 +0.5f 截断实现，与 OpenCV 定点 round-half-up 一致）。
#include "imgkit.h"

#include <cmath>

namespace imgkit {

void bilin_resize(const uint8_t* src, int sw, int sh,
                  uint8_t* dst, int dw, int dh, int channels)
{
    for (int y = 0; y < dh; ++y) {
        float fy = (y + 0.5f) * float(sh) / float(dh) - 0.5f;
        if (fy < 0.f) fy = 0.f;
        if (fy > float(sh - 1)) fy = float(sh - 1);
        const int   iy  = int(fy);
        const int   iy1 = iy + 1 < sh ? iy + 1 : sh - 1;
        const float dy  = fy - float(iy);
        const uint8_t* r0 = src + size_t(iy)  * size_t(sw) * channels;
        const uint8_t* r1 = src + size_t(iy1) * size_t(sw) * channels;
        uint8_t*       od = dst + size_t(y)   * size_t(dw) * channels;
        for (int x = 0; x < dw; ++x) {
            float fx = (x + 0.5f) * float(sw) / float(dw) - 0.5f;
            if (fx < 0.f) fx = 0.f;
            if (fx > float(sw - 1)) fx = float(sw - 1);
            const int   ix  = int(fx);
            const int   ix1 = ix + 1 < sw ? ix + 1 : sw - 1;
            const float dx  = fx - float(ix);
            for (int c = 0; c < channels; ++c) {
                const float a = float(r0[ix * channels + c]);
                const float b = float(r0[ix1 * channels + c]);
                const float cc = float(r1[ix * channels + c]);
                const float d = float(r1[ix1 * channels + c]);
                const float top = a + (b - a) * dx;
                const float bot = cc + (d - cc) * dx;
                const float v = top + (bot - top) * dy;
                od[x * channels + c] = uint8_t(v + 0.5f);   // 就近取整（非负，截断即四舍）
            }
        }
    }
}

bool bilin_sample_f32(const float* src, int sw, int sh, int channels,
                      float fx, float fy, float* dst)
{
    if (!(fx >= 0.f) || !(fy >= 0.f)) return false;          // NaN/越界统一拒采样
    if (fx > float(sw - 1) || fy > float(sh - 1)) return false;
    const int ix = int(fx), iy = int(fy);
    const int ix1 = ix + 1 < sw ? ix + 1 : sw - 1;
    const int iy1 = iy + 1 < sh ? iy + 1 : sh - 1;
    const float dx = fx - float(ix), dy = fy - float(iy);
    for (int c = 0; c < channels; ++c) {
        const float a = src[(size_t(iy)  * sw + ix ) * channels + c];
        const float b = src[(size_t(iy)  * sw + ix1) * channels + c];
        const float cc = src[(size_t(iy1) * sw + ix ) * channels + c];
        const float d = src[(size_t(iy1) * sw + ix1) * channels + c];
        const float top = a + (b - a) * dx;
        const float bot = cc + (d - cc) * dx;
        dst[c] = top + (bot - top) * dy;
    }
    return true;
}

} // namespace imgkit
