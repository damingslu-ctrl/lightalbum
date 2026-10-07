// VideoDecoder —— 内嵌视频解码核心（2.30.0）
//
// 架构：经 QProcess 调 ffmpeg.exe 输出 **rawvideo 裸流到 stdout**，在 worker 线程
// 读管道、逐帧切分，转成 QImage 逐帧抛出。
//
// ★为何不链入 libavformat/libavcodec：链入会改变 FFmpeg 的分发形态，使项目从
//   「独立进程调用（mere aggregation）」变成「同一映像内的链接」，GPL-3.0 的
//   copyleft 即传染到本项目源码 —— 那正是 2.28.0 刻意规避的立场。走 exe 则
//   立场不变（详见 docs/THIRD_PARTY_NOTICE.md 的失效条件）。
//
// ★为何 rgb24 而非 yuv420p：QImage 没有原生 YUV 平面格式，转 RGB24 后
//   QPainter 可直接 drawImage，无需自写色彩空间转换，且每帧行步长 = w*3 无 padding。
//
// 实测（9.0.2 essentials，testsrc 10s/25fps）：
//   ffmpeg -i in.mp4 -an -f rawvideo -pix_fmt rgb24 -s 640x480 -r 25 -
//   → 恰好 250 帧、字节数 230400000 = 250*640*480*3、连续帧 29/29 全不同。
// ★坑：本版本 ffmpeg 已移除 `-vsync`（Unrecognized option），只可用 `-r`。

#ifndef VIDEODECODER_H
#define VIDEODECODER_H

#include <QImage>
#include <QObject>
#include <QString>

class QThread;
class VideoDecoderWorker;

/// 逐帧解码器。configure() 设定源与输出规格，start() 起线程读管道。
///
/// 线程模型：内部 worker 对象 moveToThread(QThread)，doStart() 在该线程内
/// 创建 QProcess 并以阻塞式 waitForReadyRead 读管道；帧信号跨线程队列投递回
/// 调用方线程。调用方只需在主事件循环里收 frameDecoded。
class VideoDecoder : public QObject
{
    Q_OBJECT
public:
    explicit VideoDecoder(QObject* parent = nullptr);
    ~VideoDecoder() override;

    /// 设定解码参数。outW/outH 为输出像素尺寸（建议按显示区缩放，控制解码开销），
    /// fps 为输出帧率（由 VideoDecode::probe 取源帧率传入；未知时传 25）。
    /// startSec > 0 时从该秒开始（跳转，实测 -ss 在 -i 之前，快且准）。
    /// realtime=true 时插入 -re 让 ffmpeg 按原速率吐帧（播放用；门禁取帧设 false 走最快）。
    void configure(const QString& videoPath, int outW, int outH, double fps,
                   double startSec = 0.0, bool realtime = false);

    /// 启动解码（幂等：已在跑则忽略）。
    void start();

    /// 停止解码并回收线程。可安全重复调用，也可从 frameDecoded 回调里调用。
    void stop();

    bool isRunning() const { return m_running; }

    /// QA 反例钩子：令 ffmpeg 传入一个不存在的第二输入 → 无法建立输出流，
    /// 精确复现「解码失败但不崩溃」的真实缺陷形态（实测：0 帧）。
    static void setForceBrokenForQa(bool on);
    static bool forceBrokenForQa();

signals:
    /// 解出一帧。ptsMs 为该帧在输出时间轴上的呈现时刻（毫秒，从 0 起）。
    void frameDecoded(const QImage& frame, qint64 ptsMs);
    /// 全部解完 / 被中止。UI 收到即复位「播放中」状态。
    void finished();
    /// 无法启动或解码失败（ffmpeg 缺失、管道起不来等）。此时仍会补发 finished。
    void decodeError(const QString& message);

private:
    void onWorkerFinished();

    QString m_path;
    int     m_outW = 0;
    int     m_outH = 0;
    double  m_fps  = 25.0;
    double  m_startSec = 0.0;
    bool    m_realtime = false;

    QThread*           m_thread = nullptr;
    VideoDecoderWorker* m_worker = nullptr;
    bool                m_running = false;
    bool                m_stopping = false;
};

#endif // VIDEODECODER_H