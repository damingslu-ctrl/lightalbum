#pragma once
// P1 主窗口：左侧图标栏（7 区段）+ 顶栏（标题+⌘K+四灯）+ QStackedWidget 页面栈。
// 架构决策（与 web 版唯一有意差异）：web 是单页长滚动 + 滚动高亮；原生用页面栈——
// 每页独立，未激活页面不参与绘制（性能上严格优于长滚动）。
// 2.41.0 spec §2.6：前厅/人物馆接管 0/3 两行；灯色是「整帧语言」→ rethemeAll 整帧重建。
#include <QMainWindow>
#include <QGraphicsOpacityEffect>
#include <QTimer>
#include <QVector>
#include <QFrame>
#include "../core/Library.h"
#include "Theme.h"

class QStackedWidget;
class QLineEdit;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QWidget;
class QMenu;
class QToolButton;
class GalleryPage;
class MapPage;
class LobbyPage;       // 2.41.0 spec §2.2：前厅（接替已退役的 HomePage 导航位）
class PeoplePage;      // 2.41.0 spec §2.3：人物馆（接替已退役的 CullPage 导航位）
class CmdKPanel;       // 2.41.0 spec §2.4：⌘K 检索面板
class RetouchPage;
class LogPage;
class MapView;         // A 现状墨韵·抛光 QA 探针
class GazeLightbox;   // P15 沉浸式灯箱（2.41.0 T6 独立成 src/ui/GazeLightbox.{h,cpp}）
class VideoLightbox;    // 2.30.0 视频灯箱（内嵌解码播放器）
class QPushButton;
class QVariantAnimation;
class FullCache;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(Library* lib, const QString& thumbsDir,
               const QString& dbPath = QString(), QWidget* parent = nullptr);

    // P11 UI 截图探针：逐导航页 setCurrentRow → grab() 存 PNG 到 dir；返回 0。
    int shootPages(const QString& dir);

    // P15：打开沉浸式灯箱（图库点击 / 探针复用）
    // 2.33.0：scope = 打开方的可见集合（图库页有筛选时传入），灯箱 ←/→ 只在其中走；
    //   空 = 全库顺序。默认参数保持既有调用点（首页/地图/探针）语义不变。
    void openLightbox(const QString& path, const QStringList& scope = {});

    // 2.41.0 spec §2.5：凝视档入口（前厅今日/人物馆展室/⌘K 检索结果都走这里）。
    // gaze=false 等价 openLightbox；true 时 Lightbox 进 Mode::Gaze（光晕 + S/A/L 标）。
    void openGaze(const QString& path, const QStringList& scope);

    // P13：导航切换联动动效（淡出→换页→淡入+轻微上滑）
    void onNav(int row);

    // P29-H：QA 探针——前厅（零 chrome）应返回 false（控制簇已隐藏）
    bool isWinCtlVisible() const;

    // 2.41.0 门禁探针：⌘K 面板 / 四灯 / 整帧切灯（LA_LAMP_QA·LA_CMDK_QA 用）
    CmdKPanel* qaCmdK() const;
    int qaLampMenuActions();                 // 灯菜单项数（4 灯 + 分隔 + 跟随 = 断言骨架）
    void qaRetheme();                        // 直驱整帧重建，返回后组件树应完好

    // P15：沉浸式灯箱随窗口尺寸铺满
    void resizeEvent(QResizeEvent* e) override;
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
    void changeEvent(QEvent* e) override;

