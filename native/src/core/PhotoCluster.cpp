#include "PhotoCluster.h"   // 同目录相对引用（本项目无 include 根）

#include <QCoreApplication>
#include <QHash>
#include <QMap>
#include <algorithm>

namespace PhotoCluster {

namespace {
// QA 反例开关（生产路径恒为 false）。见头文件说明。
bool g_forceDropUnc = false;
}
void setForceDropUnclusteredForQa(bool on) { g_forceDropUnc = on; }
bool forceDropUnclusteredForQa() { return g_forceDropUnc; }

// 哨兵键：真实省/市/年月值里不会出现 '@'，故永不可能碰撞。
// 铁律 7：禁 \xNN 字节转义（QString 按 UTF-16 码点解释，会变成别的字符导致比较恒不相等）；
//         本哨兵刻意用纯 ASCII，既避免转义又便于 grep 核验。
const char* const kUnclustered = "@@unclustered@@";

QString clusterKey(const Photo& p, Dim dim)
{
    switch (dim) {
    case ByProvince: return p.prov.trimmed().isEmpty() ? QString::fromLatin1(kUnclustered) : p.prov.trimmed();
    case ByCity:     return p.city.trimmed().isEmpty() ? QString::fromLatin1(kUnclustered) : p.city.trimmed();
    case ByYear:     return p.year > 0 ? QString::number(p.year) : QString::fromLatin1(kUnclustered);
    case ByMonth: {
        // date 形如 "2024-05-01 ..." 或 "2024:05:01 ..."。取年月；解析失败 → 未分类。
        const QString d = p.date.left(7);
        if (d.size() < 7 || !d.startsWith(QLatin1Char('2'))) return QString::fromLatin1(kUnclustered);
        return d;
    }
    }
    return QString::fromLatin1(kUnclustered);
}

QString clusterLabel(const Photo& p, Dim dim)
{
    const QString k = clusterKey(p, dim);
    if (k == QLatin1String(kUnclustered))
        return QCoreApplication::translate("LightAlbum", "未分类");
    switch (dim) {
    case ByProvince: return p.prov.trimmed();
    case ByCity:     return p.city.trimmed();
    case ByYear:     return k;
    case ByMonth: {
        // "2024-05" → "2024年5月"（中文月份无需本地化，随年份数字走）
        return QCoreApplication::translate("LightAlbum", "%1年%2月")
            .arg(k.left(4)).arg(k.mid(5, 2).toInt());
    }
    }
    return k;
}

QVector<Cluster> cluster(const QVector<Photo>& photos, Dim dim, bool hidePrivate)
{
    QHash<QString, int> pos;                    // key → cluster 下标
    QVector<Cluster> out;
    out.reserve(16);
    for (int i = 0; i < photos.size(); ++i) {
        const Photo& p = photos[i];
        if (hidePrivate && p.priv) continue;    // 与 GalleryModel 口径一致
        const QString k = clusterKey(p, dim);
        // QA 反例：模拟「空键照片被丢弃」的真实缺陷形态 → 总数不守恒 → 门禁必红
        if (g_forceDropUnc && k == QLatin1String(kUnclustered)) continue;
        auto it = pos.find(k);
        if (it == pos.end()) {
            pos.insert(k, out.size());
            Cluster c;
            c.key = k;
            c.label = clusterLabel(p, dim);
            c.count = 1;
            c.indices.append(i);
            out.append(c);
        } else {
            Cluster& c = out[it.value()];
            ++c.count;
            c.indices.append(i);                 // 保持输入原序
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const Cluster& a, const Cluster& b) {
        if (a.count != b.count) return a.count > b.count;   // 多的在前
        return a.label < b.label;                            // 同数按标签升序
    });
    return out;
}

} // namespace PhotoCluster
