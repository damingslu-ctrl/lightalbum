# -*- coding: utf-8 -*-
"""光影集 · 智能修图核心「流明 Lumen」（纯 Pillow，零 numpy/torch 依赖）。

设计取向
────────
参考开源社区同类方案（自动色彩校正 / 自适应亮度对比度 / 局部对比度增强）
的共同思路：先**测量**（直方图统计量），再**自适应调整**（幅度由测量值决定，
而不是死参数）。本实现额外利用了项目独有的优势 —— 每幅照片都有真实 EXIF：

*   ISO ≥ 3200 或曝光 ≥ 1s → 判定「高感光/长曝」，自动加轻度降噪；
*   快门远快于焦距倒数且画面偏暗 → 判定可能欠曝，亮度提升幅度放宽。

所有调整幅度都有夹取（clamp），宁可保守也不毁掉原片的色彩风格 ——
修图是「提建议」，不是「做决定」。

管线（auto 预设）
────────────────
1. 白平衡   Shades-of-Gray（Minkowski p=6）估光源，线性光 von Kries 校正，增益夹取 ±6%
2. 自动色阶 ImageOps.autocontrast，前后各裁 0.5% 极值（仅「发灰」时触发）
3. 亮度     依据平均亮度 luma 向 0.46 收敛，并按高光余量限幅（宁欠勿爆）
4. 对比度   依据亮度标准差向目标区间收敛
5. 饱和度   平均饱和度低于 0.24 才提升，避免艳俗
6. 降噪     由 EXIF（ISO/快门）或画面噪声估计决定；分级：轻噪轻度高斯、强噪中值滤波，
            并在 YCbCr 色度通道更激进地压制彩色噪点；画面干净就不动
7. 锐化     UnsharpMask，半径随像素尺寸自适应
8. 去雾     （暗通道先验，纯 Pillow）仅显式 dehaze 预设或 auto 强雾时启用，受输出自检保护
9. 水平校正   （地平线拉直，纯 Pillow Hough）仅显式 level 预设启用，绝不 auto 触发
10. 暗角校正  （径向增益复原，纯 Pillow）仅显式 vignette 预设启用，绝不 auto 触发；
            无明显暗角则自动跳过
11. 人像美化  （肤色磨皮 + 美白，纯 Pillow）仅显式 portrait 预设启用，绝不 auto 触发；
            以 YCbCr 肤色掩膜限定作用区，仅肤色区做边缘保持磨皮，无肤色则跳过
12. 风格化    （胶片 film / 黑白 bw / 柔光 glow，纯 Pillow）仅显式预设启用，绝不 auto 触发；
            作为「最终风格层」作用于已校正画面（分离色调 / 胶片影调 / 颗粒 / 通道混合 / Orton）
13. 色差校正  （横向色差 / 紫边消除，纯 Pillow）仅显式 chromatic 预设启用，绝不 auto 触发；
            径向对齐 R/B 通道收回边缘镶边，边缘镜像兜底无黑边
14. 镜头畸变校正（桶形径向反畸变，纯 Pillow）仅显式 lens 预设启用，绝不 auto 触发；
            桶形模型 r_d=r·(1+k·r²) 反推，N×N 分块逐块 Image.AFFINE 反算
            （AFFINE 喂 tile 局部坐标，矩阵须以局部 dst 配全局 src），边缘镜像兜底无黑边
15. 红眼去除  （保守启发式，纯 Pillow）仅显式 redeye 预设启用，绝不 auto 触发；
            高亮强红像素压暗去红，面积闸门防误伤，需高鲁棒性应接外部关键点服务
16. HDR 色调映射（局部光照引导增益 + 高光软膝，纯 Pillow）仅显式 hdr 预设启用，
            绝不 auto 触发；压缩动态范围、抬暗部细节而不抹高频
17. 明度结构  （明度通道细尺度 USM + 中调掩膜，纯 Pillow）仅显式 structure 预设启用，
            绝不 auto 触发；提质感/立体感，深阴影与高光不动
18. 局部提亮  （dodge：局部光照引导的暗部提亮，纯 Pillow）仅显式 dodge 预设启用，
            绝不 auto 触发；只提暗部、亮部不动，乘黑场斜坡不抬真黑、单像素夹取不推爆
19. 导出     写到原文件旁 `<名>_retouched.jpg`，**绝不覆盖原片**，
            用 piexif.transplant 完整保留 EXIF/GPS

参考实现（GitHub / 社区）
────────────────────────
* Shades-of-Gray（Minkowski p=6）是色彩恒常性的稳健默认：
  Finlayson & Trezzi, "Shades of Gray and Colour Constancy" (2004)。
  工程实现参考 ComfyUI-Darkroom 的 Auto White Balance 节点、ffmpeg 的
  `colorconstancy`（difford=0 / minknorm=6）。
* 提亮中间调而不炸高光，应作用在**明度通道**而非直接乘 RGB；
  见 Clahe-Gamma-Image-Enhancer 等实现。
* 去雾采用**暗通道先验**（Kaiming He et al., "Single Image Haze Removal Using Dark
  Channel Prior", CVPR 2009），透射率细化用**导向滤波**（Kaiming He et al., ECCV 2010）
  的高斯近似；工程实现对照 ZQPei/Haze_Removal_python、cssartori/image-dehazing、
  He-Zhang/image_dehaze（均为纯 Python）。
* 暗角校正采用**径向多项式晕影模型**（Goldman & Chen, "Vignette and Exposure
  Calibration and Compensation", ICCV 2005）的简化单图版；工程实现对照
  AlbertHuyb/VignetteRemoval、quentinkaci/vignetting_correction（对数强度熵约束最小化）、
  TomNaiser/Gigapixel-Tools · VignettingCorrection.py（归一化掩膜 >1 倍乘、裁剪）。
* 降噪分级依噪声类型选滤波器（高斯噪→GaussianBlur，椒盐噪→MedianFilter），并在
  YCbCr 色度通道加强抑制彩色噪点 —— 社区（Pillow 生态）通用最佳实践。
"""
from __future__ import annotations

import io
import math
import os
import threading

import piexif
from PIL import Image, ImageChops, ImageEnhance, ImageFilter, ImageOps

# 引擎命名与版本（来自产品级单一事实源；独立运行/测试时兜底）
try:
    from ..version import ENGINE_NAME, ENGINE_NAME_CN, ENGINE_VERSION
except Exception:                                   # noqa: BLE001
    ENGINE_NAME, ENGINE_NAME_CN, ENGINE_VERSION = 'Lumen', '流明', '1.5.1'
ENGINE_ID = '%s %s' % (ENGINE_NAME, ENGINE_VERSION)     # 如 'Lumen 1.0.0'

# 预设 → 各环节幅度倍率（soft=轻、auto=标准、strong=强）
PRESETS = {
    'soft':   {'wb': 0.5, 'tone': 0.5, 'levels': 0.6, 'bright': 0.6, 'contrast': 0.6,
               'clarity': 0.5, 'vibrance': 0.6, 'color': 0.6, 'sharp': 0.7},
    'auto':   {'wb': 1.0, 'tone': 1.0, 'levels': 1.0, 'bright': 1.0, 'contrast': 1.0,
               'clarity': 1.0, 'vibrance': 1.0, 'color': 1.0, 'sharp': 1.0},
    'strong': {'wb': 1.4, 'tone': 1.3, 'levels': 1.3, 'bright': 1.35, 'contrast': 1.3,
               'clarity': 1.3, 'vibrance': 1.3, 'color': 1.3, 'sharp': 1.3},
}

# auto 预设下，雾化评分(_measure_extra['haze'])超过此值才去雾；取值保守，避免误伤正常照片。
# 正常彩色照片 ≈0.0~0.2，明显雾/霾风景 ≥0.6，故 0.64 为安全阈值（仍有二次对比门禁兜底）。
HAZE_TRIGGER = 0.64

# 暗角校正：估计到的径向压暗系数 k（v(r)=1-k·r²）低于此值即判定「无明显暗角」，不动作。
# 0.05 意味着角部仅比中心暗 <5% —— 多半是内容本身而非镜头晕影，宁可不修（不乱修）。
VIGNETTE_MIN_K = 0.05
# 角部增益上限：把晕影复原的放大倍数夹在 ≤1.8×，避免把本来偏暗的角部推爆/推噪。
VIGNETTE_MAX_GAIN = 1.8


# ------------------------------------------------------------ 测量
def _stats(img):
    """计算亮度均值/标准差与平均饱和度（0..1 缩略图上测，够准且快）。"""
    small = img.convert('RGB')
    small.thumbnail((256, 256))
    hsv = small.convert('HSV')
    h = hsv.histogram()          # 3 通道连续排列：H=0..255, S=256..511, V=512..767
    n = small.size[0] * small.size[1]
    vs = h[512:768]              # V 通道（亮度）
    ss = h[256:512]              # S 通道（饱和度）
    # 早先切成 [384:640]，横跨 S 尾和 V 头，所有统计量全错（暗图测出 0.6，实测踩过）
    mean_v = sum(i * c for i, c in enumerate(vs)) / max(1, sum(vs)) / 255.0
    var = sum(((i / 255.0) - mean_v) ** 2 * c for i, c in enumerate(vs)) / max(1, sum(vs))
    mean_s = sum(i * c for i, c in enumerate(ss)) / max(1, sum(ss)) / 255.0
    tot = max(1, sum(vs))
    hi = sum(c for i, c in enumerate(vs) if i >= 250) / tot   # 接近削顶的高光占比
    lo = sum(c for i, c in enumerate(vs) if i <= 5) / tot     # 接近死黑的暗部占比
    return {'lum': mean_v, 'lum_std': var ** 0.5, 'sat': mean_s, 'hi': hi, 'lo': lo}


# ------------------------------------------------------------ 分析用图像加载（带缓存）
# 测量 / 建议这类「只看画面特征、不需要全分辨率」的路径，原先都是先解整张大图、
# 再转 RGB、再缩，对 24MP 照片是典型卡顿源。这里统一在解码期就降采样（JPEG 走
# Image.draft；其余格式退化为 thumbnail），并把结果按文件身份缓存 —— 同一次会话里
# 用户先点「AI 建议」再点「一键修图」不会重复解同一张图。
_ANALYZE_CACHE = {}
_ANALYZE_LOCK = threading.Lock()


def _load_analyze(path, long_edge=1024):
    """打开并限幅到 long_edge 的 RGB 图；按 (路径, mtime_ns, size) 缓存解码结果。

    返回副本，调用方可自由修改；文件被改动（mtime/size 变化）即自动失效。
    """
    try:
        st = os.stat(path)
        key = (os.path.abspath(path), int(st.st_mtime_ns), st.st_size, int(long_edge))
    except Exception:                       # noqa: BLE001
        key = (os.path.abspath(path), 0, 0, int(long_edge))
    with _ANALYZE_LOCK:
        cached = _ANALYZE_CACHE.get(key)
        if cached is not None:
            return cached.copy()
    with Image.open(path) as im:
        try:                                # JPEG：解码期就降采样（大图省 10～50× 解码）
            im.draft('RGB', (long_edge, long_edge))
        except Exception:                   # noqa: BLE001
            pass
        im = ImageOps.exif_transpose(im).convert('RGB')
        if max(im.size) > long_edge:
            im.thumbnail((long_edge, long_edge))
    with _ANALYZE_LOCK:
        if len(_ANALYZE_CACHE) > 32:        # 限制长会话内存膨胀
            _ANALYZE_CACHE.clear()
        _ANALYZE_CACHE[key] = im
        return im.copy()


def _denoise_level(exif):
    """由 EXIF 决定降噪强度：0=不降噪 1=轻度 2=中轻度。

    高 ISO / 长曝是电子噪声的主要来源；画面干净的日间照片完全不碰，
    避免把细节抹掉。
    """
    iso = 0
    shutter = 0.0
    if exif:
        iso = int(exif.get('iso') or 0)
        shutter = float(exif.get('shutter') or 0)
    if iso >= 6400 or shutter >= 4.0:
        return 2
    if iso >= 3200 or shutter >= 1.0:
        return 1
    return 0


# ------------------------------------------------------------ 各环节
P_SHADES = 6.0          # Shades-of-Gray 的 Minkowski 范数（Finlayson & Trezzi，p=6 为稳健默认）


def _srgb_to_lin(c):
    """sRGB(0..1) → 线性光(0..1)。"""
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def _lin_to_srgb(l):
    """线性光(0..1) → sRGB(0..1)。"""
    return 12.92 * l if l <= 0.0031308 else 1.055 * (l ** (1.0 / 2.4)) - 0.055


def _lin_hist_lut():
    """sRGB 8bit → 线性光的 8bit 量化，用于在直方图上按线性光做统计。"""
    return [int(round(255.0 * _srgb_to_lin(v / 255.0))) for v in range(256)]


def _wb_gain_lut(gain):
    """在线性光里乘增益、再编码回 sRGB 的 256 级 LUT（von Kries 对角校正）。

    直接在 sRGB（非线性）域相乘会带来明度/色相偏移，线性光才是物理正确做法。
    """
    out = []
    for v in range(256):
        lin = _srgb_to_lin(v / 255.0) * gain
        lin = max(0.0, min(1.0, lin))
        out.append(int(max(0, min(255, round(_lin_to_srgb(lin) * 255.0)))))
    return out


def _white_balance(img, k):
    """Shades-of-Gray（Minkowski p=6）估光源 + 线性光 von Kries 校正，增益夹取 ±6%*k。

    为什么不用灰世界：灰世界假设「全图均值是灰」，在单一色主导的画面（大片草地、
    晚霞）会过度校正、把创作色调拉平。SoG 用 p-范数加权、偏向亮部（更可能是中性面），
    对这类场景稳得多，是社区公认的稳健默认（p=6）。

    返回 (图, gains) —— gains 供前端展示「改了什么」。
    """
    img = img.convert('RGB')
    small = img.copy()
    small.thumbnail((160, 160))
    lin_lut = _lin_hist_lut()
    n = max(1, small.size[0] * small.size[1])
    bands = small.split()
    ks = []
    for ch in range(3):
        hist = bands[ch].point(lin_lut).histogram()
        acc = 0.0
        for i, c in enumerate(hist):
            if c:
                acc += c * ((i / 255.0) ** P_SHADES)     # 在直方图上求 lin^p 的均值
        ks.append((acc / n) ** (1.0 / P_SHADES))
    base = sum(ks) / 3.0
    if base <= 1e-9:
        return img, None
    lo = -0.06 * k if k <= 1 else -0.08
    hi_lim = 0.06 * k if k <= 1 else 0.10
    gains = []
    for kc in ks:
        if kc <= 1e-12:
            gains.append(1.0)
            continue
        g = (base / kc - 1.0) * k
        gains.append(1.0 + max(lo, min(hi_lim, g)))
    if all(abs(g - 1.0) < 0.005 for g in gains):
        return img, None
    r, g_, b = img.split()
    r = r.point(_wb_gain_lut(gains[0]))
    g_ = g_.point(_wb_gain_lut(gains[1]))
    b = b.point(_wb_gain_lut(gains[2]))
    return Image.merge('RGB', (r, g_, b)), [round(x, 3) for x in gains]


