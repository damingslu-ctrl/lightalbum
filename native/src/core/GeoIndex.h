#pragma once
// P10 离线逆地理编码：GPS(lat,lon) → region(十国键) / 省键 / 城市。
// 数据源 = exe 内嵌地图数据 :/data/mapdata.json（P2 足迹页同源，零新增数据集、零云端）：
//   1. 主路径：换算到地图像素投影（x=(lon+180)/360*1000, y=(90-lat)/180*500）后，
//      对 233 省 331 环做 bbox 预筛 + 射线法点内测试 → (region, prov)；
//   2. 兜底：最近城市表（89 城，_gen_geo_cities.py 对齐数据集省键）——
//      ≤1.6° 可救沿海简化缝隙的 region/prov；≤1.0° 才给 ct（城市点），
//      海上/十国之外的点全部诚实留空，绝不编造。
#include <QString>
#include <QVector>

struct GeoResult {
    QString region, prov, city;
    bool empty() const { return region.isEmpty() && prov.isEmpty() && city.isEmpty(); }
};

class GeoIndex {
public:
    static GeoIndex& instance();

    // 懒加载内嵌数据（线程安全性：调用方需在载入期单线程调用，与 Library::open 一致）
    bool ensureLoaded();

    GeoResult resolve(double lat, double lon) const;

private:
    GeoIndex() = default;
    struct Ring {
        QString region, prov;
        double x0 = 0, y0 = 0, x1 = 0, y1 = 0;   // bbox（地图像素）
        QVector<double> pts;                      // 扁平 [x0,y0,x1,y1,...]
    };
    struct City {
        QString name, region, prov;
        double lon = 0, lat = 0;
    };
    QVector<Ring> rings_;
    QVector<City> cities_;
    bool loaded_ = false;
};
