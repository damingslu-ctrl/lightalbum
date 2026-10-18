#pragma once
// 2.41.0 spec §1.2 骨架 / §2.5 凝视全局灯箱（GazeLightbox）。
//
// T6 零行为迁出：本类原为 MainWindow.cpp 内的文件内类 PhotoLightbox（P15 起），
// 原样搬运、只改文件名与类名。影院黑幕 / 原图异步淡入 / 星级写库 / 私密切换 /
// 作用域翻图 / 键盘流全部逐字保留。
#include <QWidget>
#include <QPixmap>
#include <QColor>
#include <QImage>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QHash>
#include "Theme.h"
#include "../core/Library.h"
#include "../core/BrowseScope.h"

class QLabel;
class QFrame;
class QToolButton;
class QGraphicsOpacityEffect;
class QVariantAnimation;
class QTimer;
class QLayout;
class Library;
class FullCache;
namespace Motion { class Driver; }

// ============================================================================
// P15 图片灯箱（沉浸式，无边框）：直接铺满显示大图，EXIF 等信息在图片正下方。
// 作为 MainWindow 的子控件覆盖全窗口，不再弹带标题栏的对话框「框」。
// P16：可点星级（写库）+ 私密切换（native vault 非破坏标记）。
// 2.41.0 §2.5：加 Gaze 档 —— 凝视模式（主色环境光晕 + 换展动画 + S/A/L 即写标）。
// ============================================================================
class GazeLightbox : public QWidget {
    Q_OBJECT
public:
    // 2.41.0 §2.5：Browse = 现行键盘流；Gaze = 凝视模式（环境光晕/换展/S·A·L 标）。
    enum class Mode { Browse, Gaze };

    // 2.22.0：原图缓存由外部注入（MainWindow 持有，见下方 openLightbox）。
    // 刻意**不**在灯箱内 new：本轮实测「灯箱内 new FullCache」会让
    // LA_MAP_CLICK_QA 退出期 0xC0000005（灯箱惰性创建 → 构造/析构时机与
    // QObject 父子销毁序冲突），改为外部持有后该门稳定 rc=0。
    void setFullCache(FullCache* fc);
    FullCache* fullCache() const;

    explicit GazeLightbox(Library* lib, QWidget* parent = nullptr);

    void setPhoto(const Photo* ph, const QString& thumbsDir);

    // 2.41.0 §2.5：入馆（Browse 保零行为 / Gaze 上光晕与凝视键位）。
    void enter(Mode m, const QString& startPath);

    // P16：灯箱内改动过评分/私密 → 关闭后由 MainWindow 刷新图库
    bool hasChanges() const;
    void clearChanges();

    // 2.33.0 翻图作用域：空 = 全库顺序；非空 = 调用方（图库页）当前的可见集合。
    //   此前 ←/→ 恒按全库走，用户在三张图的故事/人物/分类筛选里按右箭头，会直接
    //   翻到筛选结果之外的照片上 —— 「看得见什么就走什么」是筛选的基本语义。
    void setScope(const QStringList& paths);
    // P16 隐私门：由 MainWindow 依 vaultUnlocked_ 下发（锁定态翻图须跳过私密照）
    void setHidePrivate(bool hide);

    // F3 a11y：←/→ 翻图（端点不回绕）。
    // 2.33.0 两条补漏：① 沿 setScope 给的作用域翻，不再越出用户正在看的集合；
    // ② 锁定态跳过私密照 —— 此前从一张公开照片连按 → 就能步进私密区
    //   （灯箱只在全库序列上走，从不看 vaultUnlocked_）。
    void navigate(int delta);

