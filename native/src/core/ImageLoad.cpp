#include "ImageLoad.h"
#include "RawDecode.h"
#include "VideoDecode.h"

#include <QFile>
#include <QFileInfo>
#include <QImageReader>

namespace {

// 读一个中间图并在读完后删除（RAW 的 PPM / 视频的抽帧 JPEG 都是一次性产物）。
QImage loadAndCleanup(const QString& tmpPath)
{
    if (tmpPath.isEmpty()) return QImage();
    QImageReader r(tmpPath);
    r.setAutoTransform(true);
    QImage img = r.read();          // ★ 判定唯一标准 = 实读非 null（铁律 24/37）
    QFile::remove(tmpPath);          // 中间图用完即删，不留磁盘垃圾
    return img;
}

} // namespace

namespace ImageLoad {

// QA 反例旗标（见头文件）。默认 false = 真实实现。
static bool g_forceRawFallback = false;
void setForceRawFallbackForQa(bool on) { g_forceRawFallback = on; }

bool needsExternal(const QString& path)
{
    return RawDecode::isRawExt(path) || VideoDecode::isVideoExt(path);
}

QImage loadForEdit(const QString& path, double videoFrameSec)
{
    if (path.isEmpty()) return QImage();

    if (RawDecode::isRawExt(path) && !g_forceRawFallback) {
        // 走 LibRaw 出 PPM（Qt 内建 handler，实测可读）。解码器缺失时返回 null。
        return loadAndCleanup(RawDecode::decodeToTempPpm(path));
    }
    if (VideoDecode::isVideoExt(path)) {
        // 视频：抽指定时刻的一帧用于编辑（选帧器可改秒数；默认 2.0 沿用既有行为）。
        return loadAndCleanup(VideoDecode::extractPoster(path, videoFrameSec));
    }
    // 普通照片 + QA 反例下的 RAW：行为与旧的 QImage(path) 一致（含 autoTransform 方向摆正）
    QImageReader r(path);
    r.setAutoTransform(true);
    return r.read();
}

} // namespace ImageLoad
