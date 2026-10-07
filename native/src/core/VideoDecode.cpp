#include "VideoDecode.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <cstdio>

namespace {

const char* const kVideoExt[] = {
    ".mp4", ".mov", ".m4v", ".hevc", ".avi", ".mkv", ".webm",
    ".wmv", ".flv", ".mpg", ".mpeg", ".m2ts", ".3gp", ".mts",
};

} // namespace

namespace VideoDecode {

// QA 反例旗标（见头文件 / 门禁反例说明）。默认 false = 真实实现。
bool g_trustExitCodeForQa = false;
void setForceTrustExitCodeForQa(bool on) { g_trustExitCodeForQa = on; }

// 2.31.0 选帧反例：置 true 后忽略 atSec，恒抽第2 秒（复现「换秒读到同一帧」）
bool g_ignoreFrameForQa = false;
void setForceIgnoreFrameForQa(bool on) { g_ignoreFrameForQa = on; }

bool isVideoExt(const QString& fileName)
{
    const int dot = int(fileName.lastIndexOf(QLatin1Char('.')));
    if (dot <= 0) return false;
    const QString ext = fileName.mid(dot).toLower();
    for (const char* e : kVideoExt)
        if (ext == QString::fromLatin1(e)) return true;
    return false;
}

bool needsExternalDecode(const QString& fileName, bool decoderAvailable)
{
    if (!isVideoExt(fileName)) return false;   // ★ 非视频必须 false（剪枝谓词语义边界）
    return decoderAvailable;
}

// 只认 exe 同级目录，不搜 PATH/C:（可预测 + 不污染系统，与 RawDecode 一致）
QString toolDir()
{
    static QString cached;
    static bool probed = false;
    if (probed) return cached;
    probed = true;
    const QString exeDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        exeDir + QStringLiteral("/3rdparty/ffmpeg/bin"),
        exeDir + QStringLiteral("/ffmpeg/bin"),
    };
    for (const QString& c : candidates) {
        if (QFileInfo::exists(c + QStringLiteral("/ffmpeg.exe"))
            && QFileInfo::exists(c + QStringLiteral("/ffprobe.exe"))) {
            cached = c; return cached;
        }
    }
    cached.clear();
    return cached;
}

bool available() { return !toolDir().isEmpty(); }

// 临时工作目录：进程级，退出时回收
static QString workPath(const QString& base, const char* ext)
{
    static QTemporaryDir* td = new QTemporaryDir();
    td->setAutoRemove(true);
    if (!td->isValid()) return QString();
    static int seq = 0;
    return td->filePath(QStringLiteral("%1_%2%3").arg(base).arg(++seq).arg(QLatin1String(ext)));
}

VideoProbe probe(const QString& videoPath, int timeoutMs)
{
    VideoProbe out;
    const QString dir = toolDir();
    if (dir.isEmpty()) return out;

    QProcess p;
    p.setWorkingDirectory(dir);
    p.setProgram(dir + QStringLiteral("/ffprobe.exe"));
    p.setArguments({ QStringLiteral("-v"), QStringLiteral("error"),
                     QStringLiteral("-select_streams"), QStringLiteral("v:0"),
                     QStringLiteral("-show_entries"),
                     QStringLiteral("stream=width,height,codec_name,duration,avg_frame_rate"),
                     QStringLiteral("-show_entries"), QStringLiteral("format=duration"),
                     QStringLiteral("-of"), QStringLiteral("json"),
                     videoPath });
    p.start();
    if (!p.waitForStarted(timeoutMs)) return out;
    if (!p.waitForFinished(timeoutMs)) { p.kill(); p.waitForFinished(2000); return out; }
    if (p.exitCode() != 0) return out;   // ffprobe 解析失败 → 诚实空值

    // 解析 JSON（抗 ffprobe 版本漂移，优于正则扫文本）
    const QJsonDocument doc = QJsonDocument::fromJson(p.readAllStandardOutput());
    if (!doc.isObject()) return out;
    const QJsonObject root = doc.object();

    // ★ 时长解析必须容忍「字符串 vs 数字」两种形态 —— 这是实测踩过的坑：
    //   ffprobe 的 `duration` 输出的是**字符串** "10.000000"，而 QJsonValue::toDouble()
    //   对字符串**静默返回 0.0**（不报错、不警告）→ 首版实现 dur 恒为 0 却"看起来正常"。
    //   同一 JSON 里的 width/height 是真数字，所以 toInt() 正常 —— 正是这种
    //   「一半对一半错」最难发现。凡从外部 JSON 取数值，一律走此helper。
    auto numOf = [](const QJsonValue& v) -> double {
        if (v.isDouble()) return v.toDouble();
        if (v.isString()) {
            bool ok = false;
            const double d = v.toString().toDouble(&ok);
            return ok ? d : 0.0;
        }
        return 0.0;
    };

    const QJsonArray streams = root.value(QStringLiteral("streams")).toArray();
    if (!streams.isEmpty()) {
        const QJsonObject s = streams.first().toObject();
        out.width  = s.value(QStringLiteral("width")).toInt();
        out.height = s.value(QStringLiteral("height")).toInt();
        out.codec  = s.value(QStringLiteral("codec_name")).toString();
        // ★帧率形态是**分数字符串**如 "25/1" 或 "30000/1001"，不是数字 ——
        //   故不能用 numOf（那会因 toDouble 失败返回 0）。必须按 '/' 拆分子母。
        //   "0/0" 表示未知（容器未声明），此时留 0，由调用方回退默认 25。
        const QString afr = s.value(QStringLiteral("avg_frame_rate")).toString();
        const int slash = afr.indexOf(QLatin1Char('/'));
        if (slash > 0) {
            bool okN = false, okD = false;
            const double n = afr.left(slash).toDouble(&okN);
            const double d = afr.mid(slash + 1).toDouble(&okD);
            if (okN && okD && d > 0.0)
                out.fps = n / d;
        }
    }
    // 时长优先取 format（更可靠），回退 stream
    double dur = numOf(root.value(QStringLiteral("format")).toObject()
                           .value(QStringLiteral("duration")));
    if (dur <= 0 && !streams.isEmpty())
        dur = numOf(streams.first().toObject().value(QStringLiteral("duration")));
    out.durationSec = dur > 0 ? dur : 0.0;
    return out;
}

