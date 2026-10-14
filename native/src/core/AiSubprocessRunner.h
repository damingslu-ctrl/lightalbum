// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 LightAlbum Contributors
#pragma once
#include <QByteArray>
#include <QMutex>
#include <QObject>
#include <QStringList>

class QProcess;
class QThread;

// 2.40.0 第四轮 T4：AI 子进程通用底座（spec §2.2）。自 EraseRunner 提炼的泛化，
// EraseRunner 本体一行不动（LA_ERASE_QA 锁死其行，禁回归面）。宿主：faceinfer/
// clipinfer 等一切 AiProto 线协议执行器；FaceRunner/ClipRunner 是其上的薄门面。
//
// 线程模型：构造方线程创建本对象 → 构造函数内部 moveToThread 到专属 owner QThread
// 并 start（QProcess 线程亲和：created/used 同线程，2.36.0 判例）。调用方（任意非
// GUI 工作线程）经 Qt::BlockingQueuedConnection invokeMethod 投递 slotCall/slotEnsureReady，
// owner 线程内做全部 waitFor* 同步机械；调用线程阻塞在 gate_ 锁上。
//
// 单飞不变式（spec §2.2，2.39.0 嵌套池教训同族防线；WHY 三行）：
// 同一 runner 同一时刻只允许一个 in-flight 请求——第二并发调用 tryLock 失败
// 立即返回 Busy（不排队、不死锁）。先 tryLock 再连接，杜绝「已连接未持锁」自锁。
// app 层两条批跑链路天然串行（进度对话框互斥），本守卫是文档化硬防线。
//
// GUI 线程纪律（spec §2.2「runner 永不可在 GUI 线程调用 request」）：call/ensureReady
// 首行 sIsMainThread() 硬卫拒——Q_ASSERT + qWarning 响亮报错。返回值取 Busy：§2.2
// 错误矩阵只有 blocked_no_model/exe_missing/timeout/crashed 四类用户可见失败，GUI
// 线程调用属接线误用（编译期不该发生）；Busy=「拒不执行」，调用方按 Rst≠Ok 走
// 「失败→整张放过」（§2.3），语义正确；绝不回 BlockedNoModel——那会谎报模型状态并
// 驱动 §8 面向用户的「AI 引擎未就绪」文案路径。
class AiSubprocessRunner : public QObject {
    Q_OBJECT
public:
    enum class Rst { Ok, Err, Timeout, Crashed, Busy, BlockedNoModel, NoExe };
    struct Reply {
        Rst rst = Rst::Err;
        QByteArray json;      // rst==Ok 时的 RES JSON 体（解析后即弃，无缓存表，§8 内存纪律）
        QByteArray errCode;   // 子进程 ERR 帧 code（nomodel|loadfail|inferfail|badframe）或本地诊断码
    };

    // 无 parent（moveToThread 约束）；owner 线程在本构造内创建并启动。
    explicit AiSubprocessRunner();
    ~AiSubprocessRunner() override;    // 调用方线程不限；内部 blocking 回收 owner 线程上的子进程

    AiSubprocessRunner(const AiSubprocessRunner&) = delete;
    AiSubprocessRunner& operator=(const AiSubprocessRunner&) = delete;

    // exeCandidates：按序取首个存在者（spawn 时逐次重解析——exe 热落位免重启）。
    // modelFiles：所选 exe 的模型 sibling 绝对路径清单；空表 = 无模型依赖（桩豁免，
    // 镜像 EraseRunner::modelOkFor 的 inpaint_stub 豁免判例）。
    // readyBudgetMs：READY 握手预算；qaHookEnvName：QA 一次性钩子 env 名（详见 .cpp）。
    void configure(const QStringList& exeCandidates, const QStringList& modelFiles,
                   int readyBudgetMs, const QString& qaHookEnvName = QString());

    // 任意非 GUI 线程；阻塞至应答/超时。timeoutMs 覆盖 spawn+握手之后的 REQ→RESP 全程。
    Reply call(const QString& kind, quint64 id, const QByteArray& body, int timeoutMs = 60000);

    // probe 语义（spec §2.3）：拉起 + readyBudgetMs 内 READY 1。
    // NoExe / BlockedNoModel（模型 sibling 缺 或 READY 0）/ Timeout / Crashed 经 out 透出。
    bool ensureReady(Rst* out = nullptr);

    // GUI 线程判定：当前线程 == QCoreApplication::instance()->thread()。
    static bool sIsMainThread();

private:
    // 内部状态分类（owner 线程私有）：startIfNeeded 三态返回值。
    enum class Sp { Up, Ready0, NoExe, Timeout, Crashed };

    // —— 以下全部只在 owner 线程触碰（slot 执行上下文）——
    Q_INVOKABLE void slotCall(const QString& kind, quint64 id, const QByteArray& body, int timeoutMs);
    Q_INVOKABLE void slotEnsureReady();
    Q_INVOKABLE void slotShutdown();

    bool modelsPresent();                 // 每次 call/spawn 前重查（热插拔语义，§2.2）
    Sp   startIfNeeded();                 // 拉起 + READY 握手；崩溃尸体在此自然重启一次
    void stopChild();                     // 关 stdin 写端→terminate/kill→delete，复位读线缓冲
    bool readLineOwner(QByteArray& line, qint64 timeoutMs);   // 攒 \n 读行（崩溃感知逃逸）
    bool readExactOwner(QByteArray& body, qint64 n, qint64 timeoutMs); // 体必读满（EraseProto 判例）

    QThread* owner_ = nullptr;
    QMutex   gate_;                         // 单飞闸：调用线程 tryLock→投递→取结果→unlock
    Reply    resultReply_;                  // gate_ 保护下跨 blocking 连接传递
    bool     readyRet_ = false;
    Rst      readyRst_ = Rst::Err;

    QStringList exeCandidates_;             // configure() 写；owner 线程经 gate_ happens-before 读
    QStringList modelFiles_;
    int       readyBudgetMs_ = 30000;
    QString   qaHookEnvName_;

    QProcess* proc_ = nullptr;              // 所选 exe 的持久子进程（owner 线程 new/delete）
    QByteArray rxbuf_;                      // 帧读线缓冲（跨调用存活，短批到达兜住）
    bool      inLoop_ = false;              // READY 行已读（1 或 0），子进程进入 REQ 服务循环
    bool      ready1_ = false;              // READY 1（true）还是 READY 0 保活臂（false）——复用路径须还原
    bool      crashSeen_ = false;           // errorOccurred 置位（owner 线程 direct 连接）
};
