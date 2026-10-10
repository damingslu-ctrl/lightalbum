#include "EraseBridge.h"
#include "EraseRunner.h"
#include <QTimer>

EraseBridge::EraseBridge(QObject* parent) : QObject(parent), runner_(new EraseRunner(this)) {}

bool EraseBridge::modelReady() const { return runner_ && runner_->ready(); }

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
        QTimer::singleShot(0, this, [this, id = t.id]() {
            EraseTask* q = nullptr;
            for (EraseTask& x : tasks_) if (x.id == id) { q = &x; break; }
            if (!q) return;
            q->status = QStringLiteral("running");
            emit taskUpdated(id);
            QImage out; QString err;
            const bool ok = runner_->run(quint32(id), q->base, q->mask, out, &err);
            applyResult(id, ok ? out : QImage());
            if (!ok) { for (EraseTask& y : tasks_) if (y.id == id) y.note = err; emit taskUpdated(id); }
        });
    }
    return t.id;
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
