#include "RawDecode.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <cstdio>

namespace {

// ---- RAW 扩展名（与 PathGuess::kImgExt 分工：那边管"是否照片"，这里管"是否需外部解码"）
const char* const kRawExt[] = {
    ".dng", ".cr2", ".cr3", ".nef", ".nrw", ".arw", ".srf", ".sr2",
    ".raf", ".rw2", ".orf", ".raw", ".pef", ".ptx", ".srw", ".x3f",
    ".3fr", ".fff", ".iiq", ".k25", ".kdc", ".mef", ".mos", ".mrw",
};

// PPM(P6) 头部解析：只信实测格式，解析失败一律返回无效。
// 实测产物形如：  "P6\n1737 1157\n255\n" 后接裸 RGB 字节。
QSize readPpmSize(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QSize();
    // 头部最长 64 字节足够容纳 P6 + 三段十进制数
    const QByteArray head = f.read(64);
    if (head.size() < 10 || head.startsWith("P5")) return QSize();   // P5 = 灰度，本管线不产出
    if (!head.startsWith("P6")) return QSize();

    int idx = 2;                       // 跳过 "P6"
    auto skipWs = [&]() {
        while (idx < head.size()) {
            const char c = head.at(idx);
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { ++idx; continue; }
            if (c == '#') { while (idx < head.size() && head.at(idx) != '\n') ++idx; continue; }
            break;
        }
    };
    auto nextInt = [&](int* out) -> bool {
        skipWs();
        if (idx >= head.size() || head.at(idx) < '0' || head.at(idx) > '9') return false;
        int v = 0;
        while (idx < head.size() && head.at(idx) >= '0' && head.at(idx) <= '9')
            v = v * 10 + (head.at(idx++) - '0');
        if (v <= 0) return false;
        *out = v;
        return true;
    };

    int w = 0, h = 0;
    if (!nextInt(&w) || !nextInt(&h)) return QSize();
    return QSize(w, h);
}

// 产物合理性下限：解码失败的 PPM 会是 0x0 或 1x1 噪声
bool plausibleSize(const QSize& s)
{
    return s.isValid() && s.width() >= 16 && s.height() >= 16;
}

} // namespace

namespace RawDecode {

// QA 反例旗标（见头文件）。默认 false = 真实实现。
static bool g_forceArtifactBlind = false;
void setForceArtifactBlindForQa(bool on) { g_forceArtifactBlind = on; }

bool isRawExt(const QString& fileName)
{
    const int dot = int(fileName.lastIndexOf(QLatin1Char('.')));
    if (dot <= 0) return false;
    const QString ext = fileName.mid(dot).toLower();
    for (const char* e : kRawExt)
        if (ext == QString::fromLatin1(e)) return true;
    return false;
}

// 解码器落位：<exe 同级>/3rdparty/libraw/bin/dcraw_emu.exe
// 刻意「只认 exe 同级目录」——不搜 PATH、不搜 C:、不落到用户主目录，
// 保证行为可预测（门禁可复现）且不污染系统（铁律：下载不放 C 盘）。
bool isImportable(const QString& fileName, bool decoderAvailable)
{
    if (!isRawExt(fileName)) return true;      // 非 RAW → 与既有行为完全一致
    return decoderAvailable;                   // RAW → 无解码器则不导入（见头文件说明）
}

bool needsExternalDecode(const QString& fileName, bool decoderAvailable)
{
    if (!isRawExt(fileName)) return false;     // ★ 非 RAW 必须 false（见头文件边界说明）
    return decoderAvailable;
}

QString toolDir()
{
    static QString cached;
    static bool probed = false;
    if (probed) return cached;
    probed = true;

    const QString exeDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        exeDir + QStringLiteral("/3rdparty/libraw/bin"),
        exeDir + QStringLiteral("/libraw/bin"),
    };
    for (const QString& c : candidates) {
        if (QFileInfo::exists(c + QStringLiteral("/dcraw_emu.exe"))) { cached = c; return cached; }
    }
    cached.clear();
    return cached;
}

bool available()
{
    return !toolDir().isEmpty();
}

QString decodeToTempPpm(const QString& rawPath, int timeoutMs)
{
    const QString dir = toolDir();
    if (dir.isEmpty()) return QString();

    // 临时目录随函数返回即销毁 → PPM 生命周期交给调用方读取前须在同函数内完成，
    // 故这里改为在系统 temp 下按需命名并交出路径（由调用方 QFile::remove）。
    static QTemporaryDir* tmpDir = new QTemporaryDir();   // 进程级，退出时回收
    tmpDir->setAutoRemove(true);
    if (!tmpDir->isValid()) return QString();

    const QString base = QFileInfo(rawPath).completeBaseName();
    static int seq = 0;
    const QString work = tmpDir->filePath(
        QStringLiteral("%1_%2.dng").arg(base).arg(++seq));
    const QString ppmPath = work + QStringLiteral(".ppm");

    // dcraw_emu 以「输入名 + .ppm」派生输出名 → 必须先复制成受控文件名
    if (!QFile::copy(rawPath, work)) return QString();

    QProcess p;
    p.setWorkingDirectory(dir);
    p.setProgram(dir + QStringLiteral("/dcraw_emu.exe"));
    // -h 半尺寸（实测 483ms / 6.0MB，全尺寸 24MB 过大且缩略图用不上）
    // -w 用相机白平衡（实测输出正常）
    p.setArguments({ QStringLiteral("-h"), QStringLiteral("-w"), work });
    p.start();
    if (!p.waitForStarted(timeoutMs)) { QFile::remove(work); return QString(); }
    if (!p.waitForFinished(timeoutMs)) {           // 超时：杀进程，避免僵持
        p.kill();
        p.waitForFinished(2000);
        QFile::remove(work);
        return QString();
    }
    // ★ 刻意不判 exit code：实测 dcraw_emu 对未知选项/异常输入仍可能 rc=0。
    //   成败一律以 PPM 产物为准（见头文件铁律）。

    // ★ 反例注入点：必须在「解码完成、判定成败」这一步之内（铁律 32）——
    //   注入若落在别处（如枚举阶段）会被吞，门禁将假绿。
    if (g_forceArtifactBlind) {
        // 缺陷形态：rc==0 即认为成功 + 产物名按「替换扩展名」猜测（真实缺陷形态）。
        if (p.exitCode() == 0)
            return QFileInfo(work).completeBaseName() + QStringLiteral(".ppm");
        return QString();
    }

    const QSize got = readPpmSize(ppmPath);
    if (!plausibleSize(got)) {
        QFile::remove(ppmPath);
        QFile::remove(work);
        return QString();
    }
    QFile::remove(work);                            // 保留 .ppm，删掉输入副本
    return ppmPath;
}

QSize probeSize(const QString& rawPath)
{
    const QString ppm = decodeToTempPpm(rawPath);
    if (ppm.isEmpty()) return QSize();
    const QSize s = readPpmSize(ppm);
    QFile::remove(ppm);
    return plausibleSize(s) ? s : QSize();
}

} // namespace RawDecode
