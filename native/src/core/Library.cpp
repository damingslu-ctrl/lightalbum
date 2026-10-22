#include "Library.h"
#include "GeoIndex.h"
#include "CmdFilter.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QFileInfo>
#include <QSet>
#include <QThread>
#include <algorithm>

// 方向 C·QA：反例开关（写库但跳过内存同步）
static bool g_forceCategoryNoSync = false;
// 方向 C·人脸识别（方案A）·QA：反例开关（写库但跳过内存同步）
static bool g_forcePersonNoSync = false;

bool Library::open(const QString& dbPath, QString* err)
{
    dbPath_ = dbPath;

    // 波3：重开时把惰性读写连接一并重绑到新库路径。否则第二个 Library 实例的写会落到
    // 上一个实例的旧文件（la_rw 只在「不存在」时才建，且绑的是旧 dbPath_）。
    if (QSqlDatabase::contains(QStringLiteral("la_rw")))
        QSqlDatabase::removeDatabase(QStringLiteral("la_rw"));

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
            m.exec(QStringLiteral("ALTER TABLE photos ADD COLUMN mark TEXT"));
            // 方向 C·人物自动聚类：代表脸 embedding（512-d float32 BLOB）。重复 ADD 报错无害，静默忽略。
            m.exec(QStringLiteral("ALTER TABLE photos ADD COLUMN face_emb BLOB"));
            // 2.28.0 视频：与 Scanner 的 kExtraColumns 对齐（重复 ADD 报错无害，静默忽略）
            m.exec(QStringLiteral("ALTER TABLE photos ADD COLUMN media_type TEXT"));
            m.exec(QStringLiteral("ALTER TABLE photos ADD COLUMN dur_sec REAL"));
            // 波3：seen_at 是 Scanner 基表列，但历史/测试库可能未建（同 EXTRA_COLUMNS 思路），
            // 缺列会让下方 SELECT 直接 open 失败 —— 幂等补齐。
            m.exec(QStringLiteral(
                "ALTER TABLE photos ADD COLUMN seen_at REAL NOT NULL DEFAULT 0"));
            m.exec(QStringLiteral(
                "CREATE INDEX IF NOT EXISTS idx_photos_cat ON photos(category)"));
            // 波4 R8：时间地基三列（Scanner 是唯一写点；旧库在这里一次性回填）。
            m.exec(QStringLiteral(
                "ALTER TABLE photos ADD COLUMN shot_ts REAL NOT NULL DEFAULT 0"));
            m.exec(QStringLiteral(
                "ALTER TABLE photos ADD COLUMN shot_year INTEGER NOT NULL DEFAULT 0"));
            m.exec(QStringLiteral(
                "ALTER TABLE photos ADD COLUMN shot_month INTEGER NOT NULL DEFAULT 0"));
            m.exec(QStringLiteral(
                "CREATE INDEX IF NOT EXISTS idx_photos_year_month "
                "ON photos(shot_year, shot_month)"));

            // 波4 §3.1：一次性回填（meta 标记幂等；单事务、GUI 线程：open() 只在 GUI 线程调）。
            // fail-open：任何一步失败都静默——老库照旧能打开，只是查询走全量源。
            m.exec(QStringLiteral(
                "CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT)"));
            QSqlQuery has(mig);
            has.prepare(QStringLiteral(
                "SELECT value FROM meta WHERE key='ex_cols_backfill'"));
            const bool done = has.exec() && has.next();
            has.finish();
            if (!done) {
                QVector<QPair<QString, QString>> all;
                QSqlQuery sel(mig);
                if (sel.exec(QStringLiteral("SELECT path, exif_json FROM photos"))) {
                    while (sel.next())
                        all.append({sel.value(0).toString(), sel.value(1).toString()});
                }
                sel.finish();
                if (!all.isEmpty() && mig.transaction()) {
                    QSqlQuery up(mig);
                    up.prepare(QStringLiteral(
                        "UPDATE photos SET shot_ts=?, shot_year=?, shot_month=? WHERE path=?"));
                    for (const auto& row : all) {
                        const QJsonObject o =
                            QJsonDocument::fromJson(row.second.toUtf8()).object();
                        const QString d = o.value(QLatin1String("d")).toString();
                        const QString tm = o.value(QLatin1String("tm")).toString();
                        const int y = o.value(QLatin1String("y")).toInt();
                        const QDate sd = photoDateOf(d);
                        QTime t = QTime::fromString(tm, QStringLiteral("HH:mm:ss"));
                        if (!t.isValid()) t = QTime::fromString(tm, QStringLiteral("HH:mm"));
                        const double ts = sd.isValid()
                            ? double(QDateTime(sd, t.isValid() ? t : QTime(0, 0))
                                         .toSecsSinceEpoch())
                            : 0.0;
                        up.addBindValue(ts);
                        up.addBindValue(y > 0 ? y : (sd.isValid() ? sd.year() : 0));
                        up.addBindValue(sd.isValid() ? sd.month() : 0);
                        up.addBindValue(row.first);
                        up.exec();          // 单行失败也只是少回填一行，不阻断 open
                    }
                    mig.commit();
                }
                QSqlQuery markQ(mig);
                markQ.prepare(QStringLiteral(
                    "INSERT INTO meta(key,value) VALUES('ex_cols_backfill',?) "
                    "ON CONFLICT(key) DO UPDATE SET value=excluded.value"));
                markQ.addBindValue(QString::number(QDateTime::currentMSecsSinceEpoch()));
                markQ.exec();
            }
            // 波3 curate「常设展」：展历史 / 合集 / 模板冷却（spec §4，幂等）
            m.exec(QStringLiteral(
                "CREATE TABLE IF NOT EXISTS exhibits ("
                " id INTEGER PRIMARY KEY, template TEXT, title TEXT, blurb TEXT,"
                " primary_path TEXT, paths TEXT, generated_at REAL, shown_at REAL,"
                " feedback TEXT)"));
            m.exec(QStringLiteral(
                "CREATE TABLE IF NOT EXISTS albums ("
                " id INTEGER PRIMARY KEY, name TEXT, created_at REAL, source TEXT)"));
            m.exec(QStringLiteral(
                "CREATE TABLE IF NOT EXISTS album_items ("
                " album_id INTEGER, path TEXT, ord INTEGER)"));
            m.exec(QStringLiteral(
                "CREATE TABLE IF NOT EXISTS template_mutes ("
                " template TEXT PRIMARY KEY, until REAL)"));
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
            "category, person, private, rating, mark, media_type, dur_sec, seen_at FROM photos"))) {
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
        p.mark   = q.value(9).toString();

        // 2.28.0 视频：media_type 可能是 NULL（老库未补列的极端情况）→ 兜底 "image"
        p.mediaType = q.value(10).toString();
        if (p.mediaType.isEmpty()) p.mediaType = QStringLiteral("image");
        p.durSec    = q.value(11).toDouble();
        p.seenAt    = qint64(q.value(12).toDouble());   // 波3：seen_at（入库时间，秒）
        p.mark   = q.value(9).toString();
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

    // D5：排序完成后再建path 索引（下标必须与最终顺序一致）
    rebuildPathIndex();

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

// ---- D5：批量写回单事务实现（原子；失败整体回滚，内存副本同步不动）----
//设计要点：
//  ① prepared statement 只prepare 一次、循环 addBindValue + exec → 6425 条也只解析一次 SQL；
//  ② 内存副本**仅在 commit 成功后**同步 —— 否则中途失败会出现「DB 已回滚、内存已改」的撕裂；
//  ③ qnumRowsAffected 累计成 done（路径不存在 → 命中 0 行，仍算成功，不是错误）。
BatchResult Library::applyBatch(BatchField field, const QStringList& paths, const QVariant& value)
{
    BatchResult r;
    r.total = paths.size();
    if (paths.isEmpty()) { r.ok = true; return r; }      // 空批=成功空操作（非错误）

    QSqlDatabase db = rw();
    const QString sql = (field == BatchField::Rating)  ? QStringLiteral("UPDATE photos SET rating=? WHERE path=?")
                        : (field == BatchField::Private) ? QStringLiteral("UPDATE photos SET private=? WHERE path=?")
                                                         : QStringLiteral("UPDATE photos SET mark=? WHERE path=?");

    if (!db.transaction()) {
        r.error = db.lastError().text();
        return r;
    }
    QSqlQuery q(db);
    if (!q.prepare(sql)) {
        r.error = q.lastError().text();
        db.rollback();
        return r;
    }
    int done = 0;
    for (int i = 0; i < paths.size(); ++i) {
        q.addBindValue(value);
        q.addBindValue(paths[i]);
        if (!q.exec()) {                                  // 任一条失败 → 整体回滚
            r.error = q.lastError().text();
            r.failedIndex = i;
            r.done = 0;
            db.rollback();
            return r;
        }
        done += q.numRowsAffected();
    }
    if (!db.commit()) {
        r.error = db.lastError().text();
        r.failedIndex = paths.size() - 1;
        r.done = 0;
        db.rollback();
        return r;
    }
    r.ok = true;
    r.done = done;

    // 提交成功后才同步内存副本（保持 DB ↔ 内存一致）
    if (field == BatchField::Rating) {
        const int rr = value.toInt();
        for (const QString& p : paths)
            if (Photo* ph = findMutable(p)) ph->rating = rr;
    } else if (field == BatchField::Private) {
        const bool pv = value.toBool();
        for (const QString& p : paths)
            if (Photo* ph = findMutable(p)) ph->priv = pv;
    } else { // Mark（去重/选片标记；空串=清除）
        const QString mv = value.toString();
        for (const QString& p : paths)
            if (Photo* ph = findMutable(p)) ph->mark = mv;
    }
    return r;
}

BatchResult Library::setRatings(const QStringList& paths, int rating)
{
    return applyBatch(BatchField::Rating, paths, qBound(0, rating, 5));
}

BatchResult Library::setPrivates(const QStringList& paths, bool priv)
{
    return applyBatch(BatchField::Private, paths, priv ? 1 : 0);
}

BatchResult Library::setMarks(const QStringList& paths, const QString& mark)
{
    return applyBatch(BatchField::Mark, paths, mark);
}

bool Library::setRating(const QString& path, int rating)
{
    return setRatings(QStringList{path}, rating).ok;
}

bool Library::setPrivate(const QString& path, bool priv)
{
    return setPrivates(QStringList{path}, priv).ok;
}

bool Library::setMark(const QString& path, const QString& mark)
{
    return setMarks(QStringList{path}, mark).ok;
}

void Library::setForceCategoryNoSyncForQa(bool on) { g_forceCategoryNoSync = on; }
void Library::setForcePersonNoSyncForQa(bool on) { g_forcePersonNoSync = on; }

// 记下这次写发生在哪个线程。atomic 存指针值本身即可（QThread* 生命周期由 Qt 管），
// 读侧（门禁）拿它和 QThread::currentThread() 比，不追逐指针背后的对象。
void Library::noteWriteThread()
{
    lastWriteThread_.store(reinterpret_cast<quintptr>(QThread::currentThread()),
                           std::memory_order_relaxed);
}

// ---- 方向 C·智能分类：批量写回场景标签（每条 path 各自 label）----
// 沿用 applyBatch 的「单事务 + COMMIT 后才同步内存」语义，区别在于每条值不同
// （rating/priv/mark 是统一值，category 是 per-path），故独立实现而非套用枚举。
BatchResult Library::setCategories(const QVector<CatItem>& items)
{
    BatchResult r;
    r.total = items.size();
    if (items.isEmpty()) { r.ok = true; return r; }   // 空批=成功空操作
    noteWriteThread();

    QSqlDatabase db = rw();
    const QString sql = QStringLiteral("UPDATE photos SET category=? WHERE path=?");
    if (!db.transaction()) {
        r.error = db.lastError().text();
        return r;
    }
    QSqlQuery q(db);
    if (!q.prepare(sql)) {
        r.error = q.lastError().text();
        db.rollback();
        return r;
    }
    int done = 0;
    for (int i = 0; i < items.size(); ++i) {
        const CatItem& it = items[i];
        q.addBindValue(it.label);
        q.addBindValue(it.path);
        if (!q.exec()) {                                // 任一条失败 → 整体回滚
            r.error = q.lastError().text();
            r.failedIndex = i;
            r.done = 0;
            db.rollback();
            return r;
        }
        done += q.numRowsAffected();
    }
    if (!db.commit()) {
        r.error = db.lastError().text();
        r.failedIndex = items.size() - 1;
        r.done = 0;
        db.rollback();
        return r;
    }
    r.ok = true;
    r.done = done;

    // COMMIT 成功后才同步内存副本（保持 DB ↔ 内存一致）；
    // QA 反例开关打开时故意跳过 → GalleryModel 按内存 category 检索失效（RED）。
    if (!g_forceCategoryNoSync) {
        for (const CatItem& it : items)
            if (Photo* ph = findMutable(it.path)) ph->category = it.label;
    }
    return r;
}

// ---- 方向 C·人脸识别（方案A）：批量写回人物标签（每条 path 各自 name）----
// 沿用 setCategories 的「单事务 + COMMIT 后才同步内存」语义，区别在于每条值不同
// （category/person 都是 per-path），故与 setCategories 并列独立实现。
BatchResult Library::setPersons(const QVector<PersonItem>& items)
{
    BatchResult r;
    r.total = items.size();
    if (items.isEmpty()) { r.ok = true; return r; }   // 空批=成功空操作
    noteWriteThread();

    QSqlDatabase db = rw();
    const QString sql = QStringLiteral("UPDATE photos SET person=? WHERE path=?");
    if (!db.transaction()) {
        r.error = db.lastError().text();
        return r;
    }
    QSqlQuery q(db);
    if (!q.prepare(sql)) {
        r.error = q.lastError().text();
        db.rollback();
        return r;
    }
    int done = 0;
    for (int i = 0; i < items.size(); ++i) {
        const PersonItem& it = items[i];
        q.addBindValue(it.name);
        q.addBindValue(it.path);
        if (!q.exec()) {                                // 任一条失败 → 整体回滚
            r.error = q.lastError().text();
            r.failedIndex = i;
            r.done = 0;
            db.rollback();
            return r;
        }
        done += q.numRowsAffected();
    }
    if (!db.commit()) {
        r.error = db.lastError().text();
        r.failedIndex = items.size() - 1;
        r.done = 0;
        db.rollback();
        return r;
    }
    r.ok = true;
    r.done = done;

    // COMMIT 成功后才同步内存副本（保持 DB ↔ 内存一致）；
    // QA 反例开关打开时故意跳过 → GalleryModel 按内存 person 检索失效（RED）。
    if (!g_forcePersonNoSync) {
        for (const PersonItem& it : items)
            if (Photo* ph = findMutable(it.path)) ph->person = it.name;
    }
    return r;
}

// ---- 方向 C·人脸识别（方案A）：人物重命名（token 级）----
// 遍历内存副本，对含 oldName（逗号分隔 token，trim 后精确匹配）的照片做替换：
//   · newName 非空 → 该 token 改为 newName（若与既有 token 重复则 removeDuplicates 合并）；
//   · newName 为空   → 删除该 token（照片仍保留，仅去掉该标签）。
// 单事务写回 + COMMIT 后同步内存；oldName 为空或全库无匹配均视为成功（幂等）。
bool Library::renamePerson(const QString& oldName, const QString& newName)
{
    if (oldName.isEmpty()) return false;
    QVector<QPair<QString, QString>> updates;   // path -> new person 串
    updates.reserve(photos_.size());
    for (const Photo& p : photos_) {
        if (p.person.isEmpty()) continue;
        const QStringList toks = p.person.split(',', Qt::SkipEmptyParts);
        bool changed = false;
        QStringList out;
        out.reserve(toks.size());
        for (const QString& t : toks) {
            const QString tt = t.trimmed();
            if (tt == oldName) {
                changed = true;
                if (!newName.isEmpty()) out.append(newName);   // 空=newName → 删除 token
            } else {
                out.append(tt);
            }
        }
        if (changed) {
            out.removeDuplicates();
            updates.append({p.path, out.join(QChar(','))});
        }
    }
    if (updates.isEmpty()) return true;          // 无匹配：幂等成功

    QSqlDatabase db = rw();
    if (!db.transaction()) return false;
    QSqlQuery q(db);
    if (!q.prepare(QStringLiteral("UPDATE photos SET person=? WHERE path=?"))) {
        db.rollback();
        return false;
    }
    for (const auto& u : updates) {
        q.addBindValue(u.second);
        q.addBindValue(u.first);
        if (!q.exec()) { db.rollback(); return false; }
    }
    if (!db.commit()) { db.rollback(); return false; }

    // 写回成功后才同步内存副本（保持 DB ↔ 内存一致）。
    for (const auto& u : updates)
        if (Photo* ph = findMutable(u.first)) ph->person = u.second;
    return true;
}

// ---- 方向 C·人物自动聚类：批量写回代表脸 embedding（仅 face_emb 列）----
// 单事务、COMMIT 后才返回；embedding 不进内存（不触 Photo 副本）。守恒：只改 face_emb 列。
BatchResult Library::setFaceEmbeddings(const QVector<FaceItem>& items)
{
    BatchResult r;
    r.total = items.size();
    if (items.isEmpty()) { r.ok = true; return r; }   // 空批=成功空操作
    noteWriteThread();

    QSqlDatabase db = rw();
    const QString sql = QStringLiteral("UPDATE photos SET face_emb=? WHERE path=?");
    if (!db.transaction()) {
        r.error = db.lastError().text();
        return r;
    }
    QSqlQuery q(db);
    if (!q.prepare(sql)) {
        r.error = q.lastError().text();
        db.rollback();
        return r;
    }
    int done = 0;
    for (int i = 0; i < items.size(); ++i) {
        q.addBindValue(items[i].emb);    // QByteArray → BLOB
        q.addBindValue(items[i].path);
        if (!q.exec()) {                  // 任一条失败 → 整体回滚
            r.error = q.lastError().text();
            r.failedIndex = i;
            r.done = 0;
            db.rollback();
            return r;
        }
        done += q.numRowsAffected();
    }
    if (!db.commit()) {
        r.error = db.lastError().text();
        r.failedIndex = items.size() - 1;
        r.done = 0;
        db.rollback();
        return r;
    }
    r.ok = true;
    r.done = done;
    return r;   // face_emb 不进内存，无需同步 Photo 副本
}

// ---- 方向 C·人物自动聚类：批量写回聚类簇标签（person 列）----
// 守恒 + 不覆盖非空：UPDATE WHERE person IS NULL OR person=''；内存同步仅对空者生效。
// 复用 g_forcePersonNoSync 反例钩子（写库但跳过内存同步 → GalleryModel 按 person 检索失效 RED）。
BatchResult Library::applyClusters(const QVector<ClusterItem>& items)
{
    BatchResult r;
    r.total = items.size();
    if (items.isEmpty()) { r.ok = true; return r; }   // 空批=成功空操作
    noteWriteThread();

    QSqlDatabase db = rw();
    const QString sql = QStringLiteral(
        "UPDATE photos SET person=? WHERE path=? AND (person IS NULL OR person='')");
    if (!db.transaction()) {
        r.error = db.lastError().text();
        return r;
    }
    QSqlQuery q(db);
    if (!q.prepare(sql)) {
        r.error = q.lastError().text();
        db.rollback();
        return r;
    }
    int done = 0;
    for (int i = 0; i < items.size(); ++i) {
        q.addBindValue(items[i].label);
        q.addBindValue(items[i].path);
        if (!q.exec()) {                  // 任一条失败 → 整体回滚
            r.error = q.lastError().text();
            r.failedIndex = i;
            r.done = 0;
            db.rollback();
            return r;
        }
        done += q.numRowsAffected();
    }
    if (!db.commit()) {
        r.error = db.lastError().text();
        r.failedIndex = items.size() - 1;
        r.done = 0;
        db.rollback();
        return r;
    }
    r.ok = true;
    r.done = done;

    // COMMIT 成功后才同步内存副本（保持 DB ↔ 内存一致）；
    // 仅更新 person 当前为空的照片（与 DB WHERE 语义一致），避免覆盖用户已标注。
    // QA 反例开关打开时故意跳过 → GalleryModel 按内存 person 检索失效（RED）。
    if (!g_forcePersonNoSync) {
        for (const ClusterItem& it : items)
            if (Photo* ph = findMutable(it.path)) {
                if (ph->person.isEmpty()) ph->person = it.label;
            }
    }
    return r;
}

// ---- 方向 C·人物自动聚类：读取「person 为空且有 face_emb」的照片（聚类输入集）----
// 走只读主连接；缺失 BLOB 的行跳过（不参与聚类）。不改动任何状态。
QVector<FaceItem> Library::getFaceEmbeddings() const
{
    QVector<FaceItem> out;
    if (!QSqlDatabase::contains(QStringLiteral("la_lib"))) return out;
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("la_lib")));
    if (!q.exec(QStringLiteral(
            "SELECT path, face_emb FROM photos WHERE person IS NULL OR person=''"))) {
        return out;
    }
    while (q.next()) {
        QByteArray emb = q.value(1).toByteArray();
        if (emb.isEmpty()) continue;       // 仅含已有 embedding 的行
        FaceItem it;
        it.path = q.value(0).toString();
        it.emb  = emb;
        out.append(it);
    }
    return out;
}

