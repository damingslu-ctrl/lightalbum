#pragma once
// 缩略图缓存与后台加载：主线程持缓存，QThreadPool 后台解码，完成经
// QueuedConnection 回主线程（worker 不触碰任何 UI 状态）。
#include <QObject>
#include <QPixmap>
#include <QPointer>
#include <QString>
#include <QThreadPool>
#include <QCache>
#include <QMutex>
#include <QSet>
#include <functional>

class ThumbCache : public QObject {
    Q_OBJECT
public:
    explicit ThumbCache(QString dir, QObject* parent = nullptr);

    // 未命中则后台加载；cb 在主线程回调。ctx 用于生命周期守卫（QPointer）。
    void request(const QString& thumbFile, const QObject* ctx,
                 std::function<void(const QPixmap&)> cb);
    QPixmap cached(const QString& thumbFile) const;

signals:
    void loaded(const QString& thumbFile);

private slots:
    void finish(QString thumbFile, QPixmap pm);

private:
    QString dir_;
    QCache<QString, QPixmap> cache_;      // 空 QPixmap = 加载失败（负缓存，防重试风暴）
    QThreadPool pool_;
    QMutex mutex_;
    QSet<QString> pending_;
    QHash<QString, QVector<QPair<QPointer<const QObject>, std::function<void(const QPixmap&)>>>> cbs_;
};
