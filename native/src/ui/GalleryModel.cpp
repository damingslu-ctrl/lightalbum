#include "GalleryModel.h"
#include "ThumbCache.h"
#include <algorithm>

GalleryModel::GalleryModel(QObject* parent) : QAbstractListModel(parent) {}

void GalleryModel::setSource(const QVector<Photo>* all)
{
    all_ = all;                 // 触发一次完整重建（过滤逻辑共用同一路径）
    setFilter(filter_);
}

void GalleryModel::setThumbCache(ThumbCache* c)
{
    cache_ = c;
    if (cache_) {
        connect(cache_, &ThumbCache::loaded, this, &GalleryModel::onThumbLoaded);
    }
}

void GalleryModel::setFilter(const QString& q)
{
    filter_ = q;
    beginResetModel();
    idx_.clear();
    rowByThumb_.clear();
    if (all_) {
        const QString needle = filter_.trimmed();
        for (int i = 0; i < all_->size(); ++i) {
            const Photo& p = (*all_)[i];
            if (needle.isEmpty()
                || p.title.contains(needle, Qt::CaseInsensitive)
                || p.rel.contains(needle, Qt::CaseInsensitive)
                || p.cam.contains(needle, Qt::CaseInsensitive)) {
                idx_.push_back(i);
                if (!rowByThumb_.contains(p.thumbFile))
                    rowByThumb_.insert(p.thumbFile, int(idx_.size()) - 1);
            }
        }
    }
    endResetModel();
}

int GalleryModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : idx_.size();
}

QVariant GalleryModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || !all_) return QVariant();
    const Photo& p = (*all_)[idx_[index.row()]];
    switch (role) {
    case Qt::DisplayRole:
    case TitleRole:   return p.title;
    case SubRole:     return p.date + (p.cam.isEmpty() ? QString() : QStringLiteral(" · ") + p.cam);
    case OrientRole:  return p.orient;
    case MissingRole: return p.missing;
    case DimRole:     return p.dim;
    default:          return QVariant();
    }
}

QPixmap GalleryModel::pixmapAt(int row) const
{
    if (!all_ || !cache_ || row < 0 || row >= idx_.size()) return QPixmap();
    const Photo& p = (*all_)[idx_[row]];
    if (p.thumbFile.isEmpty()) return QPixmap();
    // 未命中时触发后台加载；完成后 loaded → onThumbLoaded → dataChanged 刷新该行
    const_cast<GalleryModel*>(this)->cache_->request(
        p.thumbFile, this, [](const QPixmap&){});
    return cache_->cached(p.thumbFile);
}

void GalleryModel::onThumbLoaded(const QString& f)
{
    const int row = rowByThumb_.value(f, -1);
    if (row >= 0) {
        const QModelIndex ix = index(row);
        emit dataChanged(ix, ix, {Qt::DisplayRole});
    }
}
