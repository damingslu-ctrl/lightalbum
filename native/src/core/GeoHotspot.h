#pragma once
// 方向 C·联动：相册地理聚合 → 地图热点（World=按 region 国家热点 / Region=按 prov 省热点）。
// 纯函数，与 MapView 渲染、GalleryModel 筛选共用同一 key 语义（铁律：绘制/筛选/门禁同源）。
// 本头自包含：自带 Level 枚举与预投影常量，不依赖 MapView，避免 core↔ui 头耦合环。
#include <QVector>
#include <QString>
#include <QPointF>

struct Photo;   // 前向声明：compute 仅按 const 引用取 Photo，定义体在 .cpp 含 Library.h

class GeoHotspot {
public:
    // 数据
    QString key;          // World: Photo.region（"china"）；Region: Photo.prov（"广东省"）
    QString label;        // 显示名（World 用中文国名，Region 直接用 prov）
    bool    isProvince = false;   // true ⇒ key 走 setProvFilter；false ⇒ key 走 setRegionFilter
    QPointF centroid;     // 预投影空间 [0,MAP_W]×[0,MAP_H]（同 MapView 预投影，绘制/命中同源）
    int     count = 0;    // 该地理桶内照片数（含无 GPS 者；地理归属不依赖坐标）

    // 层级（与 MapView::Level 同序，独立定义以解耦 core↔ui 头环）
    enum class Level { World, Region, Province };
    // 预投影坐标系（与 web 版 eqX/eqY、MapView::MAP_W/H 一致）
    static constexpr double MAP_W = 1000.0, MAP_H = 500.0;

    // 聚合照片为地理热点。
    //   level==World  ⇒ 按 Photo.region 分组（国家/大区热点）
    //   level==Region ⇒ 仅取 Photo.region==regionId 的照片，按 Photo.prov 分组（省热点）
    //   level==Province ⇒ 返回空（本期省视图不叠加热点层）
    // hidePrivate: 与 GalleryModel 默认一致（true 时跳过 p.priv）。
    // centroid 落在预投影空间，MapView 用与足迹点相同的变换落屏。
    static QVector<GeoHotspot> compute(const QVector<Photo>& photos,
                                       bool hidePrivate,
                                       Level level,
                                       const QString& regionId = QString());

    // QA 反例钩子：开启后 World 级误按 prov 聚合 ⇒ key 不再是 region id，
    // 使「key 过滤图库 == hotspot.count」及「region key 存在」断言变红（非空心）。
    // 铁律 22：期望值恒为规格值，不因 broken 改变。
    static void setForceMisaggregateForQa(bool on);
};