def _auto_levels(img, st, mx, k):
    """只修「发灰/对比不足」的照片，且是**有界**拉伸（本身对比足够的绝不动）。

    为什么不用 ImageOps.autocontrast：它把 [p_low, p_high] **无条件映射到 [0,255]**，
    窄动态范围的照片（薄雾、逆光、低对比）会被直接推爆高光 —— 实测就是这么被
    「输出自检」拦下来的。这里改为映射到目标区间 [6, 249]（留出两端余量），
    并只取**亮度直方图**的分位（autocontrast 是按通道各自拉伸，会引入偏色）。
    """
    if st['lum_std'] > 0.17:
        return img, False
    small = img.convert('L')
    small.thumbnail((256, 256))
    h = small.histogram()
    tot = max(1, sum(h))
    cut = max(1, int(tot * 0.005))
    lo = 0
    acc = 0
    for i, c in enumerate(h):
        acc += c
        if acc >= cut:
            lo = i
            break
    hi = 255
    acc = 0
    for i in range(255, -1, -1):
        acc += h[i]
        if acc >= cut:
            hi = i
            break
    if hi - lo < 8:
        return img, False
    tlo, thi = 6.0, 249.0
    span = float(hi - lo)
    lut = [max(0, min(255, int(round(tlo + (v - lo) * (thi - tlo) / span))))
           for v in range(256)]
    if k < 1.0:                      # 轻力度：向恒等曲线收敛
        lut = [int(round(v + (lut[v] - v) * k)) for v in range(256)]
    return img.point(lut * 3), True


def _brightness(img, st, k):
    """按实测亮度向 0.46 收敛；**按当前画面的高光余量限幅**（宁欠勿爆）。

    余量用 V 通道的 99.5 分位估计：提亮后它不应超过 252，故增益上限 = 252 / p99.5。
    只在超过时收紧 —— 只减不增，不改变画风取向。
    （早先用「≥250 像素占比」判断，但前面的色阶可能已把画面推到 249，
      这种「接近削顶」用占比判不出来 —— 实测就是这么被「输出自检」拦下的。）
    """
    target = 0.46
    d = target - st['lum']
    if abs(d) < 0.03:                       # 曝光正常就不动
        return img, None
    f = 1.0 + d * 1.6 * k
    if f > 1.0:
        small = img.convert('HSV')
        small.thumbnail((256, 256))
        vs = small.histogram()[512:768]
        tot = max(1, sum(vs))
        acc = 0
        p995 = 255
        need = tot * 0.995
        for i, c in enumerate(vs):
            acc += c
            if acc >= need:
                p995 = i
                break
        cap = max(1.0, 252.0 / max(1.0, float(p995)))   # 不把「提亮」变成「压暗」
        f = min(f, cap)
    # 夹取 0.60..1.80：等价于最多 ±0.85EV 的曝光补偿，极端欠曝/过曝都救得动，
    # 又不会把正常照片推爆。早先上限 1.30 连严重欠曝都救不动（实测踩过）。
    f = max(0.60, min(1.80, f))
    if abs(f - 1.0) < 0.005:
        return img, None
    return ImageEnhance.Brightness(img).enhance(f), round(f, 3)


def _contrast(img, st, k):
    """对比度 LUT，以**本图的实测均值**为轴心。

    不能用 ImageEnhance.Contrast：它固定以 128 为轴心，
    (v-128)*f+128 会把整体偏暗的照片越调越黑（0.098 → 0.082，实测踩过）；
    偏亮的照片则会抵消亮度修正。以实测均值为轴心只展开分布、不动影调。
    """
    d = 0.19 - st['lum_std']
    if abs(d) < 0.02:
        return img, None
    f = 1.0 + d * 1.1 * k
    f = max(0.92, min(1.32, f))
    mid = st['lum'] * 255.0
    lut = [max(0, min(255, int((v - mid) * f + mid + 0.5))) for v in range(256)]
    return img.point(lut * 3), round(f, 3)   # RGB 图的 point() 需要 3×256 的 LUT


def _color(img, st, k):
    if st['sat'] >= 0.24:                   # 色彩已够，不推饱和度
        return img, None
    d = 0.24 - st['sat']
    f = 1.0 + d * 1.4 * k
    f = max(0.95, min(1.30, f))
    return ImageEnhance.Color(img).enhance(f), round(f, 3)


def _denoise(img, level):
    """分级降噪（纯 Pillow，参考社区最佳实践）。

    噪声类型决定滤波器（Pillow 官方/社区共识）：
    *   **高斯 / 随机（传感器）噪声** → GaussianBlur（radius 0.8~1.6），
        对弱细节最友好；
    *   **椒盐 / 脉冲噪声** → MedianFilter（size=3），对离群像素鲁棒、保阶跃边缘；
    *   **彩色噪点（高 ISO 常见色斑）** → 在 YCbCr 的 **Cb/Cr 色度通道**上更强平滑 ——
        人眼对色度分辨率不敏感，可大胆压制色噪，而亮度通道保守处理以保细节。
        这正是社区推荐的「RGB→YCbCr，Y 通道降噪后再合并」流程（避免彩噪扩散/边缘光晕）。

    于是分两级：
    *   level 1（轻噪）：Y 轻度高斯；Cb/Cr 轻度高斯。比原先的一律 MedianFilter(3)
        更保细节、不发糊，贴合「不乱修」。
    *   level 2（强噪）：Y 中值滤波（去成簇噪点）+ 轻度平滑；Cb/Cr 更强平滑。

    注：YCbCr 往返是轻微有损的（≤1~2 级），故仅在确有降噪需要（level≥1）时才转；
    不需要降噪时上层直接返回原图，不做无谓的色彩空间往返。
    """
    if level <= 0:
        return img
    work = img.convert('YCbCr')
    y, cb, cr = work.split()
    if level == 1:
        y = y.filter(ImageFilter.GaussianBlur(1.0))
        cb = cb.filter(ImageFilter.GaussianBlur(1.2))
        cr = cr.filter(ImageFilter.GaussianBlur(1.2))
    else:
        y = y.filter(ImageFilter.MedianFilter(3)).filter(ImageFilter.SMOOTH)
        cb = cb.filter(ImageFilter.MedianFilter(3)).filter(ImageFilter.GaussianBlur(1.6))
        cr = cr.filter(ImageFilter.MedianFilter(3)).filter(ImageFilter.GaussianBlur(1.6))
    return Image.merge('YCbCr', (y, cb, cr)).convert('RGB')


def _sharpen(img, k, exif, noise=0.0, portrait=False):
    """出图锐化（UnsharpMask）。参数按「题材 + 噪声」调整。

    参考社区通用参数表：人像取保守（~50-75% / 阈值 3-10），风光可强些（100-150% / 阈值 2-5）；
    有噪点时提高阈值，避免把噪声一起锐出来（也符合「先降噪再锐化」的次序）。
    """
    w, h = img.size
    radius = 1.2 if max(w, h) >= 3000 else 1.0
    percent = int(min(140, 70 * k))
    if portrait:
        percent = int(percent * 0.7)
    threshold = 2
    if noise >= 0.035:
        threshold = 5
    return (img.filter(ImageFilter.UnsharpMask(radius=radius, percent=percent,
                                               threshold=threshold)),
            {'radius': radius, 'percent': percent, 'threshold': threshold})


# ------------------------------------------------------------ 测量（补充）
def _hp_stats(img, radius):
    """高通残差统计：mean（结构能量）/ median（多数像素的高频水平 ≈ 噪声底）。

    用 |亮度 − 高斯模糊(亮度)| 做高通；中位数对「细节只占少数像素」这一点鲁棒，
    因此适合当噪声代理；均值则反映整体结构能量。
    """
    g = img.convert('L')
    small = g.copy()
    small.thumbnail((256, 256))
    hp = ImageChops.difference(small, small.filter(ImageFilter.GaussianBlur(radius)))
    h = hp.histogram()
    tot = max(1, small.size[0] * small.size[1])
    mean = sum(i * c for i, c in enumerate(h)) / tot / 255.0
    acc = 0
    med = 0
    for i, c in enumerate(h):
        acc += c
        if acc >= tot / 2.0:
            med = i
            break
    return {'mean': mean, 'median': med / 255.0}


def _measure_extra(img):
    """补充测量：局部对比(lc) / 噪声(noise) / 结构能量(sharp) / 动态范围(dr) / 色偏(cast)。"""
    hp1 = _hp_stats(img, 1.0)
    hp6 = _hp_stats(img, 6.0)
    small = img.convert('RGB')
    small.thumbnail((256, 256))
    vh = small.convert('HSV').histogram()[512:768]
    tot = max(1, sum(vh))

    def pct(p):
        acc = 0
        for i, c in enumerate(vh):
            acc += c
            if acc >= tot * p:
                return i / 255.0
        return 1.0

    p1, p99 = pct(0.01), pct(0.99)
    dr = max(0.0, p99 - p1)
    hist = small.histogram()
    ch_means = []
    for ch in range(3):
        hh = hist[ch * 256:(ch + 1) * 256]
        ch_means.append(sum(i * c for i, c in enumerate(hh)) / max(1, sum(hh)))
    overall = max(1e-6, sum(ch_means) / 3.0)
    # 雾感评分：亮区被洗白(饱和低) + 动态范围窄 + 整体偏亮 → 很像雾/霾。
    # （符合 DCP 论文直觉：雾把场景反差与色彩压向大气光 A；参考 ZQPei/Haze_Removal_python。）
    hsv = small.convert('HSV')
    bs = []
    for (_, sv, vv) in hsv.getdata():
        if vv >= 200:
            bs.append(sv)
    mean_bs = (sum(bs) / len(bs) / 255.0) if bs else 0.0
    wash = max(0.0, (0.12 - mean_bs) / 0.12)
    flat = max(0.0, (0.16 - dr) / 0.16)
    bright = max(0.0, (overall / 255.0 - 0.52) / 0.40)
    haze = max(0.0, min(1.0, 0.5 * wash + 0.3 * flat + 0.3 * bright))
    return {'lc': hp6['mean'], 'noise': hp1['median'], 'sharp': hp1['mean'],
            'p1': p1, 'p99': p99, 'dr': dr,
            'cast': (max(ch_means) - min(ch_means)) / overall, 'haze': haze}


# ------------------------------------------------------------ 新算子
def _tone_curve(img, st, k):
    """**局部（掩膜式）**阴影提亮 + 高光回收 —— 参考 OpenCV 阴影校正管线。

    与「全局曲线」不同，它不会把整张图抬成灰雾：
      1. 大半径高斯估计**局部光照** m（多尺度 Retinex 的最小可用版）；
      2. 阴影权重只在 m 偏暗处为 1，并按饱和度轻微抑制（高饱和暗部多为有色物体而非欠曝）；
      3. 权重再高斯柔化 → 边界平滑，杜绝光晕（halo）；
      4. 提亮量乘「黑场斜坡」（V→0 时归零）→ **黑场不被抬高**，夜景不会变灰；
      5. 高光侧同理，仅在局部偏亮处回收，且单像素回收量有上限（纯白不会被压灰）。
    掩膜在缩略图上计算后放大 —— 低频频段无损，且避免大图上超大半径高斯过慢。
    触发：暗部死黑占比高才抬阴影；高光削顶占比高才压高光。
    """
    lo = float(st.get('lo') or 0.0)
    hi = float(st.get('hi') or 0.0)
    lift = min(0.50, max(0.0, (lo - 0.02) * 4.0)) * k      # 抬阴影强度
    rec = min(0.35, max(0.0, (hi - 0.01) * 5.0)) * k        # 压高光强度
    if lift < 0.02 and rec < 0.02:
        return img, None

    hh, ss, vv = img.convert('HSV').split()
    sc = min(1.0, 512.0 / max(1, max(img.size)))
    sm = img.convert('HSV')
    if sc < 1.0:
        sm = sm.resize((max(1, int(img.size[0] * sc)), max(1, int(img.size[1] * sc))),
                       Image.BILINEAR)
    _hs, ss2, vs2 = sm.split()
    rs = max(3.0, min(sm.size) / 40.0)
    mm = vs2.filter(ImageFilter.GaussianBlur(rs))           # 局部光照

    out_v = vv
    if lift >= 0.02:
        T = 130.0
        w = mm.point([int(max(0, min(255, round(
            255.0 * (max(0.0, (T - x) / T) ** 1.6))))) for x in range(256)])
        gate = ss2.point([int(max(0, min(255, round(
            255.0 * (1.0 - 0.5 * (x / 255.0)))))) for x in range(256)])
        wm = ImageChops.multiply(w, gate).filter(ImageFilter.GaussianBlur(rs))
        if sc < 1.0:
            wm = wm.resize(img.size, Image.BILINEAR)
        ramp = vv.point([min(255, int(round(x * 255.0 / 24.0))) for x in range(256)])
        addv = ImageChops.multiply(ImageChops.multiply(ImageChops.invert(vv), wm), ramp)
        addv = addv.point([int(x * lift) for x in range(256)])
        out_v = ImageChops.add(out_v, addv)
    if rec >= 0.02:
        T2 = 170.0
        w2 = mm.point([int(max(0, min(255, round(
            255.0 * (max(0.0, (x - T2) / (255.0 - T2)) ** 1.6))))) for x in range(256)])
        w2 = w2.filter(ImageFilter.GaussianBlur(rs))
        if sc < 1.0:
            w2 = w2.resize(img.size, Image.BILINEAR)
        capped = ImageChops.multiply(vv, w2).point([min(x, 40) for x in range(256)])
        subv = capped.point([int(x * rec) for x in range(256)])
        out_v = ImageChops.subtract(out_v, subv)

    if out_v is vv:
        return img, None
    return (Image.merge('HSV', (hh, ss, out_v)).convert('RGB'),
            {'lift': round(lift, 3), 'recover': round(rec, 3)})


