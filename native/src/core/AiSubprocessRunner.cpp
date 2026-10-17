// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 LightAlbum Contributors
#include "AiSubprocessRunner.h"

#include "AiProto.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QThread>

#include <algorithm>

namespace {

// RES 体上限 = 64 MiB（exe 侧 kMaxReqBody 同值的对称守卫，faceinfer.cpp:48 判例：
// 巨 len 无法消费保同步，绝不预分配巨内存——超限即判坏帧，拔换子进程）。
constexpr qint64 kMaxResBody = 64ll * 1024 * 1024;

} // namespace

bool AiSubprocessRunner::sIsMainThread()
{
    const QCoreApplication* app = QCoreApplication::instance();
    return app && QThread::currentThread() == app->thread();
}

AiSubprocessRunner::AiSubprocessRunner() : QObject(nullptr), owner_(new QThread())
{
    // moveToThread 于构造方（brief Step 1）：对象诞生于调用构造的线程，立即移交给
    // 专属 owner 线程并启动其事件循环——QProcess 亲和性要求 created/used 同线程，
    // slot* 全部在 owner 线程执行（2.36.0 EraseRunner 线程化判例的泛化）。
    moveToThread(owner_);
    owner_->start();
}

AiSubprocessRunner::~AiSubprocessRunner()
{
    // 拆除顺序：先 blocking 让 owner 线程杀干净子进程并 delete proc_（proc_ 亲和
    // owner 线程，不能在本线程 delete），再收 owner 线程事件循环。
    QMetaObject::invokeMethod(this, "slotShutdown", Qt::BlockingQueuedConnection);
    owner_->quit();
    owner_->wait(5000);
    delete owner_;
}

void AiSubprocessRunner::configure(const QStringList& exeCandidates, const QStringList& modelFiles,
                                   int readyBudgetMs, const QString& qaHookEnvName)
{
    // 持闸写配置：与在飞调用互斥（门的既定用法=首 call 前配置一次；运行中重配
    // 也安全）。owner 线程对下面四字段的读全部发生在 gate_+BlockingQueuedConnection
    // 构成的 happens-before 边之后，无竞争。
    QMutexLocker lk(&gate_);
    exeCandidates_  = exeCandidates;
    modelFiles_     = modelFiles;
    readyBudgetMs_  = readyBudgetMs;
    qaHookEnvName_  = qaHookEnvName;
}

AiSubprocessRunner::Reply AiSubprocessRunner::call(const QString& kind, quint64 id,
                                                   const QByteArray& body, int timeoutMs)
{
    if (sIsMainThread()) {
        // spec §2.2 硬纪律：GUI 线程 request = 接线误用，响亮拒（头文件注释有拒值论证：
        // Busy=拒不执行；绝不伪装 BlockedNoModel，那会谎报模型态驱动 §8 用户文案）。
        Q_ASSERT_X(false, "AiSubprocessRunner::call",
                   "request from GUI thread is forbidden (spec 2.2)");
        qWarning("AiSubprocessRunner::call refused: called from the GUI thread (spec 2.2)");
        return Reply{Rst::Busy, {}, QByteArrayLiteral("gui_thread")};
    }
    // 先 tryLock 再连接（评审锚）：拿不到闸 = 已有 in-flight 请求 → Busy 立即返回，
    // 不排队不死锁（单飞不变式，spec §2.2）。
    if (!gate_.tryLock())
        return Reply{Rst::Busy, {}, QByteArrayLiteral("busy")};
    Reply r;
    const bool invoked = QMetaObject::invokeMethod(
        this, "slotCall", Qt::BlockingQueuedConnection,
        Q_ARG(QString, kind), Q_ARG(quint64, id), Q_ARG(QByteArray, body), Q_ARG(int, timeoutMs));
    if (invoked) {
        r = resultReply_;                    // blocking 连接返回即 owner 线程写完，gate_ 内读安全
        resultReply_ = Reply{};
    } else {
        r = Reply{Rst::Crashed, {}, QByteArrayLiteral("invoke_failed")};
    }
    gate_.unlock();
    return r;
}

bool AiSubprocessRunner::ensureReady(Rst* out)
{
    if (sIsMainThread()) {
        Q_ASSERT_X(false, "AiSubprocessRunner::ensureReady",
                   "probe from GUI thread is forbidden (spec 2.2)");
        qWarning("AiSubprocessRunner::ensureReady refused: called from the GUI thread (spec 2.2)");
        if (out) *out = Rst::Busy;
        return false;
    }
    if (!gate_.tryLock()) {
        if (out) *out = Rst::Busy;
        return false;
    }
    const bool invoked = QMetaObject::invokeMethod(this, "slotEnsureReady",
                                                  Qt::BlockingQueuedConnection);
    const bool ok = invoked && readyRet_;
    if (out) *out = invoked ? readyRst_ : Rst::Crashed;
    readyRet_ = false;
    readyRst_ = Rst::Err;
    gate_.unlock();
    return ok;
}

