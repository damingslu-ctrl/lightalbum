// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 LightAlbum Contributors
#include "PersonDetector.h"
#include "FaceRunner.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

// ---- QA 钩子状态（非空→detect 绕过子进程）----
static QVector<PersonItem> g_synthetic;
void PersonDetector::setSyntheticForQa(const QVector<PersonItem>& items) { g_synthetic = items; }
void PersonDetector::clearSyntheticForQa() { g_synthetic.clear(); }

// ---- QA 钩子状态（非空→detectEmbeddings 绕过子进程，直接落库合成 embedding）----
static QVector<FaceItem> g_syntheticEmb;
void PersonDetector::setSyntheticEmbForQa(const QVector<FaceItem>& items) { g_syntheticEmb = items; }
void PersonDetector::clearSyntheticEmbForQa() { g_syntheticEmb.clear(); }

const QString& PersonDetector::defaultPerson()
{
    static const QString s = QStringLiteral("人物1");
    return s;
}

bool PersonDetector::probe()
{
    // 第四轮 T4b：HTTP 桥（QNAM GET /health）净替换为 faceinfer 子进程门面。
    // probe 语义 = FaceRunner::probe()（拉起子进程 + READY 1 + 模型 sibling 齐备）。
    // Rst 细分（NoExe/BlockedNoModel/Timeout/Crashed）在调用侧做文案映射（§8），
    // 本层只回「引擎是否可用」的布尔——与旧 probe(url) 的 ok&&loaded 语义等价。
    // ⚠ 只能在非 GUI 线程调用：底座对 GUI 线程调用响亮拒（Q_ASSERT + Busy），
    //   PersonDetector::detect/detectEmbeddings 的既有 worker 线程契约天然满足。
    return FaceRunner::probe();
}

bool PersonDetector::detect(const QVector<Photo>& photos, AiWriter& writer,
                            int limit,
                            std::function<void(int, int, const QString&)> progress,
                            std::function<bool()> cancel)
{
    // ---- QA 确定路径：直接套用合成结果，不触子进程、不探测 ----
    if (!g_synthetic.isEmpty()) {
        writer.persons(g_synthetic);
        if (progress) progress(g_synthetic.size(), g_synthetic.size(), QString());
        return true;
    }

    // ---- 真路径：先探测引擎（失败即返回 false，UI 据此提示未就绪）----
    if (!probe()) return false;

    QVector<const Photo*> pending;
    pending.reserve(photos.size());
    for (const Photo& p : photos)
        if (p.person.isEmpty())       // 跳过已标注（含用户重命名），守恒 + 不覆盖
            pending.append(&p);
    if (limit > 0 && pending.size() > limit) pending.resize(limit);
    const int total = pending.size();
    int done = 0;
    if (progress) progress(0, total, QString());

    QVector<PersonItem> batch;
    batch.reserve(total);
    int failed = 0;                 // 单张可见失败数（spec §8「收尾汇总计数」在 face 轨的落地）
    bool stopped = false;           // 任一取消 break 支置真（本轮未干净跑完）
    for (int i = 0; i < total; ++i) {
        if (cancel && cancel()) { stopped = true; break; }
        const Photo* p = pending[i];
        // 协议：REQ kind="det"，body=图像绝对路径（子进程按路径读盘，**不传图像字节**，
        //   绝不碰原图）。Rst≠Ok（传输/引擎失败）→ 该张放过但计入可见失败数：坏图/坏引擎
        //   不中断整批（净替换保留的原语义），但**不再**伪装整轮干净（见收尾返回值注）。
        QJsonDocument doc;
        const bool ok = FaceRunner::detect(p->path, doc);
        if (!ok) {                      // 引擎/传输失败 → 整张放过（计入 failed）
            ++failed;
            done += 1;
            if (progress) progress(done, total, p->path);
            continue;
        }
        const QJsonObject res = doc.object();
        // 载荷保守口径（与 Classifier R-80 第 1 条同型）：ok!=true 的应答**绝不采信**其
        //   faces——要么该张有效（含「往返成功但无脸」= ok:true + faces:[]，不是失败），
        //   要么该张计入可见失败数，绝无「半条结果入库」。faceinfer 收尾恒插 ok:true
        //   （faceinfer.cpp:269 是全仓唯一插值处，:8 协议注释同旨），故 ok!=true 只可能是
        //   真失败（加载/推理/坏帧），不会被误当「没脸」。
        if (!res.value(QLatin1String("ok")).toBool(false)) {
            ++failed;
            done += 1;
            if (progress) progress(done, total, p->path);
            continue;
        }
        const int count = res.value(QLatin1String("faces")).toArray().size();
        if (count > 0)
            batch.append({p->path, defaultPerson()});
        done += 1;
        if (progress) progress(done, total, p->path);
    }
    if (!batch.isEmpty())
        writer.persons(batch);
    if (failed > 0)
        qWarning().noquote() << "PersonDetector: faceinfer 单张失败" << failed << "/" << total
                             << "张——已按「失败即放行」跳过，未写回";
    // 返回值语义（T12 修复波第 2 轮 FIX-14，与 Classifier::classify 的 F-1 契约同型）：
    // 「本轮是否全部干净跑完」——任一可见失败（传输失败或 ok!=true 的应答）或取消中断
    // → false。**不是**「是否跑过」。此前 face 轨收尾恒 return true，于是全失败轮仍被
    // MainWindow::runPersonDetect 报成「已为 N 张照片标注人物」（谎报），取消轮更矛盾：
    // runFaceCluster 已按 cancelled 跳过聚类，同一轮按钮却报成功。判据由 #75 G9 的 ERR
    // 子轮物理咬住（READY 1 ⇒ probe 仍成功 ⇒ 每 REQ ERR ⇒ 只有汇总计数能把这轮判成
    // false）。签名不变、零新增文案、零 i18n 触碰。
    return failed == 0 && !stopped;
}

