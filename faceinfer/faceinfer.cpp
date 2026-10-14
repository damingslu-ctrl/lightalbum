// faceinfer.exe —— SCRFD 检测（T2 落位）+ 对齐嵌入（T3 落位）ONNX 子进程真壳。
// stdout 只走帧（AiProto 强制 _O_BINARY），日志只走 stderr。
// READY 语义（spec §5，镜像 EraseRunner::modelOkFor：exe 同目录/ env LA_FACE_MODEL_DIR
// 有模型且会话建好才 READY 1；缺模型 READY 0 后保活，每 REQ 诚实回 ERR nomodel）。
// T3 起 rec（ArcFace recognition.onnx）会话接入：det+rec 两会话齐备才 READY 1
// （spec §5「会话全部建好才发 READY 1」）；缺 recognition.onnx 时 det 虽可建也诚实 READY 0。
// 协议（spec §5）：`REQ <id> <kind> <len>\n` + len 字节 body；kind ∈ {det, path}（T2 同义：
// body=UTF-8 图像绝对路径）→ `RES <id> <len>\n` + JSON {"ok":true,"count":N,"faces":[...]}；
// 失败 `ERR <id> <code>\n`，code ∈ nomodel|loadfail|inferfail|badframe。
// T1 评审遗留 Minor#4（本任务承接）：aiproto::parseReq 返回不设上限的 Header.len——
// 读取点加 64 MiB 体长硬上限（超限即 ERR badframe 并终止读线：巨 len 无法消费保同步，
// 流错位不可恢复，父端按断链重启，绝不预分配巨内存）。AiProto.h 本体归 T4 加固，不动。
// 平价声明（T4-T5 收口，2026-10-14 复验）：oracle 真源 D:\lightalbum_ai\face_server.py
// SHA256=b0436d21298666bf…；模型 detection.onnx SHA256=5838f7fe053675b1…（det_10g）、
// recognition.onnx SHA256=4c06341c33c2ca1f…（w600k_r50）；venv_face（ort 1.30 / numpy 2.5.3 /
// cv2 5.0）同进程直调产金。#78 基准夹具集 f1_crowd42/f2_single/f3_pair/f4_trio/c2_00c07f5a
//（5 图 51 脸）五列全 PASS（min IoU 0.9978 / max kps 0.0987px 主集内）。边界对照组
// f5_five face4（kps 0.172px→cos 0.998699）与 c1_00703838 face4（0.308px→0.997963）：
// 跨运行时 det-kps 噪声 ≥0.15px 时 cos 跌破 0.999 下限——R-11 判例：夹具须避开检测
// 决策边界（同 T6 CLIP oracle-prescreen 约定），非移植 bug。
// 对齐为 cv2-estimateAffinePartial2D 忠实 RANSAC+inlier-LS（R-09 判例，非纯 Umeyama）。
#include "core/AiProto.h"
#include "scrfd.h"
#include "align.h"

#include <QImage>
#include <QImageReader>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QByteArray>
#include <QFile>
#include <QString>

#include <windows.h>   // GetModuleFileNameA（exe 同目录 sibling 解析，免 QCoreApplication 实例）

#include <onnxruntime_cxx_api.h>   // T3 rec 会话（ArcFace recognition.onnx，ORT C API，同 scrfd.cpp 口径）

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <vector>

#include <fcntl.h>
#include <io.h>