void AiSubprocessRunner::qaRetireChildForQa()
{
    // 与析构同式：持单飞闸后把 slotShutdown 阻塞投递到 owner 线程（owner 线程空闲即
    // 执行；在飞请求持闸，故本调用必然排在它之后，绝不在请求中途拆进程）。
    // slotShutdown 内部 stopChild() 复位 proc_/inLoop_/ready1_/crashSeen_/rxbuf_，
    // 下一次 startIfNeeded 便按调用方**当时**的 systemEnvironment() 冷启动全新子进程
    // ——#75 G9「全失败轮 → 干净轮」两轮同进程所缺的那次换血。
    QMutexLocker lk(&gate_);
    QMetaObject::invokeMethod(this, "slotShutdown", Qt::BlockingQueuedConnection);
}

// ============================ owner 线程内部件 ============================

void AiSubprocessRunner::slotCall(const QString& kind, quint64 id,
                                  const QByteArray& body, int timeoutMs)
{
    const QByteArray idStr = QByteArray::number(id);
    if (!modelsPresent()) {
        // 热插拔重查（spec §2.2「每次 submit 前重查模型 sibling」）：READY 之后模型被
        // 移除 → 本地直接 BlockedNoModel，一个字节都不写给子进程；并即刻拔换尸体——
        // exe 侧 READY 后不再重查模型（会话已在内存），留着 = 陈旧会话继续服务，与
        // 「盘上无模型 = 无 AI」裁定相悖；模型回位后下一 call 自动冷启动重建新会话，
        // 这才是热替换生效路径（拔换成本一次性，语义诚实优先）。
        stopChild();
        resultReply_ = Reply{Rst::BlockedNoModel, {}, QByteArrayLiteral("nomodel")};
        return;
    }
    const Sp sp = startIfNeeded();
    if (sp == Sp::NoExe) { resultReply_ = Reply{Rst::NoExe, {}, QByteArrayLiteral("exe_missing")}; return; }
    if (sp == Sp::Timeout) { resultReply_ = Reply{Rst::Timeout, {}, QByteArrayLiteral("ready_timeout")}; return; }
    if (sp == Sp::Crashed) { resultReply_ = Reply{Rst::Crashed, {}, QByteArrayLiteral("spawn_failed")}; return; }
    // Sp::Ready0 = spec §5 保活臂：READY 0 的子进程活着且每 REQ 诚实回 ERR nomodel——
    // 照常写帧，让子进程侧的结构化失败自己说话（runner 不越权代答）。

    QElapsedTimer t; t.start();
    const QByteArray hdr = QByteArrayLiteral("REQ ") + idStr + QByteArrayLiteral(" ")
                         + kind.toLatin1() + QByteArrayLiteral(" ")
                         + QByteArray::number(body.size()) + QByteArrayLiteral("\n");
    proc_->write(hdr);
    if (!body.isEmpty()) proc_->write(body);
    // Qt6：QIODevice 无 flush；QProcess::write 的 waitForBytesWritten 显式补上（EraseRunner 注同）。
    if (!proc_->waitForBytesWritten(qMax(1, timeoutMs - int(t.elapsed())))) {
        const bool dead = proc_->state() != QProcess::Running || crashSeen_;
        stopChild();
        resultReply_ = Reply{dead ? Rst::Crashed : Rst::Timeout, {}, QByteArrayLiteral("write_failed")};
        return;
    }

    QByteArray line;
    if (!readLineOwner(line, timeoutMs - t.elapsed())) {
        const bool dead = proc_->state() != QProcess::Running || crashSeen_;
        stopChild();   // 卡死/死体都拔换：未应答 REQ 的流位不可信，此子进程不可复用（G6 腿）
        resultReply_ = Reply{dead ? Rst::Crashed : Rst::Timeout, {},
                             dead ? QByteArrayLiteral("child_died") : QByteArrayLiteral("resp_timeout")};
        return;
    }
    const QList<QByteArray> tk = line.split(' ');
    if (tk.size() == 3 && tk[0] == QByteArrayLiteral("ERR")) {
        if (tk[1] != idStr) { stopChild(); resultReply_ = Reply{Rst::Crashed, {}, QByteArrayLiteral("id_mismatch")}; return; }
        resultReply_ = Reply{Rst::Err, {}, tk[2]};   // 结构化失败：子进程保活、流在位（spec §5），不杀
        return;
    }
    if (tk.size() == 3 && tk[0] == QByteArrayLiteral("RES") && tk[1] == idStr) {
        bool lenOk = false;
        const qint64 len = tk[2].toLongLong(&lenOk);
        if (!lenOk || len < 0 || len > kMaxResBody) {
            stopChild();
            resultReply_ = Reply{Rst::Crashed, {}, QByteArrayLiteral("badframe_len")};
            return;
        }
        QByteArray b;
        if (!readExactOwner(b, len, timeoutMs - t.elapsed())) {
            const bool dead = proc_->state() != QProcess::Running || crashSeen_;
            stopChild();
            resultReply_ = Reply{dead ? Rst::Crashed : Rst::Timeout, {},
                                 dead ? QByteArrayLiteral("child_died") : QByteArrayLiteral("body_short")};
            return;
        }
        resultReply_ = Reply{Rst::Ok, b, {}};        // 体交调用方解析后即弃，runner 不缓存（§8）
        return;
    }
    stopChild();
    resultReply_ = Reply{Rst::Crashed, {}, QByteArrayLiteral("protocol_garbage")};
}

