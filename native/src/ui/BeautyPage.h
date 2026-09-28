#pragma once
// P3「美图」页面：风格化预设画廊（零云端，复用 RetouchEngine 16 预设）。
// 与 RetouchPage 的差异：不暴露手动滑杆，聚焦「一键风格」——所有预设以卡片缩略图
// 呈现，点选即套用并在大图 before/after 预览，可导出。rlAi=智能增强(recommend)。
#include <QWidget>
#include <QVector>
#include <QString>
#include "../core/Library.h"
#include "../core/RetouchEngine.h"

class QComboBox;
class QLabel;
class QPushButton;
class QToolButton;
class QTimer;

class BeautyPage : public QWidget {
    Q_OBJECT
public:
    explicit BeautyPage(const QVector<Photo>* photos, const QString& thumbsDir,
                        QWidget* parent = nullptr);

private slots:
    void onPhotoChanged(int idx);
    void onCardClicked(int i);
    void onAutoEnhance();   // rlAi
    void onExport();
    void refreshGallery();
    void updatePreview();

private:
    void loadPhoto(int idx);

    const QVector<Photo>* photos_ = nullptr;
    QString thumbsDir_;
    QVector<int> usableIdx_;
    QImage      srcImg_;                  // 当前原图（预览尺寸）
    QPixmap     srcPm_;
    int         sel_ = 0;                 // 选中预设下标

    QComboBox*   photoCombo_ = nullptr;
    QLabel*      beforeLbl_  = nullptr;
    QLabel*      afterLbl_   = nullptr;
    QLabel*      selName_    = nullptr;   // 当前风格名
    QLabel*      badge_      = nullptr;
    QPushButton* autoBtn_    = nullptr;
    QPushButton* exportBtn_  = nullptr;

    QWidget*     gallery_    = nullptr;   // 卡片容器
    QVector<QToolButton*> cards_;

    QVector<RetouchPreset> presets_;
    RetouchParams active_;              // 当前预览所用参数（预设或 AI）
};
