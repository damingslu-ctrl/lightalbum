// scrfd.cpp —— face_server.py 检测链逐行翻译件（R-P1：oracle 行级对应即正确性机制）。
// 对照表（face_server.py 行号）：
//   _anchors            :61-68   → anchorsFor()
//   distance2bbox       :71-76   → distance2bbox()
//   distance2kps        :79-84   → distance2kps()
//   _nms                :87-108  → nms()（+1 像素宽高约定、1e-9 epsilon verbatim）
//   _load（改 ORT C API）:121-129 → scrfd_init()
//   detect()            :136-189 → scrfd_detect()
// 通道方向（brief Step 1 钉死，二选一取后者）：face_server 输入 BGR，blob 组装处做
// `canvas[...,::-1]` 得到 planar [R,G,B]。本移植的输入 rgbHWC 已是 QImageReader 直出
// RGB —— 即 BGR→RGB 反转已在源头消化，blob 通道序按 c=0,1,2 直取 R,G,B；
// 若未来调用方改交 BGR，必须在 blob 处反转，否则平价静默破坏。
#include "scrfd.h"
#include "imgkit.h"

#include <onnxruntime_cxx_api.h>

#include <windows.h>   // MultiByteToWideChar（ORTCHAR_T=wchar_t on Windows）

#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

namespace Faceinfer {

// ---- verbatim 常量（face_server.py:50-54，一字不差）----
static const int   INPUT_SIZE  = 640;
static const int   STRIDES[3]  = { 8, 16, 32 };
static const int   NUM_ANCHORS = 2;
static const float DET_THRESH  = 0.5f;
static const float NMS_THR     = 0.4f;

namespace {

Ort::Env*     g_env  = nullptr;   // 会话生命周期 = 进程（子进程模型，无热重载）
Ort::Session* g_sess = nullptr;
std::string   g_inName;
std::vector<std::string>  g_outNamesStr;
std::vector<const char*>  g_outNames;

// face_server.py:61-68 _anchors —— 网格锚点中心（640 空间，每格 NUM_ANCHORS 个同中心
// anchor 相邻排列）。meshgrid 默认 'xy' + reshape(-1,2) 行主序 = y 外层、x 内层；
// astype(float32)*stride 与 int 直乘在 <2048 整数域等值（float32 精确表示）。
std::vector<float> anchorsFor(int stride)
{
    const int hf = INPUT_SIZE / stride;
    std::vector<float> centers;
    centers.reserve(size_t(hf) * hf * NUM_ANCHORS * 2);
    for (int j = 0; j < hf; ++j)
        for (int i = 0; i < hf; ++i)
            for (int a = 0; a < NUM_ANCHORS; ++a) {          // stack([c]*NA, axis=1) 相邻复制
                centers.push_back(float(i * stride));
                centers.push_back(float(j * stride));
            }
    return centers;
}

// face_server.py:71-76 distance2bbox（扁平 (N,2) 点集 + (N,4) 距离 → (N,4) 框）。
void distance2bbox(const std::vector<float>& pts, const std::vector<float>& dist,
                   std::vector<float>& out)
{
    const size_t n = pts.size() / 2;
    out.resize(n * 4);
    for (size_t p = 0; p < n; ++p) {
        const float px = pts[p * 2], py = pts[p * 2 + 1];
        out[p * 4 + 0] = px - dist[p * 4 + 0];
        out[p * 4 + 1] = py - dist[p * 4 + 1];
        out[p * 4 + 2] = px + dist[p * 4 + 2];
        out[p * 4 + 3] = py + dist[p * 4 + 3];
    }
}

// face_server.py:79-84 distance2kps（range(0,10,2) 列序 d0x,d0y,d1x…；stack(axis=-1)
// + reshape(-1,5,2) 同构为扁平 (N,5,2)）。
void distance2kps(const std::vector<float>& pts, const std::vector<float>& dist,
                  std::vector<float>& out)
{
    const size_t n = pts.size() / 2;
    out.resize(n * 10);
    for (size_t p = 0; p < n; ++p)
        for (int i = 0; i < 10; i += 2) {
            out[p * 10 + i + 0] = pts[p * 2]     + dist[p * 10 + i];
            out[p * 10 + i + 1] = pts[p * 2 + 1] + dist[p * 10 + i + 1];
        }
}

// face_server.py:87-108 _nms —— dets 为 (N,5) 扁平（x1,y1,x2,y2,score），返回 keep 行号。
// +1 像素宽/高约定与 1e-9 epsilon verbatim；s.argsort()[::-1] = 稳定升序排后整体反转
// （并列分数场景 numpy quicksort 本无定义，取确定性反转语义，#78 容差吸收极端并列）。
std::vector<size_t> nms(const std::vector<float>& dets, float thr = NMS_THR)
{
    std::vector<size_t> keep;
    const size_t n = dets.size() / 5;
    if (n == 0) return keep;                                  // dets.shape[0]==0 → []
    std::vector<float> areas(n);
    for (size_t i = 0; i < n; ++i)
        areas[i] = (dets[i * 5 + 2] - dets[i * 5 + 0] + 1.f)
                 * (dets[i * 5 + 3] - dets[i * 5 + 1] + 1.f);
    std::vector<size_t> order(n);
    for (size_t i = 0; i < n; ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(),
                     [&](size_t a, size_t b) { return dets[a * 5 + 4] < dets[b * 5 + 4]; });
    std::reverse(order.begin(), order.end());                 // [::-1]
    while (!order.empty()) {
        const size_t i = order[0];
        keep.push_back(i);
        if (order.size() == 1) break;
        std::vector<size_t> nxt;
        for (size_t k = 1; k < order.size(); ++k) {
            const size_t j = order[k];
            const float xx1 = std::max(dets[i * 5 + 0], dets[j * 5 + 0]);
            const float yy1 = std::max(dets[i * 5 + 1], dets[j * 5 + 1]);
            const float xx2 = std::min(dets[i * 5 + 2], dets[j * 5 + 2]);
            const float yy2 = std::min(dets[i * 5 + 3], dets[j * 5 + 3]);
            const float w = std::max(0.0f, xx2 - xx1 + 1.f);
            const float h = std::max(0.0f, yy2 - yy1 + 1.f);
            const float inter = w * h;
            const float ovr = inter / (areas[i] + areas[j] - inter + 1e-9f);
            if (ovr <= thr) nxt.push_back(j);                 // order = order[1:][ovr <= thr]
        }
        order.swap(nxt);
    }
    return keep;
}

std::wstring widenUtf8(const std::string& s)
{
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), &w[0], n);
    return w;
}

} // namespace

