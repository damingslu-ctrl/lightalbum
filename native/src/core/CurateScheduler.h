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
#include <QDate>
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
    //   `force` = 用户显式「换一展」：绕开节流并把「每周 ≤1」这一条频控腿放开（裁决 R-T11b）。
    //   周口径管的是**自动推送**；把手势也一起拦下，等于把用户按下的按钮做成死键。
    //   其余频控腿（模板静音、同人 60 天冷却）照旧生效——放开的只有「这周已经展过一场」。
    QFuture<void> maybeGenerate(const QVector<Photo>& photos, bool hidePrivate,
                                const QString& thumbsDir, const Curate::Now& now,
                                bool force = false);

    // ---- 三手势的**落库真源**（T11；只在 GUI 线程调用，MainWindow 只接线不写库）----
    //   铁律 20：门禁直调这一批方法，产品 UI 调的是同一批——绝不复刻第二份落库逻辑。

    // 注入式时间的唯一映射处：生产取时 = wallNow()，门禁取时 = 字面量 Now（spec 裁决 1）。
    static Curate::Now makeNow(qint64 epochSec, const QDate& d);
    static Curate::Now wallNow();

    // 载荷 → 展示/播放请求：纯映射、零 IO。帧序 = 展帧序，主图恒在首位（承 R-T10b 播放侧契约）。
    static Curate::Exhibit asExhibit(const QStringList& paths, const QString& title,
                                     const QString& blurb, qint64 generatedAt);

    // 冷启动接回「今日一展」：取库里的**顶行（max id）**——两版 generated_at 可以相同，
    // 故绝不吃 loadExhibitHistory 的排序位置巧合。库里没展 → 保持 invalid（不编造，铁律 21）。
    void restoreLast();
    const Curate::Exhibit& currentExhibit() const { return current_; }
    qint64 currentExhibitId() const { return currentId_; }

    // 收进合集：建 source='curate' 的永久合集 + 该展 feedback='kept'。返回合集 id；
    // 空载荷 / 空标题 → 0 且**不写库**（不建空壳合集）。
    qint64 keepExhibit(const QStringList& paths, const QString& title);
    // 换一展：只记 feedback='swapped'（连续 3 次隐式降权留波 5，本轮如实只记录）。
    bool markSwapped();
    // 别再这类：**永久**静音该模板。只写 template_mutes，不另写 exhibits.feedback
    //   ——同一事实两处落库 = 双真源（裁决 R-T11c）。
    bool muteExhibit(const QString& templateToken);

    // 三手势的回执文案（v1 无设置页反悔入口 → 如实标注「下轮」，不假装可用）。
    //   keepAck 的 ok=false 支路 = 建合集失败：此时**不能**报「已收进」（铁律 21）。
    static QString keepAck(const QString& title, int n, bool ok = true);
    static QString muteAck();
    static QString swapAck(bool swapped);

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
    Curate::Exhibit current_;   // 正在展示的「常设展」（手势作用在它身上）
    qint64 currentId_ = 0;      // 0 = 无展
};