private:
    // 2.41.0 spec §2.6：整窗 UI 建在一帧里——构造只调一次，切灯时 rethemeAll 再调。
    // 之所以要整帧重建：灯色在构造期就烘进各控件的 QSS（不是运行期可改的属性）。
    void buildUi();
    // 切灯唯一路径。row = 重建后要落到的导航行；-1 = 保持当前行。
    // 之所以要显式传目标行：onNav 的跟随分支是在「还没落栈」时调进来的，
    // 若只按当前栈位恢复，导航会被弹回旧页（LA_UI_SHOT 实测：切一次灯就再也出不去前厅）。
    void rethemeAll(int row = -1);
    void applyRowChrome(int row);          // onNav 与 rethemeAll 共用的本帧 chrome 归位
    Lumen::Hall currentHall() const;         // 导航行 → 空间（lampFollow 时决定灯色）
    void restyleNavIcons(int row);           // 激活态图标按当前灯金/墨重着色

    // 2.41.0 spec §2.4：⌘K 开合（面板是 central 的子控件 → 随整帧重建换新灯）
    void openCmdK();
    void closeCmdK();
    QMenu* buildLampMenu();                // 2.41.0 §2.6：4 灯互斥 + 「跟随空间」勾选锁

    // 2.41.0 spec §2.6：库写回后的统一刷新汇合（图库/前厅/人物馆三处同一真源）
    void refreshExhibits();

    // 2.41.0 spec §2.5：灯箱惰性建 + 一次性接线。Browse 与 Gaze 共用同一实例，
    // 故接线只此一份；两档各自 enter() 归位档位。
    GazeLightbox* ensureLightbox();

    // 占位页带图标与用户向说明（不再引用内部 docs）；关于页显示真实版本信息
    QWidget* makePlaceholder(const QString& secName, const QString& iconKind,
                             const QString& desc) const;
    QWidget* makeAbout() const;

    void startImport();          // P8：选目录 → 后台扫描入库 → 汇总/重启
    // 2.6.0 导入 iCloud 相册：顶栏导入钮改下拉菜单（目录 / iCloud 双入口）。
    // 写入路径唯一：两个入口都收敛到 runImportInto()，缩略图缓存键与增量规则同源。
    QMenu* buildImportMenu(QWidget* owner);        // 导入下拉菜单（探测不到则禁用 iCloud 项）
    void  importICloud();                        // 探测根 → 确认 → runImportInto
    void  runImportInto(const QString& dir, const QString& title);  // 导入主体（原 startImport 抽出）

    // P16 隐私库：解锁 / 重新上锁（非破坏私密标记 + 密码门）
    void onVaultButton();        // 点击行为随状态分流
    void refreshPrivateState();  // 解锁/上锁 → 图库/地图同步可见性

    // 方向 C·智能分类：探测 CLIP 服务 → 异步跑分类（QtConcurrent）+ 进度/取消 → 刷新重建 chips
    void runClassify();

    // 方向 C·人脸识别（方案A）：worker 线程探测 faceinfer 引擎 + 异步跑人脸检测（QtConcurrent）+ 进度/取消 → 刷新重建 chips
    void runPersonDetect();

    // 方向 C·人物自动聚类：worker 线程探测引擎 → 异步抽 embedding（落库 face_emb）→ FaceCluster
    // 阈值余弦聚类 → applyClusters 写回 person → 刷新重建人物 chips。
    void runFaceCluster();

    // P13：全客户端呼吸感 + 导航激活态管理
    bool  uiAnimEnabled() const;            // LA_* 冒烟/截图/系统减弱动效 → 关动画
    void  setupBreathing();                 // 呼吸定时器与受控元素
    void  tickBreath();                     // 正弦驱动 hero 光晕/seam/激活条/扩展目标
    void  updateNavActive(int row);         // 高亮当前导航项 + 激活呼吸条
    bool  eventFilter(QObject* obj, QEvent* ev) override;

    // P19 标题栏 E「沉浸渐隐栏」：顶栏悬浮透明，滚动/入栏时底色渐显
    void  syncTopbarFloat();                // 顶栏/seam 几何 = 侧栏右缘 → 内容区右缘
    void  updateTopSolid();                 // 依当前页滚动偏移 + hover 计算目标态
    void  applyTopSolid(double t);          // 进度 → 底色层透明度 + seam 显隐
public:
    void  setTopSolid(bool on);             // QA 门控 LA_UI_SHOT_SOLID 直驱
    void  forceTopSolid(bool on);           // QA 强制态（跳过动画与滚动状态机）
    MapView* mapView() const;              // A 现状墨韵·抛光 QA 探针
