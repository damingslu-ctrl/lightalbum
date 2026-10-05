// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 LightAlbum Contributors
#include "FullCache.h"
#include <QImageReader>
#include <QRunnable>
#include <QThreadStorage>
#include <QMutexLocker>
#include <QDebug>
#include <cstdio>

// ---- QA 反例钩子（全局，读多写少只在启动期设置，无需锁）----
static bool g_forceFailForQa = false;
void FullCache::setForceFailForQa(bool on) { g_forceFailForQa = on; }

qint64 FullCache::defaultBudgetBytes()
{
    // 实测依据：4032x3024 解码后 46.5MB（见头注释）。取 3 张 ≈ 140MB，
    // 够覆盖灯箱「上一张/当前/下一张」预取，又不至于吃穿内存。
    // 可被 LA_FULL_CACHE_QA 环境变量覆盖以做边界测试。
    const qint64 override_ = qEnvironmentVariable("LA_FULL_CACHE_BUDGET").toLongLong();
    if (override_ > 0) return override_;
    return qint64(140) * 1024 * 1024;
}

qint64 FullCache::maxPixels()
{
    // 实测最大样本 4032x3024 = 12.2MP；40MP 留足余量（高像素手机/全景拼接）
    // 又挡住解码炸弹（恶意构造的超大图会直接吃穿内存）。
    return qint64(40) * 1000 * 1000;
}

FullCache::FullCache(QObject* parent)
    : QObject(parent), lru_(defaultBudgetBytes())
{}

FullCache::~FullCache()
{
    // ★★ 关键：必须先等后台解码线程排空，再让本对象析构。
    // 现象（2026-10-05 实测）：进程 return 0 后 worker 仍在跑，随后
    // QMetaObject::invokeMethod 打到已析构的 FullCache → 0xC0000005，
    // 且 LA_MAP_CLICK_QA 间歇性崩（析构函数本身跑完，崩在回调落地时）。
    // 修法：waitForDone 让队列里的任务跑完（任务只碰局部与入参，安全），
    // 期间 finish() 会因 pending_ 已清空而直接 return，不再回调。
    {
        QMutexLocker lk(&mutex_);
        pending_.clear();
        callbacks_.clear();
    }
    QThreadPool::globalInstance()->waitForDone();
}

// 后台解码任务：只碰局部与入参，**不触碰任何 UI 状态或本对象成员**
// （worker 直连 finish() 是唯一回调口，finish() 内部加锁）。
class FullDecodeTask : public QRunnable {
public:
    FullDecodeTask(const QString& path, FullCache* owner)
        : path_(path), owner_(owner) {
        setAutoDelete(true);
    }
    void run() override {
        QImage img;
        bool ok = false;
        if (!g_forceFailForQa) {
            QImageReader r(path_);
            // 解码炸弹防护：先读尺寸，超上限直接放弃（不分配巨量内存）。
            const QSize sz = r.size();
            if (sz.isValid() && !sz.isEmpty()
                && qint64(sz.width()) * sz.height() <= FullCache::maxPixels()) {
                img = r.read();
                ok = !img.isNull();
            }
        }
        // 回主线程：QObject::connect 的 QueuedConnection 依赖接收者在主线程，
        // 跨线程调用槽函数由 Qt 自动排队（QMetaObject::invokeMethod 语义）。
        QMetaObject::invokeMethod(owner_, "finish", Qt::QueuedConnection,
                                  Q_ARG(QString, path_), Q_ARG(QImage, img),
                                  Q_ARG(bool, ok));
    }
private:
    QString path_;
    FullCache* owner_;
};

void FullCache::request(const QString& path, const QObject* ctx,
                        std::function<void(const QImage&)> cb)
{
    if (path.isEmpty()) return;

    // ① 查缓存 / 负缓存：在锁内判定，但**拷贝出值后在锁外回调**
    //    （回调会执行调用方代码，可能重入本对象 → 持锁回调即死锁）。
    bool haveCache = false, isNeg = false, needStart = false;
    QImage cachedImg;
    {
        QMutexLocker lk(&mutex_);
        if (const QImage* im = lru_.get(path)) { cachedImg = *im; haveCache = true; }
        else if (negative_.contains(path)) { isNeg = true; }
        else {
            if (!pending_.contains(path)) {   // 去重：同一张并发只解码一次
                pending_.insert(path);
                requestCount_++;
                needStart = true;
            }
            if (cb) callbacks_.insert(path, std::move(cb));
        }
    }
    if (haveCache) { if (cb) cb(cachedImg); return; }   // 命中
    if (isNeg)    { if (cb) cb(QImage()); return; }      // 负缓存：如实回空，调用方降级
    if (!needStart) return;                              // 已有同路径任务在跑，等它的回调

    // ② ctx 已死则不必解码（生命周期守卫，与 ThumbCache 同语义）
    if (ctx && QPointer<const QObject>(ctx).isNull()) return;

    QThreadPool::globalInstance()->start(new FullDecodeTask(path, this));
}

QImage FullCache::cached(const QString& path) const
{
    QMutexLocker lk(&mutex_);
    if (const QImage* im = lru_.get(path)) return *im;
    return QImage();
}

void FullCache::finish(QString path, QImage img, bool ok)
{
    QList<std::function<void(const QImage&)>> cbs;
    bool dropped = false;               // 像素超限 = 失败（非空但不可用）
    {
        QMutexLocker lk(&mutex_);
        if (!pending_.contains(path)) return;   // 已被处理过
        pending_.remove(path);
        if (ok && !img.isNull()) {
            lru_.put(path, img);
        } else {
            negative_.insert(path);
            failureCount_++;
            dropped = ok;                // 解码成功但被像素上限拦下
        }
        // QHash 无 values(key) → 逐项找（同一 path 可能有多个等待回调）
        for (auto it = callbacks_.constBegin(); it != callbacks_.constEnd(); ++it)
            if (it.key() == path) cbs.append(it.value());
        callbacks_.remove(path);
    }
    for (auto& c : cbs) c(dropped ? QImage() : img);
}

int    FullCache::cacheCount() const { QMutexLocker lk(&mutex_); return lru_.count(); }
qint64 FullCache::cacheBytes() const { QMutexLocker lk(&mutex_); return lru_.bytes(); }
qint64 FullCache::budget()     const { return lru_.budget(); }
int    FullCache::requestCount()const { QMutexLocker lk(&mutex_); return requestCount_; }
int    FullCache::failureCount()const{ QMutexLocker lk(&mutex_); return failureCount_; }
bool   FullCache::isNegative(const QString& path) const {
    QMutexLocker lk(&mutex_);
    return negative_.contains(path);
}
