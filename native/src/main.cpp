// LightAlbum Native — P0 骨架入口
// 决策依据：docs/adr/ADR-0001-full-cpp-qt-widgets-rewrite.md
// 分期计划：docs/REWRITE-PLAN.md
// 窗口基准：与现客户端一致 1440x940（对应 web 版 >768px 桌面档布局）
#include <QApplication>
#include <QMainWindow>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("LightAlbum"));
    app.setOrganizationName(QStringLiteral("lightalbum"));

    QMainWindow win;
    win.setWindowTitle(QStringLiteral("光影集 LightAlbum (native)"));
    win.resize(1440, 940);
    win.show();
    return app.exec();
}
