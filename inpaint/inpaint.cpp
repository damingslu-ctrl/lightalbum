// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 LightAlbum Contributors
// ONNX LaMa 独立执行器（inpaint.exe）：读 TASK 帧 → ORT 推理 → 写 OK/ERR 帧。
// 无 Qt（Qt 的 MSVC/MinGW ABI 均不链接；本 exe 纯 stdio + ONNX Runtime C ABI，
// 故 MSVC 或 MinGW 均可构建，见 BUILD.md）。
// 协议即契约（铁律 20）：帧字节与 native/src/core/EraseProto.h、native/tools/inpaint_stub.cpp
// 逐条对齐——TASK 头 "TASK <id> <w> <h> <nb> <nm>\n"，body nb=w*h*3 RGB + nm=w*h mask；
// OK 头 "OK <id> <w> <h> <no>\n" 且恒 no == w*h*3（父端 readResponse 有此守卫）；
// ERR 行 "ERR <id> <reason>\n" 会被父端原样并入错误字符串。每帧响应后 fflush。
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include <io.h>      // _setmode / _fileno（MSVC CRT；MinGW 同名提供）
#include <fcntl.h>   // _O_BINARY

#include <onnxruntime_cxx_api.h>

// ---- 模型契约（对钉死 SHA 的 Carve/LaMa-ONNX lama_fp32.onnx 实测确认，2026-10-11）----
// 双输入（不是早期文档假设的单张 (1,5,H,W) 五通道张量——那是另一派 SAIC 导出；
// 实测本模型输入名与布局，Node 名仍以运行时查询为准，不硬编码）：
//   in[0] "image": (batch,3,mh,mw) float32，RGB 顺序，归一化 [0,1]（=像素/255）
//   in[1] "mask":  (batch,1,mh,mw) float32，白=1=待修复区（LaMa 惯例）
//   out[0]        : (batch,3,mh,mw) float32，值域 0..255（！非 [0,1]，合成时不再乘 255）
//   空间尺寸 mh×mw 固定（本导出=512×512，batch 维动态）。README 声明与实测一致：
//   "Fixed Input Shape: The input shape is fixed at 512x512 pixels."
// 预处理/缩放公式经官方样例对（image.jpg+mask.png → output_onnx_fp32.png）复现验证：
// 上述布局下合成 MAE≈0.015（残差来自 JPEG/重采样，非布局错误；错一个归一化因子 MAE≥0.28）。
// 任意尺寸帧的处理：bilinear 缩放到模型尺寸推理，再 bilinear 缩回原生分辨率，
// 只在 mask==255 像素处覆写——mask 外逐字节等于 base（铁律 20 守恒规则，父端/门禁依赖）。
static Ort::Env*     g_env  = nullptr;
static Ort::Session* g_sess = nullptr;
static std::string   g_imageName;                 // 运行时查到的图像输入节点名
static std::string   g_maskName;                  // 运行时查到的 mask 输入节点名
static std::string   g_outName;                   // 运行时查到的实际输出节点名
static int           g_mw = 0, g_mh = 0;          // 模型固定空间尺寸（W,H）

