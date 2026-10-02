#include "ThumbCache.h"
#include <QDir>
#include <QFile>
#include <QMetaObject>
#include <QRunnable>
#include <QThread>
#include <algorithm>

// 单次解码 → 派生两级（worker 线程，全程不触碰 UI 状态）
namespace {
class LoadTask : public QRunnable {
public:
    LoadTask(ThumbCache* c, QString file, QString path, int p1, int p2)
        : c_(c), file_(std::move(file)), path_(std::move(path)), p1_(p1), p2_(p2) {}
    void run() override {
        QPixmap src;
        bool ok = false;
        if (QFile::exists(path_)) {
            src.load(path_);
            ok = !src.isNull();
        }
        QPixmap l1, l2;
        if (ok) {
            if (p1_ > 0) l1 = src.scaled(p1_, p1_, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            if (p2_ > 0) l2 = src.scaled(p2_, p2_, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        QMetaObject::invokeMethod(c_, "finish", Qt::QueuedConnection,
                                  Q_ARG(QString, file_), Q_ARG(QPixmap, l1),
                                  Q_ARG(QPixmap, l2), Q_ARG(bool, ok));
    }
private:
    ThumbCache* c_;
    QString file_, path_;
    int p1_, p2_;
};
} // namespace

int ThumbCache::pixelsFor(int lv) const
{
    // 注意：qEnvironmentVariableIntValue(name, default) 第二参是 bool* ok，不是 int 默认值
    if (lv == kFeature)
        return qEnvironmentVariable("LA_THUMB_FEATURE_PX", QStringLiteral("512")).toInt();
    return qEnvironmentVariable("LA_THUMB_TILE_PX", QStringLiteral("256")).toInt();
}

ThumbCache::ThumbCache(QString dir, QObject* parent)
    : QObject(parent), dir_(std::move(dir))
{
    // 字节预算（env 可覆盖，便于 QA 门构造小预算测淘汰）：
    // L1 默认 64MiB、L2 默认 48MiB —— 合计 ~112MiB，远低于旧实现 1.98GiB 上限。
    const qint64 b1 = qEnvironmentVariable("LA_THUMB_BUDGET1",
                                           QStringLiteral("67108864")).toLongLong();
    const qint64 b2 = qEnvironmentVariable("LA_THUMB_BUDGET2",
                                           QStringLiteral("50331648")).toLongLong();
    lru_[kTile].setBudget(b1);
    lru_[kFeature].setBudget(b2);
    pool_.setMaxThreadCount(std::max(2, QThread::idealThreadCount() / 2));
}

QPixmap ThumbCache::cached(const QString& f, int lv) const
{
    const int L = (lv == kFeature) ? kFeature : kTile;
    QMutexLocker lk(&mutex_);
    if (negative_.contains(f)) return QPixmap();          // 负缓存：空图，不触发重试
    if (const QPixmap* p = lru_[L].get(f))
        if (!p->isNull()) return *p;
    // 该级缺失（预算淘汰/尚未派生）→ 回落另一级，宁可给大图也不给空图
    const int other = (L == kTile) ? kFeature : kTile;
    if (const QPixmap* p = lru_[other].get(f))
        if (!p->isNull()) return *p;
    return QPixmap();
}

void ThumbCache::request(const QString& f, const QObject* ctx,
                         std::function<void(const QPixmap&)> cb, int lv)
{
    const int L = (lv == kFeature) ? kFeature : kTile;
    {
        QMutexLocker lk(&mutex_);
        if (negative_.contains(f)) { lk.unlock(); cb(QPixmap()); return; }
        // 任一级命中：同步回调，零加载（优先请求级，回落另一级）
        const int probes[2] = { L, (L == kTile) ? int(kFeature) : int(kTile) };
        for (int pr : probes) {
            if (const QPixmap* p = lru_[pr].get(f)) {
                if (!p->isNull()) { const QPixmap v = *p; lk.unlock(); cb(v); return; }
            }
        }
        cbs_[f].append({QPointer<const QObject>(ctx), std::move(cb)});
        if (pending_.contains(f)) return;                 // 已在加载：等 finish 合并回调
        pending_.insert(f);
        ++requestCount_;
    }
    // 锁外启动 worker（不持 mutex_ 跨线程）
    pool_.start(new LoadTask(this, f, QDir(dir_).filePath(f),
                             pixelsFor(kTile), pixelsFor(kFeature)));
}

void ThumbCache::finish(QString f, QPixmap l1, QPixmap l2, bool ok)
{
    // 显式实例化模板参数：QPair<QPointer<const QObject>, std::function<...>> 的嵌套 >> 需空格分隔
    QVector<QPair<QPointer<const QObject>, std::function<void(const QPixmap&)> > > todo;
    {
        QMutexLocker lk(&mutex_);
        pending_.remove(f);
        if (ok) {
            if (!l1.isNull()) { lru_[kTile].put(f, l1);    ++derivedCount_; }
            if (!l2.isNull()) { lru_[kFeature].put(f, l2); ++derivedCount_; }
        } else {
            negative_.insert(f);                         // 负缓存：防重试风暴
        }
        if (auto it = cbs_.find(f); it != cbs_.end()) {
            todo.swap(it.value());
            cbs_.erase(it);
        }
    }
    emit loaded(f);
    // ctx 死亡则跳过（QPointer 生命周期守卫）；取本级，缺失时 cached() 自动回落另一级
    for (auto& pr : todo) {
        if (pr.first) pr.second(cached(f, kTile));
    }
}

int    ThumbCache::cacheCount(int lv) const  { QMutexLocker lk(&mutex_); return lru_[(lv==kFeature)?kFeature:kTile].count(); }
qint64 ThumbCache::cacheBytes(int lv) const { QMutexLocker lk(&mutex_); return lru_[(lv==kFeature)?kFeature:kTile].bytes(); }
qint64 ThumbCache::budgetBytes(int lv) const{ QMutexLocker lk(&mutex_); return lru_[(lv==kFeature)?kFeature:kTile].budget(); }
int    ThumbCache::levelPixels(int lv) const { return pixelsFor((lv==kFeature)?kFeature:kTile); }
bool   ThumbCache::isNegative(const QString& f) const { QMutexLocker lk(&mutex_); return negative_.contains(f); }
int    ThumbCache::requestCount() const { QMutexLocker lk(&mutex_); return requestCount_; }
int    ThumbCache::derivedCount() const { QMutexLocker lk(&mutex_); return derivedCount_; }
