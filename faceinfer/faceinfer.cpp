// faceinfer.exe —— SCRFD 检测（T2 落位）+ 对齐嵌入（T3 续）ONNX 子进程真壳。
// stdout 只走帧（AiProto 强制 _O_BINARY），日志只走 stderr。
// READY 语义（spec §5，镜像 EraseRunner::modelOkFor：exe 同目录/ env LA_FACE_MODEL_DIR
// 有模型且会话建好才 READY 1；缺模型 READY 0 后保活，每 REQ 诚实回 ERR nomodel）。
// T2 壳转真：det 会话 = READY 的唯一判据；T3 接入 rec 会话后须两会话齐备才 READY 1
// （spec §5「会话全部建好才发 READY 1」），届时在此文件扩展。
// 协议（spec §5）：`REQ <id> <kind> <len>\n` + len 字节 body；kind ∈ {det, path}（T2 同义：
// body=UTF-8 图像绝对路径）→ `RES <id> <len>\n` + JSON {"ok":true,"count":N,"faces":[...]}；
// 失败 `ERR <id> <code>\n`，code ∈ nomodel|loadfail|inferfail|badframe。
// T1 评审遗留 Minor#4（本任务承接）：aiproto::parseReq 返回不设上限的 Header.len——
// 读取点加 64 MiB 体长硬上限（超限即 ERR badframe 并终止读线：巨 len 无法消费保同步，
// 流错位不可恢复，父端按断链重启，绝不预分配巨内存）。AiProto.h 本体归 T4 加固，不动。
#include "core/AiProto.h"
#include "scrfd.h"

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

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
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

QString resolveModelPath()
{
    const QByteArray dir = qgetenv("LA_FACE_MODEL_DIR");
    if (!dir.isEmpty()) {
        const QString p = QString::fromUtf8(dir) + QStringLiteral("/detection.onnx");
        if (QFile::exists(p)) return p;
    }
    const QString exeDir = QFileInfo(QString::fromLocal8Bit(exePathBuf())).absolutePath();
    const QString sib = exeDir + QStringLiteral("/detection.onnx");
    if (QFile::exists(sib)) return sib;
    return QString();
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

// RES JSON 体（spec §3 出参 / T3 嵌入位留空——faces 字段与 face_server /face/detect
// 逐字段同形，embedding 由 T3 追加，T2 不出该键）。
QByteArray facesJson(const Faceinfer::Det& det)
{
    QJsonArray faces;
    const size_t n = det.dets.size() / 5;
    for (size_t j = 0; j < n; ++j) {
        QJsonObject f;
        QJsonArray bbox, kps;
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
        faces.append(f);
    }
    QJsonObject root;
    root.insert(QStringLiteral("ok"), true);
    root.insert(QStringLiteral("count"), double(n));
    root.insert(QStringLiteral("faces"), faces);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

// ============================ --det-selftest 比较核 ============================
// 容差三列（brief Step 3 / spec §3 子集，#78 T3 扩 embedding 余弦+score 差两列时复用本核）：
//   人脸数精确相等；逐脸 IoU ≥ 0.98；kps 五点欧距均值 ≤ 2.0 原图像素。
// 匹配策略：金标注顺序逐个取未占用候选中 IoU 最大者（贪心 1:1；顺序无关，防 NMS
// keep 次序在数值微差下漂移导致假红）。
struct GoldFace {
    float box[4];
    float kps[5][2];
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

// 核心断言段：gold faces vs det 结果 → 打印逐脸表 + 总行；返回全 PASS 与否。
// exit 0=PASS 1=FAIL（供 #78 门与人工实测同口径消费）。
int compareGold(const std::vector<GoldFace>& gold, const Faceinfer::Det& det,
                const QString& imgPath)
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
        const bool ok = bestIou >= 0.98 && kd <= 2.0;
        if (!ok) allPass = false;
        std::printf("FACE %zu ours=%d IOU=%.4f KPS_MEAN=%.4f %s\n",
                    j, best, bestIou, kd, ok ? "PASS" : "FAIL");
    }
    std::printf("DET-SELFTEST %s\n", allPass ? "PASS" : "FAIL");
    std::fflush(stdout);
    return allPass ? 0 : 1;
}

int runDetSelftest(int argc, char** argv)
{
    // argv 已由 main 重基：argv[0]=="--det-selftest"，argv[1]=image，argv[2]=gold.json。
    if (argc < 3) {
        std::fprintf(stderr, "usage: faceinfer --det-selftest <image> <gold.json>\n");
        return 2;
    }
    const QString model = resolveModelPath();
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
    QJsonParseError perr;
    const QJsonDocument gd = QJsonDocument::fromJson(gf.readAll(), &perr);
    if (gd.isNull() || !gd.isObject()) {
        std::fprintf(stderr, "det-selftest: gold json parse failed at offset %lld: %s\n",
                     static_cast<long long>(perr.offset), perr.errorString().toUtf8().constData());
        return 2;
    }
    std::vector<GoldFace> gold;
    const QJsonArray faces = gd.object().value(QStringLiteral("faces")).toArray();
    for (const QJsonValue& v : faces) {
        const QJsonObject o = v.toObject();
        GoldFace f{};
        const QJsonArray bb = o.value(QStringLiteral("bbox")).toArray();
        if (bb.size() != 4) { std::fprintf(stderr, "det-selftest: bad bbox\n"); return 2; }
        for (int q = 0; q < 4; ++q) f.box[q] = float(bb[q].toDouble());
        const QJsonArray kk = o.value(QStringLiteral("kps")).toArray();
        if (kk.size() != 5) { std::fprintf(stderr, "det-selftest: bad kps\n"); return 2; }
        for (int k = 0; k < 5; ++k) {
            const QJsonArray pt = kk[k].toArray();
            if (pt.size() != 2) { std::fprintf(stderr, "det-selftest: bad kps pt\n"); return 2; }
            f.kps[k][0] = float(pt[0].toDouble());
            f.kps[k][1] = float(pt[1].toDouble());
        }
        gold.push_back(f);
    }

    std::vector<uint8_t> rgb; int w = 0, h = 0;
    const QString img = QString::fromLocal8Bit(argv[1]);
    if (!loadRgbHwc(img, rgb, w, h)) {
        std::fprintf(stderr, "det-selftest: image load failed: %s\n", argv[1]);
        return 2;
    }
    try {
        const Faceinfer::Det det = Faceinfer::scrfd_detect(rgb, w, h);
        return compareGold(gold, det, img);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "det-selftest: infer failed: %s\n", e.what());
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

    const QString model = resolveModelPath();
    const bool ready = !model.isEmpty() && Faceinfer::scrfd_init(model.toUtf8().constData());
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
        if (kind != "det" && kind != "path") {           // T2 面侧仅 det/path（同义）
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
            const QByteArray json = facesJson(det);
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
            std::printf("faceinfer 0.0.0 round4-t2\n");
            return 0;
        }
        // 重基 argv：runDetSelftest 内 argv[0]=旗标、[1]=image、[2]=gold。
        if (std::strcmp(argv[i], "--det-selftest") == 0)
            return runDetSelftest(argc - i, argv + i);
    }
    return runServer();
}
