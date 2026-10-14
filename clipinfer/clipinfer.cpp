// clipinfer.exe —— CLIP ViT-B-32 图像塔 + 判定规则独立子进程（第四轮 T7；#76/#77 门后端）。
// 协议（AiProto 单一真源，spec §5）：`REQ <id> <kind> <len>\n` + len 字节 body；
//   kind="jpg"（CLIP 轨唯一合法 kind），body=UTF-8 绝对图像路径（64 MiB 硬上限，超限
//   ERR badframe 并终止读线——父端重启，faceinfer T1 Minor#4 同口径）。
//   成功 `RES <id> <len>\n` + JSON（rules::renderJson：label/score/note/top/sat）；
//   失败 `ERR <id> <code>\n`，code ∈ badprompts|nomodel|loadfail|inferfail|badframe。
// 启动时序（R-14 裁定 + G10 反例装置依赖）：prompts.bin 校验【先于】ORT 会话加载——
//   矩阵 SHA 失配即刻 READY 0 后保活，每 REQ 诚实回 ERR badprompts（不吞 351MB 模型加载，
//   启动秒级）；缺模型 READY 0 + ERR nomodel；会话建不起来 READY 0 + ERR loadfail；
//   两会件齐备且会话建成才 READY 1（镜像 faceinfer spec §5「会话全部建好才 READY 1」）。
// 模型解析（镜像 EraseRunner/faceinfer 语义，无 Qt 自管）：env LA_CLIP_MODEL=模型目录
//   （含 clip_vit_b32_img.onnx + prompts.bin）优先，否则 exe 同目录 sibling。
// 平价真源：classifier_server.py prep/is_monochrome/判定三段（preproc.h 逐条对读）。
// R-15：解码走 stb_image magic 嗅探（JPEG/PNG 通解），clipinfer 全程无 Qt。
// 诊断旗标：
//   --version                → "clipinfer 0.0.0 round4-t7"（装配自检钩）
//   --parity-selftest <img> <gold.json> → §4 容差对拍（label/note verbatim；score/top
//                              Δ≤0.02；sat Δ≤1.0），打印表格 + PARITY-SELFTEST PASS/FAIL。
//   --tower-check <in.f32bin> <ref.f32bin> → R-16：本树 ORT 运行时对 npy 转储输入跑塔，
//                              打印 max_abs_diff/mean_abs/cos（预期 cos≈8.1e-06 级 Δ）。
#include "preproc.h"
#include "core/AiProto.h"

#include <onnxruntime_cxx_api.h>

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

namespace {

constexpr unsigned long long kMaxReqBody = 64ull * 1024 * 1024;

void stripEol(char* line)
{
    size_t n = std::strlen(line);
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
}

bool readExact(FILE* in, char* buf, size_t n)
{
    size_t got = 0;
    while (got < n) {
        const size_t r = std::fread(buf + got, 1, n - got, in);
        if (r == 0) return false;
        got += r;
    }
    return true;
}

bool readFileBytes(const std::string& path, std::vector<uint8_t>& out)
{
    const int wn = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), int(path.size()), nullptr, 0);
    if (wn <= 0) return false;
    std::wstring wp(size_t(wn), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), int(path.size()), &wp[0], wn);
    FILE* f = _wfopen(wp.c_str(), L"rb");
    if (!f) return false;
    uint8_t buf[65536];
    size_t r = 0;
    while ((r = std::fread(buf, 1, sizeof(buf), f)) > 0) out.insert(out.end(), buf, buf + r);
    std::fclose(f);
    return !out.empty();
}

std::wstring widenUtf8(const std::string& s)
{
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), &w[0], n);
    return w;
}

std::string exeDir()
{
    char buf[4096] = {0};
    GetModuleFileNameA(nullptr, buf, DWORD(sizeof(buf) - 1));
    std::string p(buf);
    const size_t s = p.find_last_of("\\/");
    return s == std::string::npos ? std::string(".") : p.substr(0, s);
}

