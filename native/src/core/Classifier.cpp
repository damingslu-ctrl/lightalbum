// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 LightAlbum Contributors
#include "Classifier.h"
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QHttpMultiPart>
#include <QEventLoop>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <algorithm>

// ---- QA 钩子状态（非空→classify 绕过网络）----
static QVector<CatItem> g_synthetic;
void Classifier::setSyntheticForQa(const QVector<CatItem>& items) { g_synthetic = items; }
void Classifier::clearSyntheticForQa() { g_synthetic.clear(); }

// ---- 本地守卫：仅回环地址（对齐 Python brain.assert_local）----
static bool isLocalUrl(const QUrl& u)
{
    if (!u.isValid()) return false;
    const QString s = u.scheme().toLower();
    if (s != QStringLiteral("http") && s != QStringLiteral("https")) return false;
    const QString h = u.host().toLower();
    return h == QStringLiteral("127.0.0.1") || h == QStringLiteral("localhost")
        || h == QStringLiteral("::1") || h == QStringLiteral("[::1]");
}

// 同步等一个 reply（带超时），返回是否成功读完。reply 由调用方 deleteLater。
static bool waitReply(QNetworkReply* reply, int timeoutMs)
{
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();
    if (!reply->isFinished()) { reply->abort(); return false; }
    return reply->error() == QNetworkReply::NoError;
}

bool Classifier::probe(const QString& url)
{
    const QUrl u(url);
    if (!isLocalUrl(u)) return false;   // 守卫：拒绝外网
    QNetworkAccessManager mgr;
    QNetworkRequest req(u.resolved(QStringLiteral("/health")));
    req.setTransferTimeout(2000);
    QNetworkReply* reply = mgr.get(req);
    const bool ok = waitReply(reply, 2000);
    if (!ok) { reply->deleteLater(); return false; }
    const QJsonObject info = QJsonDocument::fromJson(reply->readAll()).object();
    reply->deleteLater();
    return info.value(QLatin1String("ok")).toBool(false)
        && info.value(QLatin1String("loaded")).toBool(false);
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
                          const QString& url, int batch, int limit,
                          std::function<void(int, int, const QString&)> progress,
                          std::function<bool()> cancel)
{
    // ---- QA 确定路径：直接套用合成结果，不触网、不探测 ----
    if (!g_synthetic.isEmpty()) {
        writer.categories(g_synthetic);
        if (progress) progress(g_synthetic.size(), g_synthetic.size(), QString());
        return true;
    }

    // ---- 真路径：先探测服务（失败即返回 false，UI 据此提示启动服务）----
    if (!probe(url)) return false;

    QVector<const Photo*> missing;
    missing.reserve(photos.size());
    for (const Photo& p : photos)
        if (p.category.isEmpty() && !p.thumbFile.isEmpty())
            missing.append(&p);
    if (limit > 0 && missing.size() > limit) missing.resize(limit);
    const int total = missing.size();
    int done = 0;
    if (progress) progress(0, total, QString());

    QNetworkAccessManager mgr;
    for (int i = 0; i < total; i += batch) {
        if (cancel && cancel()) break;
        const QVector<const Photo*> chunk = missing.mid(i, batch);

        // Qt 约定：post(req, QHttpMultiPart*) 后由 reply 接管堆对象，reply 销毁时自动 delete。
        // 绝不能用栈对象——异步/循环路径会悬垂（UB）。
        QHttpMultiPart* multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);
        QVector<QString> paths;
        paths.reserve(chunk.size());
        for (const Photo* p : chunk) {
            const QString tp = QDir(thumbsDir).filePath(p->thumbFile);
            QFile f(tp);
            if (!f.open(QIODevice::ReadOnly)) continue;     // 跳过无缩略图/坏图
            QHttpPart part;
            part.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("image/jpeg"));
            part.setHeader(QNetworkRequest::ContentDispositionHeader,
                QStringLiteral("form-data; name=\"files\"; filename=\"%1\"")
                    .arg(QFileInfo(tp).fileName()));
            part.setBody(f.readAll());
            multi->append(part);
            paths.append(p->path);
        }

        if (paths.isEmpty()) {                 // 整批都没缩略图，未发请求 → 手动释放
            delete multi;
            done += chunk.size();
            if (progress) progress(done, total, QString());
            continue;
        }

        QNetworkRequest req(QUrl(url).resolved(QStringLiteral("/classify")));
        req.setTransferTimeout(60000);
        QNetworkReply* reply = mgr.post(req, multi);
        const bool ok = waitReply(reply, 60000);
        if (!ok) {                              // 网络层失败 → 整批放过
            reply->deleteLater();
            done += chunk.size();
            if (progress) progress(done, total, QString());
            continue;
        }
        const QJsonObject res = QJsonDocument::fromJson(reply->readAll()).object();
        reply->deleteLater();

        QVector<CatItem> items;
        items.reserve(paths.size());
        if (res.value(QLatin1String("ok")).toBool(false)) {
            const QJsonArray results = res.value(QLatin1String("results")).toArray();
            for (const QJsonValue& v : results) {
                const QJsonObject r = v.toObject();
                const int idx = r.value(QLatin1String("i")).toInt(-1);
                if (idx < 0 || idx >= paths.size()) continue;
                const QString label = normalizeLabel(r.value(QLatin1String("label")).toString());
                const double score = r.value(QLatin1String("score")).toDouble();
                items.append({paths[idx], label, score});
            }
            if (!items.isEmpty()) writer.categories(items);
        }
        done += chunk.size();
        if (progress) progress(done, total, QString());
    }
    return true;
}
