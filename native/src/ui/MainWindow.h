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
    MainWindow(const Library* lib, const QString& thumbsDir, QWidget* parent = nullptr);

private:
    QWidget* makePlaceholder(const QString& secName, const QString& phase) const;

    const Library* lib_;
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
