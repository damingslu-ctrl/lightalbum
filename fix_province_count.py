#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
1.7.9 修复「足迹省份显示 0」。

根因（与 1.7.7 的 r 缺陷同类）：
  normalizePhoto 的 pv 行原本是 `pv: safeStr(o.pv, '')`，
  纯靠 GPS 定位、未显式填 pv 的照片 pv='' ->
    L6976 / L7034 的 `new Set(PHOTOS.map(p=>p.pv).filter(Boolean)).size` = 0
    => 仪表盘「足迹省份」KPI=0、拍摄地两级树省份子树空、省份下钻无结果。

对称修复（对齐 1.7.7 的 r 修复）：
  1) normalizePhoto 先算出 rVal（国家 id，复用原 r 公式）与 _lo/_la（GPS）；
  2) pv 行增加回退：当 o.pv 为空且已有国家 rVal + GPS 时，
     调用新增的 nearestProvince(rVal, lo, la) 用经纬度反查所属国家的省份。
  3) nearestProvince：把经纬度经 eqX/eqY 投影到与 PROVINCES 省份代表点同源的
     SVG 坐标系，在所属国家(rid)的省份集合里取最近代表点。

改写范式（沿用 CRLF）：二进制读 -> 去 \r\n -> rep(带 count==1 断言) -> 写回 \r\n。
"""
import io, sys

PATH = 'index.html'

def rep(data, old, new, n=1):
    c = data.count(old)
    if c != n:
        raise SystemExit(f"[ASSERT] expected {n} occurrence(s) of:\n{old!r}\nbut found {c}")
    return data.replace(old, new)

with io.open(PATH, 'rb') as f:
    raw = f.read()

data = raw.decode('utf-8').replace('\r\n', '\n')

# --- 1) 插入 nearestProvince 函数（放在 normalizePhoto 之前）---
anchor = "function normalizePhoto(p, i){"
np_fn = (
"// GPS → 省份反查：把经纬度投影到地图 SVG 坐标系，在所属国家(rid)的省份集合里取最近代表点。\n"
"// 与 geoCountry 的「最近质心」思路对称，用于补齐纯 GPS 照片缺失的 pv（1.7.9）。\n"
"function nearestProvince(rid, lon, lat){\n"
"  if(!isFinite(lon) || !isFinite(lat)) return '';\n"
"  const idx = provIndex();\n"
"  const px = eqX(lon), py = eqY(lat);\n"
"  let best = '', bestD = Infinity;\n"
"  for(let i=0;i<idx.length;i++){\n"
"    const it = idx[i];\n"
"    if(rid && it.rid !== rid) continue;       // 只在所属国家范围内匹配\n"
"    const dx = it.x - px, dy = it.y - py, d = dx*dx + dy*dy;\n"
"    if(d < bestD){ bestD = d; best = it.pv; }\n"
"  }\n"
"  return best;\n"
"}\n"
"\n"
)
data = rep(data, anchor, np_fn + anchor, 1)

# --- 2) 在 return { 之前插入 rVal / _lo / _la 计算，复用原 r 公式 ---
anchor2 = "  const seriesKeys = Object.keys(SERIES);\n  return {"
inject = (
"  const seriesKeys = Object.keys(SERIES);\n"
"  const _lo = isFinite(parseFloat(o.lo)) ? parseFloat(o.lo) : null;\n"
"  const _la = isFinite(parseFloat(o.la)) ? parseFloat(o.la) : null;\n"
"  const rVal = regionIdFrom(o.r) || (_lo!==null && _la!==null ? regionIdFrom(geoCountry(_lo, _la)) : null);\n"
"  return {"
)
data = rep(data, anchor2, inject, 1)

# --- 3) r 行改为引用 rVal（原公式已上移，避免重复计算）---
old_r = "    r:    regionIdFrom(o.r) || (isFinite(parseFloat(o.lo)) && isFinite(parseFloat(o.la)) ? regionIdFrom(geoCountry(parseFloat(o.lo), parseFloat(o.la))) : null),"
new_r = "    r:    rVal,"
data = rep(data, old_r, new_r, 1)

# --- 4) pv 行增加 GPS 反查回退 ---
old_pv = "    pv:   safeStr(o.pv, ''),"
new_pv = "    pv:   safeStr(o.pv, '') || (rVal && _lo!==null && _la!==null ? nearestProvince(rVal, _lo, _la) : ''),"
data = rep(data, old_pv, new_pv, 1)

out = data.replace('\n', '\r\n').encode('utf-8')
with io.open(PATH, 'wb') as f:
    f.write(out)

print("fix_province_count: applied 4 replacements (count==1 each).")
