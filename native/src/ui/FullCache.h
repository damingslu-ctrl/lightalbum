// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 LightAlbum Contributors
#pragma once
// 灯箱原图缓存（2.22.0 客户端体验）：按字节预算的 LRU + QThreadPool 后台解码。
//
// —— 为什么需要它（实测依据，不是猜测）——
// 灯箱原先直接显示 thumbs 里的 L2 缩略图（最大 512px），放大后细节全无。
// 改读原图后用 Qt 探针在真实库抽样 12 张实测：
//   · 解码耗时 中位 46ms / 最大 175ms  → 同步解码必然掉帧，必须后台线程
//   · 解码后内存 7.9MB ~ 70MB/张（4032x3024 = 46.5MB）
//     → 按「张数」计费会吃穿内存（ThumbCache 注释里已踩过 1.98GiB 的坑），
//       故与 ThumbCache 同构：**按字节预算**淘汰
//   · 样本中 3 张 HEIC 有 2 张 decoded=NULL → **失败降级是必需路径**，
//     不是可选优化（对齐「数据诚实」铁律：缺失/失败如实呈现，绝不空白）
//
// —— 复用 ThumbCache 的成熟范式（不重造）——
//   ① 按字节预算 LRU（QHash + QList 记最近使用序，front=MRU）
//   ② QThreadPool 后台解码，完成经 QueuedConnection 回主线程
//   ③ pending_ 去重，防同一张并发重复解码
//   ④ 负缓存 negative_：解码失败记入，跨轮共享，防重试风暴
//   ⑤ 生命周期守卫：ctx 携带 QPointer，对象死亡则跳过回调
//   ⑥ 拒绝解码请求若超过单像素上限（防解码炸弹，见 maxPixels()）
#include <QObject>
#include <QImage>
#include <QPointer>
#include <QString>
#include <QThreadPool>
#include <QMutex>
#include <QSet>
#include <QHash>
#include <QList>
#include <QSize>
#include <functional>

class FullCache : public QObject {
    Q_OBJECT
public:
    explicit FullCache(QObject* parent = nullptr);
    ~FullCache() override;

    // 请求原图。未命中则后台解码，cb 在主线程回调（成功传解码图，
    // 失败传 isNull 的 QImage —— 调用方据此降级到缩略图，**不得显示空白**）。
    // ctx 用于生命周期守卫。同一 path 并发请求会被去重，只解码一次。
    void request(const QString& path, const QObject* ctx,
                 std::function<void(const QImage&)> cb);

    // 命中返回已解码原图；未命中返回 isNull 的 QImage（不触发加载）。
    QImage cached(const QString& path) const;

    // —— 预算与策略（可调，QA 可断言其存在）——
    // 字节预算。默认按实测反推：一张 4032x3024 约 46.5MB，取 3 张约 140MB。
    // 灯箱一次只看一张，取 3 份足够覆盖「上一张/当前张/下一张」预取手感。
    static qint64 defaultBudgetBytes();
    // 单张解码后的像素上限（解码炸弹防护）。实测最大样本 4032x3024 = 12.2MP，
    // 取 40MP 留足余量又不允许极端图吃穿内存。
    static qint64 maxPixels();

    // —— QA / 观测（不参与绘制路径）——
    int    cacheCount() const;      // 驻留张数
    qint64 cacheBytes() const;      // 当前占用字节
    qint64 budget() const;          // 字节预算
    bool   isNegative(const QString& path) const;  // 是否负缓存（解码失败）
    int    requestCount() const;    // 累计后台解码次数（去重后）
    int    failureCount() const;    // 累计解码失败次数

    // QA 反例钩子：打开后 request() 恒返 isNull（模拟"原图永远失败"），
    // 用于证明门禁的「原图确实被读到」断言有效而非空心。
    static void setForceFailForQa(bool on);

private slots:
    void finish(QString path, QImage img, bool ok);

private:
    // 按字节预算的 LRU：与 ThumbCache::ByteLru 同构（QHash + order_ 记 MRU 在前）。
    // QImage 才有 sizeInBytes()（QPixmap 没有）→ 直接用它算成本。
    class ByteLru {
    public:
        explicit ByteLru(qint64 budgetBytes = 0) : budget_(budgetBytes) {}
        void setBudget(qint64 b) { budget_ = b; }
        qint64 budget() const { return budget_; }
        const QImage* get(const QString& k) const {
            for (int i = 0; i < order_.size(); ++i)
                if (order_[i] == k) { const_cast<ByteLru*>(this)->order_.move(i, 0);
                                       return constFind(k); }
            return nullptr;
        }
        void put(const QString& k, const QImage& im) {
            const qint64 c = cost(im);
            drop(k);
            map_.insert(k, im);
            order_.prepend(k);
            bytes_ += c;
            evict();
        }
        void drop(const QString& k) {
            auto it = map_.find(k);
            if (it == map_.end()) return;
            bytes_ -= cost(it.value());
            map_.erase(it);
            order_.removeAll(k);
        }
        void clear() { map_.clear(); order_.clear(); bytes_ = 0; }
        int  count() const { return int(map_.size()); }
        qint64 bytes() const { return bytes_; }
        static qint64 cost(const QImage& im) {
            if (im.isNull()) return 0;
            return qint64(im.sizeInBytes());
        }
    private:
        const QImage* constFind(const QString& k) const {
            auto it = map_.constFind(k);
            return it == map_.constEnd() ? nullptr : &it.value();
        }
        void evict() {
            while (bytes_ > budget_ && !order_.isEmpty()) {
                const QString victim = order_.last();
                if (map_.contains(victim)) bytes_ -= cost(map_.value(victim));
                map_.remove(victim);
                order_.removeLast();
            }
        }
        QHash<QString, QImage> map_;
        QStringList order_;      // front = 最近使用
        qint64 bytes_ = 0;
        qint64 budget_ = 0;
    };

    ByteLru lru_;
    mutable QMutex mutex_;
    QSet<QString> negative_;      // 解码失败（文件缺失/格式不支持/超像素上限）
    QSet<QString> pending_;       // 正在解码
    QHash<QString, std::function<void(const QImage&)>> callbacks_;
    int requestCount_ = 0;
    int failureCount_ = 0;
};
