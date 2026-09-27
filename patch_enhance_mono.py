# -*- coding: utf-8 -*-
"""1.9.30 给 enhance.py 增加单色（黑白）检测与路由辅助。rep() 断言 count==1。"""
import io

TARGET = r'D:\Test\workbuddy\2026-09-18-09-00-13\client\lightalbum\core\enhance.py'

OLD = r'''    return ROUTE_TABLE.get(str(cat).strip())


def identity_lut(dim: int = LUT_DIM) -> np.ndarray:'''

NEW = r'''    return ROUTE_TABLE.get(str(cat).strip())


def is_monochrome(img, threshold: float = 0.03) -> bool:
    """像素级单色（黑白）检测：逐像素 (max-min) 通道差均值 < threshold 视为黑白。

    img: (H,W,3) float 0-1（或 0-255 数组会自动归一化）；单通道/灰度图直接判单色。
    threshold 默认 0.03（约 7.6/255），保守避开轻微偏色场景，专抓真·黑白。"""
    arr = np.asarray(img, dtype=np.float32)
    if arr.ndim != 3 or arr.shape[-1] < 3:
        return True
    if arr.max() > 1.5:                       # 0-255 输入归一化到 0-1
        arr = arr / 255.0
    chroma = arr.max(axis=-1) - arr.min(axis=-1)
    return float(chroma.mean()) < threshold


def monochrome_route(cat, mono: bool):
    """给定分类与是否单色，返回最终增强路由。

    单色照片一律改走 landscape 色调增强（避免 portrait 上色器给黑白照染色）；
    其余沿用 ROUTE_TABLE。"""
    r = route_for(cat)
    if mono and r == 'portrait':
        r = 'landscape'
    return r


def identity_lut(dim: int = LUT_DIM) -> np.ndarray:'''


def rep(path, old, new):
    with io.open(path, 'r', encoding='utf-8') as f:
        s = f.read()
    c = s.count(old)
    assert c == 1, 'count=%d (expect 1)' % c
    s = s.replace(old, new)
    with io.open(path, 'w', encoding='utf-8') as f:
        f.write(s)
    print('OK rep', path)


rep(TARGET, OLD, NEW)
print('PATCH_ENHANCE_MONO_DONE')