namespace {

// REQ 体长硬上限 = 64 MiB（T1 Minor#4 承接；协议合法体仅路径/JPEG，远小于此）。
constexpr unsigned long long kMaxReqBody = 64ull * 1024 * 1024;

// 剥除行尾 \r\n —— parseReq %n 全耗比对要求 n 为规范化长度（与桩同口径，一行式契约）。
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

// 模型解析（镜像 EraseRunner::modelOkFor 语义）：env LA_FACE_MODEL_DIR 优先，
// 否则 exe 同目录 sibling；文件存在只是前提，会话建成才算 ready。
const char* exePathBuf()
{
    static char buf[4096] = {0};
    if (buf[0] == 0) GetModuleFileNameA(nullptr, buf, DWORD(sizeof(buf) - 1));
    return buf;
}

QString resolveModelDir()
{
    const QByteArray dir = qgetenv("LA_FACE_MODEL_DIR");
    if (!dir.isEmpty()) {
        const QString p = QString::fromUtf8(dir) + QStringLiteral("/detection.onnx");
        if (QFile::exists(p)) return QString::fromUtf8(dir);
    }
    const QString exeDir = QFileInfo(QString::fromLocal8Bit(exePathBuf())).absolutePath();
    if (QFile::exists(exeDir + QStringLiteral("/detection.onnx"))) return exeDir;
    return QString();
}

// T3：det+rec 同目录成对解析（face_server FaceEngine._load :121-129 同构——model_dir 下
// detection.onnx + recognition.onnx 双文件皆存在才会两会话加载）。
QString resolveDetPath()
{
    const QString d = resolveModelDir();
    return d.isEmpty() ? QString() : d + QStringLiteral("/detection.onnx");
}
QString resolveRecPath()
{
    const QString d = resolveModelDir();
    if (d.isEmpty()) return QString();
    const QString p = d + QStringLiteral("/recognition.onnx");
    return QFile::exists(p) ? p : QString();
}

// QImageReader 读盘 → 交错 RGB HWC 字节。方向契约见 scrfd.cpp 文件头：
// face_server 走 BGR→`[...,::-1]`→planar RGB；此处直出 RGB888，等价已折叠。
bool loadRgbHwc(const QString& path, std::vector<uint8_t>& rgb, int& w, int& h)
{
    QImageReader reader(path);
    reader.setAutoTransform(false);            // cv2.imread 不应用 EXIF 方向，平价同构
    QImage img = reader.read();
    if (img.isNull()) return false;
    img = img.convertToFormat(QImage::Format_RGB888);
    w = img.width(); h = img.height();
    if (w <= 0 || h <= 0) return false;
    rgb.assign(size_t(w) * h * 3, uint8_t(0));
    const int rowBytes = int(img.bytesPerLine());
    for (int y = 0; y < h; ++y) {
        const uint8_t* src = reinterpret_cast<const uint8_t*>(img.constScanLine(y));
        if (rowBytes == w * 3)
            std::memcpy(rgb.data() + size_t(y) * w * 3, src, size_t(w) * 3);
        else
            for (int x = 0; x < w * 3; ++x) rgb[size_t(y) * w * 3 + x] = src[x];
    }
    return true;
}

// ============================ T3 rec 会话（ArcFace w600k_r50） ============================
// oracle 真源 = face_server.py embed() :192-202 + _load :128-129（R-P1 逐行翻译）。
// 通道方向与 scrfd.cpp 同口径：face_server 在 BGR warp 结果上做 `[...,::-1]` 得 planar
// RGB；本移植 warp 直出 RGB HWC，blob 按 c=0,1,2 直取 R,G,B，反转已在源头消化。
namespace {

// widenUtf8 与 scrfd.cpp 同族（ORTCHAR_T=wchar_t on Windows；两 TU 各持私有副本，
// 不跨 TU 泄漏 ORT 头依赖）。
std::wstring recWiden(const std::string& s)
{
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), &w[0], n);
    return w;
}

Ort::Env*     g_envRec  = nullptr;   // 独立 Env（ORT 支持多 Env；日志仅 stderr，同 scrfd.cpp）
Ort::Session* g_sessRec = nullptr;
std::string   g_recInName;
std::string   g_recOutName;

