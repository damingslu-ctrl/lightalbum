// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 LightAlbum Contributors
#include "EraseRunner.h"
#include "EraseProto.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStringList>

// ---- QA 反例状态（打开→run() 返回越界 copy 得到的 null QImage）----
static bool g_bad = false;
void EraseRunner::qaInjectBadSize(bool on) { g_bad = on; }

// ---- QA 钩子：置 true 令 findExe 直接返回空 -> ensureStarted 失败 -> modelReady false
//      -> EraseBridge 把任务挂成 blocked_no_model（LA_ERASE_QA 的 spec §2.4-A 断言用）。
//      复位纪律同 qaInjectBadSize：每次布防必显式解除，不得泄漏给后续门。----
static bool g_forceNoExecutor = false;
void EraseRunner::qaSetForceNoExecutor(bool on) { g_forceNoExecutor = on; }

EraseRunner::EraseRunner(QObject* parent) : QObject(parent) {}
EraseRunner::~EraseRunner()
{
    if (proc_) { proc_->terminate(); if (!proc_->waitForFinished(2000)) proc_->kill(); }
}

static QString findExe()
{
    if (g_forceNoExecutor) return QString();               // QA：强制无执行器（blocked_no_model 断言用）
    const QString appDir = QCoreApplication::applicationDirPath();
    QStringList candidates = {
        appDir + "/3rdparty/lama/bin/inpaint.exe",
        appDir + "/inpaint_stub.exe",                   // dev：构建树扁平产物（Task 2 偏离3：无子目录）
        appDir + "/../inpaint_stub/inpaint_stub.exe",   // dev build tree
    };
    for (const QString& p : candidates) if (QFileInfo::exists(p)) return p;
    return QString();
}

bool EraseRunner::ensureStarted()
{
    if (ready_ && proc_ && proc_->state() == QProcess::Running) return true;
    if (exePath_.isEmpty()) exePath_ = findExe();
    if (exePath_.isEmpty()) return false;
    // spec §2.3「exe 且 lama.onnx 均在」：真 inpaint.exe 缺同目录 lama.onnx = 执行器不可用，
    // 直接返回 false 且绝不 spawn 进程（ready_ 保持 false）。EraseBridge 据此把任务挂成
    // blocked_no_model；待用户稍后运行 fetch_lama.ps1 补上模型，下一次 submit 自动重试
    // （热替换保留：ensureStarted 每次调用都重跑下面这道 onnx 校验，所以即便 exePath_ 已缓存，
    // 模型出现与否仍逐次复查，而非只在首查时判定一次）。MinGW 开发桩（候选 2，扁平
    // inpaint_stub.exe）无需模型，按文件名精确豁免。
    if (QFileInfo(exePath_).fileName().compare(QStringLiteral("inpaint.exe"), Qt::CaseInsensitive) == 0
        && !QFileInfo::exists(QDir(QFileInfo(exePath_).absolutePath()).filePath(QStringLiteral("lama.onnx"))))
        return false;
    if (!proc_) proc_ = new QProcess(this);
    if (proc_->state() != QProcess::NotRunning) { proc_->kill(); proc_->waitForFinished(1000); }
    proc_->setProcessChannelMode(QProcess::SeparateChannels);
    proc_->setWorkingDirectory(QFileInfo(exePath_).absolutePath());   // lama.onnx 同目录解析
    proc_->start(exePath_, {});
    if (!proc_->waitForStarted(5000)) return false;
    if (!proc_->waitForReadyRead(5000)) return false;
    const QByteArray line = proc_->readLine();
    if (!line.startsWith("READY")) return false;
    ready_ = true;
    return true;
}

bool EraseRunner::ready() { return ensureStarted(); }

bool EraseRunner::processId(qint64& pid) const
{
    if (proc_ && proc_->state() == QProcess::Running) { pid = proc_->processId(); return true; }
    pid = 0; return false;
}

void EraseRunner::qaKillChild()
{
    if (proc_) proc_->kill();
    ready_ = false;
}

bool EraseRunner::run(quint32 id, const QImage& base, const QImage& mask, QImage& out, QString* err)
{
    if (!ensureStarted()) { if (err) *err = QStringLiteral("executor unavailable"); return false; }
    if (!LaEraseProto::writeRequest(proc_, id, base, mask, err)) return false;
    // Qt6：QIODevice 无 flush()；writeRequest 走 QProcess::write()，内部已 waitForBytesWritten()。
    if (!proc_->waitForReadyRead(20000)) { if (err) *err = QStringLiteral("timeout"); qaKillChild(); return false; }
    if (!LaEraseProto::readResponse(proc_, id, out, err)) { qaKillChild(); return false; }
    // QA 反例：越界 copy → null QImage → applyResult 诚实拒收（期望值不动，铁律22）。
    if (g_bad && !out.isNull()) out = out.copy(0, 0, out.width() + 1, out.height());
    return true;
}