// 建立 session。cwd 已被父端 EraseRunner 设为 exe 同目录（3rdparty/lama/bin），
// lama.onnx 与 exe 同盘相对解析（零云端：运行期绝不下载任何东西）。
// 失败（文件缺失/布局不符/ORT 异常）返回 false —— 不退出：主循环继续，READY 0 + 每 TASK 回 ERR。
static bool initModel()
{
    try {
        // ORT 日志级别设为 ERROR，且 ORT 默认写 stderr——stdout 是二进制帧通道，绝不能被日志污染。
        g_env = new Ort::Env(ORT_LOGGING_LEVEL_ERROR, "inpaint");
        Ort::SessionOptions opt;
        opt.SetIntraOpNumThreads(2);
        opt.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        g_sess = new Ort::Session(*g_env, L"lama.onnx", opt);
        Ort::AllocatorWithDefaultOptions allocator;

        const size_t nin = g_sess->GetInputCount();
        if (nin != 2) {
            std::fprintf(stderr, "inpaint: expected 2-input (image+mask) lama.onnx, got %zu inputs\n", nin);
            return false;
        }
        g_imageName = g_sess->GetInputNameAllocated(0, allocator).get();
        g_maskName  = g_sess->GetInputNameAllocated(1, allocator).get();
        g_outName   = g_sess->GetOutputNameAllocated(0, allocator).get();

        auto dimsImg  = g_sess->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        auto dimsMask = g_sess->GetInputTypeInfo(1).GetTensorTypeAndShapeInfo().GetShape();
        if (dimsImg.size() != 4 || dimsImg[1] != 3 || dimsImg[2] <= 0 || dimsImg[3] <= 0 ||
            dimsMask.size() != 4 || dimsMask[1] != 1) {
            std::fprintf(stderr, "inpaint: unexpected input layout (want NCHW 3ch image + 1ch mask, fixed H*W)\n");
            return false;
        }
        g_mh = int(dimsImg[2]);
        g_mw = int(dimsImg[3]);
        std::fprintf(stderr, "inpaint: model loaded, image=\"%s\" mask=\"%s\" output=\"%s\" fixed %dx%d\n",
                     g_imageName.c_str(), g_maskName.c_str(), g_outName.c_str(), g_mw, g_mh);
        return true;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "inpaint: model init failed: %s\n", e.what());
        return false;
    } catch (...) {
        std::fprintf(stderr, "inpaint: model init failed: unknown exception\n");
        return false;
    }
}

// 半像素中心对齐的确定性 bilinear（铁律 29：宿主无关、可复现；同尺寸时退化为恒等）。
// src：交错/行主序 uint8 平面（stride 像素步长），输出 (dh,dw) float，除以 255 → [0,1]。
static void resizeGrayU8(const uint8_t* src, int sw, int sh, size_t stride,
                         float* dst, int dw, int dh)
{
    for (int y = 0; y < dh; ++y) {
        float fy = (y + 0.5f) * float(sh) / float(dh) - 0.5f;
        if (fy < 0.f) fy = 0.f;
        if (fy > float(sh - 1)) fy = float(sh - 1);
        const int  iy  = int(fy);
        const int  iy1 = iy + 1 < sh ? iy + 1 : sh - 1;
        const float dy = fy - float(iy);
        for (int x = 0; x < dw; ++x) {
            float fx = (x + 0.5f) * float(sw) / float(dw) - 0.5f;
            if (fx < 0.f) fx = 0.f;
            if (fx > float(sw - 1)) fx = float(sw - 1);
            const int  ix  = int(fx);
            const int  ix1 = ix + 1 < sw ? ix + 1 : sw - 1;
            const float dx = fx - float(ix);
            const float a = float(src[(size_t(iy)  * sw + ix ) * stride]);
            const float b = float(src[(size_t(iy)  * sw + ix1) * stride]);
            const float c = float(src[(size_t(iy1) * sw + ix ) * stride]);
            const float d = float(src[(size_t(iy1) * sw + ix1) * stride]);
            const float top = a + (b - a) * dx;
            const float bot = c + (d - c) * dx;
            dst[size_t(y) * dw + x] = (top + (bot - top) * dy) / 255.0f;
        }
    }
}

// 同 kernel 的 float→float 平面缩放（模型输出 0..255 尺度回缩到原生分辨率用，不除 255）。
static void resizeGrayF32(const float* src, int sw, int sh,
                          float* dst, int dw, int dh)
{
    for (int y = 0; y < dh; ++y) {
        float fy = (y + 0.5f) * float(sh) / float(dh) - 0.5f;
        if (fy < 0.f) fy = 0.f;
        if (fy > float(sh - 1)) fy = float(sh - 1);
        const int  iy  = int(fy);
        const int  iy1 = iy + 1 < sh ? iy + 1 : sh - 1;
        const float dy = fy - float(iy);
        for (int x = 0; x < dw; ++x) {
            float fx = (x + 0.5f) * float(sw) / float(dw) - 0.5f;
            if (fx < 0.f) fx = 0.f;
            if (fx > float(sw - 1)) fx = float(sw - 1);
            const int  ix  = int(fx);
            const int  ix1 = ix + 1 < sw ? ix + 1 : sw - 1;
            const float dx = fx - float(ix);
            const float a = src[size_t(iy)  * sw + ix ];
            const float b = src[size_t(iy)  * sw + ix1];
            const float c = src[size_t(iy1) * sw + ix ];
            const float d = src[size_t(iy1) * sw + ix1];
            dst[size_t(y) * dw + x] = (a + (b - a) * dx) * (1 - dy) + (c + (d - c) * dx) * dy;
        }
    }
}

