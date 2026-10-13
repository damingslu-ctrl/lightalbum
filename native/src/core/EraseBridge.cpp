#include "EraseBridge.h"
#include "EraseRunner.h"
#include <QMetaObject>
#include <QThread>

// 2.38.0 R3：任务表淘汰——保留最近 kTasksKeep 个；仅修剪「前缀中已终态」的最旧项，
// running/queued 永不淘汰（spec §4.2）。终态 = done/failed/cancelled/blocked_no_model。
static constexpr int kTasksKeep = 8;

static bool isTerminalStatus(const QString& s)
{
    return s == QLatin1String("done") || s == QLatin1String("failed")
        || s == QLatin1String("cancelled") || s == QLatin1String("blocked_no_model");
}

void EraseBridge::pruneTasks()
{
    if (!qEnvironmentVariableIsEmpty("LA_ERASE_QA_FORCE_NO_EVICT")) return;   // QA 反例钩子
    while (tasks_.size() > kTasksKeep && isTerminalStatus(tasks_.first().status))
        tasks_.removeFirst();                                                   // 淘汰即释放 QImage
}

EraseBridge::EraseBridge(QObject* parent) : QObject(parent) {}

EraseBridge::~EraseBridge()
{
    if (thread_) {
        // 拆除顺序：queued 让 owner 线程自尽子进程（terminate+waitForFinished 必须在
        // owner 线程做），再 quit+wait。飞行中任务至多 20s：拆桥等它跑完，诚实代价。
        QMetaObject::invokeMethod(runner_, [r = runner_]{ r->shutdownOwned(); }, Qt::QueuedConnection);
        thread_->quit();
        thread_->wait();
        // 线程已结束，对象销毁无竞争（Qt 惯例：finished 后跨线程 delete 安全）
        delete runner_;
        delete thread_;
        runner_ = nullptr;
        thread_ = nullptr;
    }
}

bool EraseBridge::modelReady() const { return EraseRunner::executorAvailable(); }

void EraseBridge::ensureThread()
{
    if (thread_) return;
    thread_ = new QThread(this);
    thread_->setObjectName(QStringLiteral("EraseRunnerThread"));
    runner_ = new EraseRunner;                    // 无父子：moveToThread 要求
    runner_->moveToThread(thread_);
    connect(runner_, &EraseRunner::taskFinished, this, &EraseBridge::onTaskFinished);  // queued 回 GUI
    thread_->start();
}

void EraseBridge::onTaskFinished(quint32 id, bool ok, QImage out, QString err)
{
    EraseTask* q = nullptr;
    for (EraseTask& x : tasks_) if (x.id == int(id)) { q = &x; break; }
    if (!q) return;                               // 任务表里已无此 id（防御；正常不发生）
    applyResult(int(id), ok ? out : QImage());    // 内部置 done/failed 并 emit taskUpdated
    if (!ok) {
        for (EraseTask& y : tasks_) if (y.id == int(id)) y.note = err;
        emit taskUpdated(int(id));
    }
}

int EraseBridge::submit(const QImage& base, const QImage& mask)
{
    EraseTask t;
    t.id = nextId_++;
    t.base = base;
    t.mask = mask.convertToFormat(QImage::Format_ARGB32);
    if (modelReady()) { t.status = QStringLiteral("queued"); t.note = QStringLiteral("任务已入列，等待本地模型执行"); }
    else { t.status = QStringLiteral("blocked_no_model"); t.note = QStringLiteral("模型槽位空 · 任务挂起（零云端）"); }
    tasks_.append(t);
    emit taskUpdated(t.id);

    if (t.status == QLatin1String("queued")) {
        ensureThread();
        // GUI 线程即时置 running 并通知——2.35.0 之前 running 设在同步回调内经常来不及
        // 绘制；现在 worker 慢慢干，UI 先拿到 running 态。多任务天然 FIFO：worker 事件
        // 队列串行消费，协议顺序字节流不受并发影响。
        for (EraseTask& x : tasks_) if (x.id == t.id) x.status = QStringLiteral("running");
        emit taskUpdated(t.id);
        const int id = t.id;
        const QImage b = t.base, m = t.mask;
        QMetaObject::invokeMethod(runner_, [r = runner_, id, b, m]{ r->submitAsync(quint32(id), b, m); },
                                  Qt::QueuedConnection);
    }
    pruneTasks();                       // 2.38.0 R3：入列即淘汰最旧终态前缀，表长 <= kTasksKeep
    return t.id;
}

void EraseBridge::qaKillExecutorForTest()
{
    if (runner_) {
        QMetaObject::invokeMethod(runner_, [r = runner_]{ r->qaKillChild(); }, Qt::QueuedConnection);
        // 同线程事件队列 FIFO：紧随其后 queued 的 submitAsync 必先见到 kill 结果
        // （ensureStarted 对 NotRunning 自动重启），D 组崩溃恢复语义不变。
    }
}

void EraseBridge::qaPeekRunnerPid(qint64& pid) const
{
    if (runner_) runner_->processId(pid);         // atomic 缓存读，跨线程安全
    else pid = 0;
}

bool EraseBridge::applyResult(int taskId, const QImage& result)
{
    for (EraseTask& t : tasks_) {
        if (t.id != taskId) continue;
        if (result.isNull() || result.size() != t.base.size()) {
            t.status = QStringLiteral("failed");
            t.note = QStringLiteral("结果无效（空或尺寸不符），已拒绝回填");
        } else {
            t.result = result;
            t.status = QStringLiteral("done");
            t.note = QStringLiteral("消除完成 · 已回填");
        }
        emit taskUpdated(t.id);
        return t.status == QStringLiteral("done");
    }
    return false;
}

const EraseTask* EraseBridge::task(int id) const
{
    for (const EraseTask& t : tasks_)
        if (t.id == id) return &t;
    return nullptr;
}