// 把 faceinfer 返回的 embedding JSON 数组（512 个 double）转成小端序 float32 BLOB。
// ⚠ QByteArray::resize() 不做零初始化（残留堆垃圾）；此处每个元素都会被覆写，
//   仍显式零初始化以防 arr.size() 与写入数不一致时留下脏字节。
// x86 上 float* 直接落盘即小端序。
static QByteArray toEmbBlob(const QJsonArray& arr)
{
    QByteArray b(arr.size() * 4, '\0');
    float* p = reinterpret_cast<float*>(b.data());
    for (int i = 0; i < arr.size(); ++i) p[i] = float(arr[i].toDouble());
    return b;
}

bool PersonDetector::detectEmbeddings(const QVector<Photo>& photos, AiWriter& writer,
                                      int limit,
                                      std::function<void(int, int, const QString&)> progress,
                                      std::function<bool()> cancel)
{
    // ---- QA 确定路径：直接套用合成 embedding，不触子进程 ----
    if (!g_syntheticEmb.isEmpty()) {
        writer.faceEmbeddings(g_syntheticEmb);
        if (progress) progress(g_syntheticEmb.size(), g_syntheticEmb.size(), QString());
        return true;
    }

    // ---- 真路径：先探测引擎（失败即返回 false，UI 据此提示未就绪）----
    if (!probe()) return false;

    QVector<const Photo*> pending;
    pending.reserve(photos.size());
    for (const Photo& p : photos)
        if (p.person.isEmpty())       // 跳过已标注，聚类只处理未标注集
            pending.append(&p);
    if (limit > 0 && pending.size() > limit) pending.resize(limit);
    const int total = pending.size();
    int done = 0;
    if (progress) progress(0, total, QString());

    QVector<FaceItem> batch;
    batch.reserve(total);
    int failed = 0;                 // 与 detect 同口径：单张可见失败数
    bool stopped = false;
    for (int i = 0; i < total; ++i) {
        if (cancel && cancel()) { stopped = true; break; }
        const Photo* p = pending[i];
        // 协议同 detect：REQ kind="det" body=绝对路径；Rst≠Ok → 该张放过但计入可见失败数。
        QJsonDocument doc;
        const bool ok = FaceRunner::detect(p->path, doc);
        if (!ok) {                      // 引擎/传输失败 → 整张放过（计入 failed）
            ++failed;
            done += 1;
            if (progress) progress(done, total, p->path);
            continue;
        }
        const QJsonObject res = doc.object();
        // 载荷保守口径同 detect：ok!=true 绝不采信 faces（计失败）；ok:true + faces:[]
        // 是「往返成功但无脸」，不是失败，该张自然不进 batch。
        if (!res.value(QLatin1String("ok")).toBool(false)) {
            ++failed;
            done += 1;
            if (progress) progress(done, total, p->path);
            continue;
        }
        const QJsonArray faces = res.value(QLatin1String("faces")).toArray();
        // 取 score 最大的代表脸 embedding（多脸取代表脸，对齐 per-photo 单标签模型）
        if (!faces.isEmpty()) {
            double best = -1;
            QJsonObject bestFace;
            for (const QJsonValue& fv : faces) {
                const QJsonObject f = fv.toObject();
                const double s = f.value(QLatin1String("score")).toDouble(0.0);
                if (s > best) { best = s; bestFace = f; }
            }
            const QJsonArray emb = bestFace.value(QLatin1String("embedding")).toArray();
            if (!emb.isEmpty())
                batch.append({p->path, toEmbBlob(emb)});
        }
        done += 1;
        if (progress) progress(done, total, p->path);
    }
    if (!batch.isEmpty())
        writer.faceEmbeddings(batch);
    if (failed > 0)
        qWarning().noquote() << "PersonDetector: faceinfer 单张失败（embedding）" << failed
                             << "/" << total << "张——已按「失败即放行」跳过，未写回";
    // 返回值语义同 detect（FIX-14）：任一可见失败或取消中断 → false。GUI 侧
    // MainWindow::runFaceCluster 据此把「抽取失败/被取消」如实落到未就绪与中断两支，
    // 不再出现「worker 报 true 但聚类其实没跑」的谎报。
    return failed == 0 && !stopped;
}
