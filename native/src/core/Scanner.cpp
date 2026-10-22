// P8-3 原生扫描/导入器实现 —— 移植自 client/lightalbum/core/{scanner,library,thumbs}.py
#include "Scanner.h"
#include "PathGuess.h"
#include "CmdFilter.h"
#include "ExifReader.h"
#include "GeoIndex.h"
#include "RawDecode.h"
#include "VideoDecode.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <cmath>
#include <functional>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

// ---- library.py SCHEMA（含全部索引，幂等）----
const char* const kSchemaStmts[] = {
    "CREATE TABLE IF NOT EXISTS photos ("
    " path TEXT PRIMARY KEY, root TEXT NOT NULL, rel TEXT NOT NULL,"
    " src TEXT NOT NULL DEFAULT 'native', size INTEGER NOT NULL DEFAULT 0,"
    " mtime REAL NOT NULL DEFAULT 0, thumb TEXT, exif_json TEXT,"
    " missing INTEGER NOT NULL DEFAULT 0, seen_at REAL NOT NULL DEFAULT 0)",
    "CREATE INDEX IF NOT EXISTS idx_photos_root ON photos(root)",
    "CREATE INDEX IF NOT EXISTS idx_photos_src  ON photos(src)",
    "CREATE INDEX IF NOT EXISTS idx_photos_thumb ON photos(thumb)",
    "CREATE INDEX IF NOT EXISTS idx_photos_rel ON photos(rel)",
    "CREATE INDEX IF NOT EXISTS idx_photos_root_missing ON photos(root, missing)",
    "CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT)",
    "CREATE TABLE IF NOT EXISTS vault ("
    " id INTEGER PRIMARY KEY AUTOINCREMENT, path TEXT UNIQUE NOT NULL,"
    " name TEXT NOT NULL, enc TEXT NOT NULL, thumb TEXT,"
    " size INTEGER NOT NULL DEFAULT 0, mtime REAL NOT NULL DEFAULT 0,"
    " created_at REAL NOT NULL)",
    "CREATE INDEX IF NOT EXISTS idx_vault_created ON vault(created_at)",
};

// 后加列：CREATE TABLE IF NOT EXISTS 不会给旧库补列（library.py EXTRA_COLUMNS）
const char* const kExtraColumns[] = {
    "category TEXT", "cat_score REAL", "cat_at REAL",
    "rating INTEGER", "person TEXT",
    // 2.28.0 视频支持：media_type 区分 image/video（照片恒 'image'，视频 'video'）；
    // dur_sec = 时长（秒，REAL，取自 ffprobe；非视频恒 0）。
    // 二者都走下方幂等 ALTER TABLE 补列机制，老库零迁移成本。
    "media_type TEXT", "dur_sec REAL",
    // 波4 R8：时间地基。无 EXIF 的老库行落 0 而不是 NULL——索引查询与 =0 谓词同一语义。
    "shot_ts REAL NOT NULL DEFAULT 0",       // 拍摄时刻 epoch 秒（wave5 长廊地基，本波不查询）
    "shot_year INTEGER NOT NULL DEFAULT 0",  // EXIF y，与 Photo::year 同源
    "shot_month INTEGER NOT NULL DEFAULT 0", // 1..12；解不出日期 = 0
};

QSqlDatabase openDb(const QString& dbPath, QString* err)
{
    if (QSqlDatabase::contains("la_scan"))
        QSqlDatabase::removeDatabase(QStringLiteral("la_scan"));
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("la_scan"));
    db.setDatabaseName(dbPath);
    if (!db.open()) {
        if (err) *err = db.lastError().text();
        return QSqlDatabase();
    }
    QSqlQuery q(db);
    q.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    for (const char* s : kSchemaStmts)
        q.exec(QString::fromUtf8(s));
    // 补列（幂等）
    QSqlQuery cols(db);
    cols.exec(QStringLiteral("PRAGMA table_info(photos)"));
    QSet<QString> have;
    while (cols.next()) have.insert(cols.value(1).toString());
    for (const char* c : kExtraColumns) {
        const QString name = QString::fromUtf8(c).section(QLatin1Char(' '), 0, 0);
        if (!have.contains(name))
            q.exec(QStringLiteral("ALTER TABLE photos ADD COLUMN %1").arg(QString::fromUtf8(c)));
    }
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_photos_cat ON photos(category)"));
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_photos_rating ON photos(rating)"));
    q.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_photos_year_month ON photos(shot_year, shot_month)"));
    return db;
}

