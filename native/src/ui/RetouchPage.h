#pragma once
// P3 修图实验室页面：功能型修图 UI（零云端）。
// 双路径：rlAi（一键智能增强：recommend()）+ rlRun（套用下拉预设）。
// before/after 预览 + 16 预设下拉 + 建议芯片 + 手动滑杆组 + 进度条/徽章 + 导出副本。
// P4-1 起：并入几何编辑（旋转90°/翻转/拉直/按比例裁剪）——先几何后修调的固定流水线。
#include <QWidget>
#include <QVector>
#include <QMap>
#include <QString>
#include <QRectF>
#include "../core/Library.h"
#include "../core/RetouchEngine.h"
#include "../core/LumenAgent.h"
#include "../core/RtHistory.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;
class QProgressBar;
class QTimer;
class QScrollArea;
class QToolButton;
class QGridLayout;
class QStackedLayout;
class QStackedWidget;
class MaskCanvas;
class CropCanvas;
class CompareWidget;
class EraseBridge;

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

    // P15：由首页/图库灯箱跳转而来——按 path 定位并载入照片（零云端）。
    void openPhoto(const QString& path);
    void qaShootA(const QString& png);

    // 几何变换纯函数（P4-1；供 UI 与冒烟直测）：
    // rot ∈ 0..3（顺时针 90° 步进）→ 拉直 angleDeg（-10..10，先于旋转）→ 翻转 → 居中裁剪 aspect(w/h，0=不裁)。
    // 顺序固定：拉直 → 翻转 → 90°旋转 → 裁剪（确定性流水线）。
    static QImage applyGeometry(const QImage& src, int rot, bool flipH, bool flipV,
                                double angleDeg, double aspect, const QRectF& cropNorm = QRectF());

    // P4-2 蒙版混合纯函数：out = base*(1-f) + toned*f，f = mask alpha/255（蒙版尺寸可异，自动缩放）。
    static QImage blendMask(const QImage& base, const QImage& toned, const QImage& mask);

private slots:
    void onPhotoChanged(int idx);
    void onSliderChanged();
    void onPresetChanged(int idx);
    void onAutoEnhance();   // rlAi
    void onAgentRun();      // P9 AI 修图智能体（Lumen × Ollama）
    void onApplyPreset();   // rlRun
    void onReset();
    void onExport();
    void doApply();        // 防抖执行修调（几何后图像）
    void refreshChips();

    // P16.3 右侧预设画廊（自美图迁移）
    void onCardClicked(int i);
    void refreshGallery();
    void setMode(int m);

    // P4-1 几何
    void onGeoRotate(bool left);
    void onGeoFlip(bool horizontal);
    void onGeoStraighten(int v);
    void onGeoCropApply();
    void onGeoReset();

    // P4-2 蒙版
    void onMaskToggle(bool on);
    void onMaskClear();
    void onMaskApply();

    // P4-3 裁剪 / 对比
    void onCropToggle(bool on);
    void onCompareToggle(bool on);

    // P4-4 AI 消除桥（表单壳）
    void onEraseClicked();
    void onEraseTaskUpdated(int id);

    // 方向 B：非破坏式撤销/重做栈
    void onUndo();
    void onRedo();