bool rec_init(const std::string& modelPath)
{
    try {
        if (!g_envRec) g_envRec = new Ort::Env(ORT_LOGGING_LEVEL_ERROR, "faceinfer-rec");
        Ort::SessionOptions opt;
        opt.SetIntraOpNumThreads(2);
        opt.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        if (g_sessRec) { delete g_sessRec; g_sessRec = nullptr; }
        g_sessRec = new Ort::Session(*g_envRec, recWiden(modelPath).c_str(), opt);
        Ort::AllocatorWithDefaultOptions allocator;
        g_recInName  = g_sessRec->GetInputNameAllocated(0, allocator).get();
        g_recOutName = g_sessRec->GetOutputNameAllocated(0, allocator).get();
        std::fprintf(stderr, "faceinfer: rec session loaded, input=\"%s\" output=\"%s\"\n",
                     g_recInName.c_str(), g_recOutName.c_str());
        return true;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "faceinfer: rec init failed: %s\n", e.what());
        delete g_sessRec; g_sessRec = nullptr;
        return false;
    }
}

bool rec_ready() { return g_sessRec != nullptr; }

// 单脸嵌入：112×112 RGB crop → blob (x−127.5)/128 NCHW（embed() :197-198 verbatim）
// → rec.run → feat/norm(+1e-12)（:201）→ 5 位小数舍入（:218 round(float(v),5) 语义，
// std::round(v*1e5)/1e5，brief Step 2 钉死）。返回 512 维；ORT 异常上抛转 ERR inferfail。
std::vector<float> rec_embed112(const uint8_t face[112 * 112 * 3])
{
    if (!g_sessRec) throw std::runtime_error("rec_embed112: session not initialised");
    const int64_t inShape[4] = { 1, 3, 112, 112 };
    std::vector<float> blob(size_t(3) * 112 * 112);
    for (int c = 0; c < 3; ++c)
        for (int y = 0; y < 112; ++y)
            for (int x = 0; x < 112; ++x)
                blob[size_t(c) * 112 * 112 + size_t(y) * 112 + x] =
                    (float(face[(size_t(y) * 112 + x) * 3 + c]) - 127.5f) / 128.0f;

    Ort::MemoryInfo memInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value inT = Ort::Value::CreateTensor<float>(memInfo, blob.data(), blob.size(), inShape, 4);
    const char* inNames[1]  = { g_recInName.c_str() };
    const char* outNames[1] = { g_recOutName.c_str() };
    auto outputs = g_sessRec->Run(Ort::RunOptions{ nullptr }, inNames, &inT, 1, outNames, 1);
    if (outputs.empty()) throw std::runtime_error("rec_embed112: no ORT output");
    auto ti = outputs[0].GetTensorTypeAndShapeInfo();
    const size_t n = ti.GetElementCount();
    if (n != 512) throw std::runtime_error("rec_embed112: unexpected output length");
    const float* f = outputs[0].GetTensorData<float>();

    double ssq = 0.0;                                  // :201 np.linalg.norm（double 累加，
    for (size_t i = 0; i < n; ++i) ssq += double(f[i]) * double(f[i]);   //   float 域差异 ≪ 容差）
    const float norm = float(std::sqrt(ssq)) + 1e-12f;
    std::vector<float> emb(n);
    for (size_t i = 0; i < n; ++i)
        emb[i] = float(std::round(double(f[i] / norm) * 1e5) / 1e5);    // 5dp verbatim 语义
    return emb;
}

// 端到端逐脸嵌入（detect_image() :210-219 的 embed 调用位）：kps → Umeyama M → 逆向
// 双线性 warp 112×112 → rec_embed112。顺序与 det 输出一一对应。
std::vector<std::vector<float>> embedFaces(const Faceinfer::Det& det,
                                           const std::vector<uint8_t>& rgb, int w, int h)
{
    const size_t n = det.dets.size() / 5;
    std::vector<std::vector<float>> embs(n);
    for (size_t j = 0; j < n; ++j) {
        float kps[5][2];
        for (int k = 0; k < 5; ++k) {
            kps[k][0] = det.kpss[j * 10 + k * 2 + 0];
            kps[k][1] = det.kpss[j * 10 + k * 2 + 1];
        }
        float M[6];
        Faceinfer::align_similarity_transform(kps, M);
        uint8_t crop[112 * 112 * 3];
        Faceinfer::warp_affine_bilinear(rgb, w, h, M, crop);
        embs[j] = rec_embed112(crop);
    }
    return embs;
}

