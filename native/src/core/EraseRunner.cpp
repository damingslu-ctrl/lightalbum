// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 LightAlbum Contributors
#include "EraseRunner.h"
#include "EraseProto.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QStringList>

// ---- QA 反例状态（打开→run() 返回越界 copy 得到的 null QImage）----
// 2.36.0：atomic——钩子由 GUI 线程写、owner(worker) 线程读，普通 bool 是数据竞争。
static std::atomic<bool> g_bad{false};
void EraseRunner::qaInjectBadSize(bool on) { g_bad.store(on); }

// ---- QA 钩子：置 true 令探针/findExe 判空 -> ensureStarted 失败 -> modelReady false
//      -> EraseBridge 把任务挂成 blocked_no_model（LA_ERASE_QA 的 spec §2.4-A 断言用）。
//      复位纪律同 qaInjectBadSize：每次布防必显式解除，不得泄漏给后续门。----
static std::atomic<bool> g_forceNoExecutor{false};
void EraseRunner::qaSetForceNoExecutor(bool on) { g_forceNoExecutor.store(on); }

EraseRunner::EraseRunner(QObject* parent) : QObject(parent) {}
EraseRunner::~EraseRunner()
{
    if (proc_) { proc_->terminate(); if (!proc_->waitForFinished(2000)) proc_->kill(); }
}

static QString findExe()
{
    if (g_forceNoExecutor.load()) return QString();       // QA：强制无执行器（blocked_no_model 断言用）
    const QString appDir = QCoreApplication::applicationDirPath();
    QStringList candidates = {
        appDir + "/3rdparty/lama/bin/inpaint.exe",
        appDir + "/inpaint_stub.exe",                   // dev：构建树扁平产物（Task 2 偏离3：无子目录）
        appDir + "/../inpaint_stub/inpaint_stub.exe",   // dev build tree
    };
    for (const QString& p : candidates) if (QFileInfo::exists(p)) return p;
    return QString();
}

// spec §2.3「exe 且 lama.onnx 均在」：真 inpaint.exe 缺同目录 lama.onnx = 执行器不可用，
// 绝不 spawn 进程。待用户稍后运行 fetch_lama.ps1 补上模型，下一次 submit 自动重试
// （executorAvailable 探针与 ensureStarted 共用本函数，每次调用重跑校验——热替换语义保留）。
// MinGW 开发桩（候选 2，扁平 inpaint_stub.exe）无需模型，按文件名精确豁免。
static bool modelOkFor(const QString& exe)
{
    if (exe.isEmpty()) return false;
    if (QFileInfo(exe).fileName().compare(QStringLiteral("inpaint.exe"), Qt::CaseInsensitive) == 0
        && !QFileInfo::exists(QDir(QFileInfo(exe).absolutePath()).filePath(QStringLiteral("lama.onnx"))))
        return false;
    return true;
}

bool EraseRunner::executorAvailable()
{
    const QString exe = findExe();                        // 纯文件存在性探测：不 spawn、不握手
    return modelOkFor(exe);
}

bool EraseRunner::ensureStarted()
{
    if (ready_ && proc_ && proc_->state() == QProcess::Running) { pidCache_ = proc_->processId(); return true; }
    if (exePath_.isEmpty()) exePath_ = findExe();
    if (!modelOkFor(exePath_)) return false;              // 逐次复查（fetch 后自动可用，无需重启应用）
    if (!proc_) proc_ = new QProcess(this);
    if (proc_->state() != QProcess::NotRunning) { proc_->kill(); proc_->waitForFinished(1000); }
    proc_->setProcessChannelMode(QProcess::SeparateChannels);
    proc_->setWorkingDirectory(QFileInfo(exePath_).absolutePath());   // lama.onnx 同目录解析
    proc_->start(exePath_, {});
    if (!proc_->waitForStarted(5000)) { pidCache_ = 0; return false; }
    // READY 预算=真模型冷启动实测校准（2026-10-11）：inpaint.exe 首帧 READY 前要完成
    // 191MB ORT session 加载+图优化，本机实测 8.1~8.5s（ORT_ENABLE_ALL；降 BASIC 也只到
    // 6.7s），旧 5000ms 是无模型主机时代拍的预算——真机必超时（首绿门即以此红过）。
    // 2.36.0 异步化后本等待发生在 owner(worker) 线程，不阻塞 GUI，放宽无副作用；
    // stub 首帧仍是毫秒级，行为不变。
    if (!proc_->waitForReadyRead(30000)) { pidCache_ = 0; return false; }
    const QByteArray line = proc_->readLine();
    if (!line.startsWith("READY")) { pidCache_ = 0; return false; }
    ready_ = true;
    pidCache_ = proc_->processId();
    return true;
}

