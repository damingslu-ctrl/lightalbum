#include "GalleryModel.h"
#include "ThumbCache.h"
#include "../core/PhotoCluster.h"
#include <algorithm>

const QString GalleryModel::kNoCategory = QStringLiteral("\u0001n categor");

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

// 文本 + 地理（大区/省）+ 分类/人物/隐私 联合过滤：全部 AND。
void GalleryModel::rebuild()
{
    beginResetModel();
    idx_.clear();
    rowByThumb_.clear();
    if (all_) {
        const QString needle = filter_.trimmed();
        const bool hasRegion = !regionFilter_.isEmpty();
        const bool hasProv   = !provFilter_.isEmpty();
        const bool hasCat    = !categoryFilter_.isEmpty();
        const bool hasPerson = !personFilter_.isEmpty();
        // 方向 C：聚类筛选与其它筛选并存叠加
        const bool hasCluster = !clusterKey_.isEmpty() && clusterDim_ >= 0;
        const QString region = regionFilter_.toLower();
        const QString prov   = provFilter_.toLower();
        for (int i = 0; i < all_->size(); ++i) {
            const Photo& p = (*all_)[i];
            if (hidePrivate_ && p.priv) continue;               // P16 隐私门
            if (hasRegion && p.region.toLower() != region) continue;
            if (hasProv   && p.prov.toLower()   != prov)   continue;
            if (hasCat) {                                       // P16 分类
                if (categoryFilter_ == kNoCategory) {
                    if (!p.category.isEmpty()) continue;
                } else if (p.category != categoryFilter_) {
                    continue;
                }
            }
            if (hasPerson && !p.person.split(',', Qt::SkipEmptyParts)
                                 .contains(personFilter_))      // P16 人物
                continue;
            if (hasCluster) {   // 方向 C：簇键比较（真源在 core/PhotoCluster）
                if (PhotoCluster::clusterKey(p, static_cast<PhotoCluster::Dim>(clusterDim_)) != clusterKey_)
                    continue;
            }
            if (!needle.isEmpty()) {
                // 方向 C 第一步：扩展搜索 haystack（关键词/地点/时间/评分）
                const QString hay = p.title + QChar(' ') + p.rel + QChar(' ')
                    + p.cam + QChar(' ') + p.region + QChar(' ') + p.prov
                    + QChar(' ') + p.city + QChar(' ') + p.date
                    + QChar(' ') + QString::number(p.rating);
                if (!hay.contains(needle, Qt::CaseInsensitive)) continue;
            }
            idx_.push_back(i);
            if (!rowByThumb_.contains(p.thumbFile))
                rowByThumb_.insert(p.thumbFile, int(idx_.size()) - 1);
        }
    }
    endResetModel();
}

void GalleryModel::setFilter(const QString& q)     { filter_ = q;        rebuild(); }
void GalleryModel::setRegionFilter(const QString& r){ regionFilter_ = r; rebuild(); }
void GalleryModel::setProvFilter(const QString& p) { provFilter_ = p;   rebuild(); }
void GalleryModel::setCategoryFilter(const QString& c){ categoryFilter_ = c; rebuild(); }
void GalleryModel::setPersonFilter(const QString& n){ personFilter_ = n; rebuild(); }
// 方向 C：聚类筛选（key 空串 = 清除，与其余 setter 口径一致）
void GalleryModel::setClusterFilter(int dim, const QString& key)
{
    clusterDim_ = key.isEmpty() ? -1 : dim;
    clusterKey_ = key;
    rebuild();
}
void GalleryModel::clearClusterFilter() { clusterDim_ = -1; clusterKey_.clear(); rebuild(); }
void GalleryModel::setHidePrivate(bool hide)       { hidePrivate_ = hide; rebuild(); }

const Photo* GalleryModel::photoAt(int row) const
{
    if (!all_ || row < 0 || row >= idx_.size()) return nullptr;
    return &(*all_)[idx_[row]];
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
    case RatingRole:  return p.rating;
    case DimRole:     return p.dim;
    case PrivRole:    return p.priv;
    case CategoryRole:return p.category;
    default:          return QVariant();
    }
}

QPixmap GalleryModel::pixmapAt(int row, int level) const
{
    if (!all_ || !cache_ || row < 0 || row >= idx_.size()) return QPixmap();
    const Photo& p = (*all_)[idx_[row]];
    if (p.thumbFile.isEmpty()) return QPixmap();
    // 未命中时触发后台加载；完成后 loaded → onThumbLoaded → dataChanged 刷新该行
    const_cast<GalleryModel*>(this)->cache_->request(
        p.thumbFile, this, [](const QPixmap&){}, level);
    return cache_->cached(p.thumbFile, level);
}

void GalleryModel::onThumbLoaded(const QString& f)
{
    const int row = rowByThumb_.value(f, -1);
    if (row >= 0) {
        const QModelIndex ix = index(row);
        emit dataChanged(ix, ix, {Qt::DisplayRole});
    }
}