// RES JSON 体（spec §3 出参）：faces 字段与 face_server /face/detect 200 路径
// detect_image() :214-219 逐字段同形（bbox/score/kps/embedding 五维键序同 oracle dict）。
QByteArray facesJson(const Faceinfer::Det& det, const std::vector<std::vector<float>>& embs)
{
    QJsonArray faces;
    const size_t n = det.dets.size() / 5;
    for (size_t j = 0; j < n; ++j) {
        QJsonObject f;
        QJsonArray bbox, kps, emb;
        for (int q = 0; q < 4; ++q) bbox.append(double(det.dets[j * 5 + q]));
        f.insert(QStringLiteral("bbox"), bbox);
        f.insert(QStringLiteral("score"), double(det.dets[j * 5 + 4]));
        for (int k = 0; k < 5; ++k) {
            QJsonArray pt;
            pt.append(double(det.kpss[j * 10 + k * 2 + 0]));
            pt.append(double(det.kpss[j * 10 + k * 2 + 1]));
            kps.append(pt);
        }
        f.insert(QStringLiteral("kps"), kps);
        if (j < embs.size())
            for (float v : embs[j]) emb.append(double(v));   // 5dp 已舍入，JSON number
        f.insert(QStringLiteral("embedding"), emb);
        faces.append(f);
    }
    QJsonObject root;
    root.insert(QStringLiteral("ok"), true);
    root.insert(QStringLiteral("count"), double(n));
    root.insert(QStringLiteral("faces"), faces);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

} // namespace（rec 会话子块，自 131 行起）

// ============================ 端到端 selftest 比较核 ============================
// 容差五列（brief Step 3 / spec §3，#78 门同口径）：
//   人脸数精确相等；逐脸 IoU ≥ 0.98；kps 五点欧距均值 ≤ 2.0 原图像素；
//   embedding 余弦 ≥ 0.999；score 差 ≤ 0.02。
// withEmb=false 时（--det-selftest，T2 三列语义原样保留）跳过余弦/score 两列。
// 匹配策略：金标注顺序逐个取未占用候选中 IoU 最大者（贪心 1:1；顺序无关，防 NMS
// keep 次序在数值微差下漂移导致假红）。
struct GoldFace {
    float box[4];
    float kps[5][2];
    float score = 0.0f;
    std::vector<float> emb;      // 512×5dp（gold JSON 缺键时为空 = det-only）
};

double iouOf(const float a[4], const float b[4])
{
    const double ix1 = std::max(double(a[0]), double(b[0]));
    const double iy1 = std::max(double(a[1]), double(b[1]));
    const double ix2 = std::min(double(a[2]), double(b[2]));
    const double iy2 = std::min(double(a[3]), double(b[3]));
    const double inter = ix2 > ix1 && iy2 > iy1 ? (ix2 - ix1) * (iy2 - iy1) : 0.0;
    const double arA = (double(a[2]) - a[0]) * (double(a[3]) - a[1]);
    const double arB = (double(b[2]) - b[0]) * (double(b[3]) - b[1]);
    const double un = arA + arB - inter;
    return un > 0.0 ? inter / un : 0.0;
}

double kpsMeanDist(const float g[5][2], const float* mine)
{
    double s = 0.0;
    for (int k = 0; k < 5; ++k) {
        const double dx = double(g[k][0]) - double(mine[k * 2 + 0]);
        const double dy = double(g[k][1]) - double(mine[k * 2 + 1]);
        s += std::sqrt(dx * dx + dy * dy);
    }
    return s / 5.0;
}

// 余弦（两侧皆 5dp 舍入后的近单位向量；不归一再除——直接 dot/‖a‖‖b‖ 稳）。
double embCosine(const std::vector<float>& a, const std::vector<float>& b)
{
    if (a.size() != b.size() || a.size() != 512) return -2.0;   // 维数不符 = 立即红
    double dot = 0.0, na = 0.0, nb = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        dot += double(a[i]) * double(b[i]);
        na  += double(a[i]) * double(a[i]);
        nb  += double(b[i]) * double(b[i]);
    }
    if (na <= 0.0 || nb <= 0.0) return -2.0;
    return dot / (std::sqrt(na) * std::sqrt(nb));
}

