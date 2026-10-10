// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 LightAlbum Contributors
// ONNX LaMa 独立执行器（inpaint.exe）：读 TASK 帧 → ORT 推理 → 写 OK/ERR 帧。
// 无 Qt（MSVC 与父进程 native/ 的 MinGW ABI 不兼容），纯 stdio + ONNX Runtime C++ API。
// 协议即契约（铁律 20）：帧字节与 native/src/core/EraseProto.h、native/tools/inpaint_stub.cpp
// 逐条对齐——TASK 头 "TASK <id> <w> <h> <nb> <nm>\n"，body nb=w*h*3 RGB + nm=w*h mask；
// OK 头 "OK <id> <w> <h> <no>\n" 且恒 no == w*h*3（父端 readResponse 有此守卫）；
// ERR 行 "ERR <id> <reason>\n" 会被父端原样并入错误字符串。每帧响应后 fflush。
// 编译与部署见 BUILD.md（含 Netron 节点名核对表）。
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include <io.h>      // _setmode / _fileno（MSVC CRT）
#include <fcntl.h>   // _O_BINARY

#include <onnxruntime_cxx_api.h>

// ---- 节点名文档常量（SAIC-MLP/LaMa-ONNX 导出惯例，仅作文档；BUILD.md Netron 核对表）----
// 本程序【不】用常量绑定节点名：session 建立后查询第 0 输入/第 0 输出的实际名字
// （GetInputNameAllocated / GetOutputNameAllocated），因此对任意命名的 lama.onnx 都可用，
// 只要其张量布局符合下方契约（输入 (1,5,H,W) float32 NCHW、输出 (1,3,H,W) float32）。
[[maybe_unused]] static const char* IN_NAME_DOC  = "input";   // 期望名：input
[[maybe_unused]] static const char* OUT_NAME_DOC = "output";  // 期望名：output

// ---- 模型会话（Env 必须先于 Session 存活且常驻；进程生命周期内只建一次）----
static Ort::Env*     g_env  = nullptr;
static Ort::Session* g_sess = nullptr;
static std::string   g_inName;                    // 运行时查到的实际输入节点名
static std::string   g_outName;                   // 运行时查到的实际输出节点名

// 建立 session。cwd 已被父端 EraseRunner 设为 exe 同目录（3rdparty/lama/bin），
// lama.onnx 与 exe 同盘相对解析（零云端：运行期绝不下载任何东西）。
// 失败（文件缺失/ORT 异常）返回 false —— 不退出：主循环继续，READY 0 + 每 TASK 回 ERR。
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
        g_inName  = g_sess->GetInputNameAllocated(0, allocator).get();
        g_outName = g_sess->GetOutputNameAllocated(0, allocator).get();
        std::fprintf(stderr, "inpaint: model loaded, input=\"%s\" output=\"%s\"\n",
                     g_inName.c_str(), g_outName.c_str());
        return true;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "inpaint: model init failed: %s\n", e.what());
        return false;
    } catch (...) {
        std::fprintf(stderr, "inpaint: model init failed: unknown exception\n");
        return false;
    }
}

// LaMa 推理 + 守恒合成。
// 输入：base RGB (w*h*3) uint8 + mask (w*h) uint8（255=消除区）。
// 输出：w*h*3 uint8 —— mask 内为模型填充，mask 外【逐字节】等于 base（铁律 20 守恒规则）。
// ORT 任何异常向上抛，由 main 的 catch 转成 "ERR <id> INFER" 行（绝不裸崩溃挂循环）。
static std::vector<uint8_t> runInpaint(const std::vector<uint8_t>& rgb,
                                       const std::vector<uint8_t>& msk,
                                       int w, int h)
{
    const size_t hw = size_t(w) * size_t(h);

    // 1) 组装 (1,5,H,W) float32 NCHW 输入：
    //    ch0..2 = rgb[0..2]/255，ch3 = mask/255，ch4 = 全 1。
    std::vector<float> in(5 * hw);
    for (size_t i = 0; i < hw; ++i) {
        in[0 * hw + i] = float(rgb[i * 3 + 0]) / 255.0f;
        in[1 * hw + i] = float(rgb[i * 3 + 1]) / 255.0f;
        in[2 * hw + i] = float(rgb[i * 3 + 2]) / 255.0f;
        in[3 * hw + i] = float(msk[i])         / 255.0f;
        in[4 * hw + i] = 1.0f;
    }
    const int64_t shape[4] = { 1, 5, h, w };
    Ort::MemoryInfo memInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
        memInfo, in.data(), in.size(), shape, 4);

    const char* inNames[1]   = { g_inName.c_str() };
    const char* outNames[1]  = { g_outName.c_str() };
    // 6 参重载：输出 Ort::Value 由 ORT 分配并以 std::vector 返回（v1.19 C++ API）。
    auto outputs = g_sess->Run(Ort::RunOptions{ nullptr },
                               inNames, &inputTensor, 1,
                               outNames, 1);

    // 2) 输出 (1,3,H,W) float32：*255 并 clamp 到 [0,255]。
    if (outputs.size() != 1) throw std::runtime_error("unexpected output count");
    const float* outData = outputs[0].GetTensorData<float>();
    const size_t nOut = outputs[0].GetTensorTypeAndShapeInfo().GetElementCount();
    if (nOut < 3 * hw) throw std::runtime_error("model output smaller than 3*H*W");

    // 3) 关键守恒：先整体字节拷贝 rgb，只把 mask==255 的像素位置覆写模型输出，
    //    其余像素与 base 逐字节相同（父端/门禁依赖此性质）。
    std::vector<uint8_t> res = rgb;
    for (size_t i = 0; i < hw; ++i) {
        if (msk[i] != 255) continue;
        for (int c = 0; c < 3; ++c) {
            float v = outData[size_t(c) * hw + i] * 255.0f;
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

        std::vector<uint8_t> rgb(size_t(nb));
        std::vector<uint8_t> msk(size_t(nm));
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
