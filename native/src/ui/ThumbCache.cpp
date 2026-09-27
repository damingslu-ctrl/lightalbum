#include "ThumbCache.h"
#include <QDir>
#include <QFile>
#include <QMetaObject>
#include <QRunnable>
#include <QThread>
#include <algorithm>

namespace {
class LoadTask : public QRunnable {
public:
    LoadTask(ThumbCache* c, QString file, QString path)
        : c_(c), file_(std::move(file)), path_(std::move(path)) {}
    void run() override {
        QPixmap pm;
        if (QFile::exists(path_)) {
            pm.load(path_);
            if (!pm.isNull())
                pm = pm.scaled(360, 360, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        QMetaObject::invokeMethod(c_, "finish", Qt::QueuedConnection,
                                  Q_ARG(QString, file_), Q_ARG(QPixmap, pm));
    }
private:
    ThumbCache* c_;
    QString file_, path_;
};
} // namespace

ThumbCache::ThumbCache(QString dir, QObject* parent)
    : QObject(parent), dir_(std::move(dir))
{
    cache_.setMaxCost(4096);                  // 以张数为 cost
    pool_.setMaxThreadCount(std::max(2, QThread::idealThreadCount() / 2));
}

QPixmap ThumbCache::cached(const QString& f) const
{
    if (const QPixmap* p = cache_.object(f)) return *p;
    return QPixmap();
}

void ThumbCache::request(const QString& f, const QObject* ctx,
                         std::function<void(const QPixmap&)> cb)
{
    if (const QPixmap* p = cache_.object(f)) { cb(*p); return; }
    {
        QMutexLocker lk(&mutex_);
        cbs_[f].append({QPointer<const QObject>(ctx), std::move(cb)});
        if (pending_.contains(f)) return;
        pending_.insert(f);
    }
    pool_.start(new LoadTask(this, f, QDir(dir_).filePath(f)));
}

void ThumbCache::finish(QString f, QPixmap pm)
{
    QVector<QPair<QPointer<const QObject>, std::function<void(const QPixmap&)>>> todo;
    {
        QMutexLocker lk(&mutex_);
        pending_.remove(f);
        cache_.insert(f, new QPixmap(pm));
        if (auto it = cbs_.find(f); it != cbs_.end()) {
            todo.swap(it.value());
            cbs_.erase(it);
        }
    }
    emit loaded(f);
    for (auto& pr : todo) {
        if (pr.first)                       // ctx 存活才回调（QPointer 生命周期守卫）
            pr.second(pm);
    }
}
