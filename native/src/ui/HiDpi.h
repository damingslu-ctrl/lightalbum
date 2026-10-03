#pragma once
#include <QSize>
#include <QImage>
#include <QPixmap>

// F 高 DPR 渲染（方向 F 第一步）：高 DPI 烘焙唯一真源。
//
// 全应用此前从未调用 devicePixelRatio()（取证：src 内 0 处），所有自定义
// QImage/QPixmap 均以「逻辑像素」烘焙，在 125%/150%/200% 缩放下被操作系统
// 位图拉伸 → 模糊。本工具把所有「按 size() 烘焙位图」收敛到一处，统一按
// 设备像素比出图 + setDevicePixelRatio，绘制时仍用逻辑坐标 → 缩放后清晰。
//
// LA_HIDPI_QA_FORCE_LQ=1 为反例分支：故意返回逻辑尺寸（不乘 dpr），
// 用于证明 LA_HIDPI_QA 门禁非空心跳（反例必红）。
class HiDpi {
public:
    // 当前设备像素比：优先 QGuiApplication（已含 QT_SCALE_FACTOR 全局缩放），
    // 无 GUI 实例时回退 1。
    static double dpr();

    // 设备像素尺寸的 QImage（已 setDevicePixelRatio）。绘制用逻辑坐标即清晰。
    static QImage bakeImage(const QSize& logical,
                            QImage::Format fmt = QImage::Format_ARGB32_Premultiplied);

    // 同上，QPixmap 版（width() 返回逻辑尺寸，绘制用逻辑坐标）。
    static QPixmap bakePixmap(const QSize& logical);
};