bool fileExists(const std::string& path)
{
    const std::wstring w = widenUtf8(path);
    return GetFileAttributesW(w.c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::string resolveModelDir()
{
    const char* env = std::getenv("LA_CLIP_MODEL");
    if (env && env[0] && fileExists(std::string(env) + "/clip_vit_b32_img.onnx"))
        return env;
    const std::string d = exeDir();
    if (fileExists(d + "/clip_vit_b32_img.onnx")) return d;
    return std::string();
}

// ============================ 启动三段状态机 ============================
enum State { kBadPrompts = 0, kNoModel, kLoadFail, kReady };

const char* stateErrCode(State st)
{
    switch (st) {
        case kBadPrompts: return "badprompts";
        case kNoModel:    return "nomodel";
        case kLoadFail:   return "loadfail";
        default:          return "";
    }
}

State g_state = kNoModel;
rules::PromptTable g_prompts;
Ort::Env*     g_env  = nullptr;   // 会话生命周期 = 进程（子进程模型，无热重载）
Ort::Session* g_sess = nullptr;
std::string   g_inName, g_outName;

// 会话启动件：onnx 定名输入 "image" 1×3×224×224 → 输出 "feat" 1×512（T6 导出契约）；
// 名字从会话读取不硬编码（faceinfer 同口径）。
bool initTower(const std::string& onnxPath)
{
    try {
        if (!g_env) g_env = new Ort::Env(ORT_LOGGING_LEVEL_ERROR, "clipinfer");
        Ort::SessionOptions opt;
        opt.SetIntraOpNumThreads(2);
        opt.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        g_sess = new Ort::Session(*g_env, widenUtf8(onnxPath).c_str(), opt);
        Ort::AllocatorWithDefaultOptions allocator;
        g_inName  = g_sess->GetInputNameAllocated(0, allocator).get();
        g_outName = g_sess->GetOutputNameAllocated(0, allocator).get();
        std::fprintf(stderr, "clipinfer: tower session loaded, input=\"%s\" output=\"%s\"\n",
                     g_inName.c_str(), g_outName.c_str());
        return true;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "clipinfer: tower init failed: %s\n", e.what());
        delete g_sess; g_sess = nullptr;
        return false;
    }
}

// 服务器启动时序：prompts 校验先于 ORT（坏 prompts 秒级 READY 0，G10 反例依赖）。
// 模型目录未解析（env/exe sibling 皆无 onnx）→ 直接 kNoModel（不误触 prompts 路径）。
// 返回 READY_MS（会话初始化实测，进 spec 附录 A/#77 measuredMs 溯源，faceinfer 同式）。
long long startup()
{
    const std::string dir = resolveModelDir();
    if (dir.empty()) {
        std::fprintf(stderr, "clipinfer: clip_vit_b32_img.onnx not found "
                             "(LA_CLIP_MODEL dir or exe sibling)\n");
        g_state = kNoModel;
        return 0;
    }
    std::string reason;
    if (!rules::loadPrompts(dir + "/prompts.bin", g_prompts, reason)) {
        std::fprintf(stderr, "clipinfer: %s (dir=%s)\n", reason.c_str(), dir.c_str());
        g_state = kBadPrompts;
        return 0;
    }
    const auto t0 = std::chrono::steady_clock::now();
    g_state = initTower(dir + "/clip_vit_b32_img.onnx") ? kReady : kLoadFail;
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - t0).count();
}