void AiSubprocessRunner::slotEnsureReady()
{
    // probe 判定次序（spec §2.3/§8）：exe 缺失先于模型缺失透出 NoExe（引导装配；
    // exe 在而 sibling 缺才 BlockedNoModel 引导 fetch_ai.ps1）。
    bool anyExe = false;
    for (const QString& c : exeCandidates_)
        if (QFileInfo::exists(c)) { anyExe = true; break; }
    if (!anyExe) {
        stopChild();                                // exe 被移除后的残尸同样拔换（候选链即真相）
        readyRet_ = false; readyRst_ = Rst::NoExe;
        return;
    }
    if (!modelsPresent()) {
        stopChild();
        readyRet_ = false; readyRst_ = Rst::BlockedNoModel;
        return;
    }
    switch (startIfNeeded()) {
    case Sp::Up:      readyRet_ = true;  readyRst_ = Rst::Ok;              break;
    case Sp::Ready0:  readyRet_ = false; readyRst_ = Rst::BlockedNoModel;  break;  // §5 保活，probe 仍 false
    case Sp::NoExe:   readyRet_ = false; readyRst_ = Rst::NoExe;           break;
    case Sp::Timeout: readyRet_ = false; readyRst_ = Rst::Timeout;         break;
    case Sp::Crashed: readyRet_ = false; readyRst_ = Rst::Crashed;         break;
    }
}

void AiSubprocessRunner::slotShutdown()
{
    stopChild();
}

bool AiSubprocessRunner::modelsPresent()
{
    for (const QString& m : modelFiles_)
        if (!QFileInfo::exists(m)) return false;
    return true;
}

AiSubprocessRunner::Sp AiSubprocessRunner::startIfNeeded()
{
    if (proc_ && inLoop_ && !crashSeen_ && proc_->state() == QProcess::Running)
        return ready1_ ? Sp::Up : Sp::Ready0;   // 复用路径：READY 1/0 身份必须原样还原（probe 不翻绿）
    if (proc_ && inLoop_ && proc_->state() == QProcess::Running && crashSeen_)
        inLoop_ = false;                     // errorOccurred 与 Running 态微秒级竞窗：按死体处理
    // 尸体/未启动：重建（errorOccurred→Crashed→下一次 call 走到这里自动重启一次；
    // 重启后再失败即原样透 Timeout/Crashed——本函数内不二次 spawn，无隐藏重试风暴）。
    if (proc_) stopChild();

    QString exe;
    for (const QString& c : exeCandidates_)
        if (QFileInfo::exists(c)) { exe = c; break; }
    if (exe.isEmpty()) return Sp::NoExe;

    proc_ = new QProcess(nullptr);           // 无 parent：只由本（owner）线程 new/delete
    connect(proc_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        crashSeen_ = true;                   // owner 线程 direct 连接（context==this 同线程亲和）
    });

    QProcessEnvironment e = QProcessEnvironment::systemEnvironment();
    if (!qaHookEnvName_.isEmpty()) {
        // QA 一次性钩子（configure 参数；生产配置传空名 = 恒无副作用）：spawn 时若
        // 进程 env 存在该名且非空，则显式注入子进程环境并从本进程一次性消耗掉——
        // 门 #75 G6 依赖「超时→自动重启的第二子进程不再挂起」的确定性（期望表不动，
        // 分支只在桩侧，铁律22）。
        const QByteArray v = qgetenv(qaHookEnvName_.toLatin1());
        if (!v.isEmpty()) {
            e.insert(qaHookEnvName_, QString::fromLatin1(v));
            qputenv(qaHookEnvName_.toLatin1(), QByteArray());
        }
    }
    proc_->setProcessEnvironment(e);
    proc_->setProcessChannelMode(QProcess::SeparateChannels);
    proc_->setWorkingDirectory(QFileInfo(exe).absolutePath());   // 模型 sibling 以 exe 目录解析（spec §2.1）
    proc_->start(exe, {});
    rxbuf_.clear();
    inLoop_ = false;
    ready1_ = false;
    crashSeen_ = false;

    if (!proc_->waitForStarted(5000)) {      // 拉起预算 = spec §2.2 常量 5s
        stopChild();
        return Sp::Crashed;
    }
    QByteArray line;
    if (!readLineOwner(line, readyBudgetMs_)) {
        const Sp r = (proc_->state() != QProcess::Running || crashSeen_) ? Sp::Crashed : Sp::Timeout;
        stopChild();
        return r;
    }
    // 握手行前缀匹配（spec §2.2）：READY 1=就绪 / READY 0=无模型保活 / 其余=协议违例。
    if (line == QByteArrayLiteral("READY 1")) { inLoop_ = true; ready1_ = true;  return Sp::Up; }
    if (line == QByteArrayLiteral("READY 0")) { inLoop_ = true; ready1_ = false; return Sp::Ready0; }
    stopChild();
    return Sp::Crashed;
}

