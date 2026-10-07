#include "AudioPlayer.h"

#include "VideoDecode.h"

#include <QProcess>
#include <QThread>
#include <atomic>
#include <cstring>
#include <vector>

// ★winmm/windows 头必须在 Qt 之后、且关掉会污染 min/max 的宏（否则与 Qt 全局冲突）。
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>

namespace {
const int   kSampleRate = 44100;
const int   kChannels   = 2;
const int   kBytesPerSample = 2;
const int   kBlockAlign = kChannels * kBytesPerSample;      // 4 字节/帧
const int   kAvgBytesPerSec = kSampleRate * kBlockAlign;      // 176400 B/s
const int   kNumBuffers  = 6;                                // ~6×40ms 预缓冲
const int   kBufBytes   = kAvgBytesPerSec / 25;               // 每缓冲 40ms
bool g_forceBroken = false;
quint64 g_bytesWritten = 0;                                   // QA 观测

quint64 bytesWritten() { return g_bytesWritten; }

// RAII：waveOut 句柄。析构自动 reset+unprepare+close，避免线程退出时泄漏设备。
class WaveOut {
public:
    bool open()
    {
        WAVEFORMATEX wf{};
        wf.wFormatTag = WAVE_FORMAT_PCM;
        wf.nChannels = kChannels;
        wf.nSamplesPerSec = kSampleRate;
        wf.wBitsPerSample = kBytesPerSample * 8;
        wf.nBlockAlign = kBlockAlign;
        wf.nAvgBytesPerSec = kAvgBytesPerSec;
        if (waveOutOpen(&h_, WAVE_MAPPER, &wf, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
            return false;
        bufs_.resize(kNumBuffers);
        for (int i = 0; i < kNumBuffers; ++i) {
            bufs_[i] = {};
            bufs_[i].lpData = new char[kBufBytes];
            bufs_[i].dwBufferLength = 0;
        }
        return true;
    }
    ~WaveOut()
    {
        if (!h_) { for (auto& b : bufs_) delete[] b.lpData; return; }
        waveOutReset(h_);
        for (auto& b : bufs_) { if (b.lpData) waveOutUnprepareHeader(h_, &b, sizeof(b)); delete[] b.lpData; }
        waveOutClose(h_);
    }
    HWAVEOUT handle() const { return h_; }
    WAVEHDR& buf(int i) { return bufs_[i]; }
    int freeIdx()   // 空闲 = 未 prepared（从未写过/已释放） 或 已播完(DONE)
    {
        for (int i = 0; i < kNumBuffers; ++i)
            if (!(bufs_[i].dwFlags & WHDR_PREPARED) || (bufs_[i].dwFlags & WHDR_DONE))
                return i;
        return -1;
    }
private:
    HWAVEOUT h_ = nullptr;
    std::vector<WAVEHDR> bufs_;
};
} // namespace

// ---------------------------------------------------------------------------
// Worker：读 ffmpeg PCM → 喂 waveOut（在自己的线程，阻塞式）
// ---------------------------------------------------------------------------
class AudioPlayerWorker : public QObject
{
    Q_OBJECT
public:
    QString path;
    double  startSec = 0.0;
    std::atomic<bool> abortFlag{false};

public slots:
    void doStart()
    {
        if (path.isEmpty()) { emit error(QStringLiteral("no audio source")); emit finished(); return; }
        const QString dir = VideoDecode::toolDir();
        if (dir.isEmpty()) { emit error(QStringLiteral("ffmpeg runtime not found")); emit finished(); return; }

        QStringList args;
        args << QStringLiteral("-loglevel") << QStringLiteral("error")
             << QStringLiteral("-re");                    // 实时节奏，与视频同步
        if (startSec > 0.01) {
            const qint64 ms = qint64(startSec * 1000.0);
            args << QStringLiteral("-ss")
                 << QStringLiteral("%1:%2:%3.%4")
                        .arg(ms / 3600000).arg((ms / 60000) % 60)
                        .arg((ms / 1000) % 60).arg(ms % 1000, 3, 10, QLatin1Char('0'));
        }
        args << QStringLiteral("-i") << path;
        if (g_forceBroken)
            args << QStringLiteral("-i") << QStringLiteral("__la_broken_audio__.mp4");
        args << QStringLiteral("-vn")
             << QStringLiteral("-f") << QStringLiteral("s16le")
             << QStringLiteral("-ar") << QString::number(kSampleRate)
             << QStringLiteral("-ac") << QString::number(kChannels)
             << QStringLiteral("-");                      // stdout = PCM

        QProcess proc;
        proc.setProgram(dir + QStringLiteral("/ffmpeg.exe"));
        proc.setArguments(args);
        proc.setProcessChannelMode(QProcess::SeparateChannels);
        proc.start();
        if (!proc.waitForStarted(8000)) { emit error(QStringLiteral("ffmpeg audio start failed")); emit finished(); return; }

        WaveOut wo;
        if (!wo.open()) { emit error(QStringLiteral("audio device unavailable")); emit finished(); return; }

        QByteArray carry;                 // got 未必是缓冲整数倍，跨轮累积
        while (!abortFlag.load() && proc.state() == QProcess::Running) {
            if (!proc.waitForReadyRead(2000)) {
                if (proc.state() != QProcess::Running) break;
                continue;
            }
            const QByteArray got = proc.readAllStandardOutput();
            if (got.isEmpty()) continue;
            carry.append(got);
            while (carry.size() >= kBufBytes) {
                const int idx = wo.freeIdx();
                if (idx < 0) { QThread::msleep(5); continue; }   // 设备忙，等
                if (wo.buf(idx).dwFlags & WHDR_PREPARED)
                    waveOutUnprepareHeader(wo.handle(), &wo.buf(idx), sizeof(WAVEHDR));
                std::memcpy(wo.buf(idx).lpData, carry.constData(), kBufBytes);
                wo.buf(idx).dwBufferLength = kBufBytes;
                wo.buf(idx).dwFlags = 0;
                waveOutPrepareHeader(wo.handle(), &wo.buf(idx), sizeof(WAVEHDR));
                waveOutWrite(wo.handle(), &wo.buf(idx), sizeof(WAVEHDR));
                carry.remove(0, kBufBytes);
                g_bytesWritten += kBufBytes;
            }
        }
        proc.waitForFinished(2000);
        emit finished();
    }

signals:
    void finished();
    void error(const QString& message);
};

// ---------------------------------------------------------------------------
// AudioPlayer
// ---------------------------------------------------------------------------
AudioPlayer::AudioPlayer(QObject* parent) : QObject(parent) {}
AudioPlayer::~AudioPlayer() { stop(); }

void AudioPlayer::setForceBrokenForQa(bool on) { g_forceBroken = on; }
quint64 AudioPlayer::qaBytesWritten() { return g_bytesWritten; }
void   AudioPlayer::qaResetBytes() { g_bytesWritten = 0; }
int    AudioPlayer::qaBlockBytes() { return kBufBytes; }

bool AudioPlayer::deviceAvailable()
{
    WaveOut wo;
    return wo.open();   // 析构自动关
}

bool AudioPlayer::hasAudioTrack(const QString& videoPath)
{
    // 复用 ffprobe 一次：查 streams 里是否有非 v:0 的 audio 流。
    const QString dir = VideoDecode::toolDir();
    if (dir.isEmpty()) return false;
    QProcess p;
    p.setProgram(dir + QStringLiteral("/ffprobe.exe"));
    p.setArguments({ QStringLiteral("-v"), QStringLiteral("error"),
                     QStringLiteral("-select_streams"), QStringLiteral("a"),
                     QStringLiteral("-show_entries"), QStringLiteral("stream=codec_type"),
                     QStringLiteral("-of"), QStringLiteral("csv=p=0"),
                     videoPath });
    p.start();
    if (!p.waitForStarted(8000)) return false;
    if (!p.waitForFinished(8000)) { p.kill(); p.waitForFinished(2000); return false; }
    return p.readAllStandardOutput().trimmed().isEmpty() ? false : true;
}

void AudioPlayer::configure(const QString& videoPath, double startSec)
{
    m_path = videoPath;
    m_startSec = startSec > 0.0 ? startSec : 0.0;
}

void AudioPlayer::play()
{
    if (m_playing || m_path.isEmpty()) return;
    m_stopping = false;
    m_thread = new QThread(this);
    m_worker = new AudioPlayerWorker;
    m_worker->path = m_path;
    m_worker->startSec = m_startSec;
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &AudioPlayerWorker::finished, this, &AudioPlayer::onWorkerFinished);
    connect(m_worker, &AudioPlayerWorker::error, this, &AudioPlayer::error);
    m_thread->start();
    m_playing = true;
    QMetaObject::invokeMethod(m_worker, "doStart", Qt::QueuedConnection);
}

void AudioPlayer::onWorkerFinished()
{
    m_playing = false;
    if (!m_stopping)
        emit finished();
}

void AudioPlayer::stop()
{
    if (m_worker) m_worker->abortFlag.store(true);
    m_stopping = true;
    m_playing = false;
    if (m_thread) {
        m_thread->quit();
        if (!m_thread->wait(3000)) m_thread->terminate();
        m_thread = nullptr;
        m_worker = nullptr;
    }
}

#include "AudioPlayer.moc"