// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 LightAlbum Contributors
#pragma once
// 2.42.0 波3 策展编排层（spec §1.1 架构图 / R3）——薄编排：节流 → 后台纯计算 → GUI 单写者回写。
//
// 铁律（记忆 ai-single-writer-gui-thread）：后台线程**绝不**触 Library / SQLite。
//   · `pickExhibit` 是**纯函数**（输入 photos+state+now，输出 Exhibit），可在任意线程跑，门禁可直测；
//   · `maybeGenerate` 只做：读节流口径 → 快照 photos → QtConcurrent::run 跑 pickExhibit →
//     结果经 `QMetaObject::invokeMethod(this, ..., Qt::QueuedConnection)` 投回本对象（GUI 线程）落库。
//   · 「完成信号」`generated(id)` 恒在 GUI 线程发射（id=0 表示本次未生成）。
//   · 返回值 `QFuture<void>` 只为门禁存在：`waitForFinished()` 后、事件循环前的那一瞬是
//     「worker 已算完、主线程尚未落库」的唯一可观测窗口（承 LA_GUI_THREAD_QA 的取证形状）。
#include "CurateState.h"
#include "CurateEngine.h"
#include "Library.h"
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QFuture>

class CurateScheduler : public QObject {
    Q_OBJECT
public:
    explicit CurateScheduler(Library* lib, QObject* parent = nullptr) : QObject(parent), lib_(lib) {}

    // 节流口径（纯函数，门禁直测；spec R3）：从未生成（lastAt==0）或距 now **逾 7 天** → 需要生成。
    static bool shouldGenerate(qint64 lastAt, const Curate::Now& now);

    // 编排核心（纯函数）：冷启动 + 候选池 + 12 模板 propose + 打分 + 频控 + compose。
    //   无放行候选 → 返回 `Exhibit{valid=false, skipReason=...}`（缺源/冷库/静音均如实跳过，R7）。
    //   thumbsDir 只被 compose 的展内画质消费（R7：画质不越界到全局打分）。
    static Curate::Exhibit pickExhibit(const QVector<Photo>& photos, bool hidePrivate,
                                       const Curate::ExhibitHistory& hist,
                                       const Curate::MuteState& mute, const Curate::Now& now,
                                       const QStringList& recentExhibitPaths = QStringList(),
                                       const QString& thumbsDir = QString());

    // 由 exhibits 行重建纯数据视图（GUI 线程调用；只搬运，无 IO 副作用）。
    static Curate::ExhibitHistory historyFrom(const QVector<ExhibitRow>& rows,
                                              qint64 lastExhibitAt);
    // 「近 4 周已展出」路径（spec §2.2 candidatePool 的第 2 条减项；4 周 = 28 天，按 shown_at）。
    static QStringList recentShownPaths(const QVector<ExhibitRow>& rows, const Curate::Now& now);

    // 节流触发生成：需要则在**后台**跑 pickExhibit（photos 快照按值捕获），结果 QueuedConnection 回写。
    QFuture<void> maybeGenerate(const QVector<Photo>& photos, bool hidePrivate,
                                const QString& thumbsDir, const Curate::Now& now);

    // ★QA 反例钩子：让 worker **当场落库**（= 修复前的跨线程写形态）。只改「写在哪个线程」，
    //   不改任何期望值（铁律 22）；门禁的期望恒为 GUI 线程。
    static void setForceInlineForQa(bool on);
    static bool forceInlineForQa();

signals:
    void generated(qint64 exhibitId);   // 0 = 本次未生成（节流未到期 / 无放行候选）

private:
    // 仅 GUI 线程：落库 + 发信号（lib_ 的写者纪律）。
    void persist(const Curate::Exhibit& ex, const Curate::Now& now);

    Library* lib_ = nullptr;
};