def _clarity(img, mx, k):
    """清晰度 = 局部对比（大半径非锐化掩模，作用在明度通道 V 上）。

    只修「发灰/发闷」（局部对比 lc 偏低）；已有足够微反差的不动，避免脏、避免光晕。
    """
    lc = float(mx.get('lc') or 0.0)
    if lc >= 0.045:          # 已有足够微反差
        return img, None
    if lc < 0.006:           # 极其平滑（天空/虚化）——没有细节可提，别放大噪声
        return img, None
    amt = min(0.45, (0.045 - lc) * 8.0) * k
    if amt < 0.05:
        return img, None
    h, s, v = img.convert('HSV').split()
    v = v.filter(ImageFilter.UnsharpMask(radius=6, percent=int(round(amt * 100)),
                                         threshold=0))
    return Image.merge('HSV', (h, s, v)).convert('RGB'), round(amt, 3)


def _vibrance(img, st, k):
    """自然饱和度：淡的地方补得多，浓的地方几乎不动（增益随自身饱和度衰减）。

    s' = s · (1 + v·(1 - s/255))。作用在 HSV 的 S 通道 → 色相与明度都不动，
    比全局饱和度更不容易「艳俗/过饱和」。
    """
    sat = float(st.get('sat') or 0.0)
    if sat >= 0.30:          # 色彩已够
        return img, None
    if sat < 0.02:           # 近似黑白：没有彩度可提，不动（也不记步骤）
        return img, None
    v = min(0.45, (0.30 - sat) * 1.6) * k
    if v < 0.05:
        return img, None
    h, s, val = img.convert('HSV').split()
    lut = [min(255, int(round(x * (1.0 + v * (1.0 - x / 255.0))))) for x in range(256)]
    return Image.merge('HSV', (h, s.point(lut), val)).convert('RGB'), round(v, 3)


def _haze_score(mx):
    """雾化程度（0..1），来自 _measure_extra 的 haze 字段；仅用于 auto 预设下的保守触发。"""
    return float(mx.get('haze') or 0.0)


def _detect_horizon(img, max_dim=480):
    """纯 Python 检测「近水平主导直线」（地平线/水平参考线）的倾角（度）。

    降采样 → Sobel 梯度 → 单阈值边缘 → Hough（仅 θ∈[-12°,12°]）→ 取最高票直线。
    参考 jesskeepswimming/horizon-detector：Canny + 标准 Hough，最长/最强直线即地平线。
    返回 (angle_deg, 票数, 边缘像素数) 或 None（无明显水平线）。
    """
    w, h = img.size
    sc = min(1.0, max_dim / max(1, max(w, h)))
    sm = img.resize((max(1, int(w * sc)), max(1, int(h * sc))),
                    Image.BILINEAR).convert('L')
    W, H = sm.size
    # 扁平灰度列表：纯 Python 整数索引比 sm.load() 的 px[x,y] 方法调用快数倍；
    # 下面的 Sobel 循环 x∈[1,W-2]、y∈[1,H-2]，8 个邻域必在界内，故省略越界判断。
    data = list(sm.getdata())
    gw = W
    gx = [0] * (W * H)
    gy = [0] * (W * H)
    for y in range(1, H - 1):
        base = y * gw
        up = base - gw
        dn = base + gw
        for x in range(1, W - 1):
            i = base + x
            gx[i] = (data[up + x + 1] + 2 * data[base + x + 1] + data[dn + x + 1]
                     - data[up + x - 1] - 2 * data[base + x - 1] - data[dn + x - 1])
            gy[i] = (data[up + x - 1] + 2 * data[up + x] + data[up + x + 1]
                     - data[dn + x - 1] - 2 * data[dn + x] - data[dn + x + 1])
    # 固定较低的 8-bit 阈值：旋转后画布四周「透明→内容」的强边界会把相对阈值
    # （如 0.35*gmax）抬太高，把真正的地平线边缘也压掉；固定低阈值让弱地平线也能入选，
    # 而 Hough 峰值仍由近水平主导线决定（边界虽强但非水平，不进我们的 θ 投票桶）。
    hi = 55
    edges = [[1 if math.hypot(gx[y * gw + x], gy[y * gw + x]) >= hi else 0
              for x in range(W)] for y in range(H)]
    total = sum(sum(r) for r in edges)
    # 法线角区间 → 对应「近水平」直线；预计算 sin/cos，避免每个边缘像素重复 trig
    # （原实现 每像素 ×25 次 math.cos/sin，对大图是 0.8s 级开销；预计算后仅 25 次，
    # 且 rho/deg 取整键完全一致，检测结果不变）。
    _pre = [(math.radians(t), t) for t in range(78, 103)]
    _cos = [math.cos(r) for r, _ in _pre]
    _sin = [math.sin(r) for r, _ in _pre]
    acc = {}
    for y in range(H):
        row = edges[y]
        for x in range(W):
            if not row[x]:
                continue
            for i in range(len(_pre)):
                rho = x * _cos[i] + y * _sin[i]
                key = (int(round(rho)), _pre[i][1])
                acc[key] = acc.get(key, 0) + 1
    if not acc:
        return None
    (rho_r, deg), votes = max(acc.items(), key=lambda kv: kv[1])
    # 接受门槛：峰值票数既要「够强」（接近图像宽度的主导水平线），
    # 又要「占边缘总量相当比例」——纯噪声虽也有零散投票但极分散（占比≈1%），
    # 真实地平线占比可达 ~40%，由此排除噪声误检。
    if votes < max(30, int(0.3 * W)) or votes < 0.03 * total:
        return None
    # Hough 中 θ 是直线「法线角」；近水平直线的法线角 ≈ 90°。
    # 拉平所需旋转角 = -(直线角) = -(deg_normal - 90) = deg_normal - 90（即应直接施加给 _straighten 的角度）。
    return float(deg - 90), votes, total


def _straighten(img, angle):
    """按 angle（度）旋转校正水平，并裁到内切矩形以避免透明斜边。"""
    if abs(angle) < 0.3:
        return img, 0.0
    w, h = img.size
    c = math.cos(math.radians(angle)); s = math.sin(math.radians(angle))
    rot = img.rotate(angle, expand=True, resample=Image.BICUBIC)
    nw, nh = rot.size
    iw = w * c - h * s
    ih = h * c - w * s
    if iw > w * 0.5 and ih > h * 0.5:
        left = int(round((nw - iw) / 2)); top = int(round((nh - ih) / 2))
        rot = rot.crop((left, top, left + int(round(iw)), top + int(round(ih))))
    return rot, angle


def _dehaze(img, strength=1.0):
    """暗通道先验去雾（He et al. CVPR 2009），**纯 Pillow 零依赖**实现。

    参考 GitHub 纯 Python 实现（ZQPei/Haze_Removal_python、cssartori/image-dehazing、
    He-Zhang/image_dehaze）：暗通道 → 大气光 A → 透射率 t（ω=0.95）→ 复原
    J = (I - A)/t + A。透射率细化本应用导向滤波（Guided Filter, He et al. ECCV 2010），
    但为避免引入 numpy/cv2，这里用**高斯模糊近似**（雾是平滑低频场，近似足够；
    边缘处可能有极轻光晕，可接受）。复原在工作分辨率（长边 ≤ 1280）完成后 LANCZOS 放大，
    因为雾是大气低频效应，降采样几乎无损且大幅提速。
    """
    Wmax = 1280
    W, H = img.size
    scale = min(1.0, Wmax / max(1, max(W, H)))
    work = img.resize((max(1, int(W * scale)), max(1, int(H * scale))),
                      Image.LANCZOS) if scale < 1.0 else img
    R, G, B = work.split()
    k = 15
    rmin = R.filter(ImageFilter.MinFilter(k)).convert('L')
    gmin = G.filter(ImageFilter.MinFilter(k)).convert('L')
    bmin = B.filter(ImageFilter.MinFilter(k)).convert('L')
    dark = ImageChops.darker(rmin, ImageChops.darker(gmin, bmin))   # 暗通道 'L'
    # 大气光 A：暗通道最亮 0.1% 像素处，取原图该位置「最亮通道」的值
    ww, wh = work.size
    dlist = list(dark.getdata())
    n = len(dlist)
    ntop = max(1, int(n * 0.001))
    order = sorted(range(n), key=lambda i: dlist[i], reverse=True)[:ntop]
    Rd, Gd, Bd = list(R.getdata()), list(G.getdata()), list(B.getdata())
    A = [1, 1, 1]
    best = -1
    for i in order:
        b = max(Rd[i], Gd[i], Bd[i])
        if b > best:
            best = b
            A = [Rd[i], Gd[i], Bd[i]]
    A = [max(1, x) for x in A]
    omega, t0 = 0.95, 0.1
    def divc(ch, a):
        return ch.point([min(255, int(round(x / a * 255))) for x in range(256)]).convert('L')
    nr = divc(R, A[0]); ng = divc(G, A[1]); nb = divc(B, A[2])
    nd = ImageChops.darker(nr, ImageChops.darker(ng, nb))            # min_c(I_c / A_c)
    t = nd.point([min(255, int(round((1.0 - omega * (x / 255.0)) * 255))) for x in range(256)])
    t = t.point([max(int(t0 * 255), x) for x in range(256)])          # 夹取透射率下限 t0
    t = t.filter(ImageFilter.GaussianBlur(max(3.0, min(ww, wh) / 120.0)))   # 导向滤波近似
    tr = list(t.getdata())
    out = []
    for i in range(n):
        ti = max(t0, tr[i] / 255.0)
        p0 = min(255, max(0, int(round((Rd[i] - A[0]) / ti + A[0]))))
        p1 = min(255, max(0, int(round((Gd[i] - A[1]) / ti + A[1]))))
        p2 = min(255, max(0, int(round((Bd[i] - A[2]) / ti + A[2]))))
        out.append((p0, p1, p2))
    res = Image.new('RGB', work.size)
    res.putdata(out)
    if strength < 1.0:
        res = Image.blend(work, res, strength)
    if scale < 1.0:
        res = res.resize((W, H), Image.LANCZOS)
    mt = sum(tr) / len(tr) / 255.0
    return res, {'A': A, 'mean_t': round(mt, 3), 'omega': omega}


def _denoise_level_noise(noise):
    """仅凭画面噪声估计决定降噪档位（无 EXIF 时也有保护）。阈值经合成图实测标定。"""
    if noise >= 0.070:
        return 2
    if noise >= 0.045:
        return 1
    return 0


# ------------------------------------------------------------ 暗角（晕影）校正
# 参考实现（GitHub / 论文）
# *   Goldman & Chen, "Vignette and Exposure Calibration and Compensation", ICCV 2005
#     —— 多项式晕影模型（alpha_2/4/6）。工程实现：AlbertHuyb/VignetteRemoval。
# *   quentinkaci/vignetting_correction（"Revisiting Image Vignetting Correction by
#     Constrained Minimization of Log-Intensity Entropy"）—— 单图无标定。
# *   TomNaiser/Gigapixel-Tools · VignettingCorrection.py —— 归一化校正掩膜（>1 倍乘、裁剪）。
# 本项目采用最简可用的「径向多项式 + 稳健环带估计 + 全分辨率倍乘」，
# 与上面掩膜倍乘思路一致，但不依赖标定序列（单图可用）。
def _estimate_vignette(img, bins=12):
    """稳健估计径向压暗系数 k（模型 v(r)=1-k·r²，r 归一到角半径）。

    做法：把缩略图按到中心的距离分环，取每环**均值**（环上各方向内容相消，
    环均值≈径向亮度），以最亮环为基准求相对亮度 v(r)，对 v=1-k·r² 做
    r² 上的一元最小二乘。环均值对「内容不均匀」比逐像素稳定得多。
    返回 k（≥0）或 None（图太小/环带不足）。
    """
    small = img.convert('L')
    small.thumbnail((256, 256))
    w, h = small.size
    if w < 16 or h < 16:
        return None
    px = small.load()
    cx, cy = (w - 1) / 2.0, (h - 1) / 2.0
    rmax = math.hypot(cx, cy)
    if rmax < 1.0:
        return None
    sums = [0.0] * bins
    cnts = [0] * bins
    for y in range(h):
        dy = y - cy
        for x in range(w):
            r = math.hypot(x - cx, dy) / rmax
            b = bins - 1 if r >= 1.0 else int(r * bins)
            sums[b] += px[x, y]
            cnts[b] += 1
    prof, rr = [], []
    for b in range(bins):
        if cnts[b] >= 8:
            prof.append(sums[b] / cnts[b])
            rr.append(((b + 0.5) / bins) ** 2)     # r²，供线性拟合
    if len(prof) < 4:
        return None
    ref = max(prof)                                # 最亮环 ≈ 中心（晕影时中心最亮）
    if ref <= 1e-6:
        return None
    rel = [p / ref for p in prof]
    num = sum(r2 * (1.0 - v) for r2, v in zip(rr, rel))
    den = sum(r2 * r2 for r2 in rr) or 1e-9
    return max(0.0, num / den)


def _vignette_gain_map(size, k, gmax):
    """生成径向增益图：像素值 = (gain/2)·255，供 ImageChops.multiply 半标度倍乘。

    增益是**低频**场，故在低分辨率（≤256）用径向公式生成，再双线性放大到全分辨率
    —— 放大的是平滑增益、不损失画面细节（这点与「降采样整图再放大」有本质区别）。
    """
    W, H = size
    lw = max(8, min(256, W))
    lh = max(8, int(round(lw * H / max(1, W))))
    cx, cy = (lw - 1) / 2.0, (lh - 1) / 2.0
    rmax2 = (cx * cx + cy * cy) or 1.0
    NL = 512
    lut = []
    for i in range(NL + 1):
        inv = 1.0 - k * (i / NL)
        if inv < 0.35:
            inv = 0.35
        g = min(gmax, 1.0 / inv)
        lut.append(min(255, int(round(g / 2.0 * 255.0))))
    mp = Image.new('L', (lw, lh))
    mp.putdata([lut[min(NL, int(((x - cx) ** 2 + (y - cy) ** 2) / rmax2 * NL))]
                for y in range(lh) for x in range(lw)])
    if (lw, lh) != (W, H):
        mp = mp.resize((W, H), Image.BILINEAR)
    return mp


