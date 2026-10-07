// AudioPlayer —— 内嵌视频音频输出（2.30.0）
//
// 架构：第二路 ffmpeg（`-vn -f s16le -ar 44100 -ac 2 -`）把音轨解成裸 PCM 到 stdout，
// worker 线程读管道并喂给 **Windows 原生 waveOut**（winmm）。
//
// ★为何不用 Qt：实测本机 Qt6.8.3 mingw_64 **既无 Qt6Multimedia 也无 Qt6Audio**（lib 与
//   include 皆空），QAudioSink 不可用。故直接用 winmm —— MinGW 自带、`-lwinmm` 即可。
// ★为何不让 ffmpeg 出目标格式 PCM：直接出 44100/s16le/2ch，Qt/端侧零重采样。
//
// 节奏：ffmpeg 加 -re 按原速率吐 PCM（与视频同策略），故两端天然同步启动、1x 播放，
//   短片段不累积漂移。positionMs() 供后续做精细 A/V 对齐。
//
// 离屏可测：暴露 openDevice() 探测设备 + PCM 字节计数（门禁 LA_AUDIO_QA 断言
//   「解出非静音 PCM」而非真的听声）。

#ifndef AUDIOPLAYER_H
#define AUDIOPLAYER_H

#include <QByteArray>
#include <QObject>
#include <QString>

class QThread;
class AudioPlayerWorker;

class AudioPlayer : public QObject
{
    Q_OBJECT
public:
    explicit AudioPlayer(QObject* parent = nullptr);
    ~AudioPlayer() override;

    /// 设定音轨来源（与视频同一文件；ffmpeg -vn 只取音频）。
    void configure(const QString& videoPath, double startSec = 0.0);

    void play();      ///< 幂等；开设备 + 起 ffmpeg + 读 PCM
    void stop();      ///< 停 ffmpeg + 冲刷 waveOut（保留接口对称，暂停用 pause）

    bool isPlaying() const { return m_playing; }

    /// 设备与音轨可用性探测（无音频设备 / 无音轨 / 无 ffmpeg 时诚实返回 false）。
    static bool deviceAvailable();
    /// 该视频是否含音轨（ffprobe 查 streams 里的 audio）。
    static bool hasAudioTrack(const QString& videoPath);

    /// QA 反例钩子：令 ffmpeg 传入不存在的第二输入 → 建不起流 → 0 字节 PCM。
    static void setForceBrokenForQa(bool on);

    /// 已写入 waveOut 的 PCM 字节数（门禁断言「真的解出并送了音频」用）。
    static quint64 qaBytesWritten();
    static void   qaResetBytes();
    /// 单个 waveOut 缓冲的字节数（门禁断言块对齐用；禁止手写常量，铁律17）。
    static int    qaBlockBytes();

signals:
    void finished();
    void error(const QString& message);

private:
    void onWorkerFinished();

    QString m_path;
    double  m_startSec = 0.0;
    QThread*          m_thread = nullptr;
    AudioPlayerWorker* m_worker = nullptr;
    bool  m_playing = false;
    bool  m_stopping = false;
};

#endif // AUDIOPLAYER_H