// 单图全链：解码 → sat + CLIP 张量 → 塔 → 判定三段。异常上抛（调用方转 inferfail）。
std::string classifyImage(const std::string& path)
{
    preproc::RgbImage img;
    if (!preproc::loadRgb(path, img)) return std::string();   // 空串 = loadfail 信号
    std::vector<float> nchw;
    preproc::clipTensor(img, nchw);
    const double sat = preproc::meanSaturation(img);
    const int64_t shape[4] = {1, 3, 224, 224};
    Ort::MemoryInfo memInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value inT = Ort::Value::CreateTensor<float>(memInfo, nchw.data(), nchw.size(), shape, 4);
    const char* inNames[1]  = { g_inName.c_str() };
    const char* outNames[1] = { g_outName.c_str() };
    auto outputs = g_sess->Run(Ort::RunOptions{ nullptr }, inNames, &inT, 1, outNames, 1);
    auto ti = outputs[0].GetTensorTypeAndShapeInfo();
    if (ti.GetElementCount() != 512)
        throw std::runtime_error("tower output is not 512-d");
    const float* feat = outputs[0].GetTensorData<float>();
    rules::Verdict v;
    rules::decide(feat, g_prompts, sat, v);
    return rules::renderJson(v);
}

// ============================ stdio 协议主循环 ============================
int runServer()
{
    _setmode(_fileno(stdin), _O_BINARY);
    AI_PROTO_FORCE_BINARY();
    FILE* in = stdin;
    FILE* out = stdout;

    startup();
    std::fprintf(out, "READY %d\n", g_state == kReady ? 1 : 0);
    std::fflush(out);

    char line[512];
    while (std::fgets(line, sizeof(line), in)) {
        stripEol(line);
        const size_t n = std::strlen(line);
        aiproto::Header h;
        if (!aiproto::parseReq(line, n, h)) {
            std::fprintf(out, "ERR 0 badframe\n");
            std::fflush(out);
            continue;
        }
        if (h.len > kMaxReqBody) {   // 巨体无法消费保同步：报错后终止读线（父端重启）
            std::fprintf(out, "ERR %llu badframe\n", h.id);
            std::fflush(out);
            break;
        }
        std::string body(static_cast<size_t>(h.len), '\0');
        if (h.len > 0 && !readExact(in, &body[0], static_cast<size_t>(h.len))) break;

        if (g_state != kReady) {     // READY 0 保活恒答（spec §5 镜像）
            std::fprintf(out, "ERR %llu %s\n", h.id, stateErrCode(g_state));
            std::fflush(out);
            continue;
        }
        if (std::strcmp(h.kind, "jpg") != 0) {
            std::fprintf(out, "ERR %llu badframe\n", h.id);
            std::fflush(out);
            continue;
        }
        try {
            const std::string json = classifyImage(body);
            if (json.empty()) {      // ≙ classifier 无法读取图像
                std::fprintf(out, "ERR %llu loadfail\n", h.id);
                std::fflush(out);
                continue;
            }
            std::fprintf(out, "RES %llu %llu\n", h.id,
                         static_cast<unsigned long long>(json.size()));   // %llu 通吃口径
            std::fwrite(json.data(), 1, json.size(), out);
            std::fflush(out);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "clipinfer: infer failed (id=%llu): %s\n", h.id, e.what());
            std::fprintf(out, "ERR %llu inferfail\n", h.id);
            std::fflush(out);
        } catch (...) {
            std::fprintf(stderr, "clipinfer: infer failed (id=%llu): unknown exception\n", h.id);
            std::fprintf(out, "ERR %llu inferfail\n", h.id);
            std::fflush(out);
        }
    }
    return 0;
}

// ============================ 金标 JSON 手工扫描（无 Qt/无解析库） ============================
// gold 由 json.dumps(ensure_ascii=False) 生成：raw UTF-8 + \u 转义兜底解析。
struct Gold {
    std::string label, note;                 // note 空串=无（null）
    double score = 0, sat = 0;
    std::vector<std::pair<std::string, double>> top;
};

bool skipWs(const std::string& s, size_t& i)
{
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
    return i < s.size();
}