bool scrfd_init(const std::string& modelPath)
{
    try {
        if (!g_env) g_env = new Ort::Env(ORT_LOGGING_LEVEL_ERROR, "faceinfer"); // 日志只走 stderr
        Ort::SessionOptions opt;
        opt.SetIntraOpNumThreads(2);
        opt.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        if (g_sess) { delete g_sess; g_sess = nullptr; }
        g_sess = new Ort::Session(*g_env, widenUtf8(modelPath).c_str(), opt);
        Ort::AllocatorWithDefaultOptions allocator;
        g_inName = g_sess->GetInputNameAllocated(0, allocator).get();
        g_outNamesStr.clear(); g_outNames.clear();
        const size_t nout = g_sess->GetOutputCount();
        for (size_t i = 0; i < nout; ++i)
            g_outNamesStr.push_back(g_sess->GetOutputNameAllocated(i, allocator).get());
        for (auto& s : g_outNamesStr) g_outNames.push_back(s.c_str());
        std::fprintf(stderr, "faceinfer: det session loaded, input=\"%s\" outputs=%zu\n",
                     g_inName.c_str(), nout);
        return true;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "faceinfer: det init failed: %s\n", e.what());
        delete g_sess; g_sess = nullptr;
        return false;
    }
}

bool scrfd_ready() { return g_sess != nullptr; }