// 核心断言段：gold faces vs 端到端结果 → 打印逐脸表 + 总行；返回全 PASS 与否。
// exit 0=PASS 1=FAIL（供 #78 门与人工实测同口径消费）。
int compareGold(const std::vector<GoldFace>& gold, const Faceinfer::Det& det,
                const std::vector<std::vector<float>>& embs,
                const QString& imgPath, bool withEmb, const char* tag)
{
    const size_t nMine = det.dets.size() / 5;
    std::printf("IMAGE %s gold=%zu mine=%zu\n", imgPath.toUtf8().constData(),
                gold.size(), nMine);
    bool allPass = true;
    const bool countOk = gold.size() == nMine;
    if (!countOk) {
        std::printf("COUNT FAIL gold=%zu mine=%zu\n", gold.size(), nMine);
        allPass = false;
    } else {
        std::printf("COUNT PASS %zu\n", nMine);
    }
    std::vector<bool> used(nMine, false);
    for (size_t j = 0; j < gold.size(); ++j) {
        int best = -1; double bestIou = -1.0;
        for (size_t k = 0; k < nMine; ++k) {
            if (used[k]) continue;
            const double v = iouOf(gold[j].box, det.dets.data() + k * 5);
            if (v > bestIou) { bestIou = v; best = int(k); }
        }
        if (best < 0) {
            std::printf("FACE %zu MISS (no candidate)\n", j);
            allPass = false;
            continue;
        }
        used[size_t(best)] = true;
        const double kd = kpsMeanDist(gold[j].kps, det.kpss.data() + size_t(best) * 10);
        bool ok = bestIou >= 0.98 && kd <= 2.0;
        double sd = 0.0, cos = -2.0;
        if (ok && withEmb) {
            sd = std::fabs(double(det.dets[best * 5 + 4]) - double(gold[j].score));
            cos = embCosine(gold[j].emb,
                            size_t(best) < embs.size() ? embs[best] : std::vector<float>{});
            ok = sd <= 0.02 && cos >= 0.999;
        }
        if (!ok) allPass = false;
        if (withEmb)
            std::printf("FACE %zu ours=%d IOU=%.4f KPS_MEAN=%.4f SCORE_D=%.5f COS=%.6f %s\n",
                        j, best, bestIou, kd, sd, cos, ok ? "PASS" : "FAIL");
        else
            std::printf("FACE %zu ours=%d IOU=%.4f KPS_MEAN=%.4f %s\n",
                        j, best, bestIou, kd, ok ? "PASS" : "FAIL");
    }
    std::printf("%s-SELFTEST %s\n", tag, allPass ? "PASS" : "FAIL");
    std::fflush(stdout);
    return allPass ? 0 : 1;
}

// gold JSON 解析（faces[] 五字段；embedding/score 键允许缺失 = det-only 旧夹具宽容）。
std::vector<GoldFace> parseGoldJson(const QByteArray& raw, QString* err)
{
    std::vector<GoldFace> gold;
    QJsonParseError perr;
    const QJsonDocument gd = QJsonDocument::fromJson(raw, &perr);
    if (gd.isNull() || !gd.isObject()) {
        if (err) *err = QStringLiteral("json parse failed at offset %1: %2")
            .arg(perr.offset).arg(perr.errorString());
        return gold;
    }
    const QJsonArray faces = gd.object().value(QStringLiteral("faces")).toArray();
    for (const QJsonValue& v : faces) {
        const QJsonObject o = v.toObject();
        GoldFace f;
        const QJsonArray bb = o.value(QStringLiteral("bbox")).toArray();
        if (bb.size() != 4) { if (err) *err = QStringLiteral("bad bbox"); return {}; }
        for (int q = 0; q < 4; ++q) f.box[q] = float(bb[q].toDouble());
        const QJsonArray kk = o.value(QStringLiteral("kps")).toArray();
        if (kk.size() != 5) { if (err) *err = QStringLiteral("bad kps"); return {}; }
        for (int k = 0; k < 5; ++k) {
            const QJsonArray pt = kk[k].toArray();
            if (pt.size() != 2) { if (err) *err = QStringLiteral("bad kps pt"); return {}; }
            f.kps[k][0] = float(pt[0].toDouble());
            f.kps[k][1] = float(pt[1].toDouble());
        }
        f.score = float(o.value(QStringLiteral("score")).toDouble());
        const QJsonArray ee = o.value(QStringLiteral("embedding")).toArray();
        for (const QJsonValue& e : ee) f.emb.push_back(float(e.toDouble()));
        gold.push_back(std::move(f));
    }
    return gold;
}

