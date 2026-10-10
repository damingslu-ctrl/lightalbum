#pragma once
// P4-4 AI 消除桥（表单壳）：蒙版 → 任务 → 结果回填；模型槽位留空。
// 零云端铁律：本桥仅接受**本地**模型执行器接入；无执行器时任务诚实挂起
// （status=blocked_no_model），绝不伪造「消除成功」的结果。
// 模型接入后的契约：执行器消费 queued 任务 → 产出 result → applyResult() 回填
// → taskUpdated(id) 通知 UI 回填预览。
#include <QObject>
#include <QVector>
#include <QImage>
#include <QString>
#include "EraseRunner.h"  // 编译必需偏离：下方 QA 钩子为内联转发，需要 EraseRunner 完整类型（仅前置声明不够）

struct EraseTask {
    int id = 0;
    QImage base;        // 任务基准图（几何后）
    QImage mask;        // ARGB32 蒙版（alpha=强度）
    QString status;     // queued / running / blocked_no_model / done / failed
    QString note;       // 人读状态说明（诚实展示用）
    QImage result;      // 消除结果（仅 done 时非空）
};

class EraseRunner;

class EraseBridge : public QObject {
    Q_OBJECT
public:
    explicit EraseBridge(QObject* parent = nullptr);

    // 模型槽位：路径由环境变量/设置注入；执行器未接入前 isReady() 恒 false。
    void setModelSlot(const QString& path) { modelPath_ = path; }
    QString modelPath() const { return modelPath_; }
    bool modelReady() const;

    // 提交消除任务：返回 taskId（>0）。无模型 → status=blocked_no_model（任务保留、不执行）。
    int submit(const QImage& base, const QImage& mask);

    // 回填通道（模型接入后由本地执行器调用）：result 尺寸须与 base 一致。
    bool applyResult(int taskId, const QImage& result);

    const EraseTask* task(int id) const;

    // QA 钩子（LA_ERASE_QA 专用，不在正式 UI 路径上调用）：
    void qaKillExecutorForTest() { if (runner_) runner_->qaKillChild(); }
    void qaPeekRunnerPid(qint64& pid) const { if (runner_) runner_->processId(pid); }

signals:
    void taskUpdated(int id);   // 状态变化（入列 / 挂起 / 完成 / 失败）

private:
    QString modelPath_;          // 槽位（空 = 未配置）
    QVector<EraseTask> tasks_;
    int nextId_ = 1;
    class EraseRunner* runner_ = nullptr;
};