// Python os.path.abspath：Windows 原生反斜杠形态（缓存键 / DB 主键都用它）
QString normNative(const QString& p)
{
    return QFileInfo(p).absoluteFilePath().replace(QLatin1Char('/'), QLatin1Char('\\'));
}

// 云端占位文件：读它会触发按需下载、把同步卡死（library.py _is_cloud_placeholder）
bool isCloudPlaceholder(const QString& absPath, const QString& absNative)
{
    if (QFileInfo::exists(absPath + QStringLiteral(".icloud"))) return true;
#ifdef Q_OS_WIN
    const DWORD attrs = GetFileAttributesW(reinterpret_cast<const wchar_t*>(absNative.utf16()));
    if (attrs != INVALID_FILE_ATTRIBUTES
        && (attrs & (0x00040000 /*RECALL_ON_OPEN*/ | 0x00400000 /*RECALL_ON_DATA_ACCESS*/)))
        return true;
#else
    Q_UNUSED(absNative);
#endif
    return false;
}

// thumbs._key：sha1(原生绝对路径|mtime整数|size) —— 与 Python 逐字节兼容
QString thumbKey(const QString& absNative, qint64 mtimeSecs, qint64 size)
{
    const QString raw = QStringLiteral("%1|%2|%3").arg(absNative).arg(mtimeSecs).arg(size);
    if (qEnvironmentVariableIsSet("LA_SCAN_DEBUG"))
        std::fprintf(stderr, "THUMBKEY raw=[%s] sha1=%s\n", qPrintable(raw),
                     qPrintable(QString::fromLatin1(
                         QCryptographicHash::hash(raw.toUtf8(), QCryptographicHash::Sha1).toHex())));
    return QString::fromLatin1(
        QCryptographicHash::hash(raw.toUtf8(), QCryptographicHash::Sha1).toHex());
}