_DOUBLE_LUT = [min(255, v * 2) for v in range(256)]     # 半标度倍乘的 ×2 还原


def _apply_radial_gain(img, gmap):
    """按增益图乘回（全分辨率、C 级向量化，无 Python 逐像素）。"""
    r, g, b = img.convert('RGB').split()
    return Image.merge('RGB', tuple(
        ImageChops.multiply(ch, gmap).point(_DOUBLE_LUT) for ch in (r, g, b)))


def _vignette_correct(img, strength=1.0, k=None):
    """暗角（晕影）校正：径向增益复原。**仅显式 vignette 预设调用，绝不 auto 触发。**

    估计径向压暗 k → 增益 g(r)=1/(1-k·r²) 夹取 ≤1.8 → 全分辨率倍乘。
    k 低于 VIGNETTE_MIN_K（无明显暗角）时返回 (原图, None)，即「无暗角不乱修」。
    """
    if k is None:
        k = _estimate_vignette(img)
    if k is None or k < VIGNETTE_MIN_K:
        return img, None
    k = min(0.60, k * strength)
    gmax = min(VIGNETTE_MAX_GAIN, 1.0 / max(0.35, 1.0 - k))
    gmap = _vignette_gain_map(img.size, k, gmax)
    return _apply_radial_gain(img, gmap), {'k': round(k, 3), 'corner_gain': round(gmax, 3)}


# ------------------------------------------------------------ 人像美化（肤色磨皮）
# 参考实现（GitHub / 文献）
# *   wchstrife/DigitalImageProcessing · Skin-Filter —— 人像美肤 = 人脸区域 + **双边滤波**
#     （边缘保持平滑），给出双边权重公式。本实现以「中值去小瑕疵 + 高斯匀肤」逼近其
#     「保边平滑」目标（纯 Pillow 无双边滤波）。
# *   Muhammad-Irfan-Azam/beauty_filters —— selfie beauty filter：edge-preserving skin
#     smoothing（Bilateral）+ 亮度提升。
# *   LoveIiei/LuminaRetouch —— 磨皮强调「保留自然纹理」+ 去瑕疵（AI 版，功能定义对标）。
# *   肤色检测：YCbCr 空间 133≤Cr≤173 且 77≤Cb≤127 为肤色（Chai & Ngan, 1999），
#     受亮度影响小、类聚好，是社区经典肤色模型 —— 纯 LUT 可实现。
# 取向：只在**肤色区**做温和磨皮（保边、留纹理），非肤色区原样不动；肤色偏暗才轻提亮（美白）。
SKIN_MIN_FRACTION = 0.015     # 肤色（软掩膜均值）占比低于此值 → 判定非人像/无肤色，不动作


def _ramp_lut(lo0, lo1, hi0, hi1):
    """软隶属 LUT：核心区 [lo1,hi0] 取 1，边缘 [lo0,lo1]/[hi0,hi1] 线性过渡，其余 0。"""
    out = []
    for v in range(256):
        if v < lo0 or v > hi1:
            out.append(0)
        elif v < lo1:
            out.append(int(round(255.0 * (v - lo0) / max(1, lo1 - lo0))))
        elif v <= hi0:
            out.append(255)
        else:
            out.append(int(round(255.0 * (hi1 - v) / max(1, hi1 - hi0))))
    return out


def _skin_mask(img):
    """肤色软掩膜（YCbCr 规则 + 亮度门 + 羽化），返回 'L' 掩膜（0..255）。"""
    y, cb, cr = img.convert('YCbCr').split()
    cb_lut = _ramp_lut(69, 77, 127, 135)     # 核心 77..127（Chai & Ngan）
    cr_lut = _ramp_lut(125, 133, 173, 181)   # 核心 133..173
    y_lut = _ramp_lut(30, 45, 245, 253)      # 亮度门：避开死黑/接近削顶的不可靠区
    m = ImageChops.multiply(cb.point(cb_lut), cr.point(cr_lut))
    m = ImageChops.multiply(m, y.point(y_lut))
    m = m.filter(ImageFilter.GaussianBlur(max(1.5, min(img.size) / 120.0)))   # 羽化边界
    return m


def _portrait_beautify(img, strength=1.0):
    """人像美化：肤色区温和磨皮（保边、留纹理）+ 肤色偏暗时轻提亮（美白）。

    **仅显式 portrait 预设调用，绝不 auto 触发。** 无肤色则返回 (原图, None)（不乱修）。
    """
    mask = _skin_mask(img)
    h = mask.histogram()
    tot = max(1, sum(h))
    frac = sum(c for i, c in enumerate(h) if i >= 60) / tot
    if frac < SKIN_MIN_FRACTION:
        return img, None
    r = max(3.0, min(img.size) / 150.0)
    # 边缘保持磨皮：中值滤波去小瑕疵/斑点，高斯匀肤，再按掩膜只在肤色上生效
    med = img.filter(ImageFilter.MedianFilter(5))
    gau = img.filter(ImageFilter.GaussianBlur(r))
    smooth = Image.blend(med, gau, 0.5)
    soft_amt = min(1.0, 0.80 * strength)
    out = Image.composite(Image.blend(img, smooth, soft_amt), img, mask)
    info = {'skin': round(frac, 3), 'smooth': round(soft_amt, 2)}
    # 肤色提亮（美白）：仅当肤色本身不亮时才做（宁欠勿爆）
    v = img.convert('HSV').split()[2]
    vs = v.resize((64, 64)); ms = mask.resize((64, 64))
    vd = list(vs.getdata()); md = list(ms.getdata())
    skin_lum = sum(a * b for a, b in zip(vd, md)) / (sum(md) or 1) / 255.0
    if skin_lum < 0.62:
        gain = 1.0 + 0.05 * strength
        out = Image.composite(ImageEnhance.Brightness(out).enhance(gain), out, mask)
        info['brighten'] = round(gain - 1.0, 3)
    return out, info


# ------------------------------------------------------------ 风格化（胶片 / 黑白 / 柔光）
# 参考实现（GitHub）
# *   Wraywest/film-grade（MIT）—— 克制的胶片调色：暖铜中间调 + 阴影淡青、饱和度略降、
#     高光肩部滚降、抬黑场不压死、**随亮度耦合的单色颗粒**、轻微暗角；刻意不做 glow/bloom。
# *   sinanonur/film-simulation —— 胶片 profile：色彩曲线 / 对比 / 饱和 / 颗粒(量+尺寸) /
#     色温 / 交叉冲洗（本实现取「影调+分离色调+颗粒」这一最小可用子集）。
# *   Orton 柔焦（社区经典）—— 原图与「模糊且提亮」的副本做 screen 混合，营造梦幻柔焦。
# 全部**仅显式预设**（film / bw / glow），绝不 auto；色彩/影调被显著重写，属「显式编辑」，
# 与 dehaze/level/vignette/portrait 同受 reconstructed 豁免（不被微调审计整张回退）。
FILM_LIFT = 0.055        # 胶片抬黑场比例（0..1）
FILM_ROLLOFF = 0.42      # 高光肩部压缩强度（越大越柔）
GRAIN_AMOUNT = 0.55      # 颗粒强度（0..1）


def _film_tone_lut(lift, rolloff, knee=200):
    """胶片影调 LUT：高光肩部滚降（保端点不炸）+ 抬黑场（fade，不压死黑）。"""
    lut = []
    for v in range(256):
        if v <= knee:
            v1 = float(v)
        else:
            x = (v - knee) / float(255 - knee)
            y = x - rolloff * x * (1.0 - x)      # 拉低肩部 → 高光滚降
            v1 = knee + y * (255 - knee)
        lo = lift * 255.0
        v2 = lo + v1 * (255.0 - lo) / 255.0      # 抬黑场
        lut.append(int(max(0, min(255, round(v2)))))
    return lut


def _film_tone(img, lift=FILM_LIFT, rolloff=FILM_ROLLOFF):
    return img.point(_film_tone_lut(lift, rolloff) * 3)   # RGB 需 3×256 LUT


def _channel_gain(img, gr, gg, gb):
    """逐通道增益（保持细节的色彩平移，非平涂）。"""
    r, g, b = img.split()

    def _p(ch, f):
        return ch.point([max(0, min(255, int(round(v * f)))) for v in range(256)])
    return Image.merge('RGB', (_p(r, gr), _p(g, gg), _p(b, gb)))


def _split_tone(img, shadow=(0.96, 1.0, 1.05), highlight=(1.05, 1.0, 0.95),
                amount=0.55, balance=0.5):
    """分离色调：阴影加冷（青）、高光加暖（铜）—— 参考 film-grade 的「暖中间调 + 阴影淡青」。

    以亮度做权重掩膜：暗部落 shadow 通道增益、亮部落 highlight 通道增益，中灰（balance）平滑过渡。
    用「通道增益后的图」合成（而非平涂颜色），故保留细节、只移动色调。
    """
    l = img.convert('L')
    bp = int(round(balance * 255))
    sw = l.point([255 - min(255, int(255 * v / max(1, bp))) for v in range(256)])
    hw = l.point([max(0, min(255, int(255 * (v - bp) / max(1, 255 - bp)))) for v in range(256)])
    sw = sw.point([int(v * amount) for v in range(256)])
    hw = hw.point([int(v * amount) for v in range(256)])
    out = Image.composite(_channel_gain(img, *shadow), img, sw)
    out = Image.composite(_channel_gain(img, *highlight), out, hw)
    return out


def _add_grain(img, amount=GRAIN_AMOUNT):
    """随亮度耦合的单色颗粒：中间调最强、深阴影/高光最弱（如负片颗粒，非数码噪点）。

    噪声用 `Image.effect_noise`（C 级高斯噪，均值≈128）；亮度权重为三角窗（中间调=255、两端=0）；
    再按 `out = img + (noise-128)·w` 施加 —— 全部向量化，无 Python 逐像素。
    """
    if amount <= 0:
        return img
    w, h = img.size
    noise = Image.effect_noise((max(1, w), max(1, h)), 32)
    k = min(3.0, amount * 1.6)
    noise = noise.point([max(0, min(255, 128 + int(round((v - 128) * k)))) for v in range(256)])
    lw = img.convert('L').point([int(255 * (1.0 - abs(v - 128) / 128.0)) for v in range(256)])
    scaled = ImageChops.add(ImageChops.multiply(noise, lw),
                            ImageChops.multiply(ImageOps.invert(lw), Image.new('L', (w, h), 128)))
    nrgb = Image.merge('RGB', (scaled, scaled, scaled))
    return ImageChops.add(img, nrgb, scale=1.0, offset=-128)


def _black_white(img, wr=0.30, wg=0.59, wb=0.11):
    """黑白（通道混合）：gray = r·wr + g·wg + b·wb（默认近似 Rec.601 亮度权重）。"""
    r, g, b = img.split()

    def _sc(ch, wt):
        return ch.point([max(0, min(255, int(round(v * wt)))) for v in range(256)])
    gray = ImageChops.add(ImageChops.add(_sc(r, wr), _sc(g, wg)), _sc(b, wb))
    return Image.merge('RGB', (gray, gray, gray))


def _glow(img, radius=None, amount=0.5):
    """柔光 / Orton：把「模糊且提亮」的副本与原图 screen 混合，营造梦幻柔焦。

    （film-grade 刻意不做 glow/bloom；这里作为独立的显式 glow 预设提供。）
    """
    rr = radius or max(6.0, min(img.size) / 45.0)
    blur = ImageEnhance.Brightness(img.filter(ImageFilter.GaussianBlur(rr))).enhance(1.12)
    return Image.blend(img, ImageChops.screen(img, blur), max(0.0, min(0.9, amount)))


def _edge_replicate(small, w, h):
    """把不超过 w×h 的小图居中贴回 w×h 画布，用最外 1px 镜像拉伸填充四周（无黑边）。

    用于色差校正里「缩小通道」的归位：边缘一圈（≤k·尺寸）用邻近像素拉伸掩盖，
    k 极小（≤0.6%）时肉眼不可见，避免纯黑边框。
    """
    sw, sh = small.size
    if sw >= w and sh >= h:
        return small
    canvas = Image.new(small.mode, (w, h))
    ox, oy = (w - sw) // 2, (h - sh) // 2
    canvas.paste(small, (ox, oy))
    if ox > 0:
        canvas.paste(small.crop((0, 0, 1, sh)).resize((ox, h)), (0, 0))
    if ox + sw < w:
        canvas.paste(small.crop((sw - 1, 0, sw, sh)).resize((w - ox - sw, h)), (ox + sw, 0))
    if oy > 0:
        canvas.paste(small.crop((0, 0, sw, 1)).resize((w, oy)), (0, 0))
    if oy + sh < h:
        canvas.paste(small.crop((0, sh - 1, sw, sh)).resize((w, h - oy - sh)), (0, oy + sh))
    return canvas


def _chromatic_correct(img, amount=1.0):
    """色差校正（横向色差 / 紫边消除，纯 Pillow）。

    横向色差表现为高反差边缘的彩色镶边（紫/绿），越靠边缘越明显：
    本质是 R/G/B 三通道在边缘的径向放大不一致（典型：红通道聚焦偏长→边缘外扩、
    蓝通道偏短→边缘内缩）。把 R 通道轻微缩小、B 通道轻微放大（均以绿通道为基准、
    关于光学中心对齐），即可把镶边收回主体。
    纯 Pillow：通道独立缩放 + 居中归位（缩小通道用镜像边缘兜底，无黑边）。
    仅显式 'chromatic' 预设触发；amount≤0 或图过小则跳过（不乱修）。
    """
    w, h = img.size
    if w < 96 or h < 96 or amount <= 0:
        return img, None
    k = 0.006 * amount                 # 径向缩放差上限≈0.6%（克制，防过度修）
    k = min(k, 0.02)
    r, g, b = img.split()
    # R：缩小（downsample 后裁中心，收回外扩的红边）
    rw, rh = max(2, int(round(w * (1 + k)))), max(2, int(round(h * (1 + k))))
    r_big = r.resize((rw, rh))
    rx, ry = (rw - w) // 2, (rh - h) // 2
    r_corr = r_big.crop((rx, ry, rx + w, ry + h))
    # B：放大（upsample 后居中，镜像边缘兜底，收回内缩的蓝边）
    bw, bh = max(2, int(round(w * (1 - k)))), max(2, int(round(h * (1 - k))))
    b_corr = _edge_replicate(b.resize((bw, bh)), w, h)
    out = Image.merge('RGB', (r_corr, g, b_corr))
    return out, {'amount': round(amount, 2), 'k': round(k, 4),
                 'note': 'R 缩小/B 放大 %.2f%% 径向对齐（边缘镜像兜底）' % (k * 100)}


