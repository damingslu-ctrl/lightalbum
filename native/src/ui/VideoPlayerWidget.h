// VideoPlayerWidget —— 内嵌视频播放器 UI（2.30.0）
//
// 显示层：VideoDecoder 逐帧解码（经 -re 按原速率），本控件维护「最新帧」并在
// 16ms 定时器里重绘。节奏由 ffmpeg 的 -re 保证，故无需在 UI 侧做 pts 配时。
//
// 兜底：首帧到达前显示海报（Photo::thumbFile，即抽帧封面），再前显示「▶ 加载中」，
//       绝不留白屏或半成品帧（铁律：数据诚实）。
//
// 离屏可测：grab() 强制渲染，可在 offscreen 下断言像素非空（不依赖原生弹窗）。

#ifndef VIDEOPLAYERWIDGET_H
#define VIDEOPLAYERWIDGET_H

#include <QImage>
#include <QString>
#include <QWidget>

class VideoDecoder;
class QPushButton;
class QSlider;
class QLabel;
class QTimer;

class VideoPlayerWidget : public QWidget
{
    Q_OBJECT
public:
    explicit VideoPlayerWidget(QWidget* parent = nullptr);
    ~VideoPlayerWidget() override;

    /// 设定播放源。videoPath 视频绝对路径；posterPath 海报（可空）；durSec 时长（秒，0=未知）。
    void setSource(const QString& videoPath, const QString& posterPath, double durSec);

    /// 开始/继续播放（幂等）。
    void play();
    /// 暂停（保留当前帧）。
    void pause();
    bool isPlaying() const { return m_playing; }

    /// 跳转到指定秒（停-重起，轻量实现；实测 -ss 走关键帧定位）。
    void seekTo(double sec);

    /// 当前已解码到的帧（测试/门禁用；正常为空表示还没解出帧）。
    QImage currentFrame() const { return m_curFrame; }
    /// 已解码帧是否随时间推进过（>1 帧且帧间不同）——门禁用它证明「真在动」。
    bool hasAdvanced() const { return m_frameCount >= 2 && m_motionSeen; }

protected:
    void paintEvent(QPaintEvent*) override;

private slots:
    void onFrame(const QImage& frame, qint64 ptsMs);
    void onFinished();
    void onError(const QString& msg);
    void tick();

private:
    VideoDecoder* m_dec = nullptr;
    QPushButton*  m_playBtn = nullptr;
    QSlider*      m_seek = nullptr;
    QLabel*       m_timeLbl = nullptr;
    QTimer*       m_timer = nullptr;

    QString m_path, m_posterPath;
    QImage  m_poster;
    QImage  m_curFrame;
    double  m_durSec = 0.0;
    double  m_fps = 25.0;
    double  m_startSec = 0.0;
    bool    m_playing = false;
    int     m_frameCount = 0;
    bool    m_motionSeen = false;
    bool    m_seekInProgress = false;
};

#endif // VIDEOPLAYERWIDGET_H