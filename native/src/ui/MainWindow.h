#pragma once
// P1 主窗口：左侧图标栏（8 区段）+ 顶栏（标题+搜索）+ QStackedWidget 页面栈。
// 架构决策（与 web 版唯一有意差异）：web 是单页长滚动 + 滚动高亮；原生用页面栈——
// 每页独立，未激活页面不参与绘制（性能上严格优于长滚动）。
#include <QMainWindow>
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

private:
    // P12「美术馆策展」：首页为艺术 hero 仪表盘（hero 渐变 + 功能入口 + 最近作品胶片带）
    QWidget* makeHome(const QString& thumbsDir) const;
    // 占位页带图标与用户向说明（不再引用内部 docs）；关于页显示真实版本信息
    QWidget* makePlaceholder(const QString& secName, const QString& iconKind,
                             const QString& desc) const;
    QWidget* makeAbout() const;

    void startImport();          // P8：选目录 → 后台扫描入库 → 汇总/重启

    const Library* lib_;
    QString         dbPath_;
    QListWidget*    nav_ = nullptr;
    QLineEdit*      search_ = nullptr;
    QLabel*         title_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QWidget*        kpis_ = nullptr;
    GalleryPage*    gallery_ = nullptr;
    MapPage*        map_ = nullptr;
    CullPage*       cull_ = nullptr;
    RetouchPage*     retouch_ = nullptr;
    BeautyPage*      beauty_ = nullptr;
};