# ------------------------------------------------------------ 影调（HDR 色调映射 / 明度结构）
# 参考实现（GitHub / 社区 / 论文）
# *   局部色调映射（LTM）：以「大半径高斯模糊的亮度」作为**局部光照**估计，对亮度施加
#     随局部光照变化的增益（暗区放大、亮区收缩）→ 压缩全局动态范围、抬出暗部细节。
#     因增益场是**慢变**的，高频细节（V − 局部光照）被整体缩放而非抹平 —— 这是
#     Durand & Dorsey, "Fast Bilateral Filtering for the Display of HDR Images" (2002)
#     分层色调映射的纯 Pillow 高斯近似版（无 numpy/双边，只借其分层思想）。
# *   明度结构（structure / micro-contrast）：在明度通道做**细尺度**非锐化掩模，并以
#     **中调掩膜**限定作用区 —— 中间调纹理最丰富故增强最强，深阴影/高光几乎不动
#     （那里细节少、噪声/削顶风险高）。
# 二者均为**显式强效果**（hdr / structure 预设），绝不 auto；像素被显著重写，
# 与 dehaze/level/…/film/bw/glow 同受 reconstructed 豁免（不被微调审计整张回退）。
HDR_MID = 128.0          # 色调映射的目标中灰
HDR_GAIN_LO = 0.62       # 暗区增益下限（防把暗部推成灰雾）
HDR_GAIN_HI = 1.55       # 暗区增益上限（防把暗部推爆/推噪）
HDR_KNEE = 196           # 高光软膝起点（超过则平滑滚降，保端点不削顶）
STRUCT_MAX = 0.55        # 结构强度上限（相对；越大越强，仍夹取）


def _tone_map(img, amount=1.0, p=0.25):
    """HDR 局部色调映射（纯 Pillow）。

    以局部光照（大半径高斯模糊的 V）为引导，对亮度施加随局部光照变化的增益：
    暗区增益 >1（抬细节）、亮区增益 <1（收高光），压缩动态范围；再用软膝 LUT 滚降
    高光防止削顶。因增益场慢变，高频细节被整体缩放而非抹平。
    仅显式 'hdr' 预设触发；amount ≤ 0 或图过小则跳过（不乱修）。
    """
    w, h = img.size
    if w < 32 or h < 32 or amount <= 0:
        return img, None
    hh, ss, vv = img.convert('HSV').split()
    R = max(4.0, min(w, h) / 10.0)                        # 局部光照尺度（慢变）
    m = vv.filter(ImageFilter.GaussianBlur(R))
    # 增益 g(x) = clamp((mid / (x+1)) ** p, LO, HI)：暗区 >1 抬、亮区 <1 收。
    # multiply 的乘子以 255=×1.0 为上限、无法表达 g>1，故先按 g/HI 施加再按 HI 还原：
    #   step = vv · (255·g/HI)/255 = vv·g/HI ；v1 = step · HI = vv·g
    HI = HDR_GAIN_HI
    gl = [max(0, min(255, int(round(
        255.0 * max(HDR_GAIN_LO, min(HI, (HDR_MID / (x + 1.0)) ** p)) / HI))))
        for x in range(256)]
    step = ImageChops.multiply(vv, m.point(gl))           # = vv · g / HI
    v1 = step.point([min(255, int(round(v * HI))) for v in range(256)])   # = vv · g
    # 高光软膝：x>KNEE 递增压缩，单调且不越 255（不削顶）
    kl = []
    for x in range(256):
        if x <= HDR_KNEE:
            kl.append(x)
        else:
            t = (x - HDR_KNEE) / float(255 - HDR_KNEE)
            y = 1.0 - (1.0 - t) ** 1.6                    # ease-out，趋近 1
            kl.append(int(round(HDR_KNEE + y * (255 - HDR_KNEE) * 0.95)))
    v1 = v1.point(kl)
    a = max(0.0, min(1.0, amount))
    out_v = Image.blend(vv, v1, a)                        # 按强度与原亮度混合
    return (Image.merge('HSV', (hh, ss, out_v)).convert('RGB'),
            {'amount': round(a, 2), 'radius': round(R, 1),
             'gain_lo': HDR_GAIN_LO, 'gain_hi': HDR_GAIN_HI, 'knee': HDR_KNEE,
             'note': '局部光照引导增益 ×%.2f~%.2f（R=%.0f）+ 高光软膝 %d'
                     % (HDR_GAIN_LO, HDR_GAIN_HI, R, HDR_KNEE)})


def _structure(img, amount=1.0):
    """明度结构（micro-contrast，纯 Pillow）。

    在明度通道 V 上做**细尺度**非锐化掩模，并以「中调掩膜」限定作用区 —— 中间调纹理
    最丰富，故增强最强；深阴影/高光几乎不动（细节少、噪声/削顶风险高）。
    与 clarity（大半径、仅发灰时触发）互补：structure 半径更小、显式触发、专攻质感。
    仅显式 'structure' 预设触发；amount ≤ 0 或图过小则跳过（不乱修）。
    """
    w, h = img.size
    if min(w, h) < 24 or amount <= 0:
        return img, None
    hh, ss, vv = img.convert('HSV').split()
    R = max(1.0, min(w, h) / 150.0)                       # 细尺度半径
    pct = int(round(min(STRUCT_MAX, 0.25 + 0.35 * amount) * 100))
    thr = 3                                                # 阈值挡噪声
    v_sharp = vv.filter(ImageFilter.UnsharpMask(radius=max(1, int(round(R))),
                                                percent=pct, threshold=thr))
    # 中调掩膜：V≈128 处权重 1，两端趋 0（三角窗），保护阴影与高光
    mask = vv.point([int(255 * (max(0.0, 1.0 - abs(x - 128) / 128.0) ** 0.8))
                     for x in range(256)])
    out_v = Image.composite(v_sharp, vv, mask)            # 掩膜 255→锐化版，0→原亮度
    return (Image.merge('HSV', (hh, ss, out_v)).convert('RGB'),
            {'amount': round(amount, 2), 'radius': round(R, 2), 'percent': pct,
             'threshold': thr,
             'note': '明度通道细尺度 USM %d%%（R=%.1f, 阈值 %d）+ 中调掩膜'
                     % (pct, R, thr)})


DODGE_LIFT_MAX = 0.28    # 局部提亮强度上限（提亮量占「剩余高光余量」的比例）
DODGE_ADD_CAP = 80       # 单像素最大提亮量（防一次性推爆暗部）


def _dodge(img, amount=1.0):
    """局部提亮（dodge / 暗部回血，纯 Pillow）。

    以局部光照（大半径模糊的 V）为引导，**只提亮局部偏暗的区域**：暗部提得多、亮部
    几乎不动（区别于 hdr 的「暗抬 + 亮收」双向压缩，也区别于自动管线里按阈值触发的
    `_tone_curve`）。提亮量乘「黑场斜坡」→ 真黑不被抬起（夜景不泛灰），并逐像素夹取
    上限 → 不推爆。
    仅显式 'dodge' 预设触发；amount ≤ 0 或图过小则跳过（不乱修）。
    """
    w, h = img.size
    if w < 32 or h < 32 or amount <= 0:
        return img, None
    hh, ss, vv = img.convert('HSV').split()
    R = max(4.0, min(w, h) / 8.0)                         # 局部光照尺度（偏局部）
    m = vv.filter(ImageFilter.GaussianBlur(R))
    lift = max(0.0, min(1.0, amount)) * DODGE_LIFT_MAX
    # 提亮权重随局部光照衰减：m 越暗权重越高（(1-m/255)^1.5）
    wl = m.point([min(255, int(round(255.0 * lift * (1.0 - x / 255.0) ** 1.5)))
                  for x in range(256)])
    ramp = vv.point([min(255, int(x * 255.0 / 24.0)) for x in range(256)])   # 黑场斜坡
    add = ImageChops.multiply(ImageChops.multiply(ImageChops.invert(vv), wl), ramp)
    add = add.point([min(DODGE_ADD_CAP, x) for x in range(256)])            # 单像素夹取
    out_v = ImageChops.add(vv, add)
    return (Image.merge('HSV', (hh, ss, out_v)).convert('RGB'),
            {'amount': round(amount, 2), 'radius': round(R, 1),
             'lift_max': round(lift, 3), 'add_cap': DODGE_ADD_CAP,
             'note': '局部光照引导暗部提亮（R=%.0f，提亮上限 %.2f，单像素夹 %d）'
                     % (R, lift, DODGE_ADD_CAP)})


def _param_tone(img, highlights=0.0, shadows=0.0, whites=0.0, blacks=0.0):
    """决策层增量影调：局部阴影/高光 + 全局黑/白点（纯 Pillow，参数 -1..1）。

    与 retouch_file 的自动管线不同：**幅度已由决策层给出，本函数忠实执行**（不测量、
    不二次决策），但仍保留夹取与掩膜，避免越权毁图。
      * shadows / highlights —— 用「局部光照掩膜」（大半径模糊亮度）提暗部 / 收高光，
        近似 Lightroom 的局部行为；阴影抬升乘黑场斜坡（不抬真黑），高光回收有上限。
      * blacks / whites —— 平滑端点亮/暗场 LUT（全局），只挪端点、不动中调。
    全部无动作（|各值|<0.01）时返回 (img, None)。
    """
    if not any(abs(x) >= 0.01 for x in (highlights, shadows, whites, blacks)):
        return img, None
    hh, ss, vv = img.convert('HSV').split()
    sc = min(1.0, 512.0 / max(1, max(img.size)))
    small = vv
    if sc < 1.0:
        small = vv.resize((max(1, int(img.size[0] * sc)), max(1, int(img.size[1] * sc))),
                          Image.BILINEAR)
    rs = max(3.0, min(small.size) / 40.0)
    m = small.filter(ImageFilter.GaussianBlur(rs))        # 局部光照（缩略图算、原图用）
    out = vv

    if abs(shadows) >= 0.01:
        T = 130.0
        w = m.point([int(255 * (max(0.0, (T - x) / T) ** 1.6)) for x in range(256)])
        if sc < 1.0:
            w = w.resize(img.size, Image.BILINEAR)
        if shadows > 0:                                   # 提阴影（乘黑场斜坡，不抬真黑）
            ramp = vv.point([min(255, int(x * 255.0 / 24.0)) for x in range(256)])
            add = ImageChops.multiply(ImageChops.multiply(ImageChops.invert(vv), w), ramp)
            out = ImageChops.add(out, add.point([int(x * 0.55 * shadows) for x in range(256)]))
        else:                                             # 压阴影
            sub = ImageChops.multiply(vv, w)
            out = ImageChops.subtract(out, sub.point([int(x * 0.35 * (-shadows)) for x in range(256)]))

    if abs(highlights) >= 0.01:
        T2 = 170.0
        w2 = m.point([int(255 * (max(0.0, (x - T2) / (255.0 - T2)) ** 1.6)) for x in range(256)])
        if sc < 1.0:
            w2 = w2.resize(img.size, Image.BILINEAR)
        if highlights < 0:                                # 回收高光（单像素回收有上限）
            cap = ImageChops.multiply(vv, w2).point([min(x, 60) for x in range(256)])
            out = ImageChops.subtract(out, cap.point([int(x * 0.6 * (-highlights)) for x in range(256)]))
        else:                                             # 提亮高光
            addh = ImageChops.multiply(ImageChops.invert(vv), w2)
            out = ImageChops.add(out, addh.point([int(x * 0.5 * highlights) for x in range(256)]))

    if abs(blacks) >= 0.01 or abs(whites) >= 0.01:        # 黑/白点：平滑端点 LUT
        lut = []
        for x in range(256):
            y = float(x)
            if blacks >= 0:                               # 抬黑场（fade）
                y += blacks * 42.0 * (1.0 - y / 255.0) ** 2
            else:                                         # 压黑场
                y -= (-blacks) * 42.0 * (y / 255.0) ** 2
            if whites >= 0:                               # 提白点
                y += whites * 42.0 * (y / 255.0) ** 2
            else:                                         # 降白点
                y -= (-whites) * 42.0 * (1.0 - y / 255.0) ** 2
            lut.append(max(0, min(255, int(round(y)))))
        out = out.point(lut)

    if out is vv:
        return img, None
    return (Image.merge('HSV', (hh, ss, out)).convert('RGB'),
            {'highlights': round(highlights, 2), 'shadows': round(shadows, 2),
             'whites': round(whites, 2), 'blacks': round(blacks, 2)})


# ------------------------------------------------------------ 镜头 / 红眼
def _mirror_expand(img, m):
    """把图放进 (w+2m)×(h+2m) 画布并镜像扩边（供镜头畸变校正留出径向外扩余量，无黑边）。"""
    if m <= 0:
        return img
    w, h = img.size
    out = Image.new(img.mode, (w + 2 * m, h + 2 * m))
    out.paste(img, (m, m))
    lc = img.crop((0, 0, 1, h)).resize((m, h)).transpose(Image.FLIP_LEFT_RIGHT)
    out.paste(lc, (0, m))
    rc = img.crop((w - 1, 0, w, h)).resize((m, h)).transpose(Image.FLIP_LEFT_RIGHT)
    out.paste(rc, (w + m, m))
    tr = img.crop((0, 0, w, 1)).resize((w, m)).transpose(Image.FLIP_TOP_BOTTOM)
    out.paste(tr, (m, 0))
    br = img.crop((0, h - 1, w, h)).resize((w, m)).transpose(Image.FLIP_TOP_BOTTOM)
    out.paste(br, (m, h + m))
    tl = img.crop((0, 0, 1, 1)).resize((m, m)).transpose(Image.FLIP_LEFT_RIGHT).transpose(Image.FLIP_TOP_BOTTOM)
    out.paste(tl, (0, 0))
    trc = img.crop((w - 1, 0, w, 1)).resize((m, m)).transpose(Image.FLIP_LEFT_RIGHT).transpose(Image.FLIP_TOP_BOTTOM)
    out.paste(trc, (w + m, 0))
    bl = img.crop((0, h - 1, 1, h)).resize((m, m)).transpose(Image.FLIP_LEFT_RIGHT).transpose(Image.FLIP_TOP_BOTTOM)
    out.paste(bl, (0, h + m))
    brc = img.crop((w - 1, h - 1, w, h)).resize((m, m)).transpose(Image.FLIP_LEFT_RIGHT).transpose(Image.FLIP_TOP_BOTTOM)
    out.paste(brc, (w + m, h + m))
    return out