// ---- D5：path ->下标索引（批量写回去 O(N²)）----
void Library::rebuildPathIndex()
{
    pathIx_.clear();
    pathIx_.reserve(photos_.size());
    for (int i = 0; i < photos_.size(); ++i)
        pathIx_.insert(photos_[i].path, i);
}

Photo* Library::findMutable(const QString& path)
{
    const auto it = pathIx_.constFind(path);
    if (it == pathIx_.constEnd()) return nullptr;
    return &photos_[it.value()];
}

const Photo* Library::find(const QString& path) const
{
    const auto it = pathIx_.constFind(path);
    if (it == pathIx_.constEnd()) return nullptr;
    return &photos_[it.value()];
}

bool Library::verifyPathIndex() const
{
    if (pathIx_.size() != photos_.size()) return false;
    for (int i = 0; i < photos_.size(); ++i) {
        const auto it = pathIx_.constFind(photos_[i].path);
        if (it == pathIx_.constEnd() || it.value() != i) return false;
    }
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

// ================= 波3 curate「常设展」持久层 =================
// 写：走 rw()（GUI 线程唯一写者，见 open() 上方约束）；读：走只读 la_lib。

qint64 Library::addExhibit(const QString& tmpl, const QString& title, const QString& blurb,
                           const QString& primary, const QStringList& paths,
                           qint64 generatedAt, const QString& feedback)
{
    noteWriteThread();
    QSqlQuery q(rw());
    q.prepare(QStringLiteral(
        "INSERT INTO exhibits"
        "(template,title,blurb,primary_path,paths,generated_at,shown_at,feedback)"
        " VALUES(?,?,?,?,?,?,?,?)"));
    q.addBindValue(tmpl);
    q.addBindValue(title);
    q.addBindValue(blurb);
    q.addBindValue(primary);
    q.addBindValue(paths.join(QChar(',')));      // 逗号串，与 Photo::person 同格式（计划裁决 4）
    q.addBindValue(double(generatedAt));
    q.addBindValue(double(generatedAt));         // shown_at 初始 = 生成时刻
    q.addBindValue(feedback);
    if (!q.exec()) return 0;
    return q.lastInsertId().toLongLong();
}

bool Library::updateExhibitFeedback(qint64 id, const QString& feedback)
{
    noteWriteThread();
    QSqlQuery q(rw());
    q.prepare(QStringLiteral("UPDATE exhibits SET feedback=? WHERE id=?"));
    q.addBindValue(feedback);
    q.addBindValue(id);
    return q.exec();
}

QVector<ExhibitRow> Library::loadExhibitHistory() const
{
    QVector<ExhibitRow> out;
    if (!QSqlDatabase::contains(QStringLiteral("la_lib"))) return out;
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("la_lib")));
    if (!q.exec(QStringLiteral(
            "SELECT id, template, title, blurb, primary_path, paths,"
            " generated_at, shown_at, feedback FROM exhibits ORDER BY generated_at DESC"))) {
        return out;
    }
    while (q.next()) {
        ExhibitRow r;
        r.id          = q.value(0).toLongLong();
        r.tmpl        = q.value(1).toString();
        r.title       = q.value(2).toString();
        r.blurb       = q.value(3).toString();
        r.primary     = q.value(4).toString();
        r.paths       = q.value(5).toString();
        r.generatedAt = qint64(q.value(6).toDouble());
        r.shownAt     = qint64(q.value(7).toDouble());
        r.feedback    = q.value(8).toString();
        out.append(r);
    }
    return out;
}

