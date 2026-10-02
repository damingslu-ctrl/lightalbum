#pragma once
// 导入 iCloud 相册：iCloud 本地落盘根目录的探测与归一。
//
// 背景（2026-10-03 实测，本机 C:\Users\damin\Pictures\iCloud Photos）：
//   iCloud Photos 在 Windows 上**平铺**落盘，不按相册分目录 —— 实测 6778 个文件
//   里只有 1 个子目录（untitled.afcatalog），其余全是散落在 Photos\ 根下的
//   IMG_xxxx.JPG / UUID.HEIC / DSCxxxx.ARW / <uuid>.mp4。
//   所以「导入 iCloud 相册」= 定位到那个根目录后走既有 Scanner::syncRoot 全量通道，
//   **不新建任何写入路径**（铁律：缓存/写入入口必须唯一）。
//
// 探测为什么要多候选而不是写死一条：
//   iCloud for Windows 的落盘位置随版本/设置/是否启用「优化存储」而变，
//   常见三种（QStandardPaths::PicturesLocation 之下 / OneDrive 重定向 / 旧版
//   iCloud Drive）。写死一条会在别的机器上静默「导入 0 张」。
//   detectRoot() 返回**第一个真实存在**的候选；都不存在时返回空串，
//   调用方据此提示用户手动选目录，而不是假装成功。
#include <QString>
#include <QStringList>

namespace ICloudSource {

// 候选根目录（按优先级排列）。可注入 picturesDir 供 QA 沙箱断言，
// 传空串则用 QStandardPaths::PicturesLocation()。
QStringList candidateRoots(const QString& picturesDir = QString());

// 第一个真实存在的候选根；都不存在返回空串。
// realFileCount > 0 时额外要求该目录里至少有一个照片/视频文件
// （避免命中空壳目录就宣称「已找到 iCloud」）。
QString detectRoot(const QString& picturesDir = QString(),
                   int* realFileCount = nullptr);

// 路径是否位于 iCloud 落盘根之下（大小写不敏感、容忍 / 与 \ 混用）。
// 供 UI 决定是否显示「iCloud 模式」提示条。
bool isUnderICloudRoot(const QString& path, const QString& picturesDir = QString());

// 展示用短名：iCloud 相册
QString displayName();

} // namespace ICloudSource
