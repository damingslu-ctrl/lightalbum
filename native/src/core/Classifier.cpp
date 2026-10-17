// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 LightAlbum Contributors
#include "Classifier.h"
#include "ClipRunner.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QDebug>
#include <algorithm>

// ---- QA 钩子状态（非空→classify 绕过子进程）----
static QVector<CatItem> g_synthetic;
void Classifier::setSyntheticForQa(const QVector<CatItem>& items) { g_synthetic = items; }
void Classifier::clearSyntheticForQa() { g_synthetic.clear(); }

// ---- 调度分片上限（派发单 R-80 第 1 条：这是真缺陷，不是保险丝）----
// 退役中的外部分类服务桥（QNAM multipart POST）把整块缩略图打进一个请求；控制端
// 15:50 直打该服务实测：batch=5 得 5 条真标签，batch=15/40/409 一律 409 拒收
// 「batch size N exceeds configured
// maximum batch size 5」⇒ 默认 batch=32 的**在库分类在生产里整批失败**（非 2xx → 既有
// continue → 零写回），而 probe 恰用 5 条 ⇒ 门是绿的、坏的是真实用户路径。
// clip 轨逐张化后每个 REQ 天生只承载一张，409 那一类随该服务桥整体消失；本常量把
// **调度分片**钳到 ≤5。钳制语义（达上限→连续分片，不丢弃、不报错）是本处置自定的
// 决定，spec §8 没有对应条款（该节只有四行用户可见矩阵 + 内存纪律）；方向与 §8
// 「单张失败→当张放过」一致。保留 batch 形参；进度守恒自 T8 修复波起为 done += processed
// （实际发出并被处置的张数，F-2），正常腿与 chunk.size() 逐张相等、进度序列不变（R-17）。
static constexpr int kClipMaxBatch = 5;

bool Classifier::probe()
{
    // 第四轮 T8：外部分类服务桥（QNAM GET 探活 + 仅回环地址守卫）净替换为 clipinfer
    // 子进程门面。probe 语义 = ClipRunner::probe()（拉起子进程 + READY 1 + 模型两件
    // 齐备）；READY 0 是「可用进程 + 不可用功能」，与缺 exe 一样归入本函数 false，
    // 由调用侧（MainWindow）映射到面向用户的「AI 引擎未就绪」文案（spec §8 矩阵）。
    // ⚠ 只能在非 GUI 线程调用：底座对 GUI 线程调用响亮拒（Q_ASSERT + Busy），
    //   Classifier::classify 的既有 worker 线程契约天然满足（T4b runPersonDetect 判例）。
    return ClipRunner::probe();
}