bool EraseRunner::ready() { return ensureStarted(); }

bool EraseRunner::processId(qint64& pid) const
{
    pid = pidCache_.load();                               // atomic 缓存：GUI 线程读，owner 线程写
    return pid != 0;
}

void EraseRunner::qaKillChild()
{
    if (proc_) proc_->kill();
    ready_ = false;
    pidCache_ = 0;
}

void EraseRunner::shutdownOwned()
{
    if (proc_) {
        proc_->terminate();
        if (!proc_->waitForFinished(2000)) proc_->kill();
        pidCache_ = 0;
    }
    ready_ = false;
}

void EraseRunner::submitAsync(quint32 id, QImage base, QImage mask)
{
    QImage out; QString err;
    const bool ok = run(id, base, mask, out, &err);
    emit taskFinished(id, ok, out, err);                  // 跨线程：queued 回调用方（Bridge 在 GUI）
}

void EraseRunner::requestCancel(quint32 id) { cancelTarget_.store(id); }   // 任意线程：仅 atomic 写

bool EraseRunner::run(quint32 id, const QImage& base, const QImage& mask, QImage& out, QString* err)
{
    // 2.38.0 R3：取消预检（排到队首前 / 冷启动握手后各一次），命中即中止。
    if (cancelTarget_.load() == id) {
        cancelTarget_.store(0);
        if (err) *err = QStringLiteral("已取消");
        return false;
    }
    if (!ensureStarted()) { if (err) *err = QStringLiteral("executor unavailable"); return false; }
    if (cancelTarget_.load() == id) {                     // 握手期间收到的取消：不写请求、不空跑
        cancelTarget_.store(0);
        if (err) *err = QStringLiteral("已取消");
        qaKillChild();
        return false;
    }
    if (!LaEraseProto::writeRequest(proc_, id, base, mask, err)) return false;
    // Qt6：QIODevice 无 flush()；writeRequest 走 QProcess::write()，内部已 waitForBytesWritten()。
    // 分片等待（100ms 粒度观测取消；总预算 20000 不变）。子进程死亡 -> 交回失败路径。
    QElapsedTimer timer; timer.start();
    bool ready = false;
    while (timer.elapsed() < 20000) {
        if (cancelTarget_.load() == id) {
            cancelTarget_.store(0);
            if (err) *err = QStringLiteral("已取消");
            qaKillChild();                                // 杀子进程 + 复位（下次 submit 冷启动）
            return false;
        }
        if (proc_->state() != QProcess::Running) break;   // 子进程已死：永远等不到应答
        if (proc_->waitForReadyRead(100)) { ready = true; break; }
    }
    if (!ready) { if (err) *err = QStringLiteral("timeout"); qaKillChild(); return false; }
    if (!LaEraseProto::readResponse(proc_, id, out, err)) { qaKillChild(); return false; }
    // QA 反例：越界 copy → null QImage → applyResult 诚实拒收（期望值不动，铁律22）。
    if (g_bad.load() && !out.isNull()) out = out.copy(0, 0, out.width() + 1, out.height());
    return true;
}