qint64 Library::lastExhibitAt() const
{
    if (!QSqlDatabase::contains(QStringLiteral("la_lib"))) return 0;
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("la_lib")));
    if (!q.exec(QStringLiteral("SELECT max(generated_at) FROM exhibits")) || !q.next())
        return 0;
    if (q.value(0).isNull()) return 0;
    return qint64(q.value(0).toDouble());
}

bool Library::muteTemplate(const QString& tmpl, qint64 until)
{
    noteWriteThread();
    QSqlQuery q(rw());
    q.prepare(QStringLiteral(
        "INSERT INTO template_mutes(template,until) VALUES(?,?) "
        "ON CONFLICT(template) DO UPDATE SET until=excluded.until"));
    q.addBindValue(tmpl);
    q.addBindValue(double(until));
    return q.exec();
}

QStringList Library::loadMuteState(qint64 now) const
{
    QStringList out;
    if (!QSqlDatabase::contains(QStringLiteral("la_lib"))) return out;
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("la_lib")));
    q.prepare(QStringLiteral("SELECT template FROM template_mutes WHERE until > ?"));
    q.addBindValue(double(now));
    if (!q.exec()) return out;
    while (q.next()) out << q.value(0).toString();
    return out;
}

qint64 Library::createAlbum(const QString& name, const QStringList& paths, const QString& source)
{
    noteWriteThread();
    QSqlDatabase db = rw();
    if (!db.transaction()) return 0;
    qint64 id = 0;
    {
        QSqlQuery a(db);
        a.prepare(QStringLiteral("INSERT INTO albums(name,created_at,source) VALUES(?,?,?)"));
        a.addBindValue(name);
        a.addBindValue(double(QDateTime::currentSecsSinceEpoch()));
        a.addBindValue(source);
        if (!a.exec()) { db.rollback(); return 0; }
        id = a.lastInsertId().toLongLong();
    }
    {
        QSqlQuery it(db);
        it.prepare(QStringLiteral("INSERT INTO album_items(album_id,path,ord) VALUES(?,?,?)"));
        for (int i = 0; i < paths.size(); ++i) {
            it.addBindValue(id);
            it.addBindValue(paths[i]);
            it.addBindValue(i);
            if (!it.exec()) { db.rollback(); return 0; }
        }
    }
    if (!db.commit()) { db.rollback(); return 0; }
    return id;
}

