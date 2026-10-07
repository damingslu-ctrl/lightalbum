#include "VideoPlayerWidget.h"

#include "../core/VideoDecode.h"
#include "../core/VideoDecoder.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

namespace {
// 解码输出宽度上限：够清晰又压住解码开销（ffmpeg 侧缩放比 UI 侧快）。
const int kMaxDecodeW = 640;
// 「▶ 加载中」占位文案（i18n 走 translate，铁律 14）。
inline QString tr(const char* s) { return QCoreApplication::translate("LightAlbum", s); }
QString fmtMs(qint64 ms)
{
    if (ms < 0) ms = 0;
    const qint64 total = ms / 1000;
    return QStringLiteral("%1:%2")
        .arg(total / 60, 2, 10, QLatin1Char('0'))
        .arg(total % 60, 2, 10, QLatin1Char('0'));
}
} // namespace

VideoPlayerWidget::VideoPlayerWidget(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("videoPlayer"));
    setMinimumSize(320, 240);
    setAutoFillBackground(true);

    m_dec = new VideoDecoder(this);
    connect(m_dec, &VideoDecoder::frameDecoded, this, &VideoPlayerWidget::onFrame);
    connect(m_dec, &VideoDecoder::finished,    this, &VideoPlayerWidget::onFinished);
    connect(m_dec, &VideoDecoder::decodeError, this, &VideoPlayerWidget::onError);

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    auto* bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("videoBar"));
    auto* bl = new QVBoxLayout(bar);
    bl->setContentsMargins(6, 4, 6, 4);

    m_playBtn = new QPushButton(QStringLiteral("▶"), bar);
    m_playBtn->setObjectName(QStringLiteral("videoPlayBtn"));
    m_playBtn->setFixedSize(34, 28);
    m_playBtn->setToolTip(tr("播放 / 暂停"));
    connect(m_playBtn, &QPushButton::clicked, this, [this]{
        if (m_playing) pause(); else play();
    });

    m_seek = new QSlider(Qt::Horizontal, bar);
    m_seek->setObjectName(QStringLiteral("videoSeek"));
    m_seek->setEnabled(false);           // 时长未知前不可拖
    connect(m_seek, &QSlider::sliderReleased, this, [this]{
        seekTo(m_seek->value() / 1000.0);
    });

    m_timeLbl = new QLabel(QStringLiteral("0:00 / 0:00"), bar);
    m_timeLbl->setObjectName(QStringLiteral("videoTime"));

    auto* row = new QHBoxLayout();
    row->addWidget(m_playBtn);
    row->addWidget(m_seek, 1);
    row->addWidget(m_timeLbl);
    bl->addLayout(row);

    lay->addWidget(bar, 0);
    // 画面区在上、控制条固定在下：用一个占满的 stretch 把 bar 推到底。
    lay->insertWidget(0, new QWidget(this), 1);

    m_timer = new QTimer(this);
    m_timer->setInterval(16);
    connect(m_timer, &QTimer::timeout, this, &VideoPlayerWidget::tick);
}

VideoPlayerWidget::~VideoPlayerWidget()
{
    if (m_dec) m_dec->stop();   // 线程安全回收（铁律 19）
}

void VideoPlayerWidget::setSource(const QString& videoPath, const QString& posterPath,
                                  double durSec)
{
    m_path = videoPath;
    m_posterPath = posterPath;
    m_durSec = durSec > 0 ? durSec : 0.0;
    m_curFrame = QImage();
    m_frameCount = 0;
    m_motionSeen = false;
    m_startSec = 0.0;
    m_playing = false;
    if (m_playBtn) m_playBtn->setText(QStringLiteral("▶"));

    // 海报：优先用库里已有的抽帧封面（省一次解码，且首帧到达前不白屏）。
    if (!m_posterPath.isEmpty() && QFileInfo::exists(m_posterPath)) {
        QImageReader rd(m_posterPath);
        rd.setAutoTransform(true);
        m_poster = rd.read();
    } else {
        m_poster = QImage();
    }

    // 时长/帧率：优先库里的值（已落库），否则现探。
    if (m_durSec <= 0.0) {
        const VideoDecode::VideoProbe p = VideoDecode::probe(m_path);
        m_durSec = p.durationSec;
        (void)p;   // 尺寸/帧率在 play() 里按控件宽度现算
    }

    if (m_seek) {
        m_seek->setEnabled(m_durSec > 0.0);
        m_seek->setRange(0, int(m_durSec * 1000.0));
        m_seek->setValue(0);
    }
    if (m_timeLbl)
        m_timeLbl->setText(QStringLiteral("0:00 / ") + fmtMs(qint64(m_durSec * 1000)));
    update();
}

