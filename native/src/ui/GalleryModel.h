#pragma once
// 图库数据模型：签名守卫语义由「QListView 自带窗口化」承接——
// 只为可见行出委托绘制请求，替代 web 版的手工 loadMore/sig 守卫。
#include <QAbstractListModel>
#include <QPixmap>
#include <QHash>
#include "../core/Library.h"

class ThumbCache;

class GalleryModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role {
        TitleRole  = Qt::UserRole + 1,
        SubRole,      // 日期 · 器材
        OrientRole,   // landscape/portrait
        MissingRole,
        RatingRole,
        DimRole
    };
    explicit GalleryModel(QObject* parent = nullptr);

    void setSource(const QVector<Photo>* all);   // 借用，不持有
    void setThumbCache(ThumbCache* c);
    void setFilter(const QString& q);            // 标题/文件名/器材 子串过滤

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;

    QPixmap pixmapAt(int row) const;             // 委托绘制时调用（触发后台加载）

private slots:
    void onThumbLoaded(const QString& thumbFile);

private:
    const QVector<Photo>* all_ = nullptr;
    QVector<int> idx_;                           // 过滤后的行 → all_ 下标
    ThumbCache* cache_ = nullptr;
    QHash<QString, int> rowByThumb_;
    QString filter_;
};