Det scrfd_detect(const std::vector<uint8_t>& rgbHWC, int w, int h)
{
    if (!g_sess) throw std::runtime_error("scrfd_detect: session not initialised");
    if (w <= 0 || h <= 0 || rgbHWC.size() < size_t(w) * size_t(h) * 3)
        throw std::runtime_error("scrfd_detect: bad image dims");

    // ---- detect() :138-146 letterbox blob ----
    const int m = std::max(h, w);
    const double scale = m ? double(INPUT_SIZE) / double(m) : 1.0;   // python INPUT_SIZE/max(h,w)
    // int(round(x))：Python 内建 round 半偶舍入 → std::nearbyint（FE_TONEAREST 默认）同义。
    const int nh = int(std::nearbyint(h * scale));
    const int nw = int(std::nearbyint(w * scale));
    std::vector<uint8_t> canvas(size_t(INPUT_SIZE) * INPUT_SIZE * 3, uint8_t(0));
    if (nw > 0 && nh > 0) {
        std::vector<uint8_t> resized(size_t(nw) * size_t(nh) * 3);
        imgkit::bilin_resize(rgbHWC.data(), w, h, resized.data(), nw, nh, 3);   // cv2.resize
        for (int y = 0; y < nh; ++y)                        // canvas[:nh,:nw] = resized
            std::memcpy(canvas.data() + size_t(y) * INPUT_SIZE * 3,
                        resized.data() + size_t(y) * nw * 3, size_t(nw) * 3);
    }
    const int64_t inShape[4] = { 1, 3, INPUT_SIZE, INPUT_SIZE };
    std::vector<float> blob(size_t(3) * INPUT_SIZE * INPUT_SIZE);
    for (int c = 0; c < 3; ++c)
        for (int y = 0; y < INPUT_SIZE; ++y)
            for (int x = 0; x < INPUT_SIZE; ++x) {
                // (canvas.astype(float32)-127.5)/128.0；通道反转方向见文件头注释块。
                const uint8_t v = canvas[(size_t(y) * INPUT_SIZE + x) * 3 + c];
                blob[size_t(c) * INPUT_SIZE * INPUT_SIZE + size_t(y) * INPUT_SIZE + x] =
                    (float(v) - 127.5f) / 128.0f;
            }

    // ---- :148 det.run(None, {in: blob})：ORT C API 全输出 ----
    Ort::MemoryInfo memInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value inT = Ort::Value::CreateTensor<float>(memInfo, blob.data(), blob.size(), inShape, 4);
    const char* inNames[1] = { g_inName.c_str() };
    auto outputs = g_sess->Run(Ort::RunOptions{ nullptr }, inNames, &inT, 1,
                               g_outNames.data(), g_outNames.size());
    if (outputs.size() != g_outNames.size())
        throw std::runtime_error("scrfd_detect: ORT output count mismatch");

    struct Out { const float* p; size_t size; int64_t last; };
    std::vector<Out> outs;
    outs.reserve(outputs.size());
    for (size_t i = 0; i < outputs.size(); ++i) {
        auto ti = outputs[i].GetTensorTypeAndShapeInfo();
        auto shp = ti.GetShape();
        outs.push_back({ outputs[i].GetTensorData<float>(), ti.GetElementCount(),
                         shp.empty() ? -1 : shp.back() });
    }

    // ---- :150-166 分组 + bycount：末维 ∈{1,4,10} 三桶，各按元素总数降序（python sorted
    //      稳定）映射 STRIDES 降序 12800/3200/800。:156 的 `*0` 占位列表在 oracle 中即弃，
    //      不翻译；bboxs_raw/kpss_raw 从 outs 重收集为权威路径。----
    auto bycount = [](std::vector<Out>& v) {
        std::stable_sort(v.begin(), v.end(),
                         [](const Out& a, const Out& b) { return a.size > b.size; });
    };
    std::vector<Out> scores, bboxRaw, kpsRaw;
    for (const auto& o : outs) {
        if (o.last == 1) scores.push_back(o);
        else if (o.last == 4) bboxRaw.push_back(o);
        else if (o.last == 10) kpsRaw.push_back(o);
    }
    bycount(scores); bycount(bboxRaw); bycount(kpsRaw);

    // ---- :168-181 逐 stride 解码 → 原图坐标（/scale）；bs/ss/ks 平行三段暂存 ----
    std::vector<float> bb, kk;      // 拼接后 (N,4)/(N,5,2)
    std::vector<float> ss;          // (N,)
    for (size_t i = 0; i < 3; ++i) {
        if (i >= scores.size()) break;                       // :170-171
        const int st = STRIDES[i];
        const Out& sc = scores[i];
        const Out& bx = bboxRaw[i];
        const Out& kp = kpsRaw[i];
        const size_t nA = sc.size;                           // scores.reshape(-1) 长度
        const auto centers = anchorsFor(st);
        if (centers.size() != nA * 2 || bx.size != nA * 4 || kp.size != nA * 10)
            throw std::runtime_error("scrfd_detect: stride/output shape mismatch");
        std::vector<float> pts, distB, distK, segSc;
        for (size_t a = 0; a < nA; ++a) {
            if (!(sc.p[a] >= DET_THRESH)) continue;          // :176 mask
            pts.push_back(centers[a * 2]);
            pts.push_back(centers[a * 2 + 1]);
            for (int q = 0; q < 4; ++q) distB.push_back(bx.p[a * 4 + q] * float(st));   // :174
            for (int q = 0; q < 10; ++q) distK.push_back(kp.p[a * 10 + q] * float(st)); // :175
            segSc.push_back(sc.p[a]);
        }
        if (pts.empty()) continue;                           // :177-178 not mask.any()
        std::vector<float> segB, segK;
        distance2bbox(pts, distB, segB);                     // :179 /scale
        distance2kps(pts, distK, segK);                      // :180 /scale + reshape(-1,5,2)
        for (auto& v : segB) v /= float(scale);
        for (auto& v : segK) v /= float(scale);
        bb.insert(bb.end(), segB.begin(), segB.end());
        kk.insert(kk.end(), segK.begin(), segK.end());
        ss.insert(ss.end(), segSc.begin(), segSc.end());
    }

    Det det;
    if (ss.empty()) return det;                              // :182-183 无脸 = 空数组

    // ---- :184-189 hstack([b, s]) → dets float32；NMS → dets[keep], k[keep] ----
    const size_t n = ss.size();
    std::vector<float> dets(n * 5);
    for (size_t j = 0; j < n; ++j) {
        dets[j * 5 + 0] = bb[j * 4 + 0];
        dets[j * 5 + 1] = bb[j * 4 + 1];
        dets[j * 5 + 2] = bb[j * 4 + 2];
        dets[j * 5 + 3] = bb[j * 4 + 3];
        dets[j * 5 + 4] = ss[j];
    }
    const auto keep = nms(dets);
    det.dets.reserve(keep.size() * 5);
    det.kpss.reserve(keep.size() * 10);
    for (const size_t j : keep) {
        det.dets.insert(det.dets.end(), dets.begin() + j * 5, dets.begin() + j * 5 + 5);
        det.kpss.insert(det.kpss.end(), kk.begin() + j * 10, kk.begin() + j * 10 + 10);
    }
    return det;
}

} // namespace Faceinfer
