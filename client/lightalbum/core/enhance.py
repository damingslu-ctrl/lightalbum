# -*- coding: utf-8 -*-
"""enhance —— 「先分类后路由」的本地 AI 美图路由与 LUT 应用（纯逻辑，零云端）。

用户需求：将人像美图与风景美图分成两个模型，先判断风景照还是人像照再调用。
路由判定不猜：以库内 AI 分类（classifier :8766 产物，PHOTOS[].cat）为准；
分类缺失返回 None，由调用方跳过并说明（数据诚实铁律，严禁编造默认值）。

模型选型（均 Apache-2.0，本机权重）：
  landscape → Image-Adaptive-3DLUT（<600K 参数；CNN 预测 33³ 自适应 LUT，
              全分辨率以三线性插值应用——「AI 规划，Lumen 执行」，不重绘不幻觉）
  portrait  → GFPGAN v1.4（未安装时服务端 501，绝不编造结果）
"""
from __future__ import annotations

from typing import Optional

import numpy as np

# AI 分类（index.html AI_CAT_ORDER / classifier）→ 路由
ROUTE_TABLE = {
    '人像': 'portrait',
    '风光': 'landscape',
    '夜景·星空': 'landscape',
    '城市·建筑': 'landscape',
    '微距·花卉': 'landscape',
    '动物': 'landscape',
    '美食': 'landscape',
    '黑白': 'landscape',
    '其他': None,          # 语义不明 → 不增强（不猜）
}

ENHANCE_PORT = 8768
LUT_DIM = 33
DEFAULT_STRENGTH = 1.0


def route_for(cat) -> Optional[str]:
    """AI 分类 → 路由。缺失/未知 → None（调用方须跳过而非默认）。"""
    if not cat:
        return None
    return ROUTE_TABLE.get(str(cat).strip())


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


def identity_lut(dim: int = LUT_DIM) -> np.ndarray:
    """恒等 3D LUT (3,dim,dim,dim)，与 IdentityLUT33.txt 语义逐值一致：
    lut[c,i,j,k] 中 c0=R 输出随 axis2(R 输入) 变化、c1=G↔axis1、c2=B↔axis0。
    即轴序 axis0=B、axis1=G、axis2=R；通道堆叠 [R,G,B]。"""
    n = np.linspace(0.0, 1.0, dim, dtype=np.float32)
    r = n.reshape(1, 1, dim).repeat(dim, 0).repeat(dim, 1)   # axis2 = R
    g = n.reshape(1, dim, 1).repeat(dim, 0).repeat(dim, 2)   # axis1 = G
    b = n.reshape(dim, 1, 1).repeat(dim, 1).repeat(dim, 2)   # axis0 = B
    return np.stack([r, g, b], axis=0)


def apply_lut_numpy(img: np.ndarray, lut: np.ndarray) -> np.ndarray:
    """纯 numpy 三线性 LUT 应用（测试基线 + CPU 小图路径）。

    img: (H,W,3) float∈[0,1]；lut: (3,dim,dim,dim)，布局同 identity_lut。
    坐标映射：x=r→width(dim2)、y=g→height(dim1)、z=b→depth(dim0)。
    """
    dim = lut.shape[-1]
    h, w, _ = img.shape
    r = np.clip(img[..., 0], 0.0, 1.0) * (dim - 1)
    g = np.clip(img[..., 1], 0.0, 1.0) * (dim - 1)
    b = np.clip(img[..., 2], 0.0, 1.0) * (dim - 1)

    def _frac(c):
        c0 = np.floor(c).astype(np.int32)
        c0 = np.clip(c0, 0, dim - 2)
        f = (c - c0)[..., None, None]              # (H,W,1,1) 便于广播
        return c0, f

    i0, fr = _frac(r)   # dim2 (width)
    j0, fg = _frac(g)   # dim1 (height)
    k0, fb = _frac(b)   # dim0 (depth)

    H_idx = np.arange(h)[:, None]
    W_idx = np.arange(w)[None, :]

    out = np.zeros((h, w, 3), dtype=np.float32)
    for dk in (0, 1):
        wk = (1.0 - fb) if dk == 0 else fb
        for dj in (0, 1):
            wj = (1.0 - fg) if dj == 0 else fg
            for di in (0, 1):
                wi = (1.0 - fr) if di == 0 else fr
                wgt = (wk * wj * wi)                    # (H,W,1,1)
                # lut[c, k0+dk, j0+dj, i0+di]：切片在前 → (3,H,W)，转置回 (H,W,3)
                val = lut[:, k0 + dk, j0 + dj, i0 + di]
                out += wgt[..., 0] * val.transpose(1, 2, 0)
    return np.clip(out, 0.0, 1.0)


def blend(original: np.ndarray, enhanced: np.ndarray, strength: float) -> np.ndarray:
    """强度融合：out = orig*(1-s) + enh*s；s=0 恒等于原图（宁可不改也不改坏）。"""
    s = float(max(0.0, min(1.0, strength)))
    return original * (1.0 - s) + enhanced * s
