#pragma once
// 统一原图加载器（2.29.0）—— 把「照片 / RAW / 视频」三条解码路径收口到**一个**函数。
//
// 存在理由（由实测驱动，非推测）：
//   `RetouchPage.cpp:1027` 原本用 `QImage(ph.path)` 直接读原图。实测：
//     QImage("sample1.dng") → **0x0 null=1**（QImageReader 打不开 RAW）
//     QImage("x.jpg")      → 1024x683 非空
//   即 **RAW 根本无法进入修图页**（2.28.0 只让 RAW 进了库与缩略图，忘了这条入口）。
//   本模块把已有的两条外部解码路径（RawDecode 出 PPM / VideoDecode 抽帧）接进来，
//   使修图与灯箱等所有「读原图」的地方只需调一个函数（铁律 6 唯一真源）。
//
// 设计约束（全部实测，勿凭直觉改）：
//   - 尺寸：RAW 走 dcraw_emu **半尺寸**（实测 1737x1157 / 609ms），非全尺寸。
//     这是刻意的取舍：全尺寸单张解码显著更慢更费内存，而调参预览不需要全尺寸。
//   - 格式：中间图 PPM（JPEG 亦可）均由 Qt **实读**通过；
//     本机**无 qtiff.dll**，故任何环节都不得依赖 TIFF 中转。
//   - 失败一律返回 **null QImage**，绝不返回占位图或编造尺寸（数据诚实铁律）。
#include <QImage>
#include <QString>

namespace ImageLoad {

// 加载「原图」用于编辑/预览：照片走 QImageReader，RAW 走 LibRaw，视频走抽帧封面。
// 返回 null QImage 表示失败（非 RAW 且 QImage 打不开、或解码器缺失/超时）。
QImage loadForEdit(const QString& path);

// 该文件是否需要外部解码器才能加载（供 UI 决定是否显示「解码中」提示）。
bool needsExternal(const QString& path);

// QA 钩子（2.29.0）：置 true 后 loadForEdit 对 RAW **回退到 QImage 直读**，
// 即精确复现 2.28.0 的缺陷形态（实测 QImage("x.dng") → 0x0 null=1）。
// 注入点位于「按类型分派」这一步之内（铁律 32）；期望值不随本开关改变（铁律 22）。
void setForceRawFallbackForQa(bool on);

} // namespace ImageLoad