void AiSubprocessRunner::stopChild()
{
    if (proc_) {
        if (proc_->state() != QProcess::NotRunning) {
            // 优雅腿：关 stdin 写端 → 子进程 fgets 得 EOF 自然走人（faceinfer/stub 同型循环）；
            // 不走再 terminate/kill 兜底（faceAiStop 门侧判例的 owner 线程版）。
            proc_->closeWriteChannel();
            if (!proc_->waitForFinished(1000)) proc_->terminate();
            if (!proc_->waitForFinished(1000)) proc_->kill();
            proc_->waitForFinished(1000);
        }
        delete proc_;                        // owner 线程同步 delete：事件循环已无待决 queued 投递
        proc_ = nullptr;
    }
    inLoop_ = false;
    ready1_ = false;
    crashSeen_ = false;
    rxbuf_.clear();
}

bool AiSubprocessRunner::readLineOwner(QByteArray& line, qint64 timeoutMs)
{
    QElapsedTimer t; t.start();
    for (;;) {
        const int nl = rxbuf_.indexOf('\n');
        if (nl >= 0) {
            line = rxbuf_.left(nl);
            if (line.endsWith('\r')) line.chop(1);   // 行规范化：parseReq/比对共守的剥 \r 契约
            rxbuf_.remove(0, nl + 1);
            return true;
        }
        if (timeoutMs <= 0 || t.elapsed() > timeoutMs) return false;
        if (proc_->bytesAvailable() > 0) { rxbuf_.append(proc_->readAll()); continue; }
        // 崩溃感知逃逸（T1 评审遗留#2）：QIODevice::atEnd() 只盯 stdout 通道，死体在
        // 另一通道尚有余字时可空转到满超时——bytesAvailable+state+crashSeen 三条件
        // 即刻 bail，超时归因交给调用方的 dead/Timeout 分类。
        if (proc_->state() != QProcess::Running && proc_->bytesAvailable() == 0) return false;
        if (crashSeen_ && proc_->bytesAvailable() == 0) return false;
        proc_->waitForReadyRead(50);         // 到达批次边界任意：短读回循环攒行（EraseProto 判例）
    }
}

bool AiSubprocessRunner::readExactOwner(QByteArray& body, qint64 n, qint64 timeoutMs)
{
    body.clear();
    QElapsedTimer t; t.start();
    for (;;) {
        if (body.size() == n) return true;   // 体必读满：帧内分片到达批次任意（血泪规约）
        if (!rxbuf_.isEmpty()) {
            const int take = int(qMin<qint64>(rxbuf_.size(), n - body.size()));
            body.append(rxbuf_.constData(), take);
            rxbuf_.remove(0, take);
            continue;
        }
        if (timeoutMs <= 0 || t.elapsed() > timeoutMs) return false;
        if (proc_->bytesAvailable() > 0) { rxbuf_.append(proc_->readAll()); continue; }
        if (proc_->state() != QProcess::Running && proc_->bytesAvailable() == 0) return false;
        if (crashSeen_ && proc_->bytesAvailable() == 0) return false;
        proc_->waitForReadyRead(50);
    }
}
