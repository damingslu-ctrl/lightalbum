#pragma once
// P8-3 原生扫描/导入器：client/lightalbum/core/{scanner,library}.py sync_root 的 C++ 移植。
// 目的：补齐 P7 审计的唯一硬缺口 —— native Library 只读、依赖 Python 预建 library.db。
// 移植要点：
//   1. 增量规则：size 与 mtime 都没变就完全跳过（不读 EXIF、不做缩略图）。
//   2. 消失文件标 missing=1 不删行（移动硬盘没挂上不至于清空库）。
//   3. 缩略图缓存键 = sha1(原生反斜杠绝对路径 | mtime整数 | size)，与 Python
//      thumbs._key 逐字节兼容 → 双端共享同一 thumbs 目录，互相命中。
//   4. exif_json 24 字段契约与 Python process_one 完全一致（缺 GPS → JSON null）。
#include <QString>
#include <QStringList>
#include <functional>

struct ScanResult {
    int added = 0;
    int updated = 0;
    int unchanged = 0;
    int missing = 0;
    int placeholders = 0;
    int total = 0;
    double seconds = 0;
    QStringList errors;      // 前 5 条
    int errorCount = 0;
};

class Scanner {
public:
    // progress(done, total) 每 50 张回调一次（在调用线程上回调）。
    // cancelled() 返回 true 时中止（部分结果照常落库）。UI 传入自己的检查器。
    // dbPath 独立传参（ smoke/测试可指到临时库）；正式调用传 <data_home>/library.db。
    static ScanResult syncRoot(const QString& root, const QString& dbPath,
                               const QString& thumbsDir,
                               const std::function<void(int, int)>& progress = {},
                               const std::function<bool()>& cancelled = {});

    // 建库/补列（幂等）：新库建全表，旧库补 category/rating/person 等列。
    static bool ensureSchema(const QString& dbPath, QString* err = nullptr);
};