QString extractPoster(const QString& videoPath, double atSec, int timeoutMs)
{
    const QString dir = toolDir();
    if (dir.isEmpty()) return QString();

    const QString base = QFileInfo(videoPath).completeBaseName();
    // ★产物名带秒数：同一视频抽多个时刻必须互不覆盖，否则选帧器换了秒
    //   却读到上一帧的残留图（实测 t=0/2/4 三帧内容确实不同，见门禁）。
    const double effSec = g_ignoreFrameForQa ? 2.0 : atSec;
    const QString jpg = workPath(QStringLiteral("%1_t%2")
                                     .arg(base)
                                     .arg(effSec, 0, 'f', 2), ".jpg");

    QProcess p;
    p.setWorkingDirectory(dir);
    p.setProgram(dir + QStringLiteral("/ffmpeg.exe"));
    // -ss 放 -i 前 = 快进关键帧定位（更快）；-frames:v 1 取单帧；
    // -q:v 2 高质量 JPEG；-y 覆盖。默认 2 秒避开首帧黑场（见头文件说明）。
    const qint64 ms = qint64(qMax(0.0, g_ignoreFrameForQa ? 2.0 : atSec) * 1000.0);
    const QString ss = QStringLiteral("%1:%2:%3.%4")
        .arg(ms / 3600000).arg((ms / 60000) % 60)
        .arg((ms / 1000) % 60).arg(ms % 1000, 3, 10, QLatin1Char('0'));
    p.setArguments({ QStringLiteral("-y"), QStringLiteral("-ss"), ss,
                     QStringLiteral("-i"), videoPath,
                     QStringLiteral("-frames:v"), QStringLiteral("1"),
                     QStringLiteral("-q:v"), QStringLiteral("2"),
                     jpg });
    p.start();
    if (!p.waitForStarted(timeoutMs)) return QString();
    if (!p.waitForFinished(timeoutMs)) { p.kill(); p.waitForFinished(2000); return QString(); }

    // ★ 反例注入点：必须落在「解码完成、判定成败」这一步（铁律 32）。
    //   注入「rc==0 即成功 + 产物名猜测」→ 门禁必红（实测 rc 在两种模式下都是 0）。
    if (g_trustExitCodeForQa) {
        if (p.exitCode() == 0)
            return QFileInfo(jpg).completeBaseName() + QStringLiteral(".jpg");
        return QString();
    }

    // 正确判定：产物存在、可读、尺寸合理
    if (!QFileInfo::exists(jpg)) return QString();
    if (QFileInfo(jpg).size() < 1024) { QFile::remove(jpg); return QString(); }
    QImageReader rd(jpg);
    const QSize sz = rd.size();
    if (!sz.isValid() || sz.width() < 16 || sz.height() < 16) { QFile::remove(jpg); return QString(); }
    return jpg;
}

} // namespace VideoDecode
