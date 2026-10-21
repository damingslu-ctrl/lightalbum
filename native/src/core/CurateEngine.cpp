// 2.42.0 波3 策展引擎实现（spec §2.2/§2.3）。纯函数，见头文件铁律说明。
#include "CurateEngine.h"
#include "CullEngine.h"   // blurScore/exposureStats/computePhash/hamming（画质唯一真源）
#include "RawDecode.h"     // isRawExt（RAW 判定的唯一真源，禁另立扩展名表，铁律 6）
#include <QCoreApplication>
#include <QSet>
#include <QHash>
#include <QImage>
#include <QDir>
#include <QFileInfo>
#include <algorithm>
#include <atomic>

namespace {

// 门禁探针（LA_CURATE_QA W 组读）：解码成本红线可测化。计数成本可忽略于解码本身。
std::atomic<int>  g_probeQCalls{0};
std::atomic<int>  g_probeHCalls{0};
std::atomic<bool> g_qaForceFullScan{false};


// 键空间辅助：取某前缀下字典序最小的键（确定性——QHash 迭代序不定，故显式取 min）。
QString firstKeyWithPrefix(const Curate::Buckets& bk, const QString& prefix)
{
    QString best;
    for (auto it = bk.byKey.cbegin(); it != bk.byKey.cend(); ++it) {
        const QString& k = it.key();
        if (!k.startsWith(prefix)) continue;
        if (best.isEmpty() || k < best) best = k;
    }
    return best;
}

// 把若干桶键的下标并集（去重、保序）收集进 out。
void collectIndices(const Curate::Buckets& bk, const QStringList& keys, QVector<int>& out)
{
    QSet<int> seen;
    for (const QString& k : keys) {
        const auto it = bk.byKey.constFind(k);
        if (it == bk.byKey.constEnd()) continue;
        for (int ix : it.value()) {
            if (seen.contains(ix)) continue;
            seen.insert(ix);
            out.push_back(ix);
        }
    }
}

// 展名模板（`.ts` 的 source = 中文模板，`%1` = 展内张数）。
QString nameTmplFor(Curate::TemplateId id)
{
    for (const Curate::TemplateSpec& s : Curate::templateTable())
        if (s.id == id) return s.nameTmpl;
    return QString();
}

// 展名模板 → 展签成品文案。**先查目录再填数**：`%1` 一旦先被替换，source 就变了，
//   英文目录里永远查不到这一条（这是 T9/T10 展签漏出裸键之后定下的顺序）。
//   张数只有到 compose 定稿才等于「真展出的张数」，所以本函数只由 compose 调用，
//   propose 阶段 title 留空（见 Curate::propose / Curate::compose 注释）。
QString displayTitleFor(Curate::TemplateId id, int n)
{
    const QString tmpl = nameTmplFor(id);
    if (tmpl.isEmpty()) return tmpl;        // 表里没这行 → 空串，不编一个名字（铁律 21）
    // 线程口径：翻译器在 main() 启动时一次性 install，此后运行期不再增删；
    //   Qt 的目录查询走读写锁，策展 worker 上的只读查询与 GUI 线程不冲突。
    return QCoreApplication::translate("LightAlbum", tmpl.toUtf8().constData()).arg(n);
}

// v1 固定公历节日表（spec §5.6：农历节日不做，如实记）。
bool isFixedHoliday(int m, int d)
{
    return (m == 1 && d == 1)      // 元旦
        || (m == 5 && d == 1)      // 劳动节
        || (m == 10 && d == 1)     // 国庆
        || (m == 12 && d == 25);   // 圣诞
}

// 缩略图 pHash（带进程内缓存，避免 compose 多次取同一帧）；不可读 → 0（视为不重复）。
quint64 thumbPhash(const Photo& p, const QString& thumbsDir, QHash<QString, quint64>& cache)
{
    const QString tp = QDir(thumbsDir).filePath(p.thumbFile);
    const auto it = cache.constFind(tp);
    if (it != cache.constEnd()) return it.value();
    const QImage img(tp);
    const quint64 h = img.isNull() ? 0 : CullEngine::computePhash(img, 8);
    g_probeHCalls.fetch_add(1, std::memory_order_relaxed);   // 仅真实解码计数；命中缓存不计
    cache.insert(tp, h);
    return h;
}

}   // namespace