int runDetSelftest(int argc, char** argv)
{
    // argv 已由 main 重基：argv[0]=="--det-selftest"，argv[1]=image，argv[2]=gold.json。
    if (argc < 3) {
        std::fprintf(stderr, "usage: faceinfer --det-selftest <image> <gold.json>\n");
        return 2;
    }
    const QString model = resolveDetPath();
    if (model.isEmpty()) {
        std::fprintf(stderr, "det-selftest: detection.onnx not found "
                             "(LA_FACE_MODEL_DIR or exe sibling)\n");
        return 2;
    }
    if (!Faceinfer::scrfd_init(model.toUtf8().constData())) return 2;

    QFile gf(argv[2]);
    if (!gf.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "det-selftest: gold open failed: %s\n", argv[2]);
        return 2;
    }
    QString perr;
    std::vector<GoldFace> gold = parseGoldJson(gf.readAll(), &perr);
    if (!perr.isEmpty()) {
        std::fprintf(stderr, "det-selftest: gold %s\n", perr.toUtf8().constData());
        return 2;
    }

    std::vector<uint8_t> rgb; int w = 0, h = 0;
    const QString img = QString::fromLocal8Bit(argv[1]);
    if (!loadRgbHwc(img, rgb, w, h)) {
        std::fprintf(stderr, "det-selftest: image load failed: %s\n", argv[1]);
        return 2;
    }
    try {
        const Faceinfer::Det det = Faceinfer::scrfd_detect(rgb, w, h);
        return compareGold(gold, det, {}, img, false, "DET");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "det-selftest: infer failed: %s\n", e.what());
        return 2;
    }
}

// --face-selftest <png> <gold.json> —— T3 端到端五列对拍（count/IoU/kps/cos/scoreΔ）。
// 走与服务模式完全同源的 det→align→rec 管线；额外打印 READY_MS（两会话初始化实测，
// ≙ 服务模式 READY 行前的冷/暖时长，进 spec 附录 A 回填与 #78 measuredMs 溯源）。
int runFaceSelftest(int argc, char** argv)
{
    if (argc < 3) {
        std::fprintf(stderr, "usage: faceinfer --face-selftest <image> <gold.json>\n");
        return 2;
    }
    const QString detPath = resolveDetPath(), recPath = resolveRecPath();
    if (detPath.isEmpty() || recPath.isEmpty()) {
        std::fprintf(stderr, "face-selftest: detection.onnx/recognition.onnx not found "
                             "(LA_FACE_MODEL_DIR or exe sibling)\n");
        return 2;
    }
    const auto t0 = std::chrono::steady_clock::now();
    if (!Faceinfer::scrfd_init(detPath.toUtf8().constData())) return 2;
    if (!rec_init(recPath.toUtf8().constData())) return 2;
    const long long readyMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
    std::printf("READY_MS %lld\n", readyMs);

    QFile gf(argv[2]);
    if (!gf.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "face-selftest: gold open failed: %s\n", argv[2]);
        return 2;
    }
    QString perr;
    std::vector<GoldFace> gold = parseGoldJson(gf.readAll(), &perr);
    if (!perr.isEmpty()) {
        std::fprintf(stderr, "face-selftest: gold %s\n", perr.toUtf8().constData());
        return 2;
    }

    std::vector<uint8_t> rgb; int w = 0, h = 0;
    const QString img = QString::fromLocal8Bit(argv[1]);
    if (!loadRgbHwc(img, rgb, w, h)) {
        std::fprintf(stderr, "face-selftest: image load failed: %s\n", argv[1]);
        return 2;
    }
    try {
        const Faceinfer::Det det = Faceinfer::scrfd_detect(rgb, w, h);
        const std::vector<std::vector<float>> embs = embedFaces(det, rgb, w, h);
        return compareGold(gold, det, embs, img, true, "FACE");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "face-selftest: infer failed: %s\n", e.what());
        return 2;
    }
}