QString Classifier::normalizeLabel(const QString& raw)
{
    QString s = raw.trimmed();
    if (s.isEmpty()) return QStringLiteral("其他");
    // 8 固定类直接命中（与 GalleryPage::catOrder() 保持一致）
    static const QStringList fixed = {
        QStringLiteral("人像"), QStringLiteral("风光"),
        QStringLiteral("夜景·星空"), QStringLiteral("城市·建筑"),
        QStringLiteral("微距·花卉"), QStringLiteral("动物"),
        QStringLiteral("美食"), QStringLiteral("黑白") };
    for (const QString& f : fixed)
        if (s == f) return f;
    // 英文同义词映射
    static const QHash<QString, QString> syn = {
        {QStringLiteral("portrait"),   QStringLiteral("人像")},
        {QStringLiteral("people"),     QStringLiteral("人像")},
        {QStringLiteral("person"),     QStringLiteral("人像")},
        {QStringLiteral("face"),       QStringLiteral("人像")},
        {QStringLiteral("landscape"),  QStringLiteral("风光")},
        {QStringLiteral("nature"),     QStringLiteral("风光")},
        {QStringLiteral("scenery"),    QStringLiteral("风光")},
        {QStringLiteral("night"),      QStringLiteral("夜景·星空")},
        {QStringLiteral("nightscape"), QStringLiteral("夜景·星空")},
        {QStringLiteral("star"),       QStringLiteral("夜景·星空")},
        {QStringLiteral("starry"),     QStringLiteral("夜景·星空")},
        {QStringLiteral("astrophotography"), QStringLiteral("夜景·星空")},
        {QStringLiteral("city"),       QStringLiteral("城市·建筑")},
        {QStringLiteral("architecture"), QStringLiteral("城市·建筑")},
        {QStringLiteral("building"),  QStringLiteral("城市·建筑")},
        {QStringLiteral("urban"),      QStringLiteral("城市·建筑")},
        {QStringLiteral("macro"),      QStringLiteral("微距·花卉")},
        {QStringLiteral("flower"),     QStringLiteral("微距·花卉")},
        {QStringLiteral("plant"),      QStringLiteral("微距·花卉")},
        {QStringLiteral("animal"),     QStringLiteral("动物")},
        {QStringLiteral("wildlife"),   QStringLiteral("动物")},
        {QStringLiteral("bird"),       QStringLiteral("动物")},
        {QStringLiteral("pet"),        QStringLiteral("动物")},
        {QStringLiteral("food"),       QStringLiteral("美食")},
        {QStringLiteral("cuisine"),    QStringLiteral("美食")},
        {QStringLiteral("dish"),       QStringLiteral("美食")},
        {QStringLiteral("monochrome"), QStringLiteral("黑白")},
        {QStringLiteral("bw"),         QStringLiteral("黑白")},
        {QStringLiteral("blackandwhite"), QStringLiteral("黑白")},
        {QStringLiteral("grayscale"),  QStringLiteral("黑白")} };
    const QString low = s.toLower();
    auto it = syn.constFind(low);
    if (it != syn.constEnd()) return it.value();
    // 中文子串兜底
    if (low.contains(QStringLiteral("人像")) || low.contains(QStringLiteral("人物"))) return QStringLiteral("人像");
    if (low.contains(QStringLiteral("风光")) || low.contains(QStringLiteral("风景")) || low.contains(QStringLiteral("自然"))) return QStringLiteral("风光");
    if (low.contains(QStringLiteral("夜"))  || low.contains(QStringLiteral("星"))) return QStringLiteral("夜景·星空");
    if (low.contains(QStringLiteral("城市")) || low.contains(QStringLiteral("建筑"))) return QStringLiteral("城市·建筑");
    if (low.contains(QStringLiteral("微距")) || low.contains(QStringLiteral("花卉")) || low.contains(QStringLiteral("花"))) return QStringLiteral("微距·花卉");
    if (low.contains(QStringLiteral("动物")) || low.contains(QStringLiteral("鸟")) || low.contains(QStringLiteral("宠物"))) return QStringLiteral("动物");
    if (low.contains(QStringLiteral("美食")) || low.contains(QStringLiteral("食物")) || low.contains(QStringLiteral("菜"))) return QStringLiteral("美食");
    if (low.contains(QStringLiteral("黑白")) || low.contains(QStringLiteral("单色")) || low.contains(QStringLiteral("灰"))) return QStringLiteral("黑白");
    return QStringLiteral("其他");
}

