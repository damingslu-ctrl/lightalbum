#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
convert_map.py — LightAlbum 地图数据升级导入器
  · world  : Natural Earth 10m 国家/海岸线 (ne_10m_admin_0_countries.geojson)
              → 外环 + 湖泊内环，反子午线分割，预投影 1000x500，轻量抽稀
  · china  : 阿里 DataV 100000_full.json 全套 34 省级 → 替换 provinces["china"]
             其余 9 个世界大区的省集合原样保留
其余键 (regions/provinces 其他区/provZh/home/mapW/mapH) 原样保留。
下载文件不得放 C 盘；本脚本读取 _mapimport/ 下的源。
"""
import json, math, sys

MAP_W, MAP_H = 1000.0, 500.0
ROOT = r"D:/Test/workbuddy/2026-09-18-09-00-13"
SRC_DIR = ROOT + r"/_mapimport"
OLD = ROOT + r"/native/data/mapdata.json"
NE10M = SRC_DIR + r"/ne_10m_countries.geojson"
DATAV = SRC_DIR + r"/datav_china_100000.json"

# 抽稀阈值（预投影单位）：world 只看全球缩放，0.3≈0.1° 无损；省下钻放大，更细
MIN_DIST_WORLD = 0.3
MIN_DIST_PROV = 0.15


def proj(lon, lat):
    return round((lon + 180.0) / 360.0 * MAP_W, 1), round((90.0 - lat) / 180.0 * MAP_H, 1)


def split_ring(ring):
    """lon/lat 环按 ±180 反子午线拆分，避免横贯伪线。"""
    out = []
    cur = [ring[0]]
    for i in range(1, len(ring)):
        a, b = ring[i - 1], ring[i]
        if abs(b[0] - a[0]) <= 180.0:
            cur.append(b)
        else:
            bl = 180.0 if b[0] > a[0] else -180.0
            t = (bl - a[0]) / (b[0] - a[0])
            latB = a[1] + t * (b[1] - a[1])
            cur.append([bl, latB])
            out.append(cur)
            cur = [[bl, latB], b]
    out.append(cur)
    return out


def simplify(xy):
    """按相邻点最小间距抽稀（投影后）。"""
    if len(xy) < 3:
        return xy
    out = [xy[0]]
    for p in xy[1:]:
        if math.hypot(p[0] - out[-1][0], p[1] - out[-1][1]) >= (
            MIN_DIST_WORLD if simplify.world else MIN_DIST_PROV
        ):
            out.append(p)
    # 闭合点若过近则合并，避免尾巴抖动
    if len(out) >= 2 and math.hypot(out[0][0] - out[-1][0], out[0][1] - out[-1][1]) < (
        MIN_DIST_WORLD if simplify.world else MIN_DIST_PROV
    ):
        out[-1] = out[0]
    return out


def flat(xy):
    f = []
    for x, y in xy:
        f.append(x)
        f.append(y)
    return f


def polygons_of(geom):
    if geom is None:
        return []
    t = geom.get("type")
    c = geom.get("coordinates")
    if t == "Polygon":
        return [c]
    if t == "MultiPolygon":
        return c
    return []


def build_world(geo):
    feats = []
    for f in geo.get("features", []):
        g = f.get("geometry") or {}
        props = f.get("properties") or {}
        name = props.get("NAME") or props.get("ADMIN") or props.get("NAME_LONG")
        if name == "Antarctica":
            continue
        for poly in polygons_of(g):
            exts = split_ring(poly[0])
            hole_flats = []
            for h in poly[1:]:
                for hs in split_ring(h):
                    xy = [proj(*p) for p in hs]
                    xy = simplify(xy)
                    if len(xy) >= 4:
                        hole_flats.append(flat(xy))
            for i, ex in enumerate(exts):
                xy = [proj(*p) for p in ex]
                xy = simplify(xy)
                if len(xy) < 4:
                    continue
                feats.append([flat(xy)] + (hole_flats if i == 0 else []))
    return feats


def largest_exterior(geom):
    """省/州多为 MultiPolygon；取最大一块外环（保持单环格式，避免改 app）。"""
    best = None
    best_area = 0.0
    for poly in polygons_of(geom):
        ext = poly[0]
        xs = [p[0] for p in ext]
        ys = [p[1] for p in ext]
        area = (max(xs) - min(xs)) * (max(ys) - min(ys))
        if area > best_area:
            best_area = area
            best = ext
    return best


def build_china_provinces(datav):
    out = []
    for f in datav.get("features", []):
        props = f.get("properties") or {}
        if props.get("level") != "province":
            continue
        name = props.get("name")
        if not name:
            continue
        ext = largest_exterior(f.get("geometry"))
        if not ext or len(ext) < 4:
            continue
        simplify.world = False
        xy = [proj(*p) for p in ext]
        xy = simplify(xy)
        if len(xy) < 4:
            continue
        # 中心点：优先 DataV center，否则取顶点均值
        center = props.get("center")
        if center:
            cx, cy = proj(center[0], center[1])
        else:
            cx = round(sum(p[0] for p in xy) / len(xy), 1)
            cy = round(sum(p[1] for p in xy) / len(xy), 1)
        # 双层包裹 [name, [flat_ring], cx, cy]：与其余 9 大区历史格式一致，
        # GeoIndex（photo.prov 反查）按 [[扁平环],...] 解析，扁平格式会静默丢省。
        out.append([name, [flat(xy)], cx, cy])
    return out


def main():
    simplify.world = True
    old = json.load(open(OLD, encoding="utf-8"))
    # --- world (10m) ---
    geo = json.load(open(NE10M, encoding="utf-8"))
    world = build_world(geo)
    # --- china provinces (DataV) ---
    datav = json.load(open(DATAV, encoding="utf-8"))
    china = build_china_provinces(datav)
    # --- 组装 ---
    provinces = old.get("provinces", {})
    provinces["china"] = china  # 替换中国区，其余大区保留
    old["world"] = world
    old["provinces"] = provinces
    json.dump(old, open(OLD, "w", encoding="utf-8"), ensure_ascii=False, separators=(",", ":"))

    tot_rings = sum(len(ft) for ft in world)
    holes = sum(len(ft) - 1 for ft in world)
    xs = [c for ft in world for r in ft for c in r[0::2]]
    ys = [c for ft in world for r in ft for c in r[1::2]]
    print("WORLD features:", len(world), "rings(incl holes):", tot_rings, "holes(lakes):", holes)
    print("  x:", round(min(xs), 1), round(max(xs), 1), " y:", round(min(ys), 1), round(max(ys), 1))
    print("CHINA provinces:", len(china))
    print("total provinces across regions:", sum(len(v) for v in provinces.values()))


if __name__ == "__main__":
    main()
