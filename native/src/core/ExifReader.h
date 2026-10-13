#pragma once
// P8-2 EXIF 解析：client/lightalbum/core/exif.py read_exif 的 C++ 忠实移植。
// 零第三方依赖：JPEG 走 APP1("Exif\0\0")，TIFF/DNG 整文件即 TIFF 头，
// PNG 走 eXIf chunk；WebP 走 RIFF EXIF chunk、HEIC 走 ISOBMFF meta/iinf/iloc
// （2.38.0 新增，容器→TIFF blob 一层）；RAW 仍不解析。任何一步失败返回兜底值，绝不抛异常。
//
// 数据诚实铁律：缺失 = ''/0/null，严禁编造默认值；拍摄时间缺失才用文件 mtime 兜底。
#include <QString>

struct ExifData {
    QString cam, lens, ap, sh, d, tm, dim;
    double focal = 0;
    int    iso  = 0;
    bool   hasGps = false;
    double la = 0, lo = 0;            // 仅 hasGps 时有效（round 6）
    bool   hasAlt = false;
    int    alt = 0;
    double evc = 0;
    QString wb, meter;
    int    orient = 0;
};

namespace ExifReader {

ExifData readExif(const QString& path);

// 格式化器独立导出（Scanner / 冒烟对拍共用；语义 = exif.py 的 _fmt_*）
QString fmtAperture(double f);
QString fmtShutter(double t);

} // namespace ExifReader
