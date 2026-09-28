#pragma once
// P1 数据层：直读 library.db，exif_json 经 QJsonDocument 解包。
// 数据诚实铁律（平移自 web 版 L 节）：缺失 = 0/空串/hasGps=false，严禁编造默认值。
// P16：小量写回（评分/私密标记/隐私库口令）走独立读写连接；大 SELECT 仍只读。
#include <QString>
#include <QVector>
#include <QByteArray>
#include <QSqlDatabase>

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
    bool setRating(const QString& path, int rating);     // 1..5（0=清除）
    bool setPrivate(const QString& path, bool priv);     // 隐私库标记
    // meta 表 KV（隐私库口令 salt/hash 存这里）
    QString metaValue(const QString& key) const;
    bool setMetaValue(const QString& key, const QString& value);

private:
    QSqlDatabase rw();                                   // 惰性读写连接「la_rw」
    QVector<Photo> photos_;
    Kpi kpi_;
    QString dbPath_;
};
