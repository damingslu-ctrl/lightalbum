// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 LightAlbum Contributors
#include "PersonDetector.h"
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>

// ---- QA 钩子状态（非空→detect 绕过网络）----
static QVector<PersonItem> g_synthetic;
void PersonDetector::setSyntheticForQa(const QVector<PersonItem>& items) { g_synthetic = items; }
void PersonDetector::clearSyntheticForQa() { g_synthetic.clear(); }

const QString& PersonDetector::defaultPerson()
{
    static const QString s = QStringLiteral("人物1");
    return s;
}

// ---- 本地守卫：仅回环地址（对齐 face_server 的回环绑定 default 127.0.0.1）----
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

bool PersonDetector::probe(const QString& url)
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

bool PersonDetector::detect(Library* lib,
                            const QString& url, int limit,
                            std::function<void(int, int, const QString&)> progress,
                            std::function<bool()> cancel)
{
    if (!lib) return false;

    // ---- QA 确定路径：直接套用合成结果，不触网、不探测 ----
    if (!g_synthetic.isEmpty()) {
        lib->setPersons(g_synthetic);
        if (progress) progress(g_synthetic.size(), g_synthetic.size(), QString());
        return true;
    }

    // ---- 真路径：先探测服务（失败即返回 false，UI 据此提示启动服务）----
    if (!probe(url)) return false;

    const QVector<Photo>& photos = lib->photos();
    QVector<const Photo*> pending;
    pending.reserve(photos.size());
    for (const Photo& p : photos)
        if (p.person.isEmpty())       // 跳过已标注（含用户重命名），守恒 + 不覆盖
            pending.append(&p);
    if (limit > 0 && pending.size() > limit) pending.resize(limit);
    const int total = pending.size();
    int done = 0;
    if (progress) progress(0, total, QString());

    QNetworkAccessManager mgr;
    QVector<PersonItem> batch;
    batch.reserve(total);
    for (int i = 0; i < total; ++i) {
        if (cancel && cancel()) break;
        const Photo* p = pending[i];
        // 协议：POST JSON {"path": "<绝对路径>"}，服务端按路径读盘（不上传图像字节）。
        QJsonObject body;
        body.insert(QLatin1String("path"), p->path);
        QNetworkRequest req(QUrl(url).resolved(QStringLiteral("/face/detect")));
        req.setTransferTimeout(60000);
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        QNetworkReply* reply = mgr.post(
            req, QJsonDocument(body).toJson(QJsonDocument::Compact));
        const bool ok = waitReply(reply, 60000);
        if (!ok) {                      // 网络层失败 → 整张放过
            reply->deleteLater();
            done += 1;
            if (progress) progress(done, total, p->path);
            continue;
        }
        const QJsonObject res = QJsonDocument::fromJson(reply->readAll()).object();
        reply->deleteLater();
        // 无脸 → faces:[]，或服务 ok=false → 不标注该张（绝不抛错影响调用方）。
        const int count = res.value(QLatin1String("ok")).toBool(false)
            ? res.value(QLatin1String("faces")).toArray().size() : 0;
        if (count > 0)
            batch.append({p->path, defaultPerson()});
        done += 1;
        if (progress) progress(done, total, p->path);
    }
    if (!batch.isEmpty())
        lib->setPersons(batch);
    return true;
}