private:
    void buildSlider(const RtSlider& def, class QGridLayout* gl);
    void setParams(const RetouchParams& p);   // 同步滑杆 + 数值
    void syncSlidersFromCur();                // cur_ → 滑杆显示（blockSignals）
    void exitAgentMode();                     // 手动/预设操作即退出智能体模式
    void loadPhoto(int idx);
    void updateBadge();
    void rebuildGeo();                        // geoSrc_ → geoImg_（几何重算）+ before 刷新
    void clearCardSel();                      // 清除右侧预设卡片高亮
    // 方向 B：撤销/重做栈（RtHistory 状态机驱动，与 UI 无关）
    void commitEdit();                        // doApply 末尾提交当前快照（restoring_ 防重入）
    void resetHistory();                      // loadPhoto 建立基线
    RtEditState captureState() const;         // 抓取当前全部编辑状态
    void applyState(const RtEditState& s);    // 套用快照（undo/redo 路径）
    void updateUndoRedoButtons();             // 同步按钮可用态

    const QVector<Photo>* photos_ = nullptr;
    QString thumbsDir_;
    QVector<int>  usableIdx_;                 // 有 thumbFile 的照片下标
    QImage        geoSrc_;                    // 几何源（载入的缩略图，不被修改）
    QImage        geoImg_;                    // 几何变换后的当前图（修调 tone 的输入）
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
    // P9 AI 修图智能体（Lumen × Ollama；决策层 → retouchWithPlan 执行）
    QPushButton* agentBtn_   = nullptr;
    QLineEdit*   goalEdit_   = nullptr;      // 可选用户目标（自然语言）
    QMap<QString, double> agentPlan_;        // 生效中的 13 项计划
    bool         agentMode_ = false;         // true=doApply/export 走 retouchWithPlan
    QString      agentNote_;                 // 徽章展示（来源+意图+依据）
    QTimer*       applyTimer_ = nullptr;

    QMap<QString, double> cur_;              // 当前修调参数（归一量）
    QVector<QSlider*>     sliders_;
    QVector<QLabel*>      sliderVals_;
    QVector<const RtSlider*> sliderDefs_;
    QWidget*       chipRow_ = nullptr;
    QLabel*        chipTitle_ = nullptr;

    // P4-1 几何状态
    int         rot_ = 0;                    // 0..3 顺时针步进
    bool        flipH_ = false, flipV_ = false;
    double      angle_ = 0.0;                // 拉直角度（度）
    double      aspect_ = 0.0;               // 裁剪宽高比，0 = 自由（不裁）
    QPushButton* geoRotLBtn_ = nullptr;
    QPushButton* geoRotRBtn_ = nullptr;
    QPushButton* geoFlipHBtn_ = nullptr;
    QPushButton* geoFlipVBtn_ = nullptr;
    QPushButton* geoCropBtn_ = nullptr;
    QPushButton* geoResetBtn_ = nullptr;
    QSlider*     straighten_ = nullptr;
    QComboBox*   aspectCombo_ = nullptr;

    // P4-2 蒙版
    QStackedLayout* midStack_ = nullptr;     // 0=预览 1=蒙版画布 2=裁剪画布 3=对比视图
    MaskCanvas*   canvas_ = nullptr;
    QPushButton*  maskBtn_ = nullptr;
    QPushButton*  maskClearBtn_ = nullptr;
    QPushButton*  maskApplyBtn_ = nullptr;
    QSlider*      brushSlider_ = nullptr;
    bool          maskMode_ = false;
    QImage        localOut_;                 // 最近一次局部应用结果（预览展示）

    // P4-3 裁剪 / 对比
    CropCanvas*    cropCanvas_ = nullptr;
    CompareWidget* compareW_ = nullptr;
    QPushButton*   cropBtn_ = nullptr;
    QPushButton*   compareBtn_ = nullptr;
    bool           cropMode_ = false;
    bool           compareMode_ = false;
    QRectF         cropNorm_;                // 交互裁剪框（归一化 0..1；null=未裁）
    QImage         afterImg_;                // 最近修调结果（供对比视图）

    // P4-4 AI 消除桥（表单壳：蒙版→任务→结果回填，模型槽位空）
    EraseBridge*   eraseBridge_ = nullptr;
    QPushButton*   eraseBtn_ = nullptr;
    QLabel*        eraseSlotLbl_ = nullptr;

    // P16.3 右侧预设画廊（自美图 BeautyPage 迁移）
    QVector<RetouchPreset> presets_;
    QVector<QToolButton*> cards_;
    int           selCard_ = -1;
    QWidget*      galleryPanel_ = nullptr;

    // P26 三栏工作室：左模式轨 + 右栈
    QWidget*        toolrail_ = nullptr;
    QStackedWidget* rightStack_ = nullptr;
    QVector<QPushButton*> modeBtns_;
    QString         railCss_;
    QString         railOnCss_;

    // 方向 B：撤销/重做栈
    RtHistory   hist_;
    bool        restoring_ = false;
    QPushButton* undoBtn_ = nullptr;
    QPushButton* redoBtn_ = nullptr;
};
