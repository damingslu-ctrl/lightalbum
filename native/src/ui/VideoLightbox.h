// VideoLightbox —— 视频灯箱宿主（2.30.0）
//
// 之前项目没有生产级单图查看器（FullCache 只被 QA 探针引用），故本轮补上最小
// 可用的视频查看宿主：标题（文件名 + 时长）+ VideoPlayerWidget + 关闭/键盘。
// 后续里程碑可把它扩成通用灯箱（照片也走这里）。

#ifndef VIDEOLIGHTBOX_H
#define VIDEOLIGHTBOX_H

#include <QDialog>
#include <QString>

class VideoPlayerWidget;

class VideoLightbox : public QDialog
{
    Q_OBJECT
public:
    explicit VideoLightbox(QWidget* parent = nullptr);

    /// 打开并显示一个视频。videoPath 视频；posterPath 海报（可空）；durSec 时长（0=未知）。
    void openVideo(const QString& videoPath, const QString& posterPath, double durSec);

    VideoPlayerWidget* player() const { return m_player; }

protected:
    void keyPressEvent(QKeyEvent* e) override;

private:
    VideoPlayerWidget* m_player = nullptr;
    class QLabel*      m_title = nullptr;
};

#endif // VIDEOLIGHTBOX_H