def _lens_src(px, py, cx, cy, R, k):
    """径向反畸变的源坐标：校正后半径 r 处取原图半径 r·(1+k·rn²) 的内容。"""
    vx, vy = px - cx, py - cy
    rn = ((vx * vx + vy * vy) ** 0.5) / R
    f = 1.0 + k * rn * rn
    return cx + vx * f, cy + vy * f


def _solve3(A, B):
    """解 3×3 线性方程组 A·x = B（高斯消元，带主元），返回 x 列表。"""
    M = [list(A[i]) + [B[i]] for i in range(3)]
    for i in range(3):
        p = max(range(i, 3), key=lambda r: abs(M[r][i]))
        M[i], M[p] = M[p], M[i]
        piv = M[i][i] or 1e-12
        for j in range(i, 4):
            M[i][j] /= piv
        for r in range(3):
            if r != i:
                f = M[r][i]
                for j in range(i, 4):
                    M[r][j] -= f * M[i][j]
    return [M[0][3], M[1][3], M[2][3]]


def _affine_from(dst3, src3):
    """由 3 组对应点求仿射矩阵 (a,b,c,d,e,f)：src = A·dst（PIL AFFINE 把输出映射到源）。"""
    A = [[dx, dy, 1.0] for (dx, dy) in dst3]
    ab = _solve3(A, [s[0] for s in src3])
    de = _solve3(A, [s[1] for s in src3])
    return (ab[0], ab[1], ab[2], de[0], de[1], de[2])


