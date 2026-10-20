// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 LightAlbum Contributors
#include "CurateScheduler.h"
#include <QtConcurrent/QtConcurrentRun>
#include <QMetaObject>
#include <QStringList>
#include <algorithm>
#include <atomic>

namespace {
constexpr qint64 kWeekSec = 7LL * 86400;      // 节流窗（spec R3：>7 天才再生成）
constexpr qint64 kFourWeeksSec = 28LL * 86400; // 候选池减项窗（spec §2.2「近 4 周已展出」）
std::atomic<bool> g_forceInlineForQa{false};
}

bool CurateScheduler::shouldGenerate(qint64 lastAt, const Curate::Now& now)
{
    // 从未生成 → 立即需要；否则严格**逾** 7 天（边界 7 天整不生成，>7 天才生成）。
    return (lastAt == 0) || (now.epochSec - lastAt > kWeekSec);
}

void CurateScheduler::setForceInlineForQa(bool on) { g_forceInlineForQa.store(on); }
bool CurateScheduler::forceInlineForQa() { return g_forceInlineForQa.load(); }

Curate::ExhibitHistory CurateScheduler::historyFrom(const QVector<ExhibitRow>& rows,
                                                    qint64 lastExhibitAt)
{
    Curate::ExhibitHistory h;
    h.lastExhibitAt = lastExhibitAt;
    for (const ExhibitRow& r : rows) {
        // v1：paths 为逗号串（DDL 约定）；文件名含逗号属未支持边界，如实记（spec §4）。
        const QStringList ps = r.paths.split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString& p : ps)
            if (!p.isEmpty()) h.shownPaths << p;
        if (!r.tmpl.isEmpty() && r.shownAt > 0)
            h.lastShownByKey[QStringLiteral("tmpl:") + r.tmpl] = r.shownAt;
    }
    return h;
}

QStringList CurateScheduler::recentShownPaths(const QVector<ExhibitRow>& rows, const Curate::Now& now)
{
    QStringList out;
    for (const ExhibitRow& r : rows) {
        if (r.shownAt <= 0 || now.epochSec - r.shownAt >= kFourWeeksSec) continue;
        const QStringList ps = r.paths.split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString& p : ps)
            if (!p.isEmpty() && !out.contains(p)) out << p;
    }
    return out;
}

Curate::Exhibit CurateScheduler::pickExhibit(const QVector<Photo>& photos, bool hidePrivate,
                                             const Curate::ExhibitHistory& hist,
                                             const Curate::MuteState& mute, const Curate::Now& now,
                                             const QStringList& recentExhibitPaths,
                                             const QString& thumbsDir)
{
    Curate::Exhibit ex;
    // G 冷启动（spec §3）：库 <50 → 不成展。
    const QVector<Curate::TemplateId> allowed = Curate::coldStartTemplates(photos.size());
    if (allowed.isEmpty()) {
        ex.skipReason = QStringLiteral("cold-start");
        return ex;
    }

    const QVector<const Photo*> pool =
        Curate::candidatePool(photos, hidePrivate, recentExhibitPaths, now);
    const Curate::Buckets bk = Curate::bucket(pool);

    struct Cand { Curate::Theme t; double s; };
    QVector<Cand> cands;
    for (Curate::TemplateId id : allowed) {
        const QVector<Curate::Theme> themes = Curate::propose(id, bk, pool, now);
        for (const Curate::Theme& t : themes) {
            if (!Curate::frequencyAllows(t, hist, mute, now)) continue;   // 周频控/同人冷却/静音
            cands.push_back({ t, Curate::score(t, Curate::Weights(), hist, now) });
        }
    }
    if (cands.isEmpty()) {
        ex.skipReason = mute.globallyMuted ? QStringLiteral("muted")
                                           : QStringLiteral("no-source");
        return ex;
    }
    // 确定性排序：分高者先；同分按模板枚举序、再按主题键（禁依赖容器迭代序）。
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
        if (a.s != b.s) return a.s > b.s;
        if (a.t.id != b.t.id) return int(a.t.id) < int(b.t.id);
        return a.t.topicKeys.join(QLatin1Char(',')) < b.t.topicKeys.join(QLatin1Char(','));
    });

    ex.theme = Curate::compose(cands[0].t, photos, thumbsDir);
    ex.generatedAt = now.epochSec;
    ex.valid = !ex.theme.paths.isEmpty();
    if (!ex.valid) ex.skipReason = QStringLiteral("compose-empty");
    return ex;
}

QFuture<void> CurateScheduler::maybeGenerate(const QVector<Photo>& photos, bool hidePrivate,
                                             const QString& thumbsDir, const Curate::Now& now)
{
    if (!lib_) { emit generated(0); return QFuture<void>(); }
    // 节流：未到期 → 不生成（懒生成，spec R3）。读全在调用线程（GUI），worker 只见快照。
    if (!shouldGenerate(lib_->lastExhibitAt(), now)) { emit generated(0); return QFuture<void>(); }

    // 快照（值拷贝）：后台只看到纯数据，**绝不**触 lib_（铁律 ai-single-writer-gui-thread）。
    const QVector<Photo> snap = photos;
    const QVector<ExhibitRow> rows = lib_->loadExhibitHistory();
    const Curate::ExhibitHistory hist = historyFrom(rows, lib_->lastExhibitAt());
    const QStringList recent = recentShownPaths(rows, now);
    Curate::MuteState mute;
    mute.mutedTemplates = lib_->loadMuteState(now.epochSec);
    mute.globallyMuted = false;   // v1 无全局静音持久化（spec §4 未列该 API），如实记

    return QtConcurrent::run([this, snap, hidePrivate, thumbsDir, hist, recent, mute, now]() {
        const Curate::Exhibit ex = pickExhibit(snap, hidePrivate, hist, mute, now, recent, thumbsDir);
        if (forceInlineForQa()) { persist(ex, now); return; }   // 反例：worker 当场写（修复前形态）
        // 结果回写：QueuedConnection 投回本对象（GUI 线程）→ 落库 + 发信号。
        QMetaObject::invokeMethod(this, [this, ex, now]() { persist(ex, now); },
                                  Qt::QueuedConnection);
    });
}

void CurateScheduler::persist(const Curate::Exhibit& ex, const Curate::Now& now)
{
    Q_UNUSED(now);
    if (!lib_ || !ex.valid) { emit generated(0); return; }
    const Curate::Theme& t = ex.theme;
    // title/blurb 存 .ts **键**（本地化是 UI 职责，spec §3 J 组：「文案串在 .ts 内」）。
    const qint64 id = lib_->addExhibit(QString::fromLatin1(Curate::templateIdToken(t.id)),
                                       t.title, t.blurb, t.primaryPath, t.paths,
                                       ex.generatedAt, QString());
    emit generated(id);
}
