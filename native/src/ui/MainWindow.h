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
class BeautyPage;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(const Library* lib, const QString& thumbsDir,
               const QString& dbPath = QString(), QWidget* parent = nullptr);

    // P11 UI 截图探针：逐导航页 setCurrentRow → grab() 存 PNG 到 dir；返回 0。
    int shootPages(const QString& dir);

    // P13：导航切换联动动效（淡出→换页→淡入+轻微上滑）
    void onNav(int row);

private:
    // P12「美术馆策展」：首页为艺术 hero 仪表盘（hero 渐变 + 功能入口 + 最近作品胶片带）
    QWidget* makeHome(const QString& thumbsDir) const;
    // 占位页带图标与用户向说明（不再引用内部 docs）；关于页显示真实版本信息
    QWidget* makePlaceholder(const QString& secName, const QString& iconKind,
                             const QString& desc) const;
    QWidget* makeAbout() const;

    void startImport();          // P8：选目录 → 后台扫描入库 → 汇总/重启

    // P13：全客户端呼吸感 + 导航激活态管理
    bool  uiAnimEnabled() const;            // LA_* 冒烟/截图/系统减弱动效 → 关动画
    void  setupBreathing();                 // 呼吸定时器与受控元素
    void  tickBreath();                     // 正弦驱动 hero 光晕/seam/激活条/扩展目标
    void  updateNavActive(int row);         // 高亮当前导航项 + 激活呼吸条
    // P14：统一呼吸目标注册（功能卡片/画廊光晕等带相位偏移，错落呼吸）
    void  registerBreath(QGraphicsOpacityEffect* eff, double base, double amp, double phase = 0.0);

    const Library* lib_;
    QString         dbPath_;
    QListWidget*    nav_ = nullptr;
    QLineEdit*      search_ = nullptr;
    QLabel*         title_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QWidget*        kpis_ = nullptr;

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
    BeautyPage*      beauty_ = nullptr;
};
