// 相册聚类（方向 C 第一步）：按地点/时间等维度把照片分组，供 GalleryPage 聚类条使用。
//
// 设计要点（对齐项目铁律）：
//  · **纯函数、零 UI 依赖**：输入 Photo 列表 + 维度，输出簇列表。门禁可直接调用，
//    不必构造 MainWindow（与 LA_MAP_*_QA 的做法一致）。
//  · **数据诚实**：键为空的照片必须归入 kUnclustered 簇，**绝不静默丢弃** ——
//    聚类的价值就在于「让用户看见没被归类的那些」，丢了就等于骗用户。
//  · **零模型改动**：Photo 已有 prov/city/year/date/person/category 字段
//    （core/Library.h），本模块只做读取与分组。
#ifndef PHOTOCLUSTER_H
#define PHOTOCLUSTER_H

#include <QString>
#include <QVector>
#include "Library.h"   // 同目录；本项目无 include 根，core 内部头用同目录相对引用

namespace PhotoCluster {

// 聚类维度。Dim 决定 clusterKey() 取哪个字段。
enum Dim { ByProvince = 0, ByCity = 1, ByYear = 2, ByMonth = 3 };

// 空键哨兵：地点/时间缺失的照片归入此簇（显示为「未分类」）。
// 用不可能出现在真实字段里的前缀，避免与真实键碰撞。
extern const char* const kUnclustered;

// 该照片在某维度下的簇键；字段为空时返回 kUnclustered。
QString clusterKey(const Photo& p, Dim dim);

// 簇标签（面向用户）：地点维度直接给中文省/市名；时间维度格式化��
QString clusterLabel(const Photo& p, Dim dim);

struct Cluster {
    QString key;                 // 机器键（用于筛选比较）
    QString label;               // 显示名
    int     count = 0;           // 照片数
    QVector<int> indices;        // 对应输入列表下标（保持原序，稳定）
};

// 聚类主入口。hidePrivate 为真时跳过私密照片（与 GalleryModel 口径一致）。
// 排序规则：簇按 count 降序，同 count 按 label 升序（稳定、可预期）。
// 簇内 indices 保持输入原序。
QVector<Cluster> cluster(const QVector<Photo>& photos, Dim dim, bool hidePrivate);

// QA 反例钩子：置 true 后 cluster() 会**丢弃**键为空的照片（模拟「聚类骗用户」的
// 真实缺陷形态）。生产路径永不调用；仅供 LA_CLUSTER_QA_FORCE_BROKEN 验证
// 「总数守恒」断言确实能抓住这类缺陷（非空心门禁）。默认 false。
void setForceDropUnclusteredForQa(bool on);
bool forceDropUnclusteredForQa();

} // namespace PhotoCluster

#endif // PHOTOCLUSTER_H
