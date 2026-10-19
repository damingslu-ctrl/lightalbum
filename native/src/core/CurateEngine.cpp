// 2.42.0 波3 策展引擎实现（spec §2.2/§2.3）。纯函数，见头文件铁律说明。
#include "CurateEngine.h"
#include <QSet>

namespace Curate {

const QVector<TemplateSpec>& templateTable()
{
    // 静态只读；序即 spec §2.3 表序（= V6 §三）。nameKey 为 .ts 展名 key 字面量，
    // sourceId 为数据源 token（非空；缺源即 skip 的判定前提）。方向仅 BadShotSweep 为 Down。
    static const QVector<TemplateSpec> kTable = {
        { TemplateId::OnThisDay,     QStringLiteral("curate.tmpl.onThisDay"),     QStringLiteral("year+date"),              Direction::Up },
        { TemplateId::PersonAnniv,   QStringLiteral("curate.tmpl.personAnniv"),   QStringLiteral("person*year"),            Direction::Up },
        { TemplateId::HiddenReunion, QStringLiteral("curate.tmpl.hiddenReunion"), QStringLiteral("exhibitHist&seenAt"),     Direction::Up },
        { TemplateId::CitySlice,     QStringLiteral("curate.tmpl.citySlice"),     QStringLiteral("city&seenAt"),            Direction::Up },
        { TemplateId::HuePilgrimage, QStringLiteral("curate.tmpl.huePilgrimage"), QStringLiteral("category"),               Direction::Up },
        { TemplateId::MonoHour,      QStringLiteral("curate.tmpl.monoHour"),      QStringLiteral("cat:mono"),               Direction::Up },
        { TemplateId::BeyondBurst,   QStringLiteral("curate.tmpl.beyondBurst"),   QStringLiteral("phash&quality"),          Direction::Up },
        { TemplateId::TravelReel,    QStringLiteral("curate.tmpl.travelReel"),    QStringLiteral("gpsSameCityMultiDay"),    Direction::Up },
        { TemplateId::FestivalEcho,  QStringLiteral("curate.tmpl.festivalEcho"),  QStringLiteral("festivalCal&history"),    Direction::Up },
        { TemplateId::NewIntake,     QStringLiteral("curate.tmpl.newIntake"),     QStringLiteral("seenAt"),                 Direction::Up },
        { TemplateId::LightArchive,  QStringLiteral("curate.tmpl.lightArchive"),  QStringLiteral("catUexposure"),           Direction::Up },
        { TemplateId::BadShotSweep,  QStringLiteral("curate.tmpl.badShotSweep"),  QStringLiteral("burst&blur&stale"),       Direction::Down },
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

}   // namespace Curate
