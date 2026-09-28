#pragma once
// P8-2 路径推导：client/lightalbum/core/scanner.py 的 C++ 忠实移植。
// 只在 EXIF 缺失时兜底 —— GPS 优先，其次才是目录名/文件名。
// 别名表与网页版 REGIONS / PROVINCES / SERIES 键对齐；匹配语义（精确→子串、
// 最长别名优先、字典序即优先级）逐条对齐 Python，勿随意改顺序。
#include <QString>
#include <QVector>

struct PathInfo {
    QString t;    // 标题（title_from_name）
    QString s;    // 系列（SERIES 四键之一，可为空）
    QString d;    // 日期 YYYY-MM-DD（路径猜出，可空）
    QString r;    // region id（10 国键，可空）
    QString pv;   // province 规范英文键，可空
    QString ct;   // 城市（省目录下一层），可空
};

namespace PathGuess {

// 扫描时跳过的目录（小写名匹配）—— 与 Python SKIP_DIRS 一致
bool isSkipDir(const QString& name);

// 是否照片文件：IMG_EXT 扩展名 + 排除 *_retouched 修图派生
bool isPhoto(const QString& fileName);

// 综合推导（rel 必须是正斜杠相对路径）
PathInfo guessFromPath(const QString& rel);

} // namespace PathGuess
