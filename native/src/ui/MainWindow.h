#pragma once
// P1 主窗口：左侧图标栏（8 区段）+ 顶栏（标题+搜索）+ QStackedWidget 页面栈。
// 架构决策（与 web 版唯一有意差异）：web 是单页长滚动 + 滚动高亮；原生用页面栈——
// 每页独立，未激活页面不参与绘制（性能上严格优于长滚动）。
#include <QMainWindow>
#include <QGraphicsOpacityEffect>
#include <QTimer>
#include <QVector>
#include <QFrame>
#include "../core/Library.h"

class QStackedWidget;
class QLineEdit;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QWidget;
class QMenu;
class GalleryPage;
class MapPage;
class CullPage;
class RetouchPage;
class LogPage;
class MapView;         // A 现状墨韵·抛光 QA 探针
class HomePage;        // P17 沉浸式首页（Quiet Frame）
class PhotoLightbox;   // P15 沉浸式灯箱（MainWindow.cpp 文件内定义）
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
    void openLightbox(const QString& path);

    // P13：导航切换联动动效（淡出→换页→淡入+轻微上滑）
    void onNav(int row);

    // P29-H：QA 探针——首页（零 chrome）应返回 false（控制簇已隐藏）
    bool isWinCtlVisible() const;

    // P15：沉浸式灯箱随窗口尺寸铺满
    void resizeEvent(QResizeEvent* e) override;
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
    void changeEvent(QEvent* e) override;

private:
    // P12「美术馆策展」已由 P17「Quiet Frame」沉浸式首页取代（HomePage）
    // 占位页带图标与用户向说明（不再引用内部 docs）；关于页显示真实版本信息
    QWidget* makePlaceholder(const QString& secName, const QString& iconKind,
                             const QString& desc) const;
    QWidget* makeAbout() const;

    void startImport();          // P8：选目录 → 后台扫描入库 → 汇总/重启
    // 2.6.0 导入 iCloud 相册：顶栏导入钮改下拉菜单（目录 / iCloud 双入口）。
    // 写入路径唯一：两个入口都收敛到 runImportInto()，缩略图缓存键与增量规则同源。
    QMenu* buildImportMenu();                    // 导入下拉菜单（探测不到则禁用 iCloud 项）
    void  importICloud();                        // 探测根 → 确认 → runImportInto
    void  runImportInto(const QString& dir, const QString& title);  // 导入主体（原 startImport 抽出）

    // P16 隐私库：初始化密码 / 解锁 / 重新上锁（非破坏私密标记 + 密码门）
    void setupVaultUi();         // 顶栏锁按钮
    void onVaultButton();        // 点击行为随状态分流
    void refreshPrivateState();  // 解锁/上锁 → 图库/地图同步可见性

    // 方向 C·智能分类：探测 CLIP 服务 → 异步跑分类（QtConcurrent）+ 进度/取消 → 刷新重建 chips
    void runClassify();

    // 方向 C·人脸识别（方案A）：探测 face_server → 异步跑人脸检测（QtConcurrent）+ 进度/取消 → 刷新重建 chips
    void runPersonDetect();

    // 方向 C·人物自动聚类：探测 face_server → 异步抽 embedding（落库 face_emb）→ FaceCluster
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
    QLineEdit*      search_ = nullptr;
    QLabel*         title_ = nullptr;
    QWidget*        topbar_ = nullptr;      // P17：顶栏句柄（首页隐藏全局 chrome）
    // P29 无标题栏：窗口控制簇（min/max/close），独立于 topbar_，悬浮右上角。
    // P29-H：首页（零 chrome 纯沉浸）隐藏该簇；关闭改由 Alt+F4 或切到非首页页再点 ✕。
    QWidget*        winCtl_   = nullptr;
    QPushButton*    winMin_   = nullptr;
    QPushButton*    winMax_   = nullptr;
    QPushButton*    winClose_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QWidget*        kpis_ = nullptr;
    QLabel*         moduleLbl_ = nullptr;   // P15.2：顶栏模块名称（随导航切换）
    HomePage*       home_ = nullptr;        // P17：沉浸式首页

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
    QPushButton*             searchToggle_ = nullptr; // 搜索展开/收起图标钮
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
    CullPage*       cull_ = nullptr;
    RetouchPage*     retouch_ = nullptr;
    LogPage*        log_ = nullptr;

    PhotoLightbox*  lightbox_ = nullptr;   // P15 沉浸式灯箱（无边框全窗口覆盖）
    VideoLightbox*   videoLightbox_ = nullptr; // 2.30.0 视频灯箱（video 复用 openLightbox 入口）

    // P16 隐私库
    QPushButton*    vaultBtn_ = nullptr;   // 顶栏锁按钮
    bool            vaultUnlocked_ = false; // 会话内解锁状态（进程退出即回锁）
};