// 解析字符串值（i 指向开引号）；\uXXXX → UTF-8。
bool scanStr(const std::string& s, size_t& i, std::string& out)
{
    if (i >= s.size() || s[i] != '"') return false;
    ++i;
    out.clear();
    while (i < s.size() && s[i] != '"') {
        if (s[i] != '\\') { out += s[i++]; continue; }
        ++i;
        if (i >= s.size()) return false;
        switch (s[i]) {
            case 'n': out += '\n'; ++i; break;
            case 't': out += '\t'; ++i; break;
            case 'r': out += '\r'; ++i; break;
            case 'b': out += '\b'; ++i; break;
            case 'f': out += '\f'; ++i; break;
            case '"': out += '"'; ++i; break;
            case '\\': out += '\\'; ++i; break;
            case '/': out += '/'; ++i; break;
            case 'u': {
                if (i + 4 >= s.size()) return false;
                unsigned cp = std::strtoul(s.substr(i + 1, 4).c_str(), nullptr, 16);
                i += 5;
                if (cp >= 0xD800 && cp < 0xDC00 && i + 6 <= s.size() &&
                    s[i] == '\\' && s[i + 1] == 'u') {   // 代理对
                    const unsigned lo = std::strtoul(s.substr(i + 2, 4).c_str(), nullptr, 16);
                    i += 6;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                if (cp < 0x80) out += char(cp);
                else if (cp < 0x800) { out += char(0xC0 | (cp >> 6)); out += char(0x80 | (cp & 63)); }
                else if (cp < 0x10000) {
                    out += char(0xE0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 63));
                    out += char(0x80 | (cp & 63));
                } else {
                    out += char(0xF0 | (cp >> 18)); out += char(0x80 | ((cp >> 12) & 63));
                    out += char(0x80 | ((cp >> 6) & 63)); out += char(0x80 | (cp & 63));
                }
                break;
            }
            default: return false;
        }
    }
    if (i >= s.size()) return false;
    ++i;                                            // 闭引号
    return true;
}

bool scanNum(const std::string& s, size_t& i, double& out)
{
    const char* p = s.c_str() + i;
    char* end = nullptr;
    out = std::strtod(p, &end);
    if (end == p) return false;
    i = size_t(end - s.c_str());
    return true;
}

// 定位顶层键（简单全文检索 "<key>": —— gold 键唯一，无嵌套同名歧义）。
size_t findKey(const std::string& s, const char* key)
{
    return s.find(std::string("\"") + key + "\":");
}

bool parseGold(const std::string& s, Gold& g)
{
    size_t k = findKey(s, "label");
    if (k == std::string::npos) return false;
    size_t i = k + 8; skipWs(s, i);
    skipWs(s, i);
    if (!scanStr(s, i, g.label)) return false;
    k = findKey(s, "score");
    if (k == std::string::npos) return false;
    i = k + 8; skipWs(s, i);
    if (!scanNum(s, i, g.score)) return false;
    k = findKey(s, "note");
    if (k == std::string::npos) return false;
    i = k + 7; skipWs(s, i);
    if (s.compare(i, 4, "null") == 0) g.note.clear();
    else if (!scanStr(s, i, g.note)) return false;
    k = findKey(s, "sat");
    if (k == std::string::npos) return false;
    i = k + 6; skipWs(s, i);
    if (!scanNum(s, i, g.sat)) return false;
    k = findKey(s, "top");
    if (k == std::string::npos) return false;
    i = k + 6; skipWs(s, i);
    if (i >= s.size() || s[i] != '[') return false;
    ++i;
    skipWs(s, i);
    if (i < s.size() && s[i] == ']') { ++i; return true; }
    for (;;) {
        skipWs(s, i);
        if (i >= s.size() || s[i] != '[') return false;
        ++i; skipWs(s, i);
        std::string lab; double p = 0;
        if (!scanStr(s, i, lab)) return false;
        skipWs(s, i);
        if (i >= s.size() || s[i] != ',') return false;
        ++i; skipWs(s, i);
        if (!scanNum(s, i, p)) return false;
        skipWs(s, i);
        if (i >= s.size() || s[i] != ']') return false;
        ++i;
        g.top.emplace_back(lab, p);
        skipWs(s, i);
        if (i < s.size() && s[i] == ',') { ++i; continue; }
        if (i < s.size() && s[i] == ']') { ++i; break; }
        return false;
    }
    return true;
}

