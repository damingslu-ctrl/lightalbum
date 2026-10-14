// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 LightAlbum Contributors
#include "FaceRunner.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <atomic>

namespace {

// REQ id 单调不复用无表（spec §8 内存纪律）；0 留作协议哨兵，从 1 起。
std::atomic<quint64> g_faceId{1};

QStringList faceExeCandidates()
{
    // findExe 候选链 = EraseRunner::findExe 开发回退次序逐位镜像（spec §2.1 装配口径）：
    // 随包布局第一，dev 构建树扁平产物殿后。LA_FACE_STUB=1 = 桩强制标记（桩文件头 E1 组
    // 契约「runner 侧强制选桩标记，T4 接线消费」——本层即该消费点；门 #75 走底座直配，
    // 不经此 env，两路互不干扰）。
    const QString appDir = QCoreApplication::applicationDirPath();
    QStringList cand;
    if (qEnvironmentVariableIsSet("LA_FACE_STUB"))
        cand << appDir + "/faceinfer_stub.exe";                            // dev：QA 桩
    cand << appDir + "/3rdparty/face/bin/faceinfer.exe"                    // 随包布局（inpaint.exe 先例）
         << appDir + "/faceinfer.exe"                                      // dev：native/build 扁平（POST_BUILD 拷入）
         << QDir::cleanPath(appDir + "/../../faceinfer/build/faceinfer.exe"); // dev build tree 源
    return cand;
}

// 模型 sibling 清单（绝对路径）：镜像 exe 侧 resolveModelDir 的两级解析——
// LA_FACE_MODEL_DIR env 优先，其次 exe 同目录；桩按名精确豁免（EraseRunner::modelOkFor
// 的 inpaint_stub 豁免判例）。所选 exe 都不存在时回空表：probe 让 NoExe 先说话
// （§8 矩阵「exe 或模型缺」两引导语境的细分归因）。
QStringList faceModelFiles(const QStringList& candidates)
{
    QString exe;
    for (const QString& c : candidates)
        if (QFileInfo::exists(c)) { exe = c; break; }
    if (exe.isEmpty()) return {};
    if (QFileInfo(exe).fileName().compare(QStringLiteral("faceinfer_stub.exe"),
                                          Qt::CaseInsensitive) == 0)
        return {};                                        // QA 桩：无模型依赖
    QString dir = QFileInfo(exe).absolutePath();
    const QByteArray envDir = qgetenv("LA_FACE_MODEL_DIR");
    if (!envDir.isEmpty() &&
        QFile::exists(QString::fromUtf8(envDir) + QStringLiteral("/detection.onnx")))
        dir = QString::fromUtf8(envDir);                  // 与 exe 侧同一 env 语义，父子两端不劈叉
    QStringList models;
    models << dir + QStringLiteral("/detection.onnx")
           << dir + QStringLiteral("/recognition.onnx");  // T3 起 det+rec 成对（READY 1 双会话契约）
    return models;
}

} // namespace

AiSubprocessRunner& FaceRunner::runner()
{
    // Meyers 单例：C++11 magic static 线程安全初始化（PersonDetector 静态门面从
    // QtConcurrent 工作线程首调即在此完成装配）。READY 预算 30s = spec §2.1 表值
    // （lama 198MB≈8.5s 实测参照，face det+rec 合计 ~191MB 冷启动留双倍余量）。
    static AiSubprocessRunner* r = [] {
        AiSubprocessRunner* rr = new AiSubprocessRunner();
        const QStringList cand = faceExeCandidates();
        rr->configure(cand, faceModelFiles(cand), 30000, QString());  // qaHookEnvName：生产恒空
        return rr;
    }();
    return *r;
}

bool FaceRunner::probe(AiSubprocessRunner::Rst* rst)
{
    // 装配态刷新：候选链或 env 在首装配后才有意义（fetch_ai.ps1 补模型后免重启，
    // 热插拔语义）——重算 modelFiles 是一次 stat，代价可忽略。
    AiSubprocessRunner& r = runner();
    const QStringList cand = faceExeCandidates();
    r.configure(cand, faceModelFiles(cand), 30000, QString());
    return r.ensureReady(rst);
}

bool FaceRunner::detect(const QString& absPath, QJsonDocument& out)
{
    const AiSubprocessRunner::Reply rp =
        runner().call(QStringLiteral("det"),                      // canonical kind（T2 裁定，头文件注）
                      g_faceId.fetch_add(1, std::memory_order_relaxed),
                      absPath.toUtf8());                          // body=路径原字节（spec §5）
    if (rp.rst != AiSubprocessRunner::Rst::Ok) return false;      // §2.3：调用侧「失败→整张放过」视同 !ok
    QJsonParseError pe;
    const QJsonDocument d = QJsonDocument::fromJson(rp.json, &pe);
    if (pe.error != QJsonParseError::NoError || d.isNull()) return false;
    out = d;                                                      // 解析后即弃：rp.json 出栈释放，无缓存（§8）
    return true;
}