QVector<AlbumRow> Library::listAlbums() const
{
    QVector<AlbumRow> out;
    if (!QSqlDatabase::contains(QStringLiteral("la_lib"))) return out;
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("la_lib")));
    if (!q.exec(QStringLiteral(
            "SELECT a.id, a.name, a.source, a.created_at,"
            " (SELECT count(*) FROM album_items i WHERE i.album_id=a.id)"
            " FROM albums a ORDER BY a.created_at DESC"))) {
        return out;
    }
    while (q.next()) {
        AlbumRow r;
        r.id        = q.value(0).toLongLong();
        r.name      = q.value(1).toString();
        r.source    = q.value(2).toString();
        r.createdAt = qint64(q.value(3).toDouble());
        r.count     = q.value(4).toInt();
        out.append(r);
    }
    return out;
}

QStringList Library::albumPaths(qint64 albumId) const
{
    QStringList out;
    if (albumId <= 0 || !QSqlDatabase::contains(QStringLiteral("la_lib"))) return out;
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("la_lib")));
    q.prepare(QStringLiteral("SELECT path FROM album_items WHERE album_id=? ORDER BY ord"));
    q.addBindValue(albumId);
    if (!q.exec()) return out;
    while (q.next()) out << q.value(0).toString();
    return out;
}

