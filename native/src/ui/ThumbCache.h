#pragma once
// 缩略图多级缓存与后台加载（方向 D 第四步）：主线程持缓存，QThreadPool 后台解码，
// 完成经 QueuedConnection 回主线程（worker 不触碰任何 UI 状态）。
//
// —— 为什么多级 ——
// 旧实现单一 360px 级 + QCache 按「张数」计费：每张 360×360×4B ≈ 506KB，
// 4096 张上限 ≈ 1.98 GiB 内存，且 216px 普通瓦片与 feature 大图（~468px 起）
// 共用同一张 360px 图——大图不够大、小图白白占内存。
// 现改为两级、**按字节预算** LRU：
//   L1 kTile    256px —— 供普通瓦片（约 216px 显示）
//   L2 kFeature 512px —— 供 feature 首图 2×2（约 468px 显示）
// 单次解码后一次派生两级（复用同一份源像素，避免二次读盘/解码）。
//
// —— 行为不变（刻意保留）——
// ① 负缓存：加载失败（文件不存在/解码失败）记入 negative_，跨两级共享，
//    防重试风暴（原实现用「空 QPixmap 存进 QCache」表达同一语义）。
// ② pending_ 按文件名去重，worker 完成经 finish() 回主线程。
// ③ 生命周期守卫：cb 携带 QPointer<const QObject>，ctx 死亡则跳过回调。**ctx 必须是回调
//    真正解引用的那个对象**，不能图省事传宿主页面——页面重建时子控件先死、请求还在 pending_，
//    守卫恒真的回灌就是给已释放控件赋 QPixmap（2.42.0 修复：LobbyPage / PeoplePage 三处）。
// ④ 某一级因预算被淘汰时 cached() 回落另一级（宁可给大图也不给空图），
//    且不会重复触发后台加载。
#include <QObject>
#include <QPixmap>
#include <QPointer>
#include <QString>
#include <QThreadPool>
#include <QMutex>
#include <QSet>
#include <QHash>
#include <QList>
#include <functional>

class ThumbCache : public QObject {
    Q_OBJECT
public:
    // 缓存级别（决定请求/返回哪一档分辨率）。命名避开成员函数形参 level。
    enum ThumbLevel {
        kTile    = 0,   // L1 小图：普通瓦片
        kFeature = 1,   // L2 大图：feature 首图 2×2
        kLevelCount
    };

    explicit ThumbCache(QString dir, QObject* parent = nullptr);

    // 未命中则后台加载；cb 在主线程回调。ctx 用于生命周期守卫（QPointer），
    // 且必须就是 cb 内部解引用的对象（传宿主页面 = 子控件先死时回灌打在释放内存上）。
    // lv 缺省 kTile（既有调用零改动）。
    void request(const QString& thumbFile, const QObject* ctx,
                 std::function<void(const QPixmap&)> cb, int lv = kTile);
    // 命中返回该级；该级缺失时回落另一级（不触发加载）。负缓存返回空 QPixmap。
    QPixmap cached(const QString& thumbFile, int lv = kTile) const;

    // —— QA / 观测（不参与绘制路径）——
    int    cacheCount(int lv) const;               // 该级当前驻留张数
    qint64 cacheBytes(int lv) const;               // 该级当前占用字节
    qint64 budgetBytes(int lv) const;              // 该级字节预算
    int    levelPixels(int lv) const;              // 该级解码目标边长
    bool   isNegative(const QString& thumbFile) const;// 是否负缓存
    int    requestCount() const;                   // 累计后台加载次数（去重后）
    int    derivedCount() const;                   // 累计派生级数（诊断用）

signals:
    void loaded(const QString& thumbFile);

private slots:
    void finish(QString thumbFile, QPixmap l1, QPixmap l2, bool ok);

private:
    // 按字节预算的 LRU：QHash 存值 + QList 记最近使用序（front=MRU），淘汰从尾取。
    // get() 会重排 order_（LRU 语义），故声明为 const + mutable 内部状态。
    class ByteLru {
    public:
        explicit ByteLru(qint64 budgetBytes = 0) : budget_(budgetBytes) {}
        void setBudget(qint64 b) { budget_ = b; }
        qint64 budget() const { return budget_; }
        const QPixmap* get(const QString& k) const {
            for (int i = 0; i < order_.size(); ++i)
                if (order_[i] == k) { const_cast<ByteLru*>(this)->order_.move(i, 0);
                                       return constFind(k); }
            return nullptr;
        }
        void put(const QString& k, const QPixmap& pm) {
            const qint64 c = cost(pm);
            drop(k);
            map_.insert(k, pm);
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
        // QPixmap 无 sizeInBytes()（QImage 才有）→ 按 width*height*depth/8 手算
        static qint64 cost(const QPixmap& pm) {
            if (pm.isNull()) return 0;
            return qint64(pm.width()) * pm.height() * (pm.depth() / 8);
        }
    private:
        const QPixmap* constFind(const QString& k) const {
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
        qint64 budget_ = 0;
        qint64 bytes_  = 0;
        QHash<QString, QPixmap> map_;
        mutable QList<QString> order_;              // front = 最近使用
    };

    int pixelsFor(int lv) const;

    QString dir_;
    mutable ByteLru lru_[kLevelCount];
    QThreadPool pool_;
    mutable QMutex mutex_;
    QSet<QString> pending_;                       // 正在加载（按文件名去重）
    QSet<QString> negative_;                       // 负缓存：加载失败，跨级共享
    QHash<QString, QVector<QPair<QPointer<const QObject>, std::function<void(const QPixmap&)> > > > cbs_;
    mutable int requestCount_ = 0;                // 累计后台加载次数
    mutable int derivedCount_ = 0;                // 累计派生级数
};
