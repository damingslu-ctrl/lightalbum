// 自动故事相册生成器（人物故事方向·B）。见 StoryGen.h 设计说明。
#include "StoryGen.h"
#include <QCoreApplication>
#include <algorithm>

namespace StoryGen {
namespace {
bool gForceDrop = false;
} // namespace

void setForceDropForQa(bool on) { gForceDrop = on; }
bool forceDropForQa()           { return gForceDrop; }

// 地点键：prov 优先 → city → region → 未知地点（与 PhotoCluster 的诚实原则一致）
static QString placeKey(const Photo& p)
{
    if (!p.prov.isEmpty())   return p.prov;
    if (!p.city.isEmpty())   return p.city;
    if (!p.region.isEmpty()) return p.region;
    return QStringLiteral("未知地点");
}

// 解析拍摄日（复用 MapPage 既有写法，禁猜）：left(10) + yyyy-MM-dd / yyyy:MM:dd / 全长
static QDate parseDate(const Photo& p)
{
    const QString s = p.date;
    QDate d = QDate::fromString(s.left(10), QStringLiteral("yyyy-MM-dd"));
    if (!d.isValid()) d = QDate::fromString(s.left(10), QStringLiteral("yyyy:MM:dd"));
    if (!d.isValid()) d = QDate::fromString(s, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    return d;
}

// 故事标题见 StoryGen.h 注释。铁律 16：面向用户的串必须以 translate() + 字面量
// 直调，lupdate 才抽得到（本模块虽在 core，PhotoDedup 已有同例）。
QString titleFor(const QString& place, const QDate& begin, const QDate& end)
{
    if (!begin.isValid() || !end.isValid())
        return QCoreApplication::translate("LightAlbum", "%1 · 未标注日期").arg(place);
    if (begin == end)
        return QCoreApplication::translate("LightAlbum", "%1 · %2年%3月%4日")
            .arg(place).arg(begin.year()).arg(begin.month()).arg(begin.day());
    if (begin.year() == end.year()) {
        if (begin.month() == end.month())
            return QCoreApplication::translate("LightAlbum", "%1 · %2年%3月")
                .arg(place).arg(begin.year()).arg(begin.month());
        return QCoreApplication::translate("LightAlbum", "%1 · %2年%3月-%4月")
            .arg(place).arg(begin.year()).arg(begin.month()).arg(end.month());
    }
    return QCoreApplication::translate("LightAlbum", "%1 · %2年%3月-%4年%5月")
        .arg(place).arg(begin.year()).arg(begin.month())
        .arg(end.year()).arg(end.month());
}

Result build(const QVector<Photo>& photos, int gapDays, bool hidePrivate)
{
    Result res;
    res.inputCount = 0;

    // 1) 收集有效照片（跳过私密），按 地点 分桶
    struct Entry { QString path; QDate date; bool hasDate; };
    QHash<QString, QVector<Entry>> byPlace;
    for (const Photo& p : photos) {
        if (hidePrivate && p.priv) continue;
        Entry e;
        e.path    = p.path;
        e.date    = parseDate(p);
        e.hasDate = e.date.isValid();
        ++res.inputCount;
        byPlace[placeKey(p)].append(e);
    }

    // 2) 每个地点：按日期升序；同地点相邻间隔 > gapDays（或无日期）则切新故事
    for (auto it = byPlace.begin(); it != byPlace.end(); ++it) {
        const QString place = it.key();
        QVector<Entry> ents = it.value();
        std::sort(ents.begin(), ents.end(), [](const Entry& a, const Entry& b) {
            if (a.hasDate != b.hasDate) return a.hasDate > b.hasDate; // 有日期排前
            if (!a.hasDate) return a.path < b.path;                  // 无日期按路径稳定排
            return a.date < b.date;
        });

        QVector<Entry> cur;
        QDate curBegin, curEnd, lastDate;
        auto flush = [&]() {
            if (cur.isEmpty()) return;
            Story s;
            s.place  = place;
            s.begin  = curBegin;
            s.end    = curEnd;
            s.title  = titleFor(place, curBegin, curEnd);
            s.id     = QStringLiteral("%1|%2").arg(place).arg(curBegin.toString("yyyy-MM-dd"));
            for (const Entry& e : cur) s.paths.append(e.path);
            s.photoCount = s.paths.size();
            res.stories.append(s);
            cur.clear();
        };

        for (const Entry& e : ents) {
            if (!e.hasDate) {
                // 无日期：单独成一条「未标注日期」故事（仍诚实保留，不丢弃）
                flush();
                Story s;
                s.place = place;
                s.begin = QDate();
                s.end   = QDate();
                s.title = titleFor(place, QDate(), QDate());
                s.id    = QStringLiteral("%1|nodate|%2").arg(place).arg(e.path);
                s.paths.append(e.path);
                s.photoCount = 1;
                res.stories.append(s);
                continue;
            }
            if (cur.isEmpty()) {
                cur.append(e); curBegin = curEnd = lastDate = e.date;
            } else if (lastDate.daysTo(e.date) <= gapDays) {
                cur.append(e); curEnd = lastDate = e.date;   // 同一行程：并入
            } else {
                flush();                                     // 间隔过大：新行程
                cur.append(e); curBegin = curEnd = lastDate = e.date;
            }
        }
        flush();
    }

    // 3) 守恒计数
    res.outputCount = 0;
    for (const Story& s : res.stories) res.outputCount += s.photoCount;

    // 4) QA 反例：丢弃最后一张照片（守恒被破坏，供 LA_STORY_QA 验证断言非空心）
    if (gForceDrop) {
        // 优先从「未标注日期」单张故事摘掉（若存在），否则从最后一个故事摘最后一张
        int nodate = -1;
        for (int i = 0; i < res.stories.size(); ++i)
            if (res.stories[i].id.contains(QStringLiteral("|nodate|"))) { nodate = i; break; }
        if (nodate >= 0 && res.stories[nodate].photoCount == 1) {
            res.stories.removeAt(nodate);
        } else if (!res.stories.isEmpty()) {
            Story& s = res.stories.last();
            if (!s.paths.isEmpty()) {
                s.paths.removeLast();
                --s.photoCount;
                if (s.paths.isEmpty()) res.stories.removeLast();
            }
        }
        res.outputCount = 0;
        for (const Story& s : res.stories) res.outputCount += s.photoCount;
    }

    return res;
}

} // namespace StoryGen