// LaMa 推理 + 守恒合成（契约见文件头注释块）。
// 输入：base RGB (w*h*3) uint8 + mask (w*h) uint8（255=消除区）。
// 输出：w*h*3 uint8 —— mask 内为模型填充（经模型尺寸往返缩放），mask 外【逐字节】等于 base。
// ORT 任何异常向上抛，由 main 的 catch 转成 "ERR <id> INFER" 行（绝不裸崩溃挂循环）。
static std::vector<uint8_t> runInpaint(const std::vector<uint8_t>& rgb,
                                       const std::vector<uint8_t>& msk,
                                       int w, int h)
{
    const size_t hw  = size_t(w) * size_t(h);
    const size_t mhw = size_t(g_mw) * size_t(g_mh);

    // 1) 组装双输入（planar NCHW float）：image 通道 [0,1]，mask [0,1]。
    std::vector<float> imgIn(3 * mhw);
    std::vector<float> mskIn(mhw);
    for (int c = 0; c < 3; ++c)
        resizeGrayU8(rgb.data() + c, w, h, 3, imgIn.data() + size_t(c) * mhw, g_mw, g_mh);
    resizeGrayU8(msk.data(), w, h, 1, mskIn.data(), g_mw, g_mh);

    const int64_t shapeImg[4]  = { 1, 3, g_mh, g_mw };
    const int64_t shapeMsk[4]  = { 1, 1, g_mh, g_mw };
    Ort::MemoryInfo memInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value imgT = Ort::Value::CreateTensor<float>(
        memInfo, imgIn.data(), imgIn.size(), shapeImg, 4);
    Ort::Value mskT = Ort::Value::CreateTensor<float>(
        memInfo, mskIn.data(), mskIn.size(), shapeMsk, 4);

    const char* inNames[2]  = { g_imageName.c_str(), g_maskName.c_str() };
    Ort::Value  inVals[2]   = { std::move(imgT), std::move(mskT) };
    const char* outNames[1] = { g_outName.c_str() };
    // 6 参重载：输出 Ort::Value 由 ORT 分配并以 std::vector 返回（v1.19 C++ API）。
    auto outputs = g_sess->Run(Ort::RunOptions{ nullptr },
                               inNames, inVals, 2,
                               outNames, 1);

    // 2) 输出 (1,3,mh,mw) float32，实测值域 0..255（非 [0,1]——样例对复现确认）。
    if (outputs.size() != 1) throw std::runtime_error("unexpected output count");
    const size_t nOut = outputs[0].GetTensorTypeAndShapeInfo().GetElementCount();
    if (nOut != 3 * mhw) throw std::runtime_error("model output element count != 3*mh*mw");
    const float* outData = outputs[0].GetTensorData<float>();

    // 3) 缩回原生分辨率（planar，仍 0..255 尺度）。
    std::vector<float> outNative(3 * hw);
    for (int c = 0; c < 3; ++c)
        resizeGrayF32(outData + size_t(c) * mhw, g_mw, g_mh,
                      outNative.data() + size_t(c) * hw, w, h);

    // 4) 关键守恒：先整体字节拷贝 rgb，只把 mask==255 的像素位置覆写模型输出，
    //    其余像素与 base 逐字节相同（父端/门禁依赖此性质）。
    std::vector<uint8_t> res = rgb;
    for (size_t i = 0; i < hw; ++i) {
        if (msk[i] != 255) continue;
        for (int c = 0; c < 3; ++c) {
            float v = outNative[size_t(c) * hw + i];
            if (v < 0.0f)   v = 0.0f;
            if (v > 255.0f) v = 255.0f;
            res[i * 3 + c] = static_cast<uint8_t>(v + 0.5f); // 四舍五入
        }
    }
    return res; // size() 恒 == w*h*3（与入参 nb 相等，main 已校验 nb==w*h*3）
}