void VideoPlayerWidget::play()
{
    if (m_path.isEmpty() || m_playing) return;

    // 解码分辨率：按控件宽度等比缩放（上限 kMaxDecodeW），高由比例推出。
    const VideoDecode::VideoProbe p = VideoDecode::probe(m_path);
    if (p.width > 0 && p.height > 0) m_fps = p.fps > 0.1 ? p.fps : 25.0;
    int dw = qMax(2, p.width > 0 ? p.width : kMaxDecodeW);
    int dh = qMax(2, p.height > 0 ? p.height : dw * 9 / 16);
    if (dw > kMaxDecodeW) { dh = qMax(2, dh * kMaxDecodeW / dw); dw = kMaxDecodeW; }
    if (width() > 64 && width() < dw) { dh = qMax(2, dh * width() / dw); dw = qMax(2, width()); }

    m_dec->configure(m_path, dw, dh, m_fps, m_startSec, /*realtime=*/true);
    m_dec->start();
    m_playing = true;
    m_timer->start();
    if (m_playBtn) m_playBtn->setText(QStringLiteral("⏸"));
    update();
}

void VideoPlayerWidget::pause()
{
    if (!m_playing) return;
    m_playing = false;
    m_timer->stop();
    if (m_dec) m_dec->stop();
    if (m_playBtn) m_playBtn->setText(QStringLiteral("▶"));
    update();
}

void VideoPlayerWidget::seekTo(double sec)
{
    if (m_path.isEmpty()) return;
    const double target = qBound(0.0, sec, qMax(0.0, m_durSec - 0.05));
    const bool wasPlaying = m_playing;
    if (m_dec) m_dec->stop();          // 停-重起（-ss 定位，轻量）
    m_startSec = target;
    m_curFrame = QImage();
    m_frameCount = 0;
    m_motionSeen = false;
    if (wasPlaying) {
        m_playing = false;              // play() 里会置 true
        play();
    } else {
        update();
    }
}

void VideoPlayerWidget::onFrame(const QImage& frame, qint64 ptsMs)
{
    // 真动态检测（门禁 A4 同一判据的 UI 侧）：与首帧采样比对，出现差异即置位。
    if (m_frameCount == 0) {
        m_curFrame = frame;
    } else if (!m_motionSeen && frame.size() == m_curFrame.size()) {
        for (int y = 0; y < frame.height() && !m_motionSeen; y += 16)
            for (int x = 0; x < frame.width(); x += 16)
                if (frame.pixel(x, y) != m_curFrame.pixel(x, y)) { m_motionSeen = true; break; }
    }
    m_curFrame = frame;
    ++m_frameCount;

    const qint64 curMs = qint64(m_startSec * 1000.0) + ptsMs;
    if (m_seek && !m_seekInProgress && m_durSec > 0.0)
        m_seek->setValue(int(qBound(0.0, curMs / 1000.0, m_durSec)));
    if (m_timeLbl)
        m_timeLbl->setText(fmtMs(curMs) + QStringLiteral(" / ") + fmtMs(qint64(m_durSec * 1000)));
    update();
}

void VideoPlayerWidget::onFinished()
{
    m_playing = false;
    m_timer->stop();
    if (m_playBtn) m_playBtn->setText(QStringLiteral("▶"));
    update();
}

void VideoPlayerWidget::onError(const QString& msg)
{
    // 解码失败：停播放、留最后一帧（若有），不崩、不白屏。
    m_playing = false;
    m_timer->stop();
    if (m_playBtn) m_playBtn->setText(QStringLiteral("▶"));
    update();
    (void)msg;
}

void VideoPlayerWidget::tick() { /* -re 已负责节奏，这里仅保底刷新 */ update(); }

void VideoPlayerWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0x0d, 0x0f, 0x0e));   // 深底，突出画面

    const QImage* src = nullptr;
    if (!m_curFrame.isNull())      src = &m_curFrame;
    else if (!m_poster.isNull())   src = &m_poster;

    if (!src) {
        p.setPen(QColor(0xbf, 0xc8, 0xc2));
        p.drawText(rect(), Qt::AlignCenter, tr("▶ 加载中…"));
        return;
    }

    // 等比缩放居中（不拉伸变形）。
    const QSize s = src->size();
    const int dw = qMax(1, width());
    const int dh = qMax(1, rect().height() - 34);   // 让出底部控制条
    const double sc = qMin(double(dw) / s.width(), double(dh) / s.height());
    const int tw = qMax(1, int(s.width() * sc));
    const int th = qMax(1, int(s.height() * sc));
    const QRect target((dw - tw) / 2, (dh - th) / 2, tw, th);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.drawImage(target, *src);
}