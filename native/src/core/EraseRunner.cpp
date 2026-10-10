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

EraseRunner::EraseRunner(QObject* parent) : QObject(parent) {}
EraseRunner::~EraseRunner()
{
    if (proc_) { proc_->terminate(); if (!proc_->waitForFinished(2000)) proc_->kill(); }
}

static QString findExe()
{
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
