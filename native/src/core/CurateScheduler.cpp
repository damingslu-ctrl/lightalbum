// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 LightAlbum Contributors
#include "CurateScheduler.h"
#include <QtConcurrent/QtConcurrentRun>
#include <QCoreApplication>
#include <QDateTime>
#include <QMetaObject>
#include <QStringList>
#include <algorithm>
#include <atomic>

namespace {
constexpr qint64 kWeekSec = 7LL * 86400;      // 节流窗（spec R3：>7 天才再生成）
constexpr qint64 kFourWeeksSec = 28LL * 86400; // 候选池减项窗（spec §2.2「近 4 周已展出」）
// 「别再这类」= 永久静音（spec R5）。loadMuteState 的口径是 until > now，故取一个远大于
// 任何可预见 epoch 的哨兵值；它是哨兵不是时长（不做「N 天冷却」的误读）。
constexpr qint64 kPermanentMuteUntil = Q_INT64_C(9999999999999);
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
                                             const QString& thumbsDir, const Curate::Now& now,
                                             bool force)
{
    if (!lib_) { emit generated(0); return QFuture<void>(); }
    // 节流：未到期 → 不生成（懒生成，spec R3）。读全在调用线程（GUI），worker 只见快照。
    //   force 只放开这一条腿：它是用户显式按下的「换一展」，不是自动推送（裁决 R-T11b）。
    if (!force && !shouldGenerate(lib_->lastExhibitAt(), now)) { emit generated(0); return QFuture<void>(); }

    // 快照（值拷贝）：后台只看到纯数据，**绝不**触 lib_（铁律 ai-single-writer-gui-thread）。
    const QVector<Photo> snap = photos;
    const QVector<ExhibitRow> rows = lib_->loadExhibitHistory();
    Curate::ExhibitHistory hist = historyFrom(rows, lib_->lastExhibitAt());
    if (force) hist.lastExhibitAt = 0;   // 同上：只摘掉「本周已展过一条」，其余频控照旧
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
    // title/blurb 存**成品文案**（展名在 compose 里已过 .ts 目录 + 填数，见 CurateEngine
    // displayTitleFor）；DB 不再存键 —— 存键时中文界面不装翻译器就直接漏裸键（2.42.0 实测）。
    const qint64 id = lib_->addExhibit(QString::fromLatin1(Curate::templateIdToken(t.id)),
                                       t.title, t.blurb, t.primaryPath, t.paths,
                                       ex.generatedAt, QString());
    // 手势必须作用在**这一版**身上，故落库同处记下 current_/currentId_（唯一真源）。
    current_ = ex;
    currentId_ = id;
    emit generated(id);
}

Curate::Now CurateScheduler::makeNow(qint64 epochSec, const QDate& d)
{
    Curate::Now n;
    n.epochSec = epochSec;
    n.y = d.year();
    n.m = d.month();
    n.d = d.day();
    return n;
}

Curate::Now CurateScheduler::wallNow()
{
    return makeNow(qint64(QDateTime::currentSecsSinceEpoch()), QDate::currentDate());
}

Curate::Exhibit CurateScheduler::asExhibit(const QStringList& paths, const QString& title,
                                           const QString& blurb, qint64 generatedAt)
{
    Curate::Exhibit ex;
    ex.generatedAt = generatedAt;
    if (paths.isEmpty()) { ex.skipReason = QStringLiteral("empty-payload"); return ex; }
    QVector<QString> ps;
    ps.reserve(paths.size());
    for (const QString& p : paths) ps.append(p);
    ex.theme.paths = ps;
    ex.theme.primaryPath = paths.constFirst();   // 主图恒在首位：帧序 = 展帧序，播放侧不再重排
    ex.theme.title = title;
    ex.theme.blurb = blurb;
    ex.valid = true;
    return ex;
}

void CurateScheduler::restoreLast()
{
    current_ = Curate::Exhibit();
    currentId_ = 0;
    if (!lib_) return;
    const QVector<ExhibitRow> rows = lib_->loadExhibitHistory();
    const ExhibitRow* top = nullptr;
    for (const ExhibitRow& r : rows)
        if (!top || r.id > top->id) top = &r;   // 按 id 取顶行：generated_at 可以相同（同批注入 now）
    if (!top) return;                           // 库里没有展 → 如实空态，不编造（铁律 21）
    current_ = asExhibit(top->paths.split(QLatin1Char(',')), top->title, top->blurb,
                         top->generatedAt);
    currentId_ = top->id;
}

qint64 CurateScheduler::keepExhibit(const QStringList& paths, const QString& title)
{
    if (!lib_ || paths.isEmpty() || title.trimmed().isEmpty()) return 0;   // 空载荷 → 不建空壳
    const qint64 albumId = lib_->createAlbum(title, paths, QStringLiteral("curate"));
    if (albumId > 0 && currentId_ > 0)
        lib_->updateExhibitFeedback(currentId_, QStringLiteral("kept"));
    return albumId;
}

bool CurateScheduler::markSwapped()
{
    if (!lib_ || currentId_ <= 0) return false;
    return lib_->updateExhibitFeedback(currentId_, QStringLiteral("swapped"));
}

bool CurateScheduler::muteExhibit(const QString& templateToken)
{
    if (!lib_ || templateToken.isEmpty()) return false;
    return lib_->muteTemplate(templateToken, kPermanentMuteUntil);
}

qint64 CurateScheduler::keepSelection(const QString& name, const QStringList& paths)
{
    if (!lib_ || paths.isEmpty() || name.trimmed().isEmpty()) return 0;
    return lib_->createAlbum(name, paths, QStringLiteral("cmdk"));
}

bool CurateScheduler::unmuteExhibit(const QString& token)
{
    if (!lib_ || token.isEmpty()) return false;
    return lib_->unmuteTemplate(token);
}

QString CurateScheduler::keepAck(const QString& title, int n, bool ok)
{
    return ok
        // ★文案纪律：界面上写出来的每一句都是承诺。合集**数据**本版已真落库（`albums`/
        //   `album_items`，跨重启可回读），但**没有任何页面列它**（`Library::listAlbums()`
        //   本版只有门禁在调）——故此处只报已成立的事实 + 如实标未上线，不许承诺可回看。
        ? QCoreApplication::translate("CurateScheduler", "已把《%1》的 %2 张存为永久合集，合集浏览下轮上线。")
              .arg(title).arg(n)
        : QCoreApplication::translate("CurateScheduler", "没能收进合集，库里没多出一份空壳。");
}

QString CurateScheduler::muteAck()
{
    return QCoreApplication::translate("CurateScheduler",
                                       "这类展已安静下来。反悔入口下轮上线。");
}

QString CurateScheduler::swapAck(bool swapped)
{
    return swapped
        ? QCoreApplication::translate("CurateScheduler", "已换一展。")
        : QCoreApplication::translate("CurateScheduler", "暂无更多可展的照片，先补几张进来。");
}
