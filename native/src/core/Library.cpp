#include "Library.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <algorithm>

bool Library::open(const QString& dbPath, QString* err)
{
    // 只读打开；连接名固定（单实例内单库）
    if (QSqlDatabase::contains("la_lib")) {
        QSqlDatabase::removeDatabase("la_lib");
    }
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("la_lib"));
    db.setDatabaseName(dbPath);
    db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
    if (!db.open()) {
        if (err) *err = db.lastError().text();
        return false;
    }

    QSqlQuery q(db);
    q.setForwardOnly(true);
    if (!q.exec(QStringLiteral(
            "SELECT path, rel, thumb, missing, exif_json FROM photos"))) {
        if (err) *err = q.lastError().text();
        return false;
    }

    QSet<int> years;
    QSet<QString> provs;
    photos_.reserve(7000);
    while (q.next()) {
        Photo p;
        p.path      = q.value(0).toString();
        p.rel       = q.value(1).toString();
        p.thumbFile = q.value(2).toString();
        p.missing   = q.value(3).toInt() != 0;
        const QString exif = q.value(4).toString();

        if (!exif.isEmpty()) {
            const QJsonObject o = QJsonDocument::fromJson(exif.toUtf8()).object();
            p.title  = o.value(QLatin1String("t")).toString();
            p.date   = o.value(QLatin1String("d")).toString();
            p.time   = o.value(QLatin1String("tm")).toString();
            p.cam    = o.value(QLatin1String("cam")).toString();
            p.lens   = o.value(QLatin1String("lens")).toString();
            p.ap     = o.value(QLatin1String("ap")).toString();
            p.sh     = o.value(QLatin1String("sh")).toString();
            p.dim    = o.value(QLatin1String("dim")).toString();
            p.orient = o.value(QLatin1String("o")).toString();
            p.region = o.value(QLatin1String("r")).toString();
            p.prov   = o.value(QLatin1String("pv")).toString();
            p.city   = o.value(QLatin1String("ct")).toString();
            p.year   = o.value(QLatin1String("y")).toInt();
            p.iso    = o.value(QLatin1String("iso")).toInt();
            p.rating = o.value(QLatin1String("rating")).toInt();
            p.focal  = o.value(QLatin1String("focal")).toDouble();
            // GPS 诚实判定：键存在且为数字才算有（JSON null / 缺键 → 无 GPS）
            const QJsonValue lo = o.value(QLatin1String("lo"));
            const QJsonValue la = o.value(QLatin1String("la"));
            p.hasGps = lo.isDouble() && la.isDouble();
        }
        if (p.title.isEmpty()) p.title = p.rel;   // 兜底展示名（非编造 EXIF）
        if (p.year > 0) years.insert(p.year);
        if (!p.prov.isEmpty()) provs.insert(p.prov);
        photos_.push_back(std::move(p));
    }

    // 日期在 exif_json 内（非表列）：载入后按 ISO 日期串倒序（字典序=时间序）
    std::sort(photos_.begin(), photos_.end(), [](const Photo& a, const Photo& b) {
        return a.date > b.date;
    });

    kpi_.total = int(photos_.size());
    kpi_.yearCount = years.size();
    kpi_.provCount = provs.size();
    if (!years.isEmpty()) {
        kpi_.minYear = *std::min_element(years.cbegin(), years.cend());
        kpi_.maxYear = *std::max_element(years.cbegin(), years.cend());
    }
    return true;
}
