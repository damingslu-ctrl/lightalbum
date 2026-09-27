#include "EraseBridge.h"
#include <QFileInfo>

EraseBridge::EraseBridge(QObject* parent)
    : QObject(parent)
{
}

bool EraseBridge::modelReady() const
{
    // 表单壳：执行器未接入 → 即使槽位配置了路径也恒 false（诚实状态）。
    // 模型接入后改为：QFileInfo::exists(modelPath_) && runner 可加载。
    Q_UNUSED(modelPath_);
    return false;
}

int EraseBridge::submit(const QImage& base, const QImage& mask)
{
    EraseTask t;
    t.id = nextId_++;
    t.base = base;
    t.mask = mask.convertToFormat(QImage::Format_ARGB32);
    if (modelReady()) {
        t.status = QStringLiteral("queued");
        t.note = QStringLiteral("任务已入列，等待本地模型执行");
    } else {
        t.status = QStringLiteral("blocked_no_model");
        t.note = QStringLiteral("模型槽位空 · 任务挂起（本地模型未接入，零云端）");
    }
    tasks_.append(t);
    emit taskUpdated(t.id);
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