namespace Curate {

const QVector<TemplateSpec>& templateTable()
{
    // 静态只读；序即 spec §2.3 表序（= V6 §三）。nameTmpl 是 `.ts` 的 source（中文展名模板，
    // `%1` = 展内张数，由 compose 先 translate 再 arg 填数），sourceId 为数据源 token
    // （非空；缺源即 skip 的判定前提）。方向仅 BadShotSweep 为 Down。
    // ★ 展名一律**只说这一版真选出来的东西**：v1 降级的模板（OnThisDay 取当年桶、
    //   HuePilgrimage 取最大 category 桶、BadShotSweep 取 >2 年陈帧）不写 spec 原名的
    //   「那年今天 / 满墙皆青 / 可以退藏」——那是数据源尚未接满时的夸大（铁律 21）。
    static const QVector<TemplateSpec> kTable = {
        { TemplateId::OnThisDay,     QStringLiteral("今年 · %1 张"),        QStringLiteral("year+date"),              Direction::Up },
        { TemplateId::PersonAnniv,   QStringLiteral("旧识 · %1 张"),        QStringLiteral("person*year"),            Direction::Up },
        { TemplateId::HiddenReunion, QStringLiteral("搁了一年多 · %1 张"),  QStringLiteral("exhibitHist&seenAt"),     Direction::Up },
        { TemplateId::CitySlice,     QStringLiteral("一城 · %1 张"),        QStringLiteral("city&seenAt"),            Direction::Up },
        { TemplateId::HuePilgrimage, QStringLiteral("同一门类 · %1 张"),    QStringLiteral("category"),               Direction::Up },
        { TemplateId::MonoHour,      QStringLiteral("黑白之间 · %1 张"),    QStringLiteral("cat:mono"),               Direction::Up },
        { TemplateId::BeyondBurst,   QStringLiteral("连拍那天 · %1 张"),    QStringLiteral("phash&quality"),          Direction::Up },
        { TemplateId::TravelReel,    QStringLiteral("在路上 · %1 张"),      QStringLiteral("gpsSameCityMultiDay"),    Direction::Up },
        { TemplateId::FestivalEcho,  QStringLiteral("那年今日 · %1 张"),    QStringLiteral("festivalCal&history"),    Direction::Up },
        { TemplateId::NewIntake,     QStringLiteral("新入藏 · %1 张"),      QStringLiteral("seenAt"),                 Direction::Up },
        { TemplateId::LightArchive,  QStringLiteral("夜色与远山 · %1 张"),  QStringLiteral("catUexposure"),           Direction::Up },
        { TemplateId::BadShotSweep,  QStringLiteral("搁下两年 · %1 张"),    QStringLiteral("burst&blur&stale"),       Direction::Down },
    };
    return kTable;
}

QVector<const Photo*> candidatePool(const QVector<Photo>& all, bool hidePrivate,
                                    const QStringList& recentExhibitPaths, const Now& now)
{
    Q_UNUSED(now);   // v1：近 4 周已展出由调用方按展出时间戳预算后传入；此函数不做时间判定
    QVector<const Photo*> pool;
    pool.reserve(all.size());
    for (const Photo& p : all) {
        if (hidePrivate && p.priv) continue;                 // 私密照不入池（铁律 21）
        if (recentExhibitPaths.contains(p.path)) continue;   // 近 4 周已展出不入池
        pool.push_back(&p);
    }
    return pool;
}

Buckets bucket(const QVector<const Photo*>& pool)
{
    Buckets bk;
    for (int i = 0; i < pool.size(); ++i) {
        const Photo* p = pool[i];
        if (p == nullptr) continue;
        if (!p->category.isEmpty())
            bk.byKey[QStringLiteral("cat:") + p->category].push_back(i);
        if (!p->person.isEmpty()) {
            const QStringList toks = p->person.split(QLatin1Char(','), Qt::SkipEmptyParts);
            for (const QString& t : toks) {
                const QString tt = t.trimmed();
                if (!tt.isEmpty())
                    bk.byKey[QStringLiteral("person:") + tt].push_back(i);
            }
        }
        if (p->year > 0)
            bk.byKey[QStringLiteral("y:") + QString::number(p->year)].push_back(i);
        if (p->date.size() >= 7)   // "YYYY-MM-DD" → "YYYY-MM"
            bk.byKey[QStringLiteral("ym:") + p->date.left(7)].push_back(i);
        if (!p->city.isEmpty())
            bk.byKey[QStringLiteral("city:") + p->city].push_back(i);
        if (!p->mark.isEmpty())
            bk.byKey[QStringLiteral("tag:") + p->mark].push_back(i);
    }
    return bk;
}

const char* templateIdToken(TemplateId id)
{
    switch (id) {
    case TemplateId::OnThisDay:     return "OnThisDay";
    case TemplateId::PersonAnniv:   return "PersonAnniv";
    case TemplateId::HiddenReunion: return "HiddenReunion";
    case TemplateId::CitySlice:     return "CitySlice";
    case TemplateId::HuePilgrimage: return "HuePilgrimage";
    case TemplateId::MonoHour:      return "MonoHour";
    case TemplateId::BeyondBurst:   return "BeyondBurst";
    case TemplateId::TravelReel:    return "TravelReel";
    case TemplateId::FestivalEcho:  return "FestivalEcho";
    case TemplateId::NewIntake:     return "NewIntake";
    case TemplateId::LightArchive:  return "LightArchive";
    case TemplateId::BadShotSweep:  return "BadShotSweep";
    }
    return "Unknown";
}

QVector<TemplateId> coldStartTemplates(int totalCount)
{
    if (totalCount < 50) return {};                 // 冷库：不成展（Exhibit::valid 由调用方置 false）
    if (totalCount < 200)                           // 温库：仅两枚低门槛模板
        return { TemplateId::NewIntake, TemplateId::HiddenReunion };
    QVector<TemplateId> all;                        // 热库：全 12 枚，枚举/表序
    for (const TemplateSpec& s : templateTable()) all.push_back(s.id);
    return all;
}

QVector<Theme> propose(TemplateId id, const Buckets& bk, const QVector<const Photo*>& pool,
                       const Now& now)
{
    // 每个模板各一支：从**现成数据源**取候选帧下标，缺源即返回空（绝不编造，R7）。
    // 数据源不可纯算者按 spec §2.3 的 v1 降级规则处理（HuePilgrimage/TravelReel 已如实记，
    // 其余：HiddenReunion/BeyondBurst/FestivalEcho/LightArchive 的画质/主色/exposure 部分
    // 留到 compose（R7：画质只对已选出帧算），此处只用可见字段的近似子集）。
    QVector<int> ix;
    QStringList topics{QStringLiteral("tmpl:") + QLatin1String(templateIdToken(id))};

    switch (id) {
    case TemplateId::OnThisDay: {
        const QString k = QStringLiteral("y:") + QString::number(now.y);
        if (!bk.byKey.contains(k)) return {};
        collectIndices(bk, {k}, ix);
        topics << k;
        break;
    }
    case TemplateId::PersonAnniv: {
        const QString pk = firstKeyWithPrefix(bk, QStringLiteral("person:"));
        if (pk.isEmpty()) return {};
        collectIndices(bk, {pk}, ix);
        topics << pk;                                // "person:X" —— 供频率/冷却判定
        break;
    }
    case TemplateId::HiddenReunion: {
        const qint64 yr = 365LL * 86400;             // 「一直没被看见」：入库 >1 年
        for (int i = 0; i < pool.size(); ++i)
            if (pool[i] && pool[i]->seenAt > 0 && now.epochSec - pool[i]->seenAt > yr)
                ix.push_back(i);
        break;
    }
    case TemplateId::CitySlice: {
        const QString ck = firstKeyWithPrefix(bk, QStringLiteral("city:"));
        if (ck.isEmpty()) return {};                 // 池内无城市 → 缺源，返回空
        collectIndices(bk, {ck}, ix);
        topics << ck;
        break;
    }
    case TemplateId::HuePilgrimage: {
        // v1 降级（spec §2.3）：仅按 category 出题，主色聚类留后。
        QString best;
        for (auto it = bk.byKey.cbegin(); it != bk.byKey.cend(); ++it) {
            if (!it.key().startsWith(QStringLiteral("cat:"))) continue;
            if (best.isEmpty()
                || it.value().size() > bk.byKey.value(best).size()
                || (it.value().size() == bk.byKey.value(best).size() && it.key() < best))
                best = it.key();
        }
        if (best.isEmpty()) return {};
        collectIndices(bk, {best}, ix);
        topics << best;
        break;
    }
    case TemplateId::MonoHour: {
        const QString k = QStringLiteral("cat:黑白");
        if (!bk.byKey.contains(k)) return {};
        collectIndices(bk, {k}, ix);
        topics << k;
        break;
    }
    case TemplateId::BeyondBurst: {
        // v1 降级：同日 ≥3 帧视为连拍冗余邻域（phash 邻域留后，R7）。
        QHash<QString, QVector<int>> byDay;
        for (int i = 0; i < pool.size(); ++i)
            if (pool[i] && pool[i]->date.size() >= 10) byDay[pool[i]->date].push_back(i);
        QString best;
        for (auto it = byDay.cbegin(); it != byDay.cend(); ++it)
            if (it.value().size() >= 3 && (best.isEmpty() || it.key() < best)) best = it.key();
        if (best.isEmpty()) return {};
        ix = byDay.value(best);
        break;
    }
    case TemplateId::TravelReel: {
        // v1 降级（spec §2.3）：仅「同城跨日 ≥2 天」，轨迹连续段留后。
        QString best;
        for (auto it = bk.byKey.cbegin(); it != bk.byKey.cend(); ++it) {
            if (!it.key().startsWith(QStringLiteral("city:"))) continue;
            QSet<QString> days;
            for (int i : it.value()) if (pool[i]) days.insert(pool[i]->date);
            if (days.size() < 2) continue;
            if (best.isEmpty() || it.key() < best) best = it.key();
        }
        if (best.isEmpty()) return {};
        collectIndices(bk, {best}, ix);
        topics << best;
        break;
    }
    case TemplateId::FestivalEcho: {
        if (!isFixedHoliday(now.m, now.d)) return {};   // 非固定公历节日 → 无题
        const QString md = QStringLiteral("-")
                           + QString::number(now.m).rightJustified(2, QLatin1Char('0'))
                           + QLatin1Char('-')
                           + QString::number(now.d).rightJustified(2, QLatin1Char('0'));
        for (int i = 0; i < pool.size(); ++i)
            if (pool[i] && pool[i]->date.size() >= 10 && pool[i]->date.endsWith(md))
                ix.push_back(i);
        break;
    }
    case TemplateId::NewIntake: {
        const qint64 win = 14LL * 86400;                // 最近一次入库批次（R6：seenAt=入库时间）
        for (int i = 0; i < pool.size(); ++i)
            if (pool[i] && pool[i]->seenAt > 0 && now.epochSec - pool[i]->seenAt < win)
                ix.push_back(i);
        break;
    }
    case TemplateId::LightArchive: {
        collectIndices(bk, {QStringLiteral("cat:夜景·星空"), QStringLiteral("cat:风光")}, ix);
        break;
    }
    case TemplateId::BadShotSweep: {
        // v1：陈旧帧代理（burst&blur&stale 的完整巡检见 inspectBadShots，T6）；
        // 方向 Down —— 唯一「可退藏」候选。
        const qint64 stale = 730LL * 86400;
        for (int i = 0; i < pool.size(); ++i)
            if (pool[i] && pool[i]->seenAt > 0 && now.epochSec - pool[i]->seenAt > stale)
                ix.push_back(i);
        break;
    }
    }

    QVector<Theme> out;
    if (ix.isEmpty()) return out;                        // 缺源 → 空（调用方置 skipReason）
    Theme t;
    t.id = id;
    t.topicKeys = topics;
    t.paths.reserve(ix.size());
    for (int i : ix)
        if (i >= 0 && i < pool.size() && pool[i]) t.paths.push_back(pool[i]->path);
    if (t.paths.isEmpty()) return out;
    // title **不在此处填**：候选帧数不等于展出帧数（compose 还要裁到 5..9 并去冗余），
    //   拿候选数当展名张数 = 夸大（铁律 21）。展名由 compose 在定稿后产出。
    t.reasons << QStringLiteral("n=") + QString::number(t.paths.size());   // 模板拼接参数（T9 消费）
    out.push_back(t);
    return out;
}

double score(const Theme& t, const Weights& w, const ExhibitHistory& h, const Now& now)
{
    // 新鲜感：从未展出 = 1.0（最高）；否则 d/(d+365) —— 严格单调、渐近 1、恒 < 1。
    // 取主题各 topicKey 中**最近一次**展出（最小 d），使「任一相关题近期展过」即降新鲜度。
    double daysSince = -1.0;
    for (const QString& k : t.topicKeys) {
        const auto it = h.lastShownByKey.constFind(k);
        if (it == h.lastShownByKey.constEnd() || it.value() <= 0) continue;
        const double d = qMax(0.0, double(now.epochSec - it.value()) / 86400.0);
        if (daysSince < 0.0 || d < daysSince) daysSince = d;
    }
    const double fresh = (daysSince < 0.0) ? 1.0 : daysSince / (daysSince + 365.0);

    // 重复惩罚：60 天窗内线性衰减（越近期越高）。
    double repeat = 0.0;
    for (const QString& k : t.topicKeys) {
        const auto it = h.lastShownByKey.constFind(k);
        if (it == h.lastShownByKey.constEnd() || it.value() <= 0) continue;
        const double d = qMax(0.0, double(now.epochSec - it.value()) / 86400.0);
        if (d < 60.0) repeat += (60.0 - d) / 60.0;
    }

    // ★ R2：偏好项恒 0 —— 式内**不出现**任何偏好/收藏信号，`w.pref` 亦不乘。
    // ★ R7：画质不在此式（只在 compose 的展内排序/主图选择）。
    Q_UNUSED(h.shownPaths);   // 显式声明：收藏/展出明细不得影响全局选题分
    return w.fresh * fresh + 0.0 - w.repeat * repeat;
}

bool frequencyAllows(const Theme& t, const ExhibitHistory& h, const MuteState& m, const Now& now)
{
    // 1) 静音（全局或模板级）→ 一律不放行。
    if (m.globallyMuted) return false;
    if (m.mutedTemplates.contains(QString::fromLatin1(templateIdToken(t.id)))) return false;
    // 2) 周频控：每周 ≤1（距上次成展 < 7 天即本周已用）。
    if (h.lastExhibitAt != 0 && (now.epochSec - h.lastExhibitAt) < 7LL * 86400) return false;
    // 3) 同人 60 天冷却（只对 "person:X" 键判定）。
    for (const QString& k : t.topicKeys) {
        if (!k.startsWith(QStringLiteral("person:"))) continue;
        const auto it = h.lastShownByKey.constFind(k);
        if (it == h.lastShownByKey.constEnd() || it.value() <= 0) continue;
        if ((now.epochSec - it.value()) < 60LL * 86400) return false;
    }
    return true;
}

double qualityScore(const Photo& p, const QString& thumbsDir)
{
    g_probeQCalls.fetch_add(1, std::memory_order_relaxed);
    // 复用 CullEngine 的同一对本地维度算子（唯一真源，R7），归一到 [0,1]：
    //   清晰 = min(blur / blurThreshold, 1)（80 = CullOptions::blurThreshold 同锚）；
    //   光影 = 1 − min(max(欠曝比, 过曝比) / 0.45, 1)。
    const QImage img(QDir(thumbsDir).filePath(p.thumbFile));
    if (img.isNull()) return -1.0;            // 失败即未知（不编造，与 CullEngine 失败即放行同源）
    QImage gray = img.convertToFormat(QImage::Format_Grayscale8);
    const int m = std::max(gray.width(), gray.height());
    if (m > 512) {
        const double s = 512.0 / m;
        gray = gray.scaled(int(gray.width() * s), int(gray.height() * s),
                           Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    double mean = 0, under = 0, over = 0;
    QString verdict;
    CullEngine::exposureStats(gray, mean, under, over, verdict);
    // ★ blurScore 的「无法评估」哨兵是 999.0（非 Grayscale8 / 边长<3），而真实锐图的
    //   Laplacian 方差可远超 999（棋盘即 57600）——故**不能**用 `blur >= 999` 判不可评估，
    //   否则把高方差锐图误零。这里已自控格式（convertToFormat）且非 null，唯一剩余失败面是
    //   边长 <3；按此**前置条件**判定，与 CullEngine.cpp:226 的 img.isNull() 分派同源。
    const bool evaluable = (gray.width() >= 3 && gray.height() >= 3);
    const double blur = evaluable ? CullEngine::blurScore(gray) : 0.0;
    const double sharp = std::min(blur / 80.0, 1.0);
    const double light = 1.0 - std::min(std::max(under, over) / 0.45, 1.0);
    return 0.5 * sharp + 0.5 * light;
}

Theme compose(const Theme& t, const QVector<Photo>& all, const QString& thumbsDir)
{
    // ★ 成本红线（spec R7「O(5–9)」+ §5.2「O(展内张数)，不得对全库」）：**先廉价选展帧**
    //   （零解码，只用 Photo 内存字段），画质分与 pHash 只在**已选出的展帧**上算 ——
    //   主题候选再多（大库一人 2000+ 帧）也不允许放大成全库逐张解码（T5 曾按候选帧
    //   全量解码，2090 帧 ≈ 5.3 s，本红线门 W 组守此界）。廉价序 = 拍摄日期新→旧、
    //   path 升序收尾（确定性，禁依赖容器迭代序）；类内多样 = pHash 邻域去冗余，
    //   解码封顶 2n —— 超顶后停解码、余位按廉价序补齐（可能含近邻帧，如实记）。
    Theme out = t;
    QHash<QString, const Photo*> ix;
    ix.reserve(all.size());
    for (const Photo& p : all) ix.insert(p.path, &p);

    QVector<const Photo*> avail;
    avail.reserve(t.paths.size());
    {
        QSet<QString> seenPath;
        for (const QString& path : t.paths) {
            if (seenPath.contains(path)) continue;
            seenPath.insert(path);
            const Photo* p = ix.value(path, nullptr);
            if (p) avail.push_back(p);
        }
    }

    out.paths.clear();
    out.primaryPath.clear();
    if (avail.isEmpty()) return out;           // 无可用帧 → 空（不伪造主图）

    // 反例臂（LA_CURATE_QA_FORCE_BROKEN_Q=1）：复现 T5 误读的旧形态 —— 对**全候选**先算
    //   画质再裁展（超 R7 成本红线），W_quality_bounded 必红（40 候选 → 40 次调用 > 9）。
    if (g_qaForceFullScan.load())
        for (const Photo* p : avail)
            qualityScore(*p, thumbsDir);

    std::sort(avail.begin(), avail.end(), [](const Photo* a, const Photo* b) {
        if (a->date != b->date) return a->date > b->date;   // 新片先看（date 升序串 = 时序）
        return a->path < b->path;
    });

    const int n = qBound(5, avail.size(), 9);   // 展内总量裁到 5..9
    QHash<QString, quint64> hcache;
    QVector<const Photo*> kept;
    QVector<quint64> hashes;
    int decoded = 0;
    const int cap = 2 * n;                      // 去冗余解码预算（红线：邻域封顶，非全候选）
    for (const Photo* p : avail) {
        if (kept.size() >= n) break;
        quint64 h = 0;
        if (decoded < cap) {
            h = thumbPhash(*p, thumbsDir, hcache);
            ++decoded;
        }
        bool dup = false;
        for (quint64 k : hashes)                 // h==0（不可读/均匀图）不参与去重；0 也不入表
            if (k != 0 && h != 0 && CullEngine::hamming(h, k) <= 8) { dup = true; break; }
        if (dup) continue;
        kept.push_back(p);
        if (h != 0) hashes.push_back(h);
    }
    {   // 去冗余后不足 → 按廉价序回填（≥5 保底语义与去重前一致）
        QSet<QString> keptSet;
        keptSet.reserve(kept.size());
        for (const Photo* p : kept) keptSet.insert(p->path);
        for (const Photo* p : avail) {
            if (kept.size() >= n) break;
            if (keptSet.contains(p->path)) continue;
            keptSet.insert(p->path);
            kept.push_back(p);
        }
    }

    // 展内排序 = w3·画质（唯一消费 quality 的地方，R7）：仅对**已选出的 ≤9 帧**算分。
    struct Scored { const Photo* p; double q; };
    QVector<Scored> scored;
    scored.reserve(kept.size());
    for (const Photo* p : kept) scored.push_back({p, qualityScore(*p, thumbsDir)});
    // 画质降序；同分按 path 升序（确定性，禁依赖容器序）。
    std::sort(scored.begin(), scored.end(), [](const Scored& a, const Scored& b) {
        if (a.q != b.q) return a.q > b.q;
        return a.p->path < b.p->path;
    });

    for (const Scored& s : scored) out.paths.push_back(s.p->path);
    out.primaryPath = scored.constFirst().p->path;
    // 定稿帧数已知 → 此处才产展名（本地化 + 填数；见 displayTitleFor）。
    out.title = displayTitleFor(t.id, out.paths.size());
    return out;
}

BadShotBatch inspectBadShots(const QVector<Photo>& all, const QString& thumbsDir, const Now& now)
{
    // ★ 红线 3：只打包，永不删除 —— 本函数**无任何删除/写库调用**（签名只收 const）。
    //   返回的记录由 UI 侧 setMarks 落「待点头」标记，用户可整批反悔（spec §3 I 组）。
    BadShotBatch batch;
    batch.builtAt = now.epochSec;

    // —— 规则 1：连拍冗余（phash 邻域内 >2 帧 = 冗余）——
    //   邻域判据承 CullEngine::computePhash/hamming（阈值 8 = CullOptions::dedupThreshold）。
    //   缩略图不可读 → hash 0（不参与冗余，绝不用 0 当「全等」）。
    QHash<QString, quint64> hcache;
    QVector<quint64> hashes(all.size(), 0);
    for (int i = 0; i < all.size(); ++i)
        hashes[i] = thumbPhash(all[i], thumbsDir, hcache);
    QVector<bool> isBurst(all.size(), false);
    for (int i = 0; i < all.size(); ++i) {
        if (hashes[i] == 0) continue;
        int near = 0;
        for (int j = 0; j < all.size(); ++j)
            if (j != i && hashes[j] != 0 && CullEngine::hamming(hashes[i], hashes[j]) <= 8) ++near;
        if (near + 1 > 2) isBurst[i] = true;          // 邻域（含自身）>2
    }

    // —— 规则 2：糊图（Laplacian 方差 < blurThreshold=80，承 CullEngine.h:88）——
    //   ★ RAW/视频豁免（承 CullEngine.h:9-13 铁律：缩略图不代表其真实画质，与选片页同源）。
    //   ★ 不可评估（不可读 / 边长<3）→ 不判糊（失败即放行，与 CullEngine 同源）。
    QVector<bool> isBlurry(all.size(), false);
    for (int i = 0; i < all.size(); ++i) {
        const Photo& p = all[i];
        if (p.mediaType == QStringLiteral("video")) continue;
        if (RawDecode::isRawExt(p.path)) continue;
        const QImage img(QDir(thumbsDir).filePath(p.thumbFile));
        if (img.isNull()) continue;
        const QImage gray = img.convertToFormat(QImage::Format_Grayscale8);
        if (gray.width() < 3 || gray.height() < 3) continue;
        if (CullEngine::blurScore(gray) < 80.0) isBlurry[i] = true;
    }

    // —— 规则 3：截图过期（文件名启发 + 入库逾 730 天）——
    //   Photo 无像素尺寸（仅 dim 字串），故 v1 只用文件名启发（spec §2.3 v1 降级惯例，如实记）。
    QVector<bool> isStale(all.size(), false);
    for (int i = 0; i < all.size(); ++i) {
        const Photo& p = all[i];
        const QString base = QFileInfo(p.path).fileName().toLower();
        const bool shotish = base.contains(QStringLiteral("screenshot"))
                          || base.contains(QString::fromUtf8("截屏"))
                          || base.contains(QString::fromUtf8("屏幕快照"));
        if (shotish && p.seenAt > 0 && now.epochSec - p.seenAt > 730LL * 86400)
            isStale[i] = true;
    }

    // 每条恰录一次，理由优先序 burst > blurry > stale-shot（确定性，禁依赖容器序）。
    for (int i = 0; i < all.size(); ++i) {
        QString why;
        if (isBurst[i])       why = QStringLiteral("burst");
        else if (isBlurry[i]) why = QStringLiteral("blurry");
        else if (isStale[i])  why = QStringLiteral("stale-shot");
        if (!why.isEmpty()) batch.items.push_back(BadShotRecord{ all[i].path, why });
    }
    return batch;
}

QStringList badShotWhy(const BadShotBatch& batch)
{
    QStringList out;
    out.reserve(batch.items.size());
    for (const BadShotRecord& r : batch.items) out << r.why;
    return out;
}

void qaResetScanProbe() { g_probeQCalls.store(0); g_probeHCalls.store(0); }
int  qaQualityCalls()   { return g_probeQCalls.load(); }
int  qaHashCalls()      { return g_probeHCalls.load(); }
void qaSetForceFullScan(bool on) { g_qaForceFullScan.store(on); }

}   // namespace Curate