private:
    bool                     topForced_ = false;    // P19：QA 强制实底标志
    void  showEvent(QShowEvent* ev) override;
    // P14：统一呼吸目标注册（功能卡片/画廊光晕等带相位偏移，错落呼吸）
    void  registerBreath(QGraphicsOpacityEffect* eff, double base, double amp, double phase = 0.0);

    Library*        lib_;
    QString         dbPath_;
    QString         thumbsDir_;     // P15：灯箱大图源（构造时保存，供 openLightbox 复用）
    QListWidget*    nav_ = nullptr;
    QLabel*         title_ = nullptr;
    QWidget*        topbar_ = nullptr;      // P17：顶栏句柄（前厅隐藏全局 chrome）
    // P29 无标题栏：窗口控制簇（min/max/close），独立于 topbar_，悬浮右上角。
    // P29-H：前厅（零 chrome 纯沉浸）隐藏该簇；关闭改由 Alt+F4 或切到非前厅页再点 ✕。
    QWidget*        winCtl_   = nullptr;
    QPushButton*    winMin_   = nullptr;
    QPushButton*    winMax_   = nullptr;
    QPushButton*    winClose_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QWidget*        kpis_ = nullptr;
    QLabel*         moduleLbl_ = nullptr;   // P15.2：顶栏模块名称（随导航切换）
    LobbyPage*      lobby_ = nullptr;       // 2.41.0：前厅（今日回看 + 近期入藏）

    // P13 呼吸感受控元素
    QFrame*                  seam_ = nullptr;       // 顶栏发丝线
    QGraphicsOpacityEffect*  seamEff_ = nullptr;
    QTimer*                  breathTimer_ = nullptr;
    double                   breathPhase_ = 0;
    int                      activeNavRow_ = 0;
    QWidget*                 navBox_ = nullptr;     // P24 A 抛光图标轨：字标+列表容器
    QFrame*                  navBar_ = nullptr;     // 激活金条（贴栏缘，呼吸驱动）
    QVector<QListWidgetItem*> navItems_;            // 各导航项（激活态图标重着色）
    // P19 沉浸渐隐栏
    QFrame*                  topShade_ = nullptr;   // 顶栏渐显底色层（QWidget 不画 QSS 背景，必须 QFrame）
    QPushButton*             searchToggle_ = nullptr; // 2.41.0：⌘K 入口钮（原搜索展开钮）
    QToolButton*             lampBtn_ = nullptr;     // 2.41.0：四灯切换（InstantPopup 菜单）
    QVariantAnimation*       topSolidAnim_ = nullptr;
    // 2.22.0：原图字节预算 LRU，由 MainWindow 持有并注入灯箱（灯箱不自有）
    FullCache*               fullCache_ = nullptr;
    double                   topSolidT_ = 0.0;      // 0 透明 / 1 实底
    bool                     topHover_ = false;
    bool                     topSolidOn_ = false;
    QWidget*                 aboutPg_ = nullptr;    // P19：关于页句柄（顶部让位用）
    bool                     animEnabled_ = true;

    // P14：扩展呼吸目标（seam 发丝线等），与激活条共享同一节拍
    struct BreathTarget { QGraphicsOpacityEffect* eff = nullptr; double base = 0.5; double amp = 0.2; double phase = 0.0; };
    QVector<BreathTarget>    breathTargets_;

    GalleryPage*    gallery_ = nullptr;
    MapPage*        map_ = nullptr;
    PeoplePage*     people_ = nullptr;
    RetouchPage*     retouch_ = nullptr;
    LogPage*        log_ = nullptr;
    CmdKPanel*      cmdk_ = nullptr;        // 2.41.0：⌘K 检索面板（随整帧重建）

    GazeLightbox*  lightbox_ = nullptr;   // P15 沉浸式灯箱（无边框全窗口覆盖）
    VideoLightbox*   videoLightbox_ = nullptr; // 2.30.0 视频灯箱（video 复用 openLightbox 入口）

    // P16 隐私库
    QPushButton*    vaultBtn_ = nullptr;   // 顶栏锁按钮
    bool            vaultUnlocked_ = false; // 会话内解锁状态（进程退出即回锁）
};