def _lens_correct(img, amount=1.0):
    """镜头畸变校正（桶形畸变径向反畸变，纯 Pillow）。

    消费级镜头常带轻微桶形畸变：离中心越远内容越向外鼓（记录半径
    rd = r·(1 + k·r²)）。把校正后像素在半径 r 处取原图半径 rd 的内容，即可把鼓出的
    内容拉回正位。纯 Pillow：用 `Image.transform(Image.AFFINE)` 把图像细分成网格，
    逐格由「输出角点→源角点」解仿射近似该径向映射（比 MESH 可控、可验证）；为容纳
    径向外扩，先在镜像扩边画布上做变换，再裁回原尺寸。

    仅显式 'lens' 预设触发；amount≤0 或图过小则跳过（不乱修）。k 克制（≤0.30）以保安全。
    """
    w, h = img.size
    if w < 64 or h < 64 or amount <= 0:
        return img, None
    k = 0.10 * amount
    k = min(k, 0.30)
    R = 0.5 * (w * w + h * h) ** 0.5           # 半对角（像素），半径基准
    margin = int(k * R) + 2                       # 角部最大外扩量
    if margin < 2:
        return img, None
    pad = _mirror_expand(img, margin)
    pw, ph = pad.size
    cx, cy = pw / 2.0, ph / 2.0
    N = max(8, min(32, w // 60))                  # 网格密度（随尺寸，封顶 32）
    out = Image.new(pad.mode, (pw, ph))
    for iy in range(N):
        for ix in range(N):
            x0 = int(ix * pw / N); x1 = int((ix + 1) * pw / N)
            y0 = int(iy * ph / N); y1 = int((iy + 1) * ph / N)
            if x1 <= x0:
                x1 = x0 + 1
            if y1 <= y0:
                y1 = y0 + 1
            tw, th = x1 - x0, y1 - y0
            # PIL AFFINE 把**局部**瓦片坐标(0..tw)喂给矩阵，故 dst 用局部坐标，
            # 而源点取该瓦片在全局画布上的真实位置（中心/半径基准是全局的）。
            dst3 = ((0, 0), (tw, 0), (tw, th))
            src3 = tuple(_lens_src(x0 + lx, y0 + ly, cx, cy, R, k)
                         for (lx, ly) in ((0, 0), (tw, 0), (tw, th)))
            aff = _affine_from(dst3, src3)
            sub = pad.transform((tw, th), Image.AFFINE, aff, Image.BICUBIC, fill=0)
            out.paste(sub, (x0, y0))
    out = out.crop((margin, margin, margin + w, margin + h))
    return out, {'amount': round(amount, 2), 'k': round(k, 4), 'grid': N,
                 'note': '桶形畸变径向反畸变（k=%.3f，%dx%d 网格仿射，边缘镜像兜底）' % (k, N, N)}


def _is_redeye(r, g, b):
    """红眼候选像素：高亮、强红主导、绿蓝偏低。保守启发式，非人脸检测。"""
    if r < 120 or g > 110 or b > 110:
        return False
    if r < 2.2 * g or r < 2.2 * b:
        return False
    return True


def _redeye_remove(img, amount=1.0):
    """红眼去除（保守启发式，纯 Pillow，零云端依赖）。

    局限：纯 Pillow 无法做人脸/眼睛检测，这里用「高亮强红主导、绿蓝偏低」的像素启发式
    识别红眼反射，并把它们压暗去红（变成接近瞳孔的暗中性）。**可能误伤其它鲜红物体**
    （红衣/红花），故用面积闸门（红色占比过高视为非红眼、直接跳过）降低误伤；且**仅
    显式 'redeye' 预设触发**，绝不 auto。需要高鲁棒性应接外部关键点服务（本项目禁云端调用）。
    """
    if amount <= 0:
        return img, None
    w, h = img.size
    if w < 32 or h < 32:
        return img, None
    data = list(img.getdata())
    mask = [_is_redeye(r, g, b) for (r, g, b) in data]
    cnt = sum(mask)
    area = w * h
    if cnt < 20 or cnt > 0.06 * area:           # 太少=无红眼；太多=疑似整片红，跳过
        return img, None
    new = []
    for m, (r, g, b) in zip(mask, data):
        if m:
            d = int(g * 0.35)                     # 压暗去红：红眼反射→接近瞳孔的暗中性
            new.append((d, d, int(b * 0.35)))
        else:
            new.append((r, g, b))
    out = img.copy()
    out.putdata(new)
    return out, {'amount': round(amount, 2), 'pixels': cnt,
                 'note': '识别并压暗 %d 个红眼反射像素（启发式，已做面积闸门）' % cnt}


# ------------------------------------------------------------ 主入口
def _audit(st, mx, out):
    """输出自检：这一步编辑是否把画质改差？返回问题列表（空 = 通过）。

    「宁可不改，也不改坏」的兜底：任何一项被判为变差，上层降力度或直接回退原图。
    注意阈值取宽松（自检是安全网，不是挑剔的裁判）：只在「明显变差」时报警。
    """
    b_st = _stats(out)
    b_mx = _measure_extra(out)
    issues = []
    if b_st['hi'] > st['hi'] + 0.02:
        issues.append('高光削顶 %.1f%%→%.1f%%' % (st['hi'] * 100, b_st['hi'] * 100))
    if b_st['lo'] > st['lo'] + 0.03:
        issues.append('暗部死黑 %.1f%%→%.1f%%' % (st['lo'] * 100, b_st['lo'] * 100))
    if st['lum_std'] > 0.10 and b_st['lum_std'] < min(0.12, st['lum_std'] * 0.65):
        issues.append('反差塌陷 %.3f→%.3f' % (st['lum_std'], b_st['lum_std']))
    if b_mx['cast'] > mx['cast'] + 0.02:
        issues.append('色偏加重 %.3f→%.3f' % (mx['cast'], b_mx['cast']))
    return issues


def retouch_file(path, preset='auto', exif=None, quality=92):
    """智能修图单张照片。

    返回 (输出路径, 摘要 dict)；输出与原片同目录 `<名>_retouched.jpg`。
    """
    p = PRESETS.get(preset, PRESETS['auto'])
    img = Image.open(path)
    img = ImageOps.exif_transpose(img)      # 先按 EXIF 方向摆正
    img = img.convert('RGB')
    orig_img = img.copy()                   # 供「自检不过 → 回退」使用

    st = _stats(img)
    mx = _measure_extra(img)                 # 局部对比 / 噪声 / 动态范围 / 色偏
    st0 = dict(st)                           # 原图测量（自检与摘要的基准，不随后续步骤变）
    steps = []                               # 「改了什么」逐环节记录，供前端展示
    reconstructed = False                    # 是否应用了「重构变换」（去雾 / 水平校正 / 暗角）——
                                            # 这类用户显式请求的几何/重建编辑会合法地改变
                                            # 像素统计，不应被微调审计的「变差」门禁整张回退。
    portrait_mode = (preset == 'portrait')   # 人像模式：磨皮后不再做全局清晰度、锐化改为保守
    struct_mode = (preset == 'structure')    # 结构模式：明度结构即局部对比，跳过 pipeline 的 clarity 免双重增强

    # ---- 去雾（暗通道先验）----
    # 仅显式「dehaze」预设必做；auto 下仅**强雾**才碰；且受输出自检保护（去雾后变差就放弃）。
    do_dehaze = (preset == 'dehaze')
    if preset == 'auto' and _haze_score(mx) >= HAZE_TRIGGER:
        do_dehaze = True
    if do_dehaze:
        dh_strength = 1.0 if preset == 'dehaze' else 0.85
        dehazed, dh = _dehaze(img, dh_strength)
        accept = bool(dh)
        if accept and preset == 'auto':
            # 二次把关：去雾必须确实提升（或至少不削弱）整体对比，否则多半是误判
            # （如明亮但干净的雪景），放弃去雾以免无谓改动。
            if _stats(dehazed)['lum_std'] < _stats(img)['lum_std'] * 0.95:
                accept = False
        if accept:
            # 去雾是「用户显式请求」或「强雾检测」触发的重构变换（非微调），
            # 不应被 _audit 的「是否变差」门禁拦下；安全性由保守的 HAZE_TRIGGER 把关。
            img = dehazed
            reconstructed = True
            steps.append({'key': 'dehaze', 'name': '去雾（暗通道先验）',
                          'detail': '大气光 A=%s，平均透射率 %.2f（ω=0.95，导向滤波高斯近似）'
                                    % (dh['A'], dh['mean_t']),
                          'params': dh})
            st = _stats(img)
            mx = _measure_extra(img)

    # ---- 水平校正（自动地平线拉直）----
    # 仅显式「level」预设才做（几何变换，属用户主动请求，绝不 auto 触发以免误旋转）。
    if preset == 'level':
        det = _detect_horizon(img)
        if det:
            angle, votes, total = det
            if abs(angle) >= 0.5:
                img, used = _straighten(img, angle)
                reconstructed = True
                steps.append({'key': 'level', 'name': '水平校正',
                              'detail': '检测到近水平主线倾角 %.1f°（票数 %d/%d）→ 已拉直并裁内切矩形'
                                        % (angle, votes, total),
                              'params': {'angle': round(angle, 2)}})
                st = _stats(img)
                mx = _measure_extra(img)

    # ---- 暗角校正（径向增益复原）----
    # 仅显式「vignette」预设才做（光度重建变换，属用户主动请求，绝不 auto 触发）。
    # 无暗角（k < VIGNETTE_MIN_K）时自动跳过 —— 即便显式请求也不「无中生有」。
    if preset == 'vignette':
        vg, vd = _vignette_correct(img)
        if vd:
            img = vg
            reconstructed = True
            steps.append({'key': 'vignette', 'name': '暗角校正',
                          'detail': '估计径向压暗 k=%.3f → 角部增益 ×%.2f（径向增益复原，越界裁剪）'
                                    % (vd['k'], vd['corner_gain']),
                          'params': vd})
            st = _stats(img)
            mx = _measure_extra(img)

    # ---- 色差校正（横向色差 / 紫边消除）----
    # 仅显式「chromatic」预设才做（光学修复，属用户主动请求，绝不 auto 触发）。
    # 纯 Pillow：R 通道轻缩、B 通道轻放（相对绿通道径向对齐），边缘镜像兜底无黑边。
    if preset == 'chromatic':
        cc, cd = _chromatic_correct(img, 1.0)
        if cd:
            img = cc
            reconstructed = True
            steps.append({'key': 'chromatic', 'name': '色差校正',
                          'detail': 'R/B 通道径向对齐（k=%.4f）：收回高反差边缘的紫/绿镶边'
                                    % cd['k'],
                          'params': cd})
            st = _stats(img)
            mx = _measure_extra(img)

    # ---- 镜头畸变校正（桶形径向反畸变）----
    # 仅显式「lens」预设才做（几何重建，属用户主动请求，绝不 auto 触发）。
    if preset == 'lens':
        le2, le_info = _lens_correct(img, 1.0)
        if le_info:
            img = le2
            reconstructed = True
            steps.append({'key': 'lens', 'name': '镜头畸变校正',
                          'detail': '桶形畸变径向反畸变（k=%.3f，%dx%d 网格）'
                                    % (le_info['k'], le_info['grid'], le_info['grid']),
                          'params': le_info})
            st = _stats(img)
            mx = _measure_extra(img)

    # ---- 红眼去除（保守启发式）----
    # 仅显式「redeye」预设才做（属用户主动请求，绝不 auto 触发）。
    if preset == 'redeye':
        re2, re_info = _redeye_remove(img, 1.0)
        if re_info:
            img = re2
            reconstructed = True
            steps.append({'key': 'redeye', 'name': '红眼去除',
                          'detail': re_info['note'], 'params': re_info})
            st = _stats(img)
            mx = _measure_extra(img)

    # ---- 人像美化（肤色磨皮 + 美白）----
    # 仅显式「portrait」预设才做（属用户主动请求的编辑，绝不 auto 触发以免「人人磨皮」）。
    # 无肤色（占比过低）时自动跳过 —— 即便显式请求也不「无中生有」。
    if portrait_mode:
        pb, pi = _portrait_beautify(img, 1.0)
        if pi:
            img = pb
            reconstructed = True
            det = '肤色占比 %.1f%% · 磨皮强度 %.2f' % (pi['skin'] * 100, pi['smooth'])
            if 'brighten' in pi:
                det += ' · 美白 +%.0f%%' % (pi['brighten'] * 100)
            steps.append({'key': 'portrait', 'name': '人像美化（肤色磨皮）',
                          'detail': det + '（边缘保持磨皮，非肤色区不动）', 'params': pi})
            st = _stats(img)
            mx = _measure_extra(img)

    img, wb = _white_balance(img, p['wb'])
    if wb:
        steps.append({'key': 'wb', 'name': '白平衡（Shades-of-Gray p=6）',
                      'detail': '线性光 von Kries：R×%.3f  G×%.3f  B×%.3f'
                                % tuple(wb),
                      'gains': wb})
    img, tn = _tone_curve(img, st, p['tone'])
    if tn:
        steps.append({'key': 'tone', 'name': '阴影 / 高光',
                      'detail': '抬阴影 +%.2f（暗部占比 %.1f%%）· 回收高光 +%.2f（削顶占比 %.1f%%）'
                                % (tn['lift'], st['lo'] * 100, tn['recover'], st['hi'] * 100),
                      'params': tn})
    img, lv = _auto_levels(img, st, mx, p['levels'])
    if lv:
        steps.append({'key': 'levels', 'name': '自动色阶',
                      'detail': '按实测分位**有界**拉伸到 6..249（保留高光余量，仅对比不足时触发）'})
    # 色阶/阴影会改变亮度与削顶 —— 后续环节必须按「当前画面」重新测量，
    # 否则高光限幅用的是过期数据（实测：色阶把画面推到 249 后，亮度仍按 hi=0 满额提亮 → 推爆）
    st = _stats(img)
    img, bf = _brightness(img, st, p['bright'])
    if bf:
        _bh = ('；高光占比 %.1f%% 已限幅' % (st['hi'] * 100)) if st.get('hi') else ''
        steps.append({'key': 'bright', 'name': '亮度',
                      'detail': '实测亮度 %.2f → 目标 0.46，增益 ×%.2f（≈%+.2fEV）%s'
                                % (st['lum'], bf, math.log2(bf), _bh),
                      'factor': bf})
    img, cf = _contrast(img, st, p['contrast'])
    if cf:
        steps.append({'key': 'contrast', 'name': '对比度',
                      'detail': '实测离散 %.2f → 目标 0.19，以均值%.0f为轴心展开 ×%.2f'
                                % (st['lum_std'], st['lum'] * 255, cf),
                      'factor': cf})
    # ---- 影调层：HDR 色调映射 / 明度结构（显式强效果）----
    # 作用于「已完成基础校正」的画面、位于清晰度之前：hdr 重排影调后细节/噪声/锐化都在
    # 最终影调上评估；structure 抬微对比后 mx['lc'] 上升，clarity 会自然跳过（不双重增强）。
    if preset == 'hdr':
        tm2, tm = _tone_map(img, 1.0)
        if tm:
            img = tm2
            reconstructed = True
            st = _stats(img)
            mx = _measure_extra(img)
            steps.append({'key': 'hdr', 'name': 'HDR 色调映射',
                          'detail': tm['note'], 'params': tm})
    if struct_mode:
        st2, si = _structure(img, 1.0)
        if si:
            img = st2
            reconstructed = True
            st = _stats(img)
            mx = _measure_extra(img)
            steps.append({'key': 'structure', 'name': '明度结构',
                          'detail': si['note'], 'params': si})
    if preset == 'dodge':
        dg2, dg = _dodge(img, 1.0)
        if dg:
            img = dg2
            reconstructed = True
            st = _stats(img)
            mx = _measure_extra(img)
            steps.append({'key': 'dodge', 'name': '局部提亮',
                          'detail': dg['note'], 'params': dg})

    cl = None
    if not portrait_mode and not struct_mode:   # 人像：磨皮后不再做全局清晰度（否则把磨掉的纹理又锐回来）
        img, cl = _clarity(img, mx, p['clarity'])   # 结构：structure 已做局部对比，跳过 clarity 免双重增强
    if cl:
        steps.append({'key': 'clarity', 'name': '清晰度（局部对比）',
                      'detail': '局部对比 %.3f 偏低 → 明度通道大半径非锐化 ×%d%%（上限 45%%）'
                                % (mx['lc'], int(cl * 100)),
                      'amount': cl})
    img, vb = _vibrance(img, st, p['vibrance'])
    if vb:
        steps.append({'key': 'vibrance', 'name': '自然饱和度',
                      'detail': '实测 %.2f 偏淡 → 淡处多补、浓处几乎不动（强度 %.2f，上限 0.45）'
                                % (st['sat'], vb),
                      'amount': vb})
    # 降噪档位：EXIF 与「画面噪声估计」取较大者（无 EXIF 时也有保护）
    dl = max(_denoise_level(exif), _denoise_level_noise(mx['noise']))
    if dl:
        reason = []
        iso = int((exif or {}).get('iso') or 0)
        shutter = float((exif or {}).get('shutter') or 0)
        if iso >= 3200:
            reason.append('ISO %d' % iso)
        if shutter >= 1.0:
            reason.append('快门 %.0fs' % shutter)
        if _denoise_level_noise(mx['noise']) >= dl and not reason:
            reason.append('噪声估计 %.3f' % mx['noise'])
        # 真正作用到像素（此前只把步骤记进摘要、却从未对 img 施加，会误导用户
        # 且破坏「先降噪再锐化」的契约）。降噪必须在锐化之前完成。
        img = _denoise(img, dl)
        steps.append({'key': 'denoise', 'name': '降噪',
                      'detail': '档位 %d（%s）' % (dl, ' + '.join(reason) or 'EXIF'),
                      'level': dl})
    img, shp = _sharpen(img, p['sharp'], exif, noise=mx['noise'], portrait=portrait_mode)
    steps.append({'key': 'sharp', 'name': '锐化',
                  'detail': 'UnsharpMask 半径 %.1f · 强度 %d%% · 阈值 %d（噪声 %.3f）'
                            % (shp['radius'], shp['percent'], shp['threshold'], mx['noise']),
                  'params': shp})

    # ---- 风格化（显式预设：胶片 / 黑白 / 柔光）----
    # 作为「最终风格层」作用于已校正、已锐化的画面，故不会被前面的微调环节抵消。
    if preset == 'film':
        img = _film_tone(img)
        img = _split_tone(img, amount=0.55)
        img = ImageEnhance.Color(img).enhance(0.92)      # 饱和度略降（film-grade 取向）
        img = _add_grain(img, GRAIN_AMOUNT)
        reconstructed = True
        steps.append({'key': 'film', 'name': '胶片风格',
                      'detail': '抬黑+高光滚降（lift=%.2f/rolloff=%.2f）、阴影冷/高光暖分离色调、'
                                '饱和 −8%%、随亮度耦合颗粒（%.2f）'
                                % (FILM_LIFT, FILM_ROLLOFF, GRAIN_AMOUNT),
                      'params': {'lift': FILM_LIFT, 'rolloff': FILM_ROLLOFF, 'grain': GRAIN_AMOUNT}})
    elif preset == 'bw':
        img = _black_white(img)
        img = ImageEnhance.Contrast(img).enhance(1.05)   # 黑白轻微加对比
        reconstructed = True
        steps.append({'key': 'bw', 'name': '黑白（通道混合）',
                      'detail': '通道混合灰度 r·0.30+g·0.59+b·0.11，轻微加对比',
                      'params': {'weights': [0.30, 0.59, 0.11]}})
    elif preset == 'glow':
        img = _glow(img, amount=0.5)
        reconstructed = True
        steps.append({'key': 'glow', 'name': '柔光（Orton）',
                      'detail': '模糊提亮副本与原图 screen 混合（强度 0.50），梦幻柔焦',
                      'params': {'amount': 0.5}})

    # ---- 输出自检 + 回退（宁可不改，也不改坏）----
    issues = _audit(st0, mx, img)
    if not issues:
        steps.append({'key': 'audit', 'name': '质量自检',
                      'detail': '通过：无新增削顶/死黑，反差与色偏未变差', 'issues': []})
    elif reconstructed:
        # 去雾 / 水平校正 属用户显式请求的「重构变换」：几何拉直（裁内切矩形 /
        # 旋转插值）或去雾重建会合法地改变像素统计（含轻微色偏/反差变动），
        # 不应被微调审计的「变差」门禁整张回退——否则用户要的校正会被丢弃。
        # 安全性已由 HAZE_TRIGGER / 地平线票数门禁把关，此处仅记录提示并保留结果。
        steps.append({'key': 'audit', 'name': '质量自检',
                      'detail': '显式编辑（去雾/水平校正/暗角/人像美化）已保留；统计变动来自编辑本身（%s）'
                                % '；'.join(issues),
                      'issues': issues})
    else:
        # 降级重做：只保留最温和的白平衡 + 局部阴影
        g = Image.open(path)
        g = ImageOps.exif_transpose(g).convert('RGB')
        g, wb2 = _white_balance(g, p['wb'])
        g, tn2 = _tone_curve(g, st0, p['tone'])
        steps_g = []
        if wb2:
            steps_g.append({'key': 'wb', 'name': '白平衡（Shades-of-Gray p=6）',
                            'detail': '线性光 von Kries：R×%.3f  G×%.3f  B×%.3f' % tuple(wb2),
                            'gains': wb2})
        if tn2:
            steps_g.append({'key': 'tone', 'name': '阴影 / 高光（轻力度）',
                            'detail': '抬阴影 +%.2f · 回收高光 +%.2f'
                                      % (tn2['lift'], tn2['recover']), 'params': tn2})
        if _audit(st0, mx, g):
            img = orig_img
            steps = [{'key': 'audit', 'name': '质量自检',
                      'detail': '编辑会让画质变差 → 已回退为原图（%s）' % '；'.join(issues),
                      'issues': issues}]
        else:
            img = g
            steps = steps_g + [{'key': 'audit', 'name': '质量自检',
                                'detail': '强力度未通过（%s）→ 已降为轻力度' % '；'.join(issues),
                                'issues': issues}]

    root, _ = os.path.splitext(path)
    out = root + '_retouched.jpg'
    save_kwargs = {'quality': quality, 'subsampling': 1, 'optimize': True}
    # EXIF/GPS 完整搬运（transplant 自带方向标记，导出前不要再 transpose 保存）
    try:
        exif_bytes = piexif.load(path)
        # 方向已物理摆正，清掉 Orientation，防止查看器再转一次
        exif_bytes['0th'].pop(piexif.ImageIFD.Orientation, None)
        save_kwargs['exif'] = piexif.dump(exif_bytes)
    except Exception:
        pass
    img.save(out, 'JPEG', **save_kwargs)

    summary = {
        'engine': ENGINE_ID,
        'in': path, 'out': out,
        'size': img.size, 'denoise': dl,
        'lum_before': round(st0['lum'], 3),
        'lum_after': round(_stats(img)['lum'], 3),
        'sat_before': round(st0['sat'], 3),
        'sat_after': round(_stats(img)['sat'], 3),
        'steps': steps,
        'bytes': os.path.getsize(out),
    }
    return out, summary


def retouch_with_plan(path, params, exif=None, quality=92):
    """按**显式参数增量**执行修图（决策层 EditPlan 的本地执行入口）。

    与 retouch_file 的区别：retouch_file 自己测量并决定幅度，
    这里幅度已由决策层给出（params 是 -1..1 的增量），本函数只负责忠实执行。

    本地管线**完整覆盖**决策层的 13 项增量：warmth / brightness / contrast /
    highlights / shadows / whites / blacks / saturation / vibrance / clarity /
    tint / denoise / sharpness。其中 highlights/shadows/whites/blacks 走局部影调
    `_param_tone`（局部光照掩膜 + 端点 LUT），vibrance/clarity/tint 亦本地实现 ——
    不再有「需 Pixel Cake 才能执行」的缺口（引擎 1.3.0 起）。
    """
    import math as _math

    params = params or {}
    img = Image.open(path)
    img = ImageOps.exif_transpose(img)
    img = img.convert('RGB')
    st = _stats(img)
    steps = []

    def _d(k):
        try:
            return float(params.get(k, 0) or 0)
        except Exception:
            return 0.0

    # 1. 色温（通道增益，刻意保守：±0.05/单位，避免毁掉创作色调）
    w = _d('warmth')
    if abs(w) >= 0.01:
        r, g_, b = img.split()
        r = r.point(lambda v: min(255, int(v * (1 + w * 0.05))))
        b = b.point(lambda v: min(255, int(v * (1 - w * 0.05))))
        img = Image.merge('RGB', (r, g_, b))
        steps.append({'key': 'warmth', 'name': '色温',
                      'detail': 'R×%.3f  B×%.3f（指令 %+.2f）'
                                % (1 + w * 0.05, 1 - w * 0.05, w)})

    # 2. 亮度
    bd = _d('brightness')
    if abs(bd) >= 0.01:
        f = max(0.60, min(1.60, 1 + bd * 0.55))
        img = ImageEnhance.Brightness(img).enhance(f)
        steps.append({'key': 'brightness', 'name': '亮度',
                      'detail': '指令 %+.2f → 增益 ×%.2f（≈%+.2fEV）'
                                % (bd, f, _math.log2(f)), 'factor': round(f, 3)})

    # 3. 对比度（以实测均值为轴心展开，避免固定 128 轴心把暗部压死）
    cd = _d('contrast')
    if abs(cd) >= 0.01:
        f = max(0.80, min(1.50, 1 + cd * 0.60))
        mid = st['lum'] * 255.0
        lut = [max(0, min(255, int((v - mid) * f + mid + 0.5))) for v in range(256)]
        img = img.point(lut * 3)
        steps.append({'key': 'contrast', 'name': '对比度',
                      'detail': '指令 %+.2f → 以均值 %.0f 为轴心展开 ×%.2f'
                                % (cd, mid, f), 'factor': round(f, 3)})

    # 3b. 局部影调（highlights / shadows / whites / blacks）
    img, tn2 = _param_tone(img, _d('highlights'), _d('shadows'), _d('whites'), _d('blacks'))
    if tn2:
        steps.append({'key': 'tonal', 'name': '阴影/高光/黑场/白场',
                      'detail': '高光 %+.2f · 阴影 %+.2f · 白场 %+.2f · 黑场 %+.2f'
                                '（局部光照掩膜 + 端点 LUT）'
                                % (tn2['highlights'], tn2['shadows'], tn2['whites'], tn2['blacks']),
                      'params': tn2})

    # 4. 饱和度
    sd = _d('saturation')
    if abs(sd) >= 0.01:
        f = max(0.0, min(2.0, 1 + sd * 0.50))
        img = ImageEnhance.Color(img).enhance(f)
        steps.append({'key': 'saturation', 'name': '饱和度',
                      'detail': '指令 %+.2f → 增益 ×%.2f' % (sd, f),
                      'factor': round(f, 3)})

    # 4b. 自然饱和度（淡处多补、浓处几乎不动）
    vb2 = _d('vibrance')
    if abs(vb2) >= 0.01:
        h_v, s_v, v_v = img.convert('HSV').split()
        vv2 = min(0.45, abs(vb2) * 0.45) * (1.0 if vb2 > 0 else -1.0)
        lut = [max(0, min(255, int(round(x * (1.0 + vv2 * (1.0 - x / 255.0))))))
               for x in range(256)]
        img = Image.merge('HSV', (h_v, s_v.point(lut), v_v)).convert('RGB')
        steps.append({'key': 'vibrance', 'name': '自然饱和度',
                      'detail': '指令 %+.2f（淡处多补、浓处几乎不动，上限 0.45）' % vb2})

    # 4c. 清晰度（明度通道局部对比；负向为柔化）
    cld = _d('clarity')
    if abs(cld) >= 0.01:
        h_c, s_c, v_c = img.convert('HSV').split()
        if cld > 0:
            pct = int(min(60, cld * 55))
            v_c2 = v_c.filter(ImageFilter.UnsharpMask(radius=6, percent=pct, threshold=0))
            note = '局部对比 +%d%%（大半径非锐化）' % pct
        else:
            rr = 0.8 + 1.6 * (-cld)
            v_c2 = v_c.filter(ImageFilter.GaussianBlur(rr))
            note = '柔化 −%.2f（明度模糊 R=%.1f）' % (-cld, rr)
        img = Image.merge('HSV', (h_c, s_c, v_c2)).convert('RGB')
        steps.append({'key': 'clarity', 'name': '清晰度',
                      'detail': '指令 %+.2f → %s' % (cld, note)})

    # 4d. 色调（绿 ↔ 品红）
    td = _d('tint')
    if abs(td) >= 0.01:
        r_t, g_t, b_t = img.split()
        if td > 0:            # + → 品红：R、B 升，G 降
            fr, fg, fb = 1 + td * 0.05, 1 - td * 0.04, 1 + td * 0.05
        else:                 # − → 绿：G 升，R、B 降
            m_t = -td
            fr, fg, fb = 1 - m_t * 0.04, 1 + m_t * 0.05, 1 - m_t * 0.04
        img = Image.merge('RGB', (
            r_t.point([min(255, int(v * fr)) for v in range(256)]),
            g_t.point([min(255, int(v * fg)) for v in range(256)]),
            b_t.point([min(255, int(v * fb)) for v in range(256)])))
        steps.append({'key': 'tint', 'name': '色调（绿↔品红）',
                      'detail': '指令 %+.2f → R×%.3f G×%.3f B×%.3f' % (td, fr, fg, fb)})

    # 5. 降噪（指令优先，EXIF 兜底）
    nd = _d('denoise')
    lvl = 2 if nd >= 0.35 else (1 if nd >= 0.12 else 0)
    if lvl == 0:
        lvl = _denoise_level(exif)
    if lvl:
        img = _denoise(img, lvl)
        steps.append({'key': 'denoise', 'name': '降噪',
                      'detail': '档位 %d（指令 %+.2f / EXIF 兜底）' % (lvl, nd),
                      'level': lvl})

    # 6. 锐化
    shd = _d('sharpness')
    percent = int(max(30, min(140, 70 + shd * 70)))
    radius = 1.2 if max(img.size) >= 3000 else 1.0
    img = img.filter(ImageFilter.UnsharpMask(radius=radius, percent=percent,
                                             threshold=2))
    steps.append({'key': 'sharp', 'name': '锐化',
                  'detail': 'UnsharpMask 半径 %.1f · 强度 %d%%（指令 %+.2f）'
                            % (radius, percent, shd)})

    root, _ = os.path.splitext(path)
    out = root + '_retouched.jpg'
    save_kwargs = {'quality': quality, 'subsampling': 1}
    try:
        eb = piexif.load(path)
        eb['0th'].pop(piexif.ImageIFD.Orientation, None)
        save_kwargs['exif'] = piexif.dump(eb)
    except Exception:
        pass
    img.save(out, 'JPEG', **save_kwargs)

    after = _stats(img)
    summary = {
        'engine': ENGINE_ID,
        'in': path, 'out': out,
        'size': img.size, 'denoise': lvl,
        'lum_before': round(st['lum'], 3), 'lum_after': round(after['lum'], 3),
        'sat_before': round(st['sat'], 3), 'sat_after': round(after['sat'], 3),
        'steps': steps,
        'bytes': os.path.getsize(out),
    }
    return out, summary


def retouch_many(paths, preset='auto', exif_map=None, progress=None):
    """批量修图。progress(done, total, path) 用于向 UI 汇报。

    返回 (成功列表, 失败列表[(path, err)])。
    """
    exif_map = exif_map or {}
    oks, errs = [], []
    total = len(paths)
    for i, p in enumerate(paths):
        try:
            out, s = retouch_file(p, preset, exif_map.get(p))
            oks.append(s)
        except Exception as e:                     # 单张失败不影响批次
            errs.append((p, str(e)))
        if progress:
            progress(i + 1, total, p)
    return oks, errs


# ------------------------------------------------------------ 本地预设建议（决策层）
# 纯本地、零云端：只用引擎自身的测量（_stats/_measure_extra + 几何/检测算子）为预设打分，
# 给出「带证据的建议」—— **仅建议，不改图**；是否采纳由用户决定（问句而非命令）。
# 服务端的 /api/suggest、桌面端的 aiSuggest 都走这里。
_PRESET_LABELS = {
    'dehaze': '去雾', 'level': '水平校正', 'vignette': '暗角校正',
    'chromatic': '色差校正', 'lens': '镜头畸变校正', 'redeye': '红眼去除',
    'hdr': 'HDR 色调映射', 'structure': '明度结构', 'dodge': '局部提亮',
    'portrait': '人像美化', 'film': '胶片风格', 'bw': '黑白', 'glow': '柔光',
    'strong': '强力', 'auto': '标准',
}
_PRESET_GROUP = {
    'dehaze': '修复校正', 'level': '修复校正', 'vignette': '修复校正',
    'chromatic': '修复校正', 'lens': '修复校正', 'redeye': '修复校正',
    'hdr': '影调', 'structure': '影调', 'dodge': '影调',
    'portrait': '人像', 'film': '风格化', 'bw': '风格化', 'glow': '风格化',
    'strong': '基础强度', 'auto': '基础强度',
}


def _cl01(v):
    """夹到 0..1（建议打分用）。"""
    return max(0.0, min(1.0, float(v)))


def recommend_presets(path, exif=None, limit=4, quality=None):
    """本地 AI 预设建议：按画面特征为预设打分排序，**仅建议、不改图**（纯本地零云端）。

    依据（全部来自引擎自身测量，无额外依赖）：
      * 雾感 haze → `dehaze`；地平线倾角 → `level`；径向压暗 k → `vignette`
      * 死黑/削顶占比 → `hdr`；整体偏暗且非大片死黑 → `dodge`
      * 局部对比偏低（发闷）→ `structure`；肤色占比 → `portrait`
      * 疑似红眼像素 → `redeye`；近无色 → `bw`；色彩偏淡 → `film`
      * 欠曝/平淡 → `strong`
      * （P4 可选）`quality={指标:分}`：pyiqa 画质综合分偏低 → 给 `strong` 加分救细节
    返回 {'suggestions': [{id,label,group,score,reason,kind}...], 'measure': {...},
          'quality': {...}|None, 'note': str}
    """
    out = {'suggestions': [], 'measure': {}, 'note': '', 'quality': None}
    # P4 画质证据（pyiqa）：独立于下方图像分析，提前计算，以便即使文件不可读
    # 也能把画质分随建议一并返回（画质分由 ai._quality_hint 经 Pixel Cake 取得）。
    qvals = {}
    qboost = 0.0
    if quality and isinstance(quality, dict):
        qvals = {k: float(v) for k, v in quality.items()
                 if isinstance(v, (int, float))}
        if qvals:
            avg = sum(qvals.values()) / len(qvals)
            qboost = max(0.0, (0.50 - avg)) * 0.6
            out['quality'] = qvals
    if not path or not os.path.isfile(path):
        out['note'] = '文件不存在'
        return out
    try:
        # 分析只需粗尺度（≤1024 长边足够：暗角/肤色/地平线/红眼都是低频或结构特征），
        # 解码期降采样 + 缓存，避免对大图整张解码与冗余 convert。
        img = _load_analyze(path, 1024)
    except Exception as e:                              # noqa: BLE001
        out['note'] = '无法读取：%s' % e
        return out

    st = _stats(img)
    mx = _measure_extra(img)
    try:
        # 肤色占比：用**硬阈值**（软掩膜 ≥200 才算），否则灰/中性像素（Cb≈Cr≈128 落在
        # 肤色窗口边缘）会被软掩膜误判成肤色 —— 建议必须严格，否则 portrait 到处乱报。
        sk = _skin_mask(img)
        hs = sk.histogram()
        tot = max(1, sum(hs))
        skin = sum(c for i, c in enumerate(hs) if i >= 200) / float(tot)
    except Exception:                                   # noqa: BLE001
        skin = 0.0
    try:
        # 径向压暗系数：单图估计对「内容本身中间亮」很敏感，故要求**跨环带数稳定**
        # （bins=10 与 14 估计一致才算）——真实的镜头晕影在各分环下都稳，内容造成的
        # 假晕影会随分环剧烈变化。不稳定即判 0（不乱建议）。
        k1 = _estimate_vignette(img, bins=10)
        k2 = _estimate_vignette(img, bins=14)
        if k1 and k2 and abs(k1 - k2) <= 0.25 * max(k1, k2):
            vk = float(min(k1, k2))
        else:
            vk = 0.0
    except Exception:                                   # noqa: BLE001
        vk = 0.0
    ang = 0.0
    try:                                                # 地平线倾角
        det = _detect_horizon(img)
        if det:
            ang = abs(float(det[0]))
    except Exception:                                   # noqa: BLE001
        ang = 0.0
    re_px = 0
    try:                                                # 疑似红眼像素
        small = img.copy()
        small.thumbnail((640, 640))
        _, ri = _redeye_remove(small, 1.0)
        if ri:
            re_px = int(ri.get('pixels') or 0)
    except Exception:                                   # noqa: BLE001
        re_px = 0

    lum, std, sat = st['lum'], st['lum_std'], st['sat']
    lo, hi = st['lo'], st['hi']
    haze, lc, dr = mx['haze'], mx['lc'], mx['dr']
    iso = int((exif or {}).get('iso') or 0)

    # P4 画质证据（qvals / qboost）已在函数开头提前计算并写入 out['quality']，
    # 此处直接使用 qboost（画质越差 → 给「强力」预设加分越多）。

    cands = []

    def add(pid, score, reason, kind='feature'):
        s = _cl01(score)
        if s >= 0.35:
            cands.append({'id': pid, 'label': _PRESET_LABELS.get(pid, pid),
                          'group': _PRESET_GROUP.get(pid, ''), 'score': round(s, 2),
                          'reason': reason, 'kind': kind})

    # 内容门（std）：近乎平场（无内容）时 haze 指标会虚高，不该建议去雾
    content = _cl01((std - 0.008) / 0.020)
    add('dehaze', (haze - 0.40) / 0.30 * content,
        '雾感 %.2f（亮区被洗白 / 反差窄）→ 去雾' % haze, 'repair')
    add('level', (ang - 0.5) / 2.5,
        '检测到近水平主线倾角 %.1f° → 拉直' % ang, 'repair')
    add('vignette', min(0.85, (vk - 0.12) / 0.15),
        '径向压暗 k=%.3f（四角偏暗）→ 暗角校正' % vk, 'repair')
    if re_px >= 30:
        add('redeye', min(0.95, 0.5 + re_px / 300.0),
            '检出 %d 个疑似红眼像素 → 红眼去除' % re_px, 'repair')
    add('portrait', min(0.95, (skin - 0.05) / 0.10),
        '肤色占比 %.1f%% → 人像美化（非肤色区保持不变）' % (skin * 100), 'portrait')
    add('hdr', max(lo * 8.0, hi * 8.0),
        '死黑 %.1f%% / 削顶 %.1f%% → HDR 色调映射压缩动态范围'
        % (lo * 100, hi * 100), 'tone')
    add('dodge', (0.42 - lum) / 0.16 * (1.0 - _cl01(lo * 6.0) * 0.5),
        '整体偏暗（亮度 %.2f）且非大片死黑 → 局部提亮' % lum, 'tone')
    add('structure', ((0.045 - lc) / 0.030) if lc >= 0.006 else -1.0,
        '局部对比 %.3f 偏低（发闷）→ 明度结构提质感' % lc, 'tone')
    # 风格类（film / bw）是主观取向，分数**封顶**（bw≤0.80 / film≤0.60），
    # 让「有实测证据」的功能性建议优先；用户仍是最终决定者。
    add('bw', min(0.80, (0.05 - sat) / 0.04),
        '近乎无色（饱和 %.2f）→ 黑白' % sat, 'style')
    add('film', min(0.60, (0.20 - sat) / 0.12),
        '色彩偏淡（%.2f）→ 胶片风格' % sat, 'style')
    add('strong', max((0.32 - lum) / 0.14, (0.13 - std) / 0.09) - 0.10 + qboost,
        '欠曝 / 平淡（亮度 %.2f，离散 %.2f）→ 强力' % (lum, std), 'base')

    cands.sort(key=lambda d: (-d['score'], d['id']))
    out['suggestions'] = cands[:max(1, int(limit))]
    out['measure'] = {'lum': round(lum, 3), 'lum_std': round(std, 3), 'sat': round(sat, 3),
                      'lo': round(lo, 3), 'hi': round(hi, 3), 'haze': round(haze, 3),
                      'lc': round(lc, 3), 'dr': round(dr, 3), 'skin': round(skin, 3),
                      'vignette_k': round(vk, 4), 'tilt': round(ang, 2),
                      'redeye': re_px, 'iso': iso}
    out['note'] = ('按画面特征给出 %d 条建议（本地规则，仅建议不改图）' % len(out['suggestions'])
                   if out['suggestions'] else '各项指标正常，用「标准（auto）」即可')
    if qvals:
        out['note'] += ' ｜ 画质(pyiqa): ' + ', '.join(
            '%s=%.2f' % (k, v) for k, v in qvals.items())
    return out
