// 方向 C·联动：相册地理聚合纯函数（门禁与产品绘制共用唯一真源）。
#include "GeoHotspot.h"
#include "Library.h"   // Photo 完整定义（compute 遍历成员）
#include <QHash>
#include <cmath>

namespace {
// World 级 region id → 中文国名（仅已知项；未知回退到 key 本身）
const QHash<QString, QString>& regionZh()
{
    static const QHash<QString, QString> m = {
        {QStringLiteral("china"),      QStringLiteral("中国")},
        {QStringLiteral("usa"),        QStringLiteral("美国")},
        {QStringLiteral("japan"),      QStringLiteral("日本")},
        {QStringLiteral("iceland"),    QStringLiteral("冰岛")},
        {QStringLiteral("norway"),     QStringLiteral("挪威")},
        {QStringLiteral("newzealand"), QStringLiteral("新西兰")},
        {QStringLiteral("morocco"),    QStringLiteral("摩洛哥")},
        {QStringLiteral("namibia"),    QStringLiteral("纳米比亚")},
        {QStringLiteral("peru"),       QStringLiteral("秘鲁")},
        {QStringLiteral("bolivia"),    QStringLiteral("玻利维亚")},
    };
    return m;
}
} // namespace

static bool g_forceMisaggregate = false;
void GeoHotspot::setForceMisaggregateForQa(bool on) { g_forceMisaggregate = on; }

QVector<GeoHotspot> GeoHotspot::compute(const QVector<Photo>& photos,
                                        bool hidePrivate,
                                        Level level,
                                        const QString& regionId)
{
    QVector<GeoHotspot> out;
    if (level == Level::Province) return out;   // 本期省视图不叠加热点层

    const bool broken = g_forceMisaggregate;
    // 正常：World→region，Region→prov；反例：World 误按 prov 聚合
    auto keyOf = [&](const Photo& p) -> QString {
        if (level == Level::World) return broken ? p.prov : p.region;
        return p.prov;   // Region：regionId 已先行过滤
    };

    struct Acc { double sx = 0, sy = 0; int n = 0; int count = 0; };
    QHash<QString, Acc> acc;
    for (const Photo& p : photos) {
        if (hidePrivate && p.priv) continue;
        if (level == Level::Region && p.region.toLower() != regionId.toLower()) continue;
        const QString k = keyOf(p);
        if (k.isEmpty()) continue;
        Acc& a = acc[k];
        a.count += 1;
        if (p.hasGps && !(p.la == 0.0 && p.lo == 0.0)) {
            a.sx += (p.lo + 180.0) / 360.0 * MAP_W;
            a.sy += (90.0 - p.la) / 180.0 * MAP_H;
            a.n  += 1;
        }
    }

    out.reserve(acc.size());
    for (auto it = acc.constBegin(); it != acc.constEnd(); ++it) {
        GeoHotspot h;
        h.key = it.key();
        h.count = it.value().count;
        h.isProvince = (level == Level::Region);
        if (level == Level::World)
            h.label = regionZh().value(it.key().toLower(), it.key());
        else
            h.label = it.key();   // prov 名本身即显示名
        const Acc& a = it.value();
        if (a.n > 0) h.centroid = QPointF(a.sx / a.n, a.sy / a.n);
        else          h.centroid = QPointF();   // 无 GPS：留空（MapView 对 (0,0) 不绘徽章）
        out.append(h);
    }
    return out;
}
