#pragma once
// P1 数据层：直读 library.db，exif_json 经 QJsonDocument 解包。
// 数据诚实铁律（平移自 web 版 L 节）：缺失 = 0/空串/hasGps=false，严禁编造默认值。
// P16：小量写回（评分/私密标记/隐私库口令）走独立读写连接；大 SELECT 仍只读。
#include <QString>
#include <QStringList>
#include <QVector>
#include <QVariant>
#include <QHash>
#include <QByteArray>
#include <QSqlDatabase>

// 方向 D 第五步：批量写回结果。
// 语义铁律：**原子** —— 任一条失败则整体回滚，done 归零、内存副本不动。
// failedIndex 给出首个失败下标（-1 = 无失败）；error 为 SQL 原文（便于门断言/排障）。
struct BatchResult {
    int     total = 0;        // 请求条数
    int     done = 0;// 实际命中行数（UPDATE 影响行数之和）
    int     failedIndex = -1; // 首个失败下标
    bool    ok = false;       // 事务是否提交成功
    QString error;      // ok=false 时的 SQL 错误原文
    bool isNoop() const { return total == 0; }
};

struct Photo {
    QString path, rel, thumbFile;
    // exif_json 字段（与 web 版 PHOTOS 对象一一对应）
    QString title, date, time, cam, lens, ap, sh, dim, orient;  // orient: landscape/portrait
    QString region, prov, city;
    int    year = 0, iso = 0, rating = 0;
    double focal = 0;
    bool   hasGps = false;
    double la = 0, lo = 0;    // GPS 十进制度（仅 hasGps 时有效；P10 逆地理用）
    bool   missing = false;
    // P16 旧版移植列（photos 表列，非 exif_json）
    QString category;         // CLIP 本地批量分类标签；空 = 未分类
    QString person;           // 人物（逗号分隔）；空 = 无
    bool   priv = false;      // 隐私库标记（native vault：密码门，非破坏）
};

struct Kpi {
    int total = 0;
    int yearCount = 0;
    int provCount = 0;
    int minYear = 0, maxYear = 0;
};

class Library {
public:
    bool open(const QString& dbPath, QString* err = nullptr);
    const QVector<Photo>& photos() const { return photos_; }
    const Kpi& kpi() const { return kpi_; }

    // ---- P16 写回 API（独立读写连接；同时同步内存副本）----
    // 方向 D5：单条 = 长度为 1 的批量（统一走唯一事务入口，禁旁路直写）
    bool setRating(const QString& path, int rating);     // 1..5（0=清除）
    bool setPrivate(const QString& path, bool priv);     // 隐私库标记

    // ---- 方向 D5：批量写回（单事务原子；任一失败整体回滚，DB 与内存副本同时不变）----
    BatchResult setRatings(const QStringList& paths, int rating);
    BatchResult setPrivates(const QStringList& paths, bool priv);
    // 索引自检（QA 门）：path <-> photos_ 下标双向一致，且大小 == photos_.size()
    bool verifyPathIndex() const;
    // meta 表 KV（隐私库口令 salt/hash 存这里）
    QString metaValue(const QString& key) const;
    bool setMetaValue(const QString& key, const QString& value);

private:
    enum class BatchField { Rating, Private };
    BatchResult applyBatch(BatchField field, const QStringList& paths, const QVariant& value);

    void rebuildPathIndex();                             // photos_ 变动后唯一重建入口
    Photo* findMutable(const QString& path);             // O(1) 走 pathIx_（禁线性扫描）

    QSqlDatabase rw();                                   // 惰性读写连接「la_rw」
    QVector<Photo> photos_;
    QHash<QString, int> pathIx_;                         // path -> photos_ 下标（D5：批量写回去 O(N²)）
    Kpi kpi_;
    QString dbPath_;
};
