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
class QWidget;
class GalleryPage;
class MapPage;
class CullPage;
class RetouchPage;
class PhotoLightbox;   // P15 沉浸式灯箱（MainWindow.cpp 文件内定义）
class QPushButton;

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

    // P15：沉浸式灯箱随窗口尺寸铺满
    void resizeEvent(QResizeEvent* e) override;

private:
    // P12「美术馆策展」：首页为艺术 hero 仪表盘（hero 渐变 + 功能入口 + 最近作品胶片带）
    QWidget* makeHome(const QString& thumbsDir) const;
    // 占位页带图标与用户向说明（不再引用内部 docs）；关于页显示真实版本信息
    QWidget* makePlaceholder(const QString& secName, const QString& iconKind,
                             const QString& desc) const;
    QWidget* makeAbout() const;

    void startImport();          // P8：选目录 → 后台扫描入库 → 汇总/重启

    // P16 隐私库：初始化密码 / 解锁 / 重新上锁（非破坏私密标记 + 密码门）
    void setupVaultUi();         // 顶栏锁按钮
    void onVaultButton();        // 点击行为随状态分流
    void refreshPrivateState();  // 解锁/上锁 → 图库/地图同步可见性

    // P13：全客户端呼吸感 + 导航激活态管理
    bool  uiAnimEnabled() const;            // LA_* 冒烟/截图/系统减弱动效 → 关动画
    void  setupBreathing();                 // 呼吸定时器与受控元素
    void  tickBreath();                     // 正弦驱动 hero 光晕/seam/激活条/扩展目标
    void  updateNavActive(int row);         // 高亮当前导航项 + 激活呼吸条
    // P14：统一呼吸目标注册（功能卡片/画廊光晕等带相位偏移，错落呼吸）
    void  registerBreath(QGraphicsOpacityEffect* eff, double base, double amp, double phase = 0.0);

    Library*        lib_;
    QString         dbPath_;
    QString         thumbsDir_;     // P15：灯箱大图源（构造时保存，供 openLightbox 复用）
    QListWidget*    nav_ = nullptr;
    QLineEdit*      search_ = nullptr;
    QLabel*         title_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QWidget*        kpis_ = nullptr;
    QLabel*         moduleLbl_ = nullptr;   // P15.2：顶栏模块名称（随导航切换）

    // P13 呼吸感受控元素
    QFrame*                  seam_ = nullptr;       // 顶栏发丝线
    QGraphicsOpacityEffect*  seamEff_ = nullptr;
    QLabel*                  heroGlow_ = nullptr;   // 首页 hero 径向光晕
    QTimer*                  breathTimer_ = nullptr;
    double                   breathPhase_ = 0;
    int                      activeNavRow_ = 0;
    QVector<QWidget*>        navCells_;             // 各导航自绘单元格
    QVector<QFrame*>         navBars_;              // 各导航激活金条
    bool                     animEnabled_ = true;

    // P14：扩展呼吸目标（功能卡片光晕/画廊光晕），与 heroGlow/seam/激活条共享同一节拍
    struct BreathTarget { QGraphicsOpacityEffect* eff = nullptr; double base = 0.5; double amp = 0.2; double phase = 0.0; };
    QVector<BreathTarget>    breathTargets_;

    GalleryPage*    gallery_ = nullptr;
    MapPage*        map_ = nullptr;
    CullPage*       cull_ = nullptr;
    RetouchPage*     retouch_ = nullptr;

    PhotoLightbox*  lightbox_ = nullptr;   // P15 沉浸式灯箱（无边框全窗口覆盖）

    // P16 隐私库
    QPushButton*    vaultBtn_ = nullptr;   // 顶栏锁按钮
    bool            vaultUnlocked_ = false; // 会话内解锁状态（进程退出即回锁）
};
