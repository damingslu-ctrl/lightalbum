// 导入 iCloud 相册：候选根探测（实现）。见 ICloudSource.h 的实测背景。
#include "ICloudSource.h"
#include "PathGuess.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace ICloudSource {

namespace {

// 相对 PicturesLocation 的候选子路径。三条的顺序 = 置信度顺序：
//   1. iCloud Photos\Photos    —— 「优化存储」关闭时的经典落盘（本机实测命中）
//   2. iCloud Photos           —— 部分版本少一层 Photos
//   3. iCloud Drive\Photos     —— 旧版 iCloud for Windows
QStringList kRelCandidates() {
    return { QStringLiteral("iCloud Photos/Photos"),
             QStringLiteral("iCloud Photos"),
             QStringLiteral("iCloud Drive/Photos") };
}

// 目录里是否至少有一个「像媒体」的文件（照片或视频）。
// 只数一层、不递归：iCloud 平铺落盘，一层就是全部；子目录（如 afcatalog）不算。
bool hasMediaFile(const QString& dir)
{
    QDir d(dir);
    const QFileInfoList fs =
        d.entryInfoList(QDir::Files | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name);
    int n = 0;
    for (const QFileInfo& fi : fs) {
        if (PathGuess::isPhoto(fi.fileName())) return true;
        const QString ext = fi.suffix().toLower();
        if (ext == QLatin1String("mp4") || ext == QLatin1String("mov")
            || ext == QLatin1String("hevc") || ext == QLatin1String("m4v"))
            return true;
        if (++n >= 200) break;                  // 200 个非媒体文件即判空壳，止损
    }
    return false;
}

} // namespace

QStringList candidateRoots(const QString& picturesDir)
{
    const QString base = !picturesDir.isEmpty()
        ? picturesDir
        : QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    QStringList out;
    if (base.isEmpty()) return out;
    for (const QString& rel : kRelCandidates()) {
        const QString p = QDir(base).filePath(rel);
        if (!out.contains(p, Qt::CaseInsensitive)) out << p;
    }
    return out;
}

QString detectRoot(const QString& picturesDir, int* realFileCount)
{
    for (const QString& cand : candidateRoots(picturesDir)) {
        QDir d(cand);
        if (!d.exists()) continue;
        if (realFileCount) {
            *realFileCount = static_cast<int>(
                d.entryInfoList(QDir::Files | QDir::NoDotAndDotDot | QDir::NoSymLinks,
                                QDir::Name).size());
        }
        // 空壳目录（只有 desktop.ini / 0 个媒体）不算命中——避免虚假「已找到」
        if (hasMediaFile(cand)) return QDir(cand).absolutePath();
    }
    if (realFileCount) *realFileCount = 0;
    return QString();
}

bool isUnderICloudRoot(const QString& path, const QString& picturesDir)
{
    // 逐级上剥目录链，命中任一候选根即真。归一化用反斜杠（Windows 落盘形态）。
    const QStringList roots = candidateRoots(picturesDir);
    QString p = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    while (p.size() > 3) {                      // >3 保住 "C:\" 根不被剥成 "C:"
        for (const QString& cand : roots) {
            if (p.compare(QDir::toNativeSeparators(QDir(cand).absolutePath()),
                          Qt::CaseInsensitive) == 0)
                return true;
        }
        const int cut = p.lastIndexOf(QLatin1Char('\\'));
        if (cut <= 2) break;
        p = p.left(cut);
    }
    return false;
}

QString displayName()
{
    return QStringLiteral("iCloud 相册");
}

} // namespace ICloudSource
