#include "VideoLightbox.h"

#include "VideoPlayerWidget.h"

#include <QFileInfo>
#include <QKeyEvent>
#include <QLabel>
#include <QVBoxLayout>

VideoLightbox::VideoLightbox(QWidget* parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("videoLightbox"));
    setWindowTitle(tr("LightAlbum — 视频播放"));
    resize(880, 560);

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    m_title = new QLabel(this);
    m_title->setObjectName(QStringLiteral("videoLightboxTitle"));
    m_title->setContentsMargins(10, 6, 10, 6);
    m_title->setTextInteractionFlags(Qt::TextSelectableByMouse);
    lay->addWidget(m_title, 0);

    m_player = new VideoPlayerWidget(this);
    lay->addWidget(m_player, 1);
}

void VideoLightbox::openVideo(const QString& videoPath, const QString& posterPath,
                              double durSec)
{
    m_title->setText(QFileInfo(videoPath).fileName());
    m_player->setSource(videoPath, posterPath, durSec);
    m_player->play();                 // 打开即播（视频查看器惯例）
    show();
    raise();
    activateWindow();
}

void VideoLightbox::keyPressEvent(QKeyEvent* e)
{
    // 空格 = 播放/暂停，Esc = 关闭（视频查看器通用键位）。
    if (e->key() == Qt::Key_Space) {
        if (m_player->isPlaying()) m_player->pause(); else m_player->play();
        return;
    }
    if (e->key() == Qt::Key_Escape) { close(); return; }
    QDialog::keyPressEvent(e);
}