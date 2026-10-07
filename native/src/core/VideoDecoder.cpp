#include "VideoDecoder.h"

#include "VideoDecode.h"

#include <QElapsedTimer>
#include <QProcess>
#include <QThread>
#include <atomic>
#include <cstring>

namespace {
bool g_forceBroken = false;   // QA 反例旗标（进程级，同一轮门只设一次）
const int kReadTimeoutMs = 2000;
const int kStartTimeoutMs = 8000;
} // namespace

// ---------------------------------------------------------------------------
// Worker：在自己的线程里跑 ffmpeg 并读管道
// ---------------------------------------------------------------------------
class VideoDecoderWorker : public QObject
{
    Q_OBJECT
public:
    QString path;
    int     outW = 0;
    int     outH = 0;
    double  fps  = 25.0;
    std::atomic<bool> abortFlag{false};

public slots:
    void doStart()
    {
        const qint64 frameBytes = qint64(outW) * outH * 3;   // rgb24
        if (outW <= 0 || outH <= 0 || path.isEmpty()) {
            emit decodeError(QStringLiteral("bad geometry or empty path"));
            emit finished();
            return;
        }
        const QString dir = VideoDecode::toolDir();
        if (dir.isEmpty()) {
            emit decodeError(QStringLiteral("ffmpeg runtime not found"));
            emit finished();
            return;
        }

        QStringList args;
        args << QStringLiteral("-loglevel") << QStringLiteral("error")
             << QStringLiteral("-i") << path;
        if (g_forceBroken) {
            // 反例：塞一个不存在的第二输入 → ffmpeg 建不起输出流，静默 0 帧退出。
            // 这正是「解码失败却看起来一切正常」的真实缺陷形态。
            args << QStringLiteral("-i")
                 << QStringLiteral("__la_broken_no_such_input__.mp4");
        }
        args << QStringLiteral("-an")
             << QStringLiteral("-f") << QStringLiteral("rawvideo")
             << QStringLiteral("-pix_fmt") << QStringLiteral("rgb24")
             << QStringLiteral("-s")
             << QStringLiteral("%1x%2").arg(outW).arg(outH)
             << QStringLiteral("-r") << QString::number(fps, 'f', 3)
             << QStringLiteral("-");                    // stdout = 裸流

        QProcess proc;
        proc.setProgram(dir + QStringLiteral("/ffmpeg.exe"));
        proc.setArguments(args);
        // ★stdout 是二进制裸流，必须与 stderr 分离，否则日志会污染帧数据。
        proc.setProcessChannelMode(QProcess::SeparateChannels);
        proc.start();
        if (!proc.waitForStarted(kStartTimeoutMs)) {
            emit decodeError(QStringLiteral("ffmpeg failed to start"));
            emit finished();
            return;
        }

        QByteArray buf;
        qint64 frameIndex = 0;
        const qint64 rowBytes = qint64(outW) * 3;

        auto drain = [&](bool block) {
            if (block) {
                if (!proc.waitForReadyRead(kReadTimeoutMs)
                    && proc.state() == QProcess::Running)
                    return;
            }
            buf.append(proc.readAllStandardOutput());
            while (buf.size() >= frameBytes && !abortFlag.load()) {
                QImage img(outW, outH, QImage::Format_RGB888);
                const char* src = buf.constData();
                for (int y = 0; y < outH; ++y)
                    std::memcpy(img.scanLine(y), src + y * rowBytes, size_t(rowBytes));
                buf.remove(0, int(frameBytes));
                const qint64 ptsMs = qint64(frameIndex * 1000.0 / fps);
                emit frameDecoded(img, ptsMs);
                ++frameIndex;
            }
        };

        while (!abortFlag.load() && proc.state() == QProcess::Running)
            drain(true);

        // 收尾：进程已退出但缓冲区可能还有整帧（末帧不因 EOF 丢失）
        proc.waitForFinished(kReadTimeoutMs);
        drain(false);

        if (abortFlag.load())
            emit decodeError(QStringLiteral("aborted by caller"));
        emit finished();
    }

signals:
    void frameDecoded(const QImage& frame, qint64 ptsMs);
    void finished();
    void decodeError(const QString& message);
};

// ---------------------------------------------------------------------------
// VideoDecoder
// ---------------------------------------------------------------------------
VideoDecoder::VideoDecoder(QObject* parent) : QObject(parent) {}

VideoDecoder::~VideoDecoder()
{
    stop();
}

void VideoDecoder::setForceBrokenForQa(bool on) { g_forceBroken = on; }
bool VideoDecoder::forceBrokenForQa() { return g_forceBroken; }

void VideoDecoder::configure(const QString& videoPath, int outW, int outH, double fps)
{
    m_path = videoPath;
    m_outW = outW > 0 ? outW : 1;
    m_outH = outH > 0 ? outH : 1;
    m_fps  = fps > 0.1 ? fps : 25.0;
}

void VideoDecoder::start()
{
    if (m_running)
        return;
    m_stopping = false;

    m_thread = new QThread(this);
    m_worker = new VideoDecoderWorker;
    m_worker->path = m_path;
    m_worker->outW = m_outW;
    m_worker->outH = m_outH;
    m_worker->fps  = m_fps;
    m_worker->moveToThread(m_thread);

    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &VideoDecoderWorker::frameDecoded,
            this, &VideoDecoder::frameDecoded);
    connect(m_worker, &VideoDecoderWorker::decodeError,
            this, &VideoDecoder::decodeError);
    connect(m_worker, &VideoDecoderWorker::finished,
            this, &VideoDecoder::onWorkerFinished);

    m_thread->start();
    m_running = true;
    // 队列投递：确保 doStart 在 worker 线程的事件循环里执行，
    // QProcess 也就在正确的线程中被创建（铁律：QProcess 归其所属线程）。
    QMetaObject::invokeMethod(m_worker, "doStart", Qt::QueuedConnection);
}

void VideoDecoder::onWorkerFinished()
{
    m_running = false;
    if (!m_stopping)
        emit finished();
}

void VideoDecoder::stop()
{
    if (m_worker)
        m_worker->abortFlag.store(true);
    m_stopping = true;
    m_running = false;
    if (m_thread) {
        m_thread->quit();
        // ★必须等线程真正结束：worker 析构后仍可能被 QProcess 回调触碰
        //   （铁律 19：拥有 worker 的对象析构时先清标志再 wait）。
        if (!m_thread->wait(3000))
            m_thread->terminate();
        m_thread = nullptr;
        m_worker = nullptr;
    }
}

#include "VideoDecoder.moc"