// ============================ stdio 协议主循环 ============================
int runServer()
{
    _setmode(_fileno(stdin), _O_BINARY);
    AI_PROTO_FORCE_BINARY();
    FILE* in = stdin;
    FILE* out = stdout;

    const QString detPath = resolveDetPath(), recPath = resolveRecPath();
    const bool detOk = !detPath.isEmpty() && Faceinfer::scrfd_init(detPath.toUtf8().constData());
    const bool recOk = detOk && !recPath.isEmpty() && rec_init(recPath.toUtf8().constData());
    const bool ready = detOk && recOk;   // spec §5：会话全部建好才 READY 1（T3 起含 rec）
    std::fprintf(out, "READY %d\n", ready ? 1 : 0);
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
        // T1 Minor#4 承接：len 上限守卫——巨体无法消费保同步，报错后终止读线（父端重启）。
        if (h.len > kMaxReqBody) {
            std::fprintf(out, "ERR %llu badframe\n", h.id);
            std::fflush(out);
            break;
        }
        std::string body(static_cast<size_t>(h.len), '\0');
        if (h.len > 0 && !readExact(in, &body[0], static_cast<size_t>(h.len))) break;

        if (!ready) {                                    // spec §5：READY 0 保活恒答 nomodel
            std::fprintf(out, "ERR %llu nomodel\n", h.id);
            std::fflush(out);
            continue;
        }
        const std::string kind = h.kind;
        if (kind != "det" && kind != "path") {           // 面侧 det/path 同义（canonical det 归 T4 收口）
            std::fprintf(out, "ERR %llu badframe\n", h.id);
            std::fflush(out);
            continue;
        }
        const QString path = QString::fromUtf8(QByteArray(body.data(), int(body.size())));
        std::vector<uint8_t> rgb; int w = 0, h2 = 0;
        if (path.isEmpty() || !loadRgbHwc(path, rgb, w, h2)) {
            std::fprintf(out, "ERR %llu loadfail\n", h.id);  // ≙ face_server 400 无法读取图像
            std::fflush(out);
            continue;
        }
        try {
            const Faceinfer::Det det = Faceinfer::scrfd_detect(rgb, w, h2);
            const std::vector<std::vector<float>> embs = embedFaces(det, rgb, w, h2);
            const QByteArray json = facesJson(det, embs);
            std::fprintf(out, "RES %llu %llu\n", h.id,
                         static_cast<unsigned long long>(json.size()));   // %llu 非 %zu（通吃口径）
            std::fwrite(json.constData(), 1, size_t(json.size()), out);
            std::fflush(out);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "faceinfer: infer failed (id=%llu): %s\n", h.id, e.what());
            std::fprintf(out, "ERR %llu inferfail\n", h.id);
            std::fflush(out);
        } catch (...) {
            std::fprintf(stderr, "faceinfer: infer failed (id=%llu): unknown exception\n", h.id);
            std::fprintf(out, "ERR %llu inferfail\n", h.id);
            std::fflush(out);
        }
    }
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) {   // 装配自检钩（release C10 腿复用）
            std::printf("faceinfer 0.0.0 round4-t3\n");
            return 0;
        }
        // 重基 argv：selftest 内 argv[0]=旗标、[1]=image、[2]=gold。
        if (std::strcmp(argv[i], "--det-selftest") == 0)
            return runDetSelftest(argc - i, argv + i);
        if (std::strcmp(argv[i], "--face-selftest") == 0)
            return runFaceSelftest(argc - i, argv + i);
    }
    return runServer();
}
