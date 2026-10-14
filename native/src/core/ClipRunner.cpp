// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 LightAlbum Contributors
#include "ClipRunner.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDebug>

#include <atomic>

namespace {

// REQ id 单调不复用无表（spec §8 内存纪律）；0 留作协议哨兵，从 1 起。
std::atomic<quint64> g_clipId{1};

QStringList clipExeCandidates()
{
    // findExe 候选链 = FaceRunner.cpp 的 faceExeCandidates 四跳逐位镜像（派发单 R-51：
    // 不许增删候选、不许改顺序）：随包布局优先于 dev 扁平，源树殿后。
    // LA_CLIP_STUB=1 = 桩强制标记（桩文件头钩子表「runner 侧强制选桩标记，QA 接线消费」
    // ——本层即该消费点；门 #76 的协议腿走底座直配、门 #77 直起真 exe，两路互不干扰）。
    const QString appDir = QCoreApplication::applicationDirPath();
    QStringList cand;
    if (qEnvironmentVariableIsSet("LA_CLIP_STUB"))
        cand << appDir + "/clipinfer_stub.exe";                             // dev：QA 桩
    cand << appDir + "/3rdparty/clip/bin/clipinfer.exe"                     // 随包布局（inpaint.exe 先例；T10 C10 断言目标）
         << appDir + "/clipinfer.exe"                                       // dev：native/build 扁平（POST_BUILD 拷入 + ORT dll 同目录）
         << QDir::cleanPath(appDir + "/../../clipinfer/build/clipinfer.exe"); // dev build tree 源
    return cand;
}

// 模型 sibling 清单（绝对路径）= **只做镜像解析，绝不注入 env**（派发单 R-64 订正：
// 真实机制是「镜像解析 + 环境自然继承」——子进程由 QProcess 默认继承父进程 env 拿到
// LA_CLIP_MODEL，门面侧写 setChildEnvironment 的代码在底座里根本不存在）。
// 解析口径与 clipinfer/clipinfer.cpp:98-106 resolveModelDir 逐字同语义：env 优先，
// 且**必须** env 目录内真有 clip_vit_b32_img.onnx 才采用（否则把不存在的路径交给
// 底座判成 BlockedNoModel）；否则退到 exe 同目录。桩豁免 = 文件名精确比较
// （FaceRunner.cpp:43-45 判例，EraseRunner::modelOkFor 同族）→ 回空表。
// 候选全不存在也回空表，让 NoExe 先说话（§8 矩阵「exe 或模型缺」两引导语境的细分归因）。
// ⚠ 两轨 env 名**故意不对称**：本轨 = LA_CLIP_MODEL，face 轨 = LA_FACE_MODEL_DIR
//   （派发单 R-57：名字属 T7 已评审落地物，改名会让 #77 与 clipinfer 同时返工）。
//   后来者请勿「顺手统一」。
QStringList clipModelFiles(const QStringList& candidates)
{
    QString exe;
    for (const QString& c : candidates)
        if (QFileInfo::exists(c)) { exe = c; break; }
    if (exe.isEmpty()) return {};
    if (QFileInfo(exe).fileName().compare(QStringLiteral("clipinfer_stub.exe"),
                                          Qt::CaseInsensitive) == 0)
        return {};                                        // QA 桩：无模型依赖
    QString dir = QFileInfo(exe).absolutePath();
    const QByteArray envDir = qgetenv("LA_CLIP_MODEL");
    if (!envDir.isEmpty() &&
        QFile::exists(QString::fromUtf8(envDir) + QStringLiteral("/clip_vit_b32_img.onnx")))
        dir = QString::fromUtf8(envDir);                  // 与子端同一 env 语义，父子两端不劈叉
    QStringList models;
    models << dir + QStringLiteral("/clip_vit_b32_img.onnx")
           << dir + QStringLiteral("/prompts.bin");      // 两件齐备才 READY 1（R-57 启动时序契约）
    return models;
}

// READY 预算 60000ms = spec §2.1 表值（605MB ViT-B-32 塔；face 轨的 30s 是本表上一行，
// 不许互相「对齐」）。实测 ready_ms 摆动 493..3680ms（#77 在册五轮），60s 留双倍冷启动余量。
constexpr int kClipReadyBudgetMs = 60000;

} // namespace

AiSubprocessRunner& ClipRunner::runner()
{
    // Meyers 单例：C++11 magic static 线程安全初始化（Classifier 静态门面从
    // QtConcurrent 工作线程首调即在此完成装配）。
    static AiSubprocessRunner* r = [] {
        AiSubprocessRunner* rr = new AiSubprocessRunner();
        const QStringList cand = clipExeCandidates();
        rr->configure(cand, clipModelFiles(cand), kClipReadyBudgetMs, QString());  // qaHookEnvName：生产恒空
        return rr;
    }();
    return *r;
}

bool ClipRunner::probe(AiSubprocessRunner::Rst* rst)
{
    // 装配态刷新：候选链或 env 在首装配后才有意义（fetch_ai.ps1 补模型后免重启，
    // 热插拔语义）——重算 modelFiles 是一次 stat，代价可忽略。
    AiSubprocessRunner& r = runner();
    const QStringList cand = clipExeCandidates();
    r.configure(cand, clipModelFiles(cand), kClipReadyBudgetMs, QString());
    // exe 缺失**天然不进任何缓存**：底座 startIfNeeded 在候选链全缺时即刻
    // `return Sp::NoExe`（AiSubprocessRunner.cpp:236-239），位置早于任何结果落地
    // ——故每次 probe 都是真重探。⚠ 请勿「顺手给 NoExe 加缓存」：那会把
    //   「先构建 clipinfer 再点 AI 分类」这类一次性启动竞态固化成永久「引擎未就绪」
    //   （派发单 R-80 第 2 条；无缓存=免守卫，本注释即该判据的落点）。
    return r.ensureReady(rst);
}

bool ClipRunner::classifyOne(const QString& absPath, QJsonDocument& out)
{
    const AiSubprocessRunner::Reply rp =
        runner().call(QStringLiteral("jpg"),                    // clip 轨唯一合法 kind（clipinfer.cpp:232）
                      g_clipId.fetch_add(1, std::memory_order_relaxed),
                      absPath.toUtf8());                        // body=路径原字节（spec §5 R-75 订正）
    if (rp.rst != AiSubprocessRunner::Rst::Ok) {
        // 派发单 R-63：子端 ERR 帧 code（badprompts|nomodel|loadfail|inferfail|badframe）
        // 或底座本地码（exe_missing）必须**透出到日志**——静默放过会让「整库零写回」
        // 变成不可归因的哑故障。返回值仍是 false（调用侧＝该张放过，§2.3）。
        qWarning().noquote() << "ClipRunner: classifyOne rst=" << int(rp.rst)
                             << "code=" << QString::fromUtf8(rp.errCode)
                             << "path=" << absPath;
        return false;
    }
    QJsonParseError pe;
    const QJsonDocument d = QJsonDocument::fromJson(rp.json, &pe);
    if (pe.error != QJsonParseError::NoError || d.isNull()) return false;
    out = d;                                                    // 解析后即弃：rp.json 出栈释放，无缓存（§8）
    return true;
}
