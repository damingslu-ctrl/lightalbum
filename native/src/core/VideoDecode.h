#pragma once
// 视频解码桥（2.28.0）—— 通过**外部进程**调用 FFmpeg（ffmpeg/ffprobe），
// 与 RawDecode 同一架构：宿主不链接解码库，只起独立进程，故 ABI 完全解耦。
//
// 与 RAW 的对称关系（刻意保持一致，便于统一理解与门禁复用）：
//   RawDecode : dcraw_emu -h -w → PPM   → QImageReader（Qt 内建 handler）
//   VideoDecode: ffmpeg -frames:v 1 → JPEG → QImageReader（qjpeg 插件）
// 中间格式选 JPEG：Qt 侧实测 640x480 null=0（PNG 亦可，同为 Qt 内建 handler）。
//
// 抽帧时刻选 00:00:02 而非 0：许多视频首帧是黑场/淡入，抽 0 会得到近黑封面。
// 该值是经验值，改动前请用 tools/video_probe.cpp 在真实样本上复测。
//
// 数据诚实铁律：失败一律返回「空 / 0 / invalid」，绝不返回编造的时长或尺寸。
//   视频的 EXIF 概念不存在（无 Camera/ISO/光圈），故 VideoProbe 只取容器与视频流
//   客观量：duration / width / height / codec。
#include <QString>
#include <QSize>

namespace VideoDecode {

// 视频扩展名白名单（唯一真源；ICloudSource 的空壳判定应改用本函数，避免两处漂移）。
bool isVideoExt(const QString& fileName);

// 枚举并联判据：与 RawDecode::needsExternalDecode 同构 —— 只对视频为真。
bool needsExternalDecode(const QString& fileName, bool decoderAvailable);

// 解码器可用性（ffmpeg.exe + ffprobe.exe 是否就位于 <exe 同级>/3rdparty/ffmpeg/bin）。
bool available();
QString toolDir();

// 视频客观元数据。
struct VideoProbe {
    double durationSec = 0;      // 秒；0 = 未知
    int    width = 0;            // 0 = 未知
    int    height = 0;
    double fps = 0;              // 平均帧率；0 = 未知（ffprobe avg_frame_rate 形如 "25/1"）
    QString codec;               // 如 "h264"；空 = 未知
    bool   valid() const { return durationSec > 0 || width > 0 || height > 0; }
};

// 取元数据（走 ffprobe）。失败返回默认构造的无效值。
// 解析 JSON 而非正则扫文本：ffprobe 的 `-of json` 输出结构稳定，抗版本漂移。
VideoProbe probe(const QString& videoPath, int timeoutMs = 20000);

// 抽一帧到临时 JPEG，返回路径（调用方负责删除）；失败返回空串。
// atSec = 抽帧时刻（秒）。默认 2.0 避开开场黑场（既有行为，保持兼容）。
// ★ 产物名带秒数：同一视频抽多个时刻互不覆盖——实测 t=0/2/4s 三帧内容确实不同
//   （md5各异），若共用产物名，选帧器换秒后会读到上一帧残留图。
// ★ 与 RAW 同款铁律：成败**只能**看产物是否存在且可读，绝不信 exit code。
QString extractPoster(const QString& videoPath, double atSec = 2.0, int timeoutMs = 30000);

// QA 钩子（2.28.0）：置 true 后 extractPoster 改走缺陷形态 ——
// 「rc==0 即认为成功，且产物名按 completeBaseName 猜测」，跳过产物校验。
// 与 RawDecode::setForceArtifactBlindForQa 同构；期望值不随本开关改变（铁律 22）。
void setForceTrustExitCodeForQa(bool on);

// QA 钩子（2.31.0）：置 true 后 extractPoster **忽略 atSec**，恒抽默认第 2 秒 ——
// 精确复现「换了秒却读到同一帧」的选帧器头号缺陷形态。期望值不随本开关改变（铁律 22）。
void setForceIgnoreFrameForQa(bool on);

// ---- 2.31.0 抽帧磁盘缓存（ Frame Cache ） ----
// 背景：extractPoster 每次起 ffmpeg 抽帧，~0.3-0.8s/次；同一视频同一秒被多次请求
// （Scanner 缩略图、RetouchPage 反复进入、选帧器回退）时浪费严重。
// 磁盘缓存以「文件身份 + 修改时间 + 大小 + 秒数」为键，把 JPEG 帧持久化到
// QStandardPaths::CacheLocation/frames，命中时直接从缓存拷贝到临时目录返回，
// 调用方仍按旧契约删除临时产物；未命中则 ffmpeg 抽帧后写入缓存再返回。
//
// 当前策略：
//   - 默认磁盘预算 512MiB（env LA_VIDEO_CACHE_BUDGET_BYTES 可覆盖）
//   - 写缓存时若超预算，按文件 mtime 淘汰最旧的条目
//   - 命中时验证缓存文件本身可读且非平凡（与 extractPoster 成败标准一致）
//   - 任何 QA 反例钩子激活时自动绕过缓存，避免把缺陷形态固化到磁盘
//
// 注意：这不替代 FullCache（内存原图缓存），而是把「ffmpeg 抽帧」这一昂贵步骤
// 的结果持久化；FullCache 仍负责修图页/灯箱的内存 QImage 缓存。

// 缓存目录（按需创建）。返回空串表示无法创建/不可用。
QString frameCacheDir();

// 设置/获取磁盘预算（字节）。默认 512MiB。
void   setFrameCacheBudgetBytes(qint64 bytes);
qint64 frameCacheBudgetBytes();

// 当前缓存占用（字节）与条目数。
qint64 frameCacheBytes();
int    frameCacheCount();

// 清空磁盘缓存。
void clearFrameCache();

// QA 钩子（2.31.0）：置 true 后 extractPoster 完全绕过磁盘缓存，
// 用于正例验证「确实走了 ffmpeg」和反例验证「文件修改后缓存失效」。
void setForceBypassFrameCacheForQa(bool on);

} // namespace VideoDecode