// thumbs._make 的 Qt 对应：EXIF 摆正 → 缩到 512 → 透明合成白底 → JPEG q82
// （Qt 的 setScaledSize 对 JPEG 走缩减解码，等价 PIL draft() 提速）
//
// ★ 2.28.0 RAW 分流：RAW 无法被 QImageReader 直接读（实测 null），
//   故先经 RawDecode 出 PPM 中间图，再喂进**完全相同**的下游逻辑 ——
//   刻意不复制一份缩略图代码，避免两条实现漂移（铁律 6：契约同源）。
QString makeThumb(const QString& absPath, const QString& outPath)
{
    // RAW → 先解码出 PPM；视频 → 先抽封面 JPEG。失败即诚实返回空（不产出半成品缩略图）
    QString srcPath = absPath;
    QString ppmToRemove;
    if (RawDecode::isRawExt(absPath)) {
        srcPath = RawDecode::decodeToTempPpm(absPath);
        if (srcPath.isEmpty()) return QString();
        ppmToRemove = srcPath;
    } else if (VideoDecode::isVideoExt(absPath)) {
        srcPath = VideoDecode::extractPoster(absPath);
        if (srcPath.isEmpty()) return QString();
        ppmToRemove = srcPath;
    }

    QImageReader reader(srcPath);
    reader.setAutoTransform(true);                      // = ImageOps.exif_transpose
    const QSize full = reader.size();
    if (full.isValid() && full.width() > 0 && full.height() > 0) {
        const int mx = qMax(full.width(), full.height());
        if (mx > 512)
            reader.setScaledSize(QSize(qMax(1, full.width() * 512 / mx),
                                       qMax(1, full.height() * 512 / mx)));
    }
    QImage img = reader.read();
    if (img.isNull()) { QFile::remove(ppmToRemove); return QString(); }
    if (img.width() > 512 || img.height() > 512) {
        const int mx = qMax(img.width(), img.height());
        img = img.scaled(qMax(1, img.width() * 512 / mx), qMax(1, img.height() * 512 / mx),
                         Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    if (img.hasAlphaChannel()) {                        // 透明区合白底（thumbs.py 同款）
        QImage bg(img.size(), QImage::Format_RGB32);
        bg.fill(Qt::white);
        QPainter p(&bg);
        p.drawImage(0, 0, img);
        p.end();
        img = bg;
    } else {
        img = img.convertToFormat(QImage::Format_RGB32);
    }
    // Python 对 .png 源也存 JPEG 内容（仅文件名带 .png），这里同构
    const bool ok = img.save(outPath, "JPEG", 82);
    QFile::remove(ppmToRemove);                         // 中间图用完即删（不留磁盘垃圾）
    if (!ok) return QString();
    return outPath;
}

} // namespace

bool Scanner::ensureSchema(const QString& dbPath, QString* err)
{
    QSqlDatabase db = openDb(dbPath, err);
    if (!db.isValid()) return false;
    db.close();
    QSqlDatabase::removeDatabase(QStringLiteral("la_scan"));
    return true;
}

ScanResult Scanner::syncRoot(const QString& rootIn, const QString& dbPathIn,
                             const QString& thumbsDirIn,
                             const std::function<void(int, int)>& progress,
                             const std::function<bool()>& cancelled)
{
    ScanResult res;
    QElapsedTimer timer;
    timer.start();

    const QString root = QFileInfo(rootIn).absoluteFilePath();      // 工作形态（正斜杠）
    const QString rootNative = normNative(rootIn);                  // 入库形态（反斜杠）
    const QString thumbsDir = QFileInfo(thumbsDirIn).absoluteFilePath();
    if (!QFileInfo(root).isDir()) {
        res.errors.append(QStringLiteral("root 不存在: %1").arg(root));
        res.errorCount = 1;
        return res;
    }
    QDir().mkpath(thumbsDir);

    // RAW 解码器可用性（2.28.0）：整轮扫描只探测一次。
    // 缺失时 RAW 不参与导入 —— 详见 RawDecode::isImportable 的降级理由。
    const bool rawAvailable = RawDecode::available();
    const bool videoAvailable = VideoDecode::available();

    QString dbErr;
    QSqlDatabase db = openDb(dbPathIn, &dbErr);
    if (!db.isValid()) {
        res.errors.append(QStringLiteral("db open: %1").arg(dbErr));
        res.errorCount = 1;
        return res;
    }

    // ---- 1) 枚举（scan_dir：就地剪枝 + IMG_EXT + *_retouched 排除 + max 20000）
    struct Found { QString rel; QString abs; };
    QVector<Found> files;
    std::function<void(const QString&, const QString&)> walk =
        [&](const QString& dir, const QString& relBase) {
            if (files.size() >= 20000 || (cancelled && cancelled())) return;
            QDir d(dir);
            const QFileInfoList subs =
                d.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name);
            for (const QFileInfo& s : subs) {
                if (PathGuess::isSkipDir(s.fileName()) || s.fileName().startsWith(QLatin1Char('.')))
                    continue;
                walk(s.absoluteFilePath(),
                     relBase.isEmpty() ? s.fileName()
                                       : relBase + QLatin1Char('/') + s.fileName());
                if (files.size() >= 20000) return;
            }
            const QFileInfoList fs = d.entryInfoList(QDir::Files, QDir::Name);
            for (const QFileInfo& fi : fs) {
                // RAW 走外部解码器（2.28.0）：kImgExt 不含 RAW，故并联判定 ——
                // 判据是 needsExternalDecode（只对 RAW 为真），**不可**用 isImportable：
                // 后者对非 RAW 返回 true，会把 .json/.ini/.AAE/.icloud 一并放行。
                if (!PathGuess::isPhoto(fi.fileName())
                    && !RawDecode::needsExternalDecode(fi.fileName(), rawAvailable)
                    && !VideoDecode::needsExternalDecode(fi.fileName(), videoAvailable))
                    continue;
                files.append({ relBase.isEmpty() ? fi.fileName()
                                                 : relBase + QLatin1Char('/') + fi.fileName(),
                               fi.absoluteFilePath() });
                if (files.size() >= 20000) return;
            }
        };
    walk(root, QString());
    res.total = files.size();

    // ---- 2) 已知索引 {path: (size, mtime)} —— size/mtime 都没变就完全跳过
    QHash<QString, QPair<qint64, double>> known;
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("SELECT path,size,mtime FROM photos WHERE root=?"));
        q.addBindValue(rootNative);
        q.exec();
        while (q.next())
            known.insert(q.value(0).toString(),
                         { q.value(1).toLongLong(), q.value(2).toDouble() });
    }

    struct Rec {
        QString path, rel, thumb;
        qint64 size = 0;
        double mtime = 0;
        QJsonObject exif;
        bool isUpdate = false;
        // 2.28.0 视频：mediaType = "image" | "video"；durSec = 时长（秒，非视频 0）
        QString mediaType = QStringLiteral("image");
        double  durSec = 0;
        // 波4 R8：时间地基三列（与 exif_json 同源，见下方派生处）
        double shotTs = 0;
        int    shotYear = 0;
        int    shotMonth = 0;
    };
    QVector<Rec> pending;
    QSet<QString> seen;
    const bool isIcloud = rootNative.toLower().contains(QLatin1String("icloud"));
    auto bump = [&](int done) {
        if (progress && (done % 50 == 0 || done == res.total)) progress(done, res.total);
    };

    // ---- UPSERT（library.py _upsert_many 语义；攒 200 张一提交）
    const auto flush = [&](bool force) {
        if (pending.isEmpty() || (!force && pending.size() < 200)) return;
        const double now = double(QDateTime::currentSecsSinceEpoch());
        QSqlQuery q(db);
        db.transaction();
        q.prepare(QStringLiteral(
            "INSERT INTO photos(path,root,rel,src,size,mtime,thumb,exif_json,missing,seen_at,rating,"
            "media_type,dur_sec,shot_ts,shot_year,shot_month)"
            " VALUES(?,?,?,?,?,?,?,?,0,?,0,?,?,?,?,?)"
            " ON CONFLICT(path) DO UPDATE SET size=excluded.size, mtime=excluded.mtime,"
            " thumb=excluded.thumb, exif_json=excluded.exif_json, rel=excluded.rel,"
            " src=excluded.src, missing=0, seen_at=excluded.seen_at,"
            " media_type=excluded.media_type, dur_sec=excluded.dur_sec,"
            " shot_ts=excluded.shot_ts, shot_year=excluded.shot_year,"
            " shot_month=excluded.shot_month"));
        for (const Rec& r : pending) {
            q.addBindValue(r.path);
            q.addBindValue(rootNative);
            q.addBindValue(r.rel);
            q.addBindValue(QStringLiteral("native"));
            q.addBindValue(r.size);
            q.addBindValue(r.mtime);
            q.addBindValue(r.thumb);
            q.addBindValue(QString::fromUtf8(
                QJsonDocument(r.exif).toJson(QJsonDocument::Compact)));
            q.addBindValue(now);
            q.addBindValue(r.mediaType);
            q.addBindValue(r.durSec);
            q.addBindValue(r.shotTs);
            q.addBindValue(r.shotYear);
            q.addBindValue(r.shotMonth);
            if (!q.exec()) {
                ++res.errorCount;
                if (res.errors.size() < 5)
                    res.errors.append(
                        QStringLiteral("%1: %2").arg(r.rel, q.lastError().text()));
            }
            q.finish();
        }
        db.commit();
        pending.clear();
    };

    // ---- 3) 逐张处理（增量规则与 Python process_one 逐条对齐）
    int done = 0;
    for (const Found& f : files) {
        if (cancelled && cancelled()) break;
        QFileInfo fi(f.abs);
        if (!fi.isFile()) continue;
        const qint64 size = fi.size();
        const double mtime = double(fi.lastModified().toSecsSinceEpoch());
        const QString absNative = normNative(f.abs);
        seen.insert(absNative);
        ++done;
        const auto prev = known.constFind(absNative);
        if (prev != known.cend() && prev->first == size
            && std::abs(prev->second - mtime) < 1e-6) {
            ++res.unchanged;
            bump(done);
            continue;
        }
        if (isCloudPlaceholder(f.abs, absNative)
            || (isIcloud && size < 24 * 1024)) {
            ++res.placeholders;
            bump(done);
            continue;
        }

        Rec r;
        r.path = absNative;
        r.rel = f.rel;
        r.size = size;
        r.mtime = mtime;
        r.isUpdate = (prev != known.cend());

        // EXIF（缺失 → 兜底值，绝不崩；mtime 日期兜底在 readExif 内部）
        const ExifData ex = ExifReader::readExif(f.abs);
        // 路径推导兜底（GPS 优先；日期 EXIF 优先于目录名）
        const PathInfo g = PathGuess::guessFromPath(f.rel);
        const QString d = ex.d.isEmpty() ? g.d : ex.d;
        const int y = (d.size() >= 4 && d.left(4).at(0).isDigit()
                       && d.left(4).toInt() > 0)
                          ? d.left(4).toInt() : 0;

        // 波4 R8：三列与内存谓词同一真源（photoDateOf / y），这里唯一写点。
        const QDate shotDate = photoDateOf(d);
        r.shotYear  = y > 0 ? y : (shotDate.isValid() ? shotDate.year() : 0);
        r.shotMonth = shotDate.isValid() ? shotDate.month() : 0;
        {
            QTime t = QTime::fromString(ex.tm, QStringLiteral("HH:mm:ss"));
            if (!t.isValid()) t = QTime::fromString(ex.tm, QStringLiteral("HH:mm"));
            r.shotTs = shotDate.isValid()
                ? double(QDateTime(shotDate, t.isValid() ? t : QTime(0, 0)).toSecsSinceEpoch())
                : 0.0;
        }

        // ★ 2.28.0 RAW 尺寸补齐：ExifReader 的 out.dim 走 QImageReader::size()，
        //   对 RAW 恒为 invalid（QImageReader 打不开 RAW）→ 实测 DNG 行 dim 为空。
        //   此处按需解码一次取真实尺寸；失败/非 RAW 一律留空（诚实空值，不编造）。
        //   仅在 dim 缺失时才付解码代价，JPEG 等常规格式零开销。
        QString dim = ex.dim;
        if (dim.isEmpty() && RawDecode::isRawExt(f.abs)) {
            const QSize rs = RawDecode::probeSize(f.abs);
            if (rs.isValid() && rs.width() > 0 && rs.height() > 0)
                dim = QStringLiteral("%1%2%3").arg(rs.width())
                          .arg(QChar(0x00D7))                  // ×
                          .arg(rs.height());
        }

        // ★ 2.28.0：视频客观量（供 UI 展示与门禁断言；非视频恒 0 / 空）
        double  o_dur = 0;
        QString o_codec;

        // ★ 2.28.0 视频：ffprobe 取客观元数据（duration / 宽高 / codec）。
        //   视频无 EXIF 概念（无 Camera/ISO/光圈），故 cam/ap/sh/iso 一律留空 ——
        //   编造这些值是最典型的「不诚实数据」，绝不做。
        //   抽帧已在 makeThumb 内完成（extractPoster），此处不再重复解码。
        if (VideoDecode::isVideoExt(f.abs)) {
            r.mediaType = QStringLiteral("video");
            const VideoDecode::VideoProbe vp = VideoDecode::probe(f.abs);
            r.durSec = vp.durationSec;
            if (vp.width > 0 && vp.height > 0)
                dim = QStringLiteral("%1%2%3").arg(vp.width)
                          .arg(QChar(0x00D7))                  // ×
                          .arg(vp.height);
            o_dur = vp.durationSec;
            o_codec = vp.codec;
        }

        // 缩略图：缓存优先（命中连图都不解）
        const QString key = thumbKey(absNative, qint64(mtime), size);
        const bool pngSrc = f.rel.toLower().endsWith(QLatin1String(".png"));
        const QString outPath = QDir(thumbsDir).filePath(
            key + (pngSrc ? QStringLiteral(".png") : QStringLiteral(".jpg")));
        QString thumbName;
        if (QFileInfo::exists(outPath)) {
            thumbName = QFileInfo(outPath).fileName();
        } else {
            const QString made = makeThumb(f.abs, outPath);
            if (!made.isEmpty()) thumbName = QFileInfo(made).fileName();
        }

        QJsonObject o;
        o.insert(QStringLiteral("t"), g.t);
        o.insert(QStringLiteral("s"), g.s);
        o.insert(QStringLiteral("y"), y);
        o.insert(QStringLiteral("o"),
                 (ex.orient >= 5 && ex.orient <= 8) ? QStringLiteral("portrait")
                                                    : QStringLiteral("landscape"));
        o.insert(QStringLiteral("r"), g.r);
        o.insert(QStringLiteral("pv"), g.pv);
        o.insert(QStringLiteral("ct"), g.ct);
        o.insert(QStringLiteral("lo"), ex.hasGps ? QJsonValue(ex.lo) : QJsonValue());
        o.insert(QStringLiteral("la"), ex.hasGps ? QJsonValue(ex.la) : QJsonValue());
        o.insert(QStringLiteral("cam"), ex.cam);
        o.insert(QStringLiteral("lens"), ex.lens);
        o.insert(QStringLiteral("focal"), ex.focal);
        o.insert(QStringLiteral("ap"), ex.ap);
        o.insert(QStringLiteral("sh"), ex.sh);
        o.insert(QStringLiteral("iso"), ex.iso);
        o.insert(QStringLiteral("d"), d);
        o.insert(QStringLiteral("tm"), ex.tm);
        o.insert(QStringLiteral("tag"), QString());
        o.insert(QStringLiteral("alt"), ex.hasAlt ? QJsonValue(ex.alt) : QJsonValue());
        o.insert(QStringLiteral("evc"), ex.evc);
        o.insert(QStringLiteral("wb"), ex.wb);
        o.insert(QStringLiteral("meter"), ex.meter);
        o.insert(QStringLiteral("dim"), dim);
        // 2.28.0 视频字段：非视频为 0 / 空（诚实空值，不编造）
        o.insert(QStringLiteral("mt"), r.mediaType);
        o.insert(QStringLiteral("dur"), o_dur);
        o.insert(QStringLiteral("codec"), o_codec);
        // P10 离线逆地理：路径推导无地区但有真 GPS → 内嵌数据回填 r/pv/ct（写入库）。
        // LA_GEO_OFF 供 P8 金样对拍 harness 关闭（对齐 Python 无逆地理的行为）。
        if (g.r.isEmpty() && ex.hasGps && qEnvironmentVariableIsEmpty("LA_GEO_OFF")) {
            GeoIndex::instance().ensureLoaded();
            const GeoResult gres = GeoIndex::instance().resolve(ex.la, ex.lo);
            if (!gres.region.isEmpty()) o.insert(QStringLiteral("r"), gres.region);
            if (!gres.prov.isEmpty())   o.insert(QStringLiteral("pv"), gres.prov);
            if (!gres.city.isEmpty())   o.insert(QStringLiteral("ct"), gres.city);
        }
        r.exif = o;
        r.thumb = thumbName;
        pending.append(r);
        if (r.isUpdate) ++res.updated; else ++res.added;
        flush(false);
        bump(done);
    }
    flush(true);   // 收尾：不足一批的尾部（含取消中断的场景）

    // ---- 4) 剪枝：消失的标 missing=1；重新出现的复位 0（library.py prune 同款）
    if (!cancelled || !cancelled()) {
        QVector<QString> gone;
        {
            QSqlQuery q(db);
            q.prepare(QStringLiteral("SELECT path FROM photos WHERE root=? AND missing=0"));
            q.addBindValue(rootNative);
            q.exec();
            while (q.next())
                if (!seen.contains(q.value(0).toString()))
                    gone.append(q.value(0).toString());
        }
        if (!gone.isEmpty()) {
            QSqlQuery q(db);
            db.transaction();
            q.prepare(QStringLiteral("UPDATE photos SET missing=1 WHERE path=?"));
            for (const QString& p : gone) { q.addBindValue(p); q.exec(); q.finish(); }
            db.commit();
            res.missing = gone.size();
        }
        if (!seen.isEmpty()) {
            QSqlQuery q(db);
            db.transaction();
            q.prepare(QStringLiteral("UPDATE photos SET missing=0 WHERE path=?"));
            for (const QString& p : seen) { q.addBindValue(p); q.exec(); q.finish(); }
            db.commit();
        }
    }

    // meta last_sync
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO meta(key,value) VALUES(?,?) "
            "ON CONFLICT(key) DO UPDATE SET value=excluded.value"));
        q.addBindValue(QStringLiteral("last_sync:%1").arg(rootNative));
        q.addBindValue(QString::number(double(QDateTime::currentMSecsSinceEpoch()) / 1000.0));
        q.exec();
    }

    db.close();
    QSqlDatabase::removeDatabase(QStringLiteral("la_scan"));
    res.seconds = double(timer.elapsed()) / 1000.0;
    if (progress) progress(res.total, res.total);
    return res;
}
