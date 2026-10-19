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
        // 故事相册（方向·B）：仅显示某故事的照片路径集合（与其它筛选并存叠加）
        const bool hasStory  = !storyFilter_.isEmpty();
        // 方向 C：聚类筛选与其它筛选并存叠加
        const bool hasCluster = !clusterKey_.isEmpty() && clusterDim_ >= 0;
        // 2.41.0 ⌘K：自然语言过滤袋（同样并存叠加；empty=不启用，零成本短路）
        const bool hasCmd = !cmd_.empty();
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
            if (hasStory && !storyFilter_.contains(p.path))  // 故事相册（方向·B）
                continue;
            if (hasCluster) {   // 方向 C：簇键比较（真源在 core/PhotoCluster）
                if (PhotoCluster::clusterKey(p, static_cast<PhotoCluster::Dim>(clusterDim_)) != clusterKey_)
                    continue;
            }
            if (hasCmd) {   // 2.41.0 ⌘K：年/月/日/季/人/地/风格/兜底词，逐条 AND
                if (cmd_.yearMin != 0
                    && !(p.year >= cmd_.yearMin && p.year <= cmd_.yearMax))
                    continue;
                if (cmd_.month != 0 || cmd_.day != 0 || !cmd_.season.isEmpty()) {
                    // 时间谓词需要真日期：解不出日期的照片在有时间条件时一律落选
                    const QDate d = photoDateOf(p.date);
                    if (!d.isValid()) continue;
                    if (cmd_.month != 0 && d.month() != cmd_.month) continue;
                    if (cmd_.day   != 0 && d.day()   != cmd_.day)   continue;
                    if (!cmd_.season.isEmpty() && seasonNameOf(d.month()) != cmd_.season)
                        continue;
                }
                if (!cmd_.person.isEmpty()
                    && !p.person.contains(cmd_.person, Qt::CaseInsensitive))
                    continue;
                if (!cmd_.tokenPlace.isEmpty()
                    && !p.city.contains(cmd_.tokenPlace, Qt::CaseInsensitive)
                    && !p.prov.contains(cmd_.tokenPlace, Qt::CaseInsensitive)
                    && !p.region.contains(cmd_.tokenPlace, Qt::CaseInsensitive))
                    continue;
                if (!cmd_.category.isEmpty() && p.category != cmd_.category)
                    continue;
                if (!cmd_.text.isEmpty()
                    && !p.title.contains(cmd_.text, Qt::CaseInsensitive)
                    && !p.rel.contains(cmd_.text, Qt::CaseInsensitive)
                    && !p.cam.contains(cmd_.text, Qt::CaseInsensitive))
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
// 故事相册（方向·B）：setStoryFilter(空列表) = 清除（与 setPersonFilter 口径一致）
void GalleryModel::setStoryFilter(const QStringList& paths)
{
    storyFilter_.clear();
    for (const QString& p : paths) storyFilter_.insert(p);
    rebuild();
}
void GalleryModel::clearStoryFilter() { storyFilter_.clear(); rebuild(); }
// 方向 C：聚类筛选（key 空串 = 清除，与其余 setter 口径一致）
void GalleryModel::setClusterFilter(int dim, const QString& key)
{
    clusterDim_ = key.isEmpty() ? -1 : dim;
    clusterKey_ = key;
    rebuild();
}
void GalleryModel::clearClusterFilter() { clusterDim_ = -1; clusterKey_.clear(); rebuild(); }
// 2.41.0 ⌘K：filter 空 = 清除（与其余 setter 口径一致）
void GalleryModel::setCmdFilter(const CmdFilter& f) { cmd_ = f;    rebuild(); }
void GalleryModel::clearCmdFilter()                 { cmd_ = CmdFilter(); rebuild(); }
void GalleryModel::setHidePrivate(bool hide)       { hidePrivate_ = hide; rebuild(); }

const Photo* GalleryModel::photoAt(int row) const
{
    if (!all_ || row < 0 || row >= idx_.size()) return nullptr;
    return &(*all_)[idx_[row]];
}

QStringList GalleryModel::visiblePaths() const
{
    QStringList out;
    if (!all_) return out;
    out.reserve(idx_.size());
    for (int row = 0; row < idx_.size(); ++row)
        out.append((*all_)[idx_[row]].path);
    return out;
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
    case MediaTypeRole:return p.mediaType;
    case DurRole:      return p.durSec;
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
