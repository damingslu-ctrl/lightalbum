#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""1.7.7 — fix 拍摄地显示 0：normalizePhoto 把 GPS 反向地理编码得到的国家「名」
错误地直接写进 r（系统要求 r 是 REGIONS 的 id），导致 usedRegions() 永远为空，
拍摄地显示 "0 地"、地图不点亮任何国家、无照片标点。
修复：显式 r 与 GPS 回退都经 regionIdFrom() 归一为 REGIONS id。"""
import io, sys

PATH = 'index.html'

def rep(data, old, new):
    c = data.count(old)
    assert c == 1, 'count==1 expected for edit, got %d' % c
    return data.replace(old, new)

with io.open(PATH, 'rb') as f:
    raw = f.read()
data = raw.decode('utf-8').replace('\r\n', '\n')

old = "    r:    REGIONS.some(x=>x.id===o.r) ? o.r : (isFinite(parseFloat(o.lo)) && isFinite(parseFloat(o.la)) ? geoCountry(parseFloat(o.lo), parseFloat(o.la)) : null),"
new = "    r:    regionIdFrom(o.r) || (isFinite(parseFloat(o.lo)) && isFinite(parseFloat(o.la)) ? regionIdFrom(geoCountry(parseFloat(o.lo), parseFloat(o.la))) : null),"

final = rep(data, old, new)

with io.open(PATH, 'wb') as f:
    f.write(final.replace('\n', '\r\n').encode('utf-8'))

print('OK: region r normalization fixed')
