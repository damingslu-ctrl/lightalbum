#pragma once
// P3 修图实验室页面：功能型修图 UI（零云端）。
// 双路径：rlAi（一键智能增强：recommend()）+ rlRun（套用下拉预设）。
// before/after 预览 + 16 预设下拉 + 建议芯片 + 手动滑杆组 + 进度条/徽章 + 导出副本。
#include <QWidget>
#include <QVector>
#include <QMap>
#include <QString>
#include "../core/Library.h"
#include "../core/RetouchEngine.h"

class QComboBox;
class QLabel;
class QPushButton;
class QSlider;
class QProgressBar;
class QTimer;
class QScrollArea;

struct RtSlider {
    const char* key;
    const char* label;
    double min, max;
};

class RetouchPage : public QWidget {
    Q_OBJECT
public:
    explicit RetouchPage(const QVector<Photo>* photos, const QString& thumbsDir,
                        QWidget* parent = nullptr);

private slots:
    void onPhotoChanged(int idx);
    void onSliderChanged();
    void onPresetChanged(int idx);
    void onAutoEnhance();   // rlAi
    void onApplyPreset();   // rlRun
    void onReset();
    void onExport();
    void doApply();        // 防抖执行修图
    void refreshChips();

private:
    void buildSlider(const RtSlider& def, class QGridLayout* gl);
    void setParams(const RetouchParams& p);   // 同步滑杆 + 数值
    void loadPhoto(int idx);
    void updateBadge();

    const QVector<Photo>* photos_ = nullptr;
    QString thumbsDir_;
    QVector<int>  usableIdx_;                 // 有 thumbFile 的照片下标
    QImage        srcImg_;                    // 当前原始图（缩略图，已裁到预览尺寸）
    QPixmap       srcPm_;

    QComboBox*    photoCombo_ = nullptr;
    QComboBox*    presetCombo_ = nullptr;
    QLabel*       beforeLbl_ = nullptr;
    QLabel*       afterLbl_  = nullptr;
    QLabel*       badge_     = nullptr;
    QProgressBar* prog_      = nullptr;
    QPushButton* autoBtn_    = nullptr;
    QPushButton* resetBtn_   = nullptr;
    QPushButton* exportBtn_  = nullptr;
    QTimer*       applyTimer_ = nullptr;

    QMap<QString, double> cur_;              // 当前参数（归一量）
    QVector<QSlider*>     sliders_;
    QVector<QLabel*>      sliderVals_;
    QVector<const RtSlider*> sliderDefs_;
    QWidget*       chipRow_ = nullptr;
    QLabel*        chipTitle_ = nullptr;
};