bool Classifier::classify(const QVector<Photo>& photos, AiWriter& writer,
                          const QString& thumbsDir,
                          int batch, int limit,
                          std::function<void(int, int, const QString&)> progress,
                          std::function<bool()> cancel)
{
    // ---- QA 确定路径：直接套用合成结果，不触子进程、不探测 ----
    if (!g_synthetic.isEmpty()) {
        writer.categories(g_synthetic);
        if (progress) progress(g_synthetic.size(), g_synthetic.size(), QString());
        return true;
    }

    // ---- 真路径：先探测引擎（失败即返回 false，UI 据此提示引擎未就绪）----
    if (!probe()) return false;

    QVector<const Photo*> missing;
    missing.reserve(photos.size());
    for (const Photo& p : photos)
        if (p.category.isEmpty() && !p.thumbFile.isEmpty())
            missing.append(&p);
    if (limit > 0 && missing.size() > limit) missing.resize(limit);
    const int total = missing.size();
    int done = 0;
    if (progress) progress(0, total, QString());

    // 调度分片宽度 = min(batch, kClipMaxBatch)（R-80 第 1 条缺陷修复：达上限连续分片，
    // 不丢弃、不报错）；batch 形参原样保留（R-17）。进度守恒口径 T8 修复波第 1 轮收紧为
    // `done += processed`（本片**实际发出并被处置**的张数，F-2：张间取消后不再按整片超报）。
    const int chunkSize = std::max(1, std::min(batch, kClipMaxBatch));
    int failed = 0;                 // 单张可见失败数（spec §8「收尾汇总计数」）
    bool stopped = false;           // 任一取消 break 支置真（F-1：本轮未干净跑完）
    for (int i = 0; i < total; i += chunkSize) {
        if (cancel && cancel()) { stopped = true; break; }
        const QVector<const Photo*> chunk = missing.mid(i, chunkSize);

        QVector<QString> paths;     // 本张请求的路径清单（逐张传输 ⇒ 恒 1 元，R-17）
        paths.reserve(1);
        QVector<CatItem> items;
        items.reserve(chunk.size());
        int processed = 0;          // 本片实际发出并被处置的张数（含失败/不采信，F-2）
        for (const Photo* p : chunk) {
            // 取消检查点收窄到每张之间（R-18：原语义是「在途批次跑完」，批内逐张后
            // 更早退出——行为收紧，方向与原「尽快中止且不脏写」一致）。
            if (cancel && cancel()) { stopped = true; break; }
            const QString tp = QDir(thumbsDir).filePath(p->thumbFile);
            if (!QFileInfo::exists(tp)) continue;      // 跳过无缩略图/坏图（不发请求）
            ++processed;                               // 已发出 ⇒ 计入进度（成功/失败都算处置）
            paths.clear();
            paths.append(p->path);
            // 协议：REQ kind="jpg"，body=缩略图绝对路径的 UTF-8 原字节（spec §5 R-75）。
            QJsonDocument doc;
            if (!ClipRunner::classifyOne(tp, doc)) {   // 传输/引擎失败 → **该张**放过（R-18）
                ++failed;
                continue;
            }
            const QJsonObject r = doc.object();
            // R-80 第 1 条保守口径：ok!=true 的应答**绝不采信**其 label/score——要么该张有效，
            // 要么该张计入可见失败数，绝无「半条结果入库」。
            if (!r.value(QLatin1String("ok")).toBool(false)) { ++failed; continue; }
            // 单张应答没有批服务时代的 "i" 字段（clipinfer/rules.cpp renderJson 的
            // 逐字节形状里没有它）⇒ 索引恒 0；越界守卫同款保留（R-17）。
            // 越界即载荷残缺：按 spec §8「载荷残缺=该张可见失败」的保守口径计入 failed，
            // 不再静默 continue（T12 复审席 3 SC-5：face 轨 FIX-14/21 已把同型支路改成计数，
            // CLIP 双胞胎这里曾是纯静默——两条轨的降级语义不能劈叉）。
            // ⚠ 该支今日不可达（paths 恒 1 元素、idx 缺省 0），故**无门禁腿**：夹具层要咬住它
            // 需 ClipRunner 补 qaRetireChildForQa + 桩补 LA_CLIP_FORCE_BADIDX 臂，已登记第五轮
            // 结转（CHANGELOG「结转下一轮」③），此处不假装已被覆盖。
            const int idx = r.value(QLatin1String("i")).toInt(0);
            if (idx < 0 || idx >= paths.size()) { ++failed; continue; }
            const QString label = normalizeLabel(r.value(QLatin1String("label")).toString());
            const double score = r.value(QLatin1String("score")).toDouble();
            items.append({paths[idx], label, score});
        }

        if (!items.isEmpty()) writer.categories(items);
        done += processed;
        if (progress) progress(done, total, QString());
    }
    if (failed > 0)
        qWarning().noquote() << "Classifier: clipinfer 单张失败" << failed << "/" << total
                             << "张——失败张按「失败即放行」跳过未写回，成功张照常写回";
    // F-1（T8 修复波第 1 轮）：返回值语义 = 「本轮是否全部干净写回」——任一可见失败
    // （failed>0）或取消中断（stopped）→ false。**不是**「是否跑过」。GUI 侧
    // （MainWindow::runClassify 的 finished 支）据此从「已为 N 张写入场景分类」的
    // 预估假成功，如实落到已翻译的「分类过程中断（可能已部分写入）」支——全失败/
    // 被取消不再对用户报成功。签名不变（spec §10:267 门面三函数签名零变化）、
    // 零新增文案、零 i18n 触碰。
    return failed == 0 && !stopped;
}