// ============================ --parity-selftest ============================
// §4 容差：label/note verbatim 全等；score Δ≤0.005；top3 逐对 Δ≤0.02（前 3 对齐）；
// sat Δ≤1.0（R-14 严格带保留：若某夹具越带而判定段+note 全等，门侧可降 band+note——
// 实测阶段裁定，见 task-7-report）。FAIL 退 1，用法/装载错退 2。
int runParitySelftest(int argc, char** argv)
{
    if (argc < 3) {
        std::fprintf(stderr, "usage: clipinfer --parity-selftest <image> <gold.json>\n");
        return 2;
    }
    const long long readyMs = startup();
    std::printf("READY_MS %lld\n", readyMs);
    if (g_state != kReady) {
        std::fprintf(stderr, "parity-selftest: startup failed (state=%s)\n",
                     stateErrCode(g_state));
        return 2;
    }
    std::vector<uint8_t> gb;
    if (!readFileBytes(argv[2], gb)) {
        std::fprintf(stderr, "parity-selftest: gold open failed: %s\n", argv[2]);
        return 2;
    }
    Gold gold;
    if (!parseGold(std::string(gb.begin(), gb.end()), gold)) {
        std::fprintf(stderr, "parity-selftest: gold parse failed: %s\n", argv[2]);
        return 2;
    }
    std::string json;
    try {
        json = classifyImage(argv[1]);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "parity-selftest: infer failed: %s\n", e.what());
        return 2;
    }
    if (json.empty()) {
        std::fprintf(stderr, "parity-selftest: image load failed: %s\n", argv[1]);
        return 2;
    }
    Gold ours;
    if (!parseGold(json, ours)) {   // 自家 RES 同语法，直接复用扫描器
        std::fprintf(stderr, "parity-selftest: self json parse failed: %s\n", json.c_str());
        return 2;
    }

    int fails = 0;
    auto check = [&](const char* name, bool ok) {
        std::printf("%-14s %s\n", name, ok ? "PASS" : "FAIL");
        if (!ok) ++fails;
    };
    std::printf("image   %s\n", argv[1]);
    std::printf("label   gold=\"%s\" ours=\"%s\"\n", gold.label.c_str(), ours.label.c_str());
    check("label", gold.label == ours.label);
    std::printf("note    gold=\"%s\" ours=\"%s\"\n", gold.note.c_str(), ours.note.c_str());
    check("note", gold.note == ours.note);
    std::printf("score   gold=%.4f ours=%.4f delta=%.6f\n",
                gold.score, ours.score, std::fabs(gold.score - ours.score));
    check("score", std::fabs(gold.score - ours.score) <= 0.005);
    std::printf("sat     gold=%.1f ours=%.1f delta=%.6f\n",
                gold.sat, ours.sat, std::fabs(gold.sat - ours.sat));
    check("sat(<=1.0)", std::fabs(gold.sat - ours.sat) <= 1.0);
    for (size_t i = 0; i < 3; ++i) {
        const bool labOk = i < gold.top.size() && i < ours.top.size()
                         && gold.top[i].first == ours.top[i].first;
        const double d = (i < gold.top.size() && i < ours.top.size())
                       ? std::fabs(gold.top[i].second - ours.top[i].second) : 1.0;
        std::printf("top%d      gold=(%s,%.4f) ours=(%s,%.4f) delta=%.6f\n", i,
                    i < gold.top.size() ? gold.top[i].first.c_str() : "-",
                    i < gold.top.size() ? gold.top[i].second : -1.0,
                    i < ours.top.size() ? ours.top[i].first.c_str() : "-",
                    i < ours.top.size() ? ours.top[i].second : -1.0, d);
        check(i == 0 ? "top1(<=0.02)" : (i == 1 ? "top2(<=0.02)" : "top3(<=0.02)"),
              labOk && d <= 0.02);
    }
    if (fails == 0) { std::printf("PARITY-SELFTEST PASS\n"); return 0; }
    std::printf("PARITY-SELFTEST FAIL (%d)\n", fails);
    return 1;
}

