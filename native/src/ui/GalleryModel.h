#pragma once
// 图库数据模型：签名守卫语义由「QListView 自带窗口化」承接——
// 只为可见行出委托绘制请求，替代 web 版的手工 loadMore/sig 守卫。
#include <QAbstractListModel>
#include <QPixmap>
#include <QHash>
#include <QSet>
#include <QStringList>
#include "../core/CmdFilter.h"
#include "../core/Library.h"
#include "ThumbCache.h"   // 需要 ThumbCache::Level 作 pixmapAt 默认实参（非前向声明）

class GalleryModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role {
        TitleRole  = Qt::UserRole + 1,
        SubRole,      // 日期 · 器材
        OrientRole,   // landscape/portrait
        MissingRole,
        RatingRole,
        DimRole,
        PrivRole,     // P16 隐私标记（卡片右上角锁徽章）
        CategoryRole, // P16 分类标签
        MediaTypeRole,// 2.28.0 "image" | "video"（卡片左上播放徽标）
        DurRole       // 2.28.0 时长秒（0 = 未知 → 徽标只画三角不画时长）
    };
    explicit GalleryModel(QObject* parent = nullptr);

    void setSource(const QVector<Photo>* all);   // 借用，不持有
    void setThumbCache(ThumbCache* c);
    void setFilter(const QString& q);            // 标题/文件名/器材 子串过滤
    void setRegionFilter(const QString& regionId); // 地理筛选（大区，空=清除）
    void setProvFilter(const QString& provEn);     // 地理筛选（省，空=清除）
    void setCategoryFilter(const QString& cat);    // P16 分类（空=全部；kNoCategory=未分类）
    void setPersonFilter(const QString& name);     // P16 人物（空=全部）
    void setStoryFilter(const QStringList& paths);  // 故事相册（人物故事方向·B）：只显示某故事的若干照片（空=不启用）
    void clearStoryFilter();                        // 与 clearClusterFilter 对称（2.33.0）
    bool hasStoryFilter() const { return !storyFilter_.isEmpty(); }
    // 方向 C：聚类筛选（与上面各筛选**并存叠加**，互不清除；空=不启用）
    // dim 同时决定比较用哪个字段；clusterKeyOf(p) 给出该照片在此维度的键。
    void setClusterFilter(int dim, const QString& key);
    void clearClusterFilter();
    int  clusterDim() const { return clusterDim_; }
    bool hasClusterFilter() const { return !clusterKey_.isEmpty(); }
    // 2.41.0 spec §2.4：⌘K 自然语言过滤袋（年/月/日/季/人/地/风格/兜底词）。
    // 与上面各筛选**并存叠加**（AND），空 filter = 不启用。
    void setCmdFilter(const CmdFilter& f);
    void clearCmdFilter();
    bool hasCmdFilter() const { return !cmd_.empty(); }
    void setHidePrivate(bool hide);                // P16 隐私库锁定时隐藏私密照片
    const Photo* photoAt(int row) const;           // 行 → Photo*（供点击跳转修图）
    // 隐私门（2.33.0）：当前过滤后可见的行序路径集。
    // 灯箱据此把 ←/→ 限定在用户实际看得见的范围内，越界/私密行不得跳入。
    QStringList visiblePaths() const;

    // 「未分类」哨兵值（与真实分类名不可能撞车）
    static const QString kNoCategory;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;

    void rebuild();   // 文本 + 地理 + 分类/人物/隐私 联合过滤

    QPixmap pixmapAt(int row, int level = ThumbCache::kTile) const;  // 委托绘制时调用（level：kTile 普通瓦片 / kFeature 首图，触发后台加载）

private slots:
    void onThumbLoaded(const QString& thumbFile);

private:
    const QVector<Photo>* all_ = nullptr;
    QVector<int> idx_;                           // 过滤后的行 → all_ 下标
    ThumbCache* cache_ = nullptr;
    QHash<QString, int> rowByThumb_;
    QString filter_;
    QString regionFilter_;   // 大区 id（Photo.region 命中）
    QString provFilter_;     // 省 en 名（Photo.prov 命中）
    QString categoryFilter_; // P16 分类标签（空=全部）
    QString personFilter_;   // P16 人物名（空=全部）
    QSet<QString> storyFilter_;    // 故事相册（方向·B）：仅显示这些路径（空=不启用）
                                   // 2.33.0：QStringList→QSet，rebuild 每行的 contains 从
                                   //   O(故事长度) 降为 O(1)（大故事可达数千张 × 全库行）
    int    clusterDim_  = -1;      // 方向 C：聚类维度（-1=未启用）
    QString clusterKey_;           // 方向 C：簇键（空=不筛选）
    CmdFilter cmd_;                // 2.41.0 ⌘K：值语义过滤袋（empty()=未启用）
    bool   hidePrivate_ = true;   // P16 锁定时隐藏私密照片（默认锁）
};