    // 2.41.0 §2.5 主色纯函数（铁律 20：产品环境光晕与 LA_GAZE_QA 直调同一实现）。
    //   缩略图缩到 64px → 中心 1/2 半径内全采 + 外圈每 2px 采 → RGB 转 HSL 取
    //   饱和度 >0.25 的样本中位数；无达标样本 → 回退 emerald 亮度 -10%。
    //   全程只读该 QImage：不落库、不写设置、无副作用。
    static QColor dominantColor(const QImage& thumb);

signals:
    void openInRetouch(const QString& path);
    // 2.41.0 §2.5：凝视键位 S/A/L 即发（MainWindow 端落 Library::setMark，单张 len=1）。
    //   空 mark = 撤销（同键再按 / toast 期内反向）。
    void markRequested(const QString& path, const QString& mark);
    // 星级观测信号（T7 仍直写库，信号供 T8 改读型刷新）。
    void ratingChanged(const QString& path, int rating);
    // dirty = 本次会话内改过评分/私密（MainWindow 据此决定是否刷新图库）。
    void dismissed(bool dirty);

protected:
    void showEvent(QShowEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    bool eventFilter(QObject* obj, QEvent* ev) override;
    void closeEvent(QCloseEvent* e) override;

private:
    static void clearLayout(QLayout* lay);
    // P16：星级按钮着色（r 颗金星，其余灰）
    void setStars(int r);
    // P20：原图解完后淡入替换，避免"啪"地闪一下（opacity 渐入 120ms）。
    void fadeSwapIn();
    // —— QA 钩子（2.22.0）——
    // 当前是否已换成原图（门禁判别器：原图分辨率必远大于 512px 缩略图）
    bool qaHasFull() const;
    // 当前显示图的边长（供门禁与缩略图档位对比）
    QSize qaDisplaySize() const;
    FullCache* qaFullCache() const;
    // P20：按当前窗口尺寸缩放原图（留 44px 呼吸边）；无图时占位文案
    void layoutImage();

    // —— 2.41.0 §2.5 凝视模式私有面 ——
    // 环境光晕：把窗口自身的影院黑幕换成「主色四缘淡晕」（Lumen::glowCss）。
    void applyGlow(const QColor& c);
    // 换展动画：旧画淡出 120ms（const）+ 新画 Motion::Driver(kLightbox) 入画。
    // ≤2 并发：起新前若已有 2 个活跃 Driver，先停最旧（删 Driver = 停其子动画）。
    void startExhibitAnim();
    // 凝视键位 S/A/L：同键在 toast 期内再按 → 撤销（发空 mark）。
    void handleGazeMark(const QString& mark);
    // 底部 toast（3s 自动收起；收起时清掉撤销窗口）。
    void showToast(const QString& text);

    Library* lib_ = nullptr;
    QLabel* img_  = nullptr;
    QPixmap orig_;
    // 2.22.0：当前显示的是原图还是缩略图（供 QA 断言"确实换成了原图"）。
    // 判别器：原图分辨率必然远大于 512px 缩略图，故 hasFull_ 可由尺寸独立复核。
    bool hasFull_ = false;
    FullCache* full_ = nullptr;     // 原图字节预算 LRU（后台解码）
    QFrame* bottom_ = nullptr;                      // P20：影院黑幕信息层
    QGraphicsOpacityEffect* bottomEff_ = nullptr;
    QVariantAnimation* fade_ = nullptr;
    QLabel* title_ = nullptr;
    QLabel* meta_ = nullptr;
    QWidget* starRow_ = nullptr;
    QWidget* ctl_ = nullptr;
    QString  path_;
    QString  thumbsDir_;   // F3 a11y：翻图复用
    QVector<BrowseSlot>  slots_;      // 2.33.0 翻图作用域槽（空 scope = 全库顺序）
    QHash<QString,int>   slotPos_;    // path → 槽序号（O(1)，禁线性扫）
    bool               hidePrivate_ = true;  // P16 隐私门（默认锁，与 GalleryModel 同口径）
    int      curRating_ = 0;
    bool     changed_ = false;
    QVector<QToolButton*> starBtns_;

    // —— 2.41.0 §2.5 Gaze 档状态 ——
    Mode     mode_ = Mode::Browse;
    QLabel*  toast_ = nullptr;
    QTimer*  toastTimer_ = nullptr;
    QString  pendingMark_;        // 撤销窗口内的「已发过的 mark」
    QString  pendingPath_;        // 撤销窗口内的目标照片
    QVector<Motion::Driver*> drivers_;   // 活跃换展 Driver（≤2）
};
