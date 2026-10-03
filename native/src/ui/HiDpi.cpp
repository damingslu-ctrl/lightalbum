#include "HiDpi.h"
#include <QGuiApplication>
#include <QScreen>
#include <QCoreApplication>

double HiDpi::dpr()
{
    // LA_HIDPI_DPR 覆盖：门禁/Headless 用，模拟指定 DPI 显示器（与 offscreen 平台
    // devicePixelRatio 不稳定解耦，使高 DPI 门禁可复现）。
    if (qEnvironmentVariableIsSet("LA_HIDPI_DPR"))
        return qEnvironmentVariable("LA_HIDPI_DPR").toDouble();
    if (QGuiApplication* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
        if (QScreen* s = app->primaryScreen())
            return s->devicePixelRatio(); // 含 QT_SCALE_FACTOR 全局缩放
    }
    return 1.0;
}

QImage HiDpi::bakeImage(const QSize& logical, QImage::Format fmt)
{
    if (qEnvironmentVariableIsSet("LA_HIDPI_QA_FORCE_LQ")) {
        // 反例分支：故意返回逻辑尺寸（不乘 dpr），门禁应判红。
        return QImage(logical, fmt);
    }
    const double d = dpr();
    const QSize dev(int(logical.width() * d + 0.5),
                    int(logical.height() * d + 0.5));
    QImage img(dev, fmt);
    img.setDevicePixelRatio(d);
    return img;
}

QPixmap HiDpi::bakePixmap(const QSize& logical)
{
    if (qEnvironmentVariableIsSet("LA_HIDPI_QA_FORCE_LQ")) {
        // 反例分支：故意返回逻辑尺寸（不乘 dpr），门禁应判红。
        return QPixmap(logical);
    }
    const double d = dpr();
    const QSize dev(int(logical.width() * d + 0.5),
                    int(logical.height() * d + 0.5));
    QPixmap pm(dev);
    pm.setDevicePixelRatio(d);
    return pm;
}
