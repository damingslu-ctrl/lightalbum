// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 LightAlbum Contributors
#include "PersonDetector.h"
#include "FaceRunner.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

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
    for (int i = 0; i < total; ++i) {
        if (cancel && cancel()) break;
        const Photo* p = pending[i];
        // 协议：REQ kind="det"，body=图像绝对路径（子进程按路径读盘，**不传图像字节**，
        //   绝不碰原图）。Rst≠Ok / JSON 破损 → detect 返回 false，视同旧 !ok（净替换后
        //   「失败→整张放过」分支逐字保留，坏图/坏引擎不中断整批）。
        QJsonDocument doc;
        const bool ok = FaceRunner::detect(p->path, doc);
        if (!ok) {                      // 引擎/传输失败 → 整张放过
            done += 1;
            if (progress) progress(done, total, p->path);
            continue;
        }
        const QJsonObject res = doc.object();
        // 无脸 → faces:[]，或 ok=false → 不标注该张（绝不抛错影响调用方）。
        const int count = res.value(QLatin1String("ok")).toBool(false)
            ? res.value(QLatin1String("faces")).toArray().size() : 0;
        if (count > 0)
            batch.append({p->path, defaultPerson()});
        done += 1;
        if (progress) progress(done, total, p->path);
    }
    if (!batch.isEmpty())
        writer.persons(batch);
    return true;
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
    for (int i = 0; i < total; ++i) {
        if (cancel && cancel()) break;
        const Photo* p = pending[i];
        // 协议同 detect：REQ kind="det" body=绝对路径；Rst≠Ok 视同旧 !ok → 整张放过。
        QJsonDocument doc;
        const bool ok = FaceRunner::detect(p->path, doc);
        if (!ok) {                      // 引擎/传输失败 → 整张放过
            done += 1;
            if (progress) progress(done, total, p->path);
            continue;
        }
        const QJsonObject res = doc.object();
        const QJsonArray faces = res.value(QLatin1String("ok")).toBool(false)
            ? res.value(QLatin1String("faces")).toArray() : QJsonArray();
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
    return true;
}
