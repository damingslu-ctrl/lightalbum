#pragma once
// P1 数据层：直读 library.db（只读），exif_json 经 QJsonDocument 解包。
// 数据诚实铁律（平移自 web 版 L 节）：缺失 = 0/空串/hasGps=false，严禁编造默认值。
#include <QString>
#include <QVector>

struct Photo {
    QString path, rel, thumbFile;
    // exif_json 字段（与 web 版 PHOTOS 对象一一对应）
    QString title, date, time, cam, lens, ap, sh, dim, orient;  // orient: landscape/portrait
    QString region, prov, city;
    int    year = 0, iso = 0, rating = 0;
    double focal = 0;
    bool   hasGps = false;
    bool   missing = false;
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

private:
    QVector<Photo> photos_;
    Kpi kpi_;
};
