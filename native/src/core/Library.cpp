#include "Library.h"
#include "GeoIndex.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <algorithm>

bool Library::open(const QString& dbPath, QString* err)
{
    dbPath_ = dbPath;

    // ---- P16 列迁移：旧库可能缺 category/person/private 列（CREATE TABLE IF NOT EXISTS
    //      不会补列，同 Scanner EXTRA_COLUMNS 思路）。重复 ADD 报错无害，静默忽略。
    {
        QSqlDatabase mig = QSqlDatabase::addDatabase(
            QStringLiteral("QSQLITE"), QStringLiteral("la_mig"));
        mig.setDatabaseName(dbPath);
        if (mig.open()) {
            QSqlQuery m(mig);
            m.exec(QStringLiteral("ALTER TABLE photos ADD COLUMN category TEXT"));
            m.exec(QStringLiteral("ALTER TABLE photos ADD COLUMN person TEXT"));
            m.exec(QStringLiteral(
                "ALTER TABLE photos ADD COLUMN private INTEGER DEFAULT 0"));
            m.exec(QStringLiteral(
                "CREATE INDEX IF NOT EXISTS idx_photos_cat ON photos(category)"));
            mig.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("la_mig"));
    }

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
            "SELECT path, rel, thumb, missing, exif_json, "
            "category, person, private, rating FROM photos"))) {
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
        p.category  = q.value(5).toString();
        p.person    = q.value(6).toString();
        p.priv      = q.value(7).toInt() != 0;
        int rating  = q.value(8).toInt();     // 列 = 用户可编辑事实源

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
            p.focal  = o.value(QLatin1String("focal")).toDouble();
            // GPS 诚实判定：键存在且为数字才算有（JSON null / 缺键 → 无 GPS）
            const QJsonValue lo = o.value(QLatin1String("lo"));
            const QJsonValue la = o.value(QLatin1String("la"));
            p.hasGps = lo.isDouble() && la.isDouble();
            if (p.hasGps) { p.lo = lo.toDouble(); p.la = la.toDouble(); }
            // 评分：列未写时回退 exif_json 里的旧值（旧版 Python 端可能写过）
            if (rating <= 0) rating = o.value(QLatin1String("rating")).toInt();
        }
        p.rating = qBound(0, rating, 5);
        if (p.title.isEmpty()) p.title = p.rel;   // 兜底展示名（非编造 EXIF）
        // P10 离线逆地理回填（读时计算，库保持只读）：仅路径推导缺失且真有 GPS 时
        if (p.region.isEmpty() && p.hasGps) {
            GeoIndex::instance().ensureLoaded();
            const GeoResult g = GeoIndex::instance().resolve(p.la, p.lo);
            if (!g.region.isEmpty()) p.region = g.region;
            if (!g.prov.isEmpty())   p.prov = g.prov;
            if (p.city.isEmpty() && !g.city.isEmpty()) p.city = g.city;
        }
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

// ---- P16 写回：独立读写连接（主连接保持只读语义）----
QSqlDatabase Library::rw()
{
    if (!QSqlDatabase::contains(QStringLiteral("la_rw"))) {
        QSqlDatabase db = QSqlDatabase::addDatabase(
            QStringLiteral("QSQLITE"), QStringLiteral("la_rw"));
        db.setDatabaseName(dbPath_);
        db.open();
    }
    return QSqlDatabase::database(QStringLiteral("la_rw"));
}

Photo* findMutable(QVector<Photo>& v, const QString& path)
{
    for (Photo& p : v)
        if (p.path == path) return &p;
    return nullptr;
}

bool Library::setRating(const QString& path, int rating)
{
    const int r = qBound(0, rating, 5);
    QSqlQuery q(rw());
    q.prepare(QStringLiteral("UPDATE photos SET rating=? WHERE path=?"));
    q.addBindValue(r);
    q.addBindValue(path);
    if (!q.exec()) return false;
    if (Photo* p = findMutable(photos_, path)) p->rating = r;
    return true;
}

bool Library::setPrivate(const QString& path, bool priv)
{
    QSqlQuery q(rw());
    q.prepare(QStringLiteral("UPDATE photos SET private=? WHERE path=?"));
    q.addBindValue(priv ? 1 : 0);
    q.addBindValue(path);
    if (!q.exec()) return false;
    if (Photo* p = findMutable(photos_, path)) p->priv = priv;
    return true;
}

QString Library::metaValue(const QString& key) const
{
    // meta 读取走只读主连接（const）
    if (!QSqlDatabase::contains(QStringLiteral("la_lib"))) return QString();
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("la_lib")));
    q.prepare(QStringLiteral("SELECT value FROM meta WHERE key=?"));
    q.addBindValue(key);
    if (!q.exec() || !q.next()) return QString();
    return q.value(0).toString();
}

bool Library::setMetaValue(const QString& key, const QString& value)
{
    QSqlQuery q(rw());
    q.prepare(QStringLiteral(
        "INSERT INTO meta(key,value) VALUES(?,?) "
        "ON CONFLICT(key) DO UPDATE SET value=excluded.value"));
    q.addBindValue(key);
    q.addBindValue(value);
    return q.exec();
}