QVector<Library::MuteRow> Library::listMutes(qint64 now) const
{
    QVector<MuteRow> out;
    if (!QSqlDatabase::contains(QStringLiteral("la_lib"))) return out;
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("la_lib")));
    q.prepare(QStringLiteral(
        "SELECT template, until FROM template_mutes WHERE until > ? ORDER BY until DESC"));
    q.addBindValue(double(now));
    if (!q.exec()) return out;
    while (q.next()) out.append({q.value(0).toString(), qint64(q.value(1).toDouble())});
    return out;
}

bool Library::unmuteTemplate(const QString& tmpl)
{
    if (tmpl.isEmpty()) return false;
    noteWriteThread();
    QSqlQuery q(rw());
    q.prepare(QStringLiteral("DELETE FROM template_mutes WHERE template = ?"));
    q.addBindValue(tmpl);
    if (!q.exec()) return false;
    return q.numRowsAffected() > 0;   // 0 行 = 本来就没静音：如实 false
}

// ---- QA 合成库辅助（仅门禁用；生产零调用）----
bool Library::makeSyntheticDb(const QString& dbPath, const QVector<SynthRow>& rows, QString* err)
{
    if (QSqlDatabase::contains(QStringLiteral("la_synth")))
        QSqlDatabase::removeDatabase(QStringLiteral("la_synth"));
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                QStringLiteral("la_synth"));
    db.setDatabaseName(dbPath);
    if (!db.open()) {
        if (err) *err = db.lastError().text();
        QSqlDatabase::removeDatabase(QStringLiteral("la_synth"));
        return false;
    }
    bool ok = true;
    {
        QSqlQuery q(db);
        ok = q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS photos ("
            " path TEXT PRIMARY KEY, root TEXT NOT NULL DEFAULT '', rel TEXT NOT NULL DEFAULT '',"
            " src TEXT NOT NULL DEFAULT 'native', size INTEGER NOT NULL DEFAULT 0,"
            " mtime REAL NOT NULL DEFAULT 0, thumb TEXT, exif_json TEXT,"
            " missing INTEGER NOT NULL DEFAULT 0, seen_at REAL NOT NULL DEFAULT 0,"
            " category TEXT, cat_score REAL, cat_at REAL, rating INTEGER DEFAULT 0,"
            " person TEXT, media_type TEXT DEFAULT 'image', dur_sec REAL DEFAULT 0,"
            " private INTEGER DEFAULT 0, mark TEXT, face_emb BLOB)"));
        ok = ok && q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT)"));
        if (!ok && err) *err = q.lastError().text();
    }
    for (const SynthRow& r : rows) {
        QSqlQuery ins(db);
        ins.prepare(QStringLiteral(
            "INSERT OR REPLACE INTO photos"
            "(path,root,rel,src,size,mtime,thumb,exif_json,missing,seen_at,"
            " category,rating,person,media_type,dur_sec,private,mark)"
            " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
        // date/year/GPS 经 exif_json 还原，与真实扫描同格式。
        QJsonObject o;
        o.insert(QStringLiteral("d"), r.date);
        o.insert(QStringLiteral("y"), r.year);
        if (r.hasGps) { o.insert(QStringLiteral("la"), r.la); o.insert(QStringLiteral("lo"), r.lo); }
        if (!r.city.isEmpty()) o.insert(QStringLiteral("ct"), r.city);
        ins.addBindValue(r.path);
        ins.addBindValue(QString());
        ins.addBindValue(QFileInfo(r.path).fileName());
        ins.addBindValue(QStringLiteral("native"));
        ins.addBindValue(qint64(0));
        ins.addBindValue(r.seenAt);
        ins.addBindValue(QString());
        ins.addBindValue(QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)));
        ins.addBindValue(r.missing ? 1 : 0);
        ins.addBindValue(r.seenAt);
        ins.addBindValue(r.category);
        ins.addBindValue(r.rating);
        ins.addBindValue(r.person);
        ins.addBindValue(r.mediaType);
        ins.addBindValue(0.0);
        ins.addBindValue(r.priv ? 1 : 0);
        ins.addBindValue(r.mark);
        if (!ins.exec()) { ok = false; if (err) *err = ins.lastError().text(); break; }
    }
    db.close();
    QSqlDatabase::removeDatabase(QStringLiteral("la_synth"));
    return ok;
}