// ============================ --tower-check（R-16） ============================
// 输入：in.f32bin = 1×3×224×224 float32LE 原始体（in_f32.npy 剥壳），
//       ref.f32bin = 512 float32LE（ref_feat_f32.npy 剥壳，导出口 ORT 1.17/1.18 生成）。
// 输出：本树 ORT 1.19.2 会话跑同一输入 → max_abs_diff / mean_abs / cos。
int runTowerCheck(int argc, char** argv)
{
    if (argc < 3) {
        std::fprintf(stderr, "usage: clipinfer --tower-check <in.f32bin> <ref.f32bin>\n");
        return 2;
    }
    std::vector<uint8_t> ib, rb;
    if (!readFileBytes(argv[1], ib) || ib.size() != size_t(3) * 224 * 224 * 4) {
        std::fprintf(stderr, "tower-check: input bin must be 3*224*224 float32 (%zu bytes)\n",
                     size_t(3) * 224 * 224 * 4);
        return 2;
    }
    if (!readFileBytes(argv[2], rb) || rb.size() != size_t(512) * 4) {
        std::fprintf(stderr, "tower-check: ref bin must be 512 float32\n");
        return 2;
    }
    const std::string dir = resolveModelDir();
    if (dir.empty() || !initTower(dir + "/clip_vit_b32_img.onnx")) {
        std::fprintf(stderr, "tower-check: tower init failed\n");
        return 2;
    }
    std::vector<float> in(ib.size() / 4);
    std::memcpy(in.data(), ib.data(), ib.size());
    std::vector<float> ref(rb.size() / 4);
    std::memcpy(ref.data(), rb.data(), rb.size());

    const int64_t shape[4] = {1, 3, 224, 224};
    Ort::MemoryInfo memInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value inT = Ort::Value::CreateTensor<float>(memInfo, in.data(), in.size(), shape, 4);
    const char* inNames[1]  = { g_inName.c_str() };
    const char* outNames[1] = { g_outName.c_str() };
    try {
        auto outputs = g_sess->Run(Ort::RunOptions{ nullptr }, inNames, &inT, 1, outNames, 1);
        auto ti = outputs[0].GetTensorTypeAndShapeInfo();
        if (ti.GetElementCount() != 512) {
            std::fprintf(stderr, "tower-check: output not 512-d\n");
            return 2;
        }
        const float* feat = outputs[0].GetTensorData<float>();
        double maxAbs = 0.0, sumAbs = 0.0, dot = 0, na = 0, nb = 0;
        for (int i = 0; i < 512; ++i) {
            const double d = std::fabs(double(feat[i]) - double(ref[i]));
            maxAbs = std::max(maxAbs, d);
            sumAbs += d;
            dot += double(feat[i]) * double(ref[i]);
            na += double(feat[i]) * double(feat[i]);
            nb += double(ref[i]) * double(ref[i]);
        }
        std::printf("ORT %s\n", Ort::GetVersionString().c_str());
        std::printf("max_abs_diff=%.9g mean_abs=%.9g cos=%.12f\n",
                    maxAbs, sumAbs / 512.0, dot / (std::sqrt(na) * std::sqrt(nb)));
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "tower-check: run failed: %s\n", e.what());
        return 2;
    }
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) {   // 装配自检钩（release C10 腿复用）
            std::printf("clipinfer 0.0.0 round4-t7\n");
            return 0;
        }
        // 重基 argv：selftest 内 argv[0]=旗标、[1]/[2]=位参（faceinfer 同式）。
        if (std::strcmp(argv[i], "--parity-selftest") == 0)
            return runParitySelftest(argc - i, argv + i);
        if (std::strcmp(argv[i], "--tower-check") == 0)
            return runTowerCheck(argc - i, argv + i);
    }
    return runServer();
}