int main()
{
    // 偏差（正确性必需，Task 2 MinGW stub 实测教训）：Windows CRT stdin/stdout 默认文本模式，
    // 输出侧会把二进制 RGB 体内的每个 0x0A 扩大为 0D0A（长度超出 OK 头承诺的 no，父端
    // EraseProto::readResponse 必坏），输入侧 CRLF 也会被改写。必须在打印 READY 之前强制二进制模式。
    _setmode(_fileno(stdin),  _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);

    // session 只建一次，且在 READY 之前：成功→READY 1；失败→READY 0。
    // 父端 EraseRunner::ensureStarted 只检查 readLine() startsWith("READY")，READY 0 合法通过；
    // 此后每个 TASK 回 "ERR <id> no model"，父端 applyResult 把任务诚实标记失败（无崩溃、无挂起）。
    const bool modelOk = initModel();
    fprintf(stdout, "READY %d\n", modelOk ? 1 : 0);
    fflush(stdout);

    char hdr[256];
    while (fgets(hdr, sizeof(hdr), stdin)) {   // stdin EOF → fgets 返回 NULL → 退出循环、return 0（与 stub 一致）
        unsigned id; int w, h, nb, nm;
        // 头行解析失败：流无法再对齐，终止循环让父端走重启路径（绝不拿错位字节当 body 读）。
        if (sscanf(hdr, "TASK %u %d %d %d %d", &id, &w, &h, &nb, &nm) != 5) break;
        if (w <= 0 || h <= 0 || nb < 0 || nm < 0) break; // 畸形尺寸：同样无法安全对齐

        std::vector<uint8_t> rgb((size_t(nb)));
        std::vector<uint8_t> msk((size_t(nm)));
        if (nb && fread(rgb.data(), 1, size_t(nb), stdin) != size_t(nb)) break; // 短读=流损坏
        if (nm && fread(msk.data(), 1, size_t(nm), stdin) != size_t(nm)) break;

        if (!modelOk) {                          // 模型不可用：协议保持在线，逐任务诚实报错
            fprintf(stdout, "ERR %u no model\n", id);
            fflush(stdout);
            continue;
        }
        // nb/nm 与 w,h 的一致性守卫（父端保证，但契约要求字节精确；错位会毁掉后续帧）。
        if (size_t(nb) != size_t(w) * size_t(h) * 3 || size_t(nm) != size_t(w) * size_t(h)) {
            fprintf(stdout, "ERR %u BADSIZE\n", id);
            fflush(stdout);
            continue;
        }

        try {
            std::vector<uint8_t> res = runInpaint(rgb, msk, w, h);
            // 兜底：res 恒应 == nb == w*h*3（父端 readResponse 拒绝 no != w*h*3）。
            if (res.size() != size_t(nb)) {
                fprintf(stdout, "ERR %u SIZE\n", id);
                fflush(stdout);
                continue;
            }
            fprintf(stdout, "OK %u %d %d %d\n", id, w, h, int(res.size()));
            fwrite(res.data(), 1, res.size(), stdout);
            fflush(stdout);
        } catch (const std::exception& e) {
            fprintf(stderr, "inpaint: inference failed (id=%u): %s\n", id, e.what());
            fprintf(stdout, "ERR %u INFER\n", id);
            fflush(stdout);
        } catch (...) {
            fprintf(stderr, "inpaint: inference failed (id=%u): unknown exception\n", id);
            fprintf(stdout, "ERR %u INFER\n", id);
            fflush(stdout);
        }
    }
    return 0;
}
