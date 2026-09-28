# -*- coding: utf-8 -*-
"""P5 金样生成器：用 Python Lumen（core/retouch.py）对 N 张样图跑 auto 子集管线，
输出 PNG 金样 + steps key 序列 JSON，供 C++ LumenEngine 冒烟对拍（阈值容差）。

用法：client/venv/Scripts/python.exe _gold_lumen.py [样本数=6]
铁律：与 native LumenEngine::autoPipeline 的算子顺序/门控逐条一致
（wb→tone→levels→重测→bright→contrast→clarity→vibrance→denoise→sharp→audit）。
"""
import json
import os
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(ROOT, 'client'))

from lightalbum.core import retouch  # noqa: E402
from PIL import Image, ImageEnhance  # noqa: E402

GOLD_DIR = os.path.join(ROOT, '_gold_lumen')
THUMBS_DIR = os.environ.get('LA_THUMBS', r'D:\lightalbum\thumbs')
N = int(sys.argv[1]) if len(sys.argv) > 1 else 6


def pick_inputs(n):
    names = sorted(f for f in os.listdir(THUMBS_DIR)
                   if f.lower().endswith(('.jpg', '.jpeg', '.png')))
    picked = []
    for name in names:
        if len(picked) >= n:
            break
        try:
            im = Image.open(os.path.join(THUMBS_DIR, name)).convert('RGB')
            if min(im.size) < 96:
                continue
            if max(im.size) > 640:   # 金样与冒烟都在粗尺度跑，速度优先
                im.thumbnail((640, 640))
            picked.append(im)
        except Exception:            # noqa: BLE001
            continue
    return picked


def auto_subset(img):
    """与 native LumenEngine::autoPipeline 完全一致的 auto 子集管线。"""
    steps = []
    st0 = retouch._stats(img)
    mx0 = retouch._measure_extra(img)
    out = img

    out, wb = retouch._white_balance(out, 1.0)
    if wb:
        steps.append('wb')
    out, tn = retouch._tone_curve(out, st0, 1.0)
    if tn:
        steps.append('tone')
    out, lv = retouch._auto_levels(out, st0, mx0, 1.0)
    if lv:
        steps.append('levels')
    st = retouch._stats(out)

    out, bf = retouch._brightness(out, st, 1.0)
    if bf:
        steps.append('bright')
    out, cf = retouch._contrast(out, st, 1.0)
    if cf:
        steps.append('contrast')
    out, cl = retouch._clarity(out, mx0, 1.0)
    if cl:
        steps.append('clarity')
    out, vb = retouch._vibrance(out, st, 1.0)
    if vb:
        steps.append('vibrance')

    dl = retouch._denoise_level_noise(mx0['noise'])
    if dl:
        out = retouch._denoise(out, dl)
        steps.append('denoise')
    out = retouch._sharpen(out, 1.0, None, noise=mx0['noise'])[0]
    steps.append('sharp')

    reverted = False
    issues = retouch._audit(st0, mx0, out)
    if not issues:
        steps.append('audit')
    else:
        g, wb2 = retouch._white_balance(img, 1.0)
        g, tn2 = retouch._tone_curve(g, st0, 1.0)
        gentle = []
        if wb2:
            gentle.append('wb')
        if tn2:
            gentle.append('tone')
        if retouch._audit(st0, mx0, g):
            out = img
            steps = ['audit']
            reverted = True
        else:
            out = g
            steps = gentle + ['audit']
    return out, steps, reverted


def op_gold(img):
    """P5-2：显式算子**逐算子独立**金样（隔离比对，避免 auto 管线差异累积）。
    颗粒(_add_grain)为 PRNG 噪声，C++ 无法字节对齐 → film 金样不含颗粒（文档化）。
    """
    out = {}
    # film：影调 LUT + 分离色调 + 饱和 0.92（不含颗粒）
    f = retouch._film_tone(img)
    f = retouch._split_tone(f, amount=0.55)
    f = ImageEnhance.Color(f).enhance(0.92)
    out['film'] = f
    # bw：通道混合 + 轻微加对比
    b = retouch._black_white(img)
    out['bw'] = ImageEnhance.Contrast(b).enhance(1.05)
    # glow：Orton
    out['glow'] = retouch._glow(img, amount=0.5)
    # redeye：启发式（多数图无红眼 → 返回原图，用于验证「不乱修」）
    re_img, re_info = retouch._redeye_remove(img, 1.0)
    out['redeye'] = re_img
    out['_redeye_px'] = int((re_info or {}).get('pixels') or 0)
    # vignette：无暗角则原图（验证 VIGNETTE_MIN_K 门控）
    vg, vd = retouch._vignette_correct(img)
    out['vignette'] = vg
    out['_vignette_k'] = float((vd or {}).get('k') or 0.0)
    # hdr / structure / dodge
    out['hdr'] = retouch._tone_map(img, 1.0)[0]
    out['structure'] = retouch._structure(img, 1.0)[0]
    out['dodge'] = retouch._dodge(img, 1.0)[0]
    # P5-3：去雾 / 色差 / 人像美化
    out['dehaze'] = retouch._dehaze(img, 1.0)[0]
    out['chromatic'] = retouch._chromatic_correct(img, 1.0)[0]
    pb, pi = retouch._portrait_beautify(img, 1.0)
    out['portrait'] = pb
    out['_portrait_skin'] = float((pi or {}).get('skin') or 0.0)
    # P5-4：镜头畸变校正（确定性网格 AFFINE，任意图皆可）
    out['lens'] = retouch._lens_correct(img, 1.0)[0]
    return out


# P5-4 retouch_with_plan 固定参数字典（覆盖全部 13 项增量）
PLAN_PARAMS = {
    'warmth': 0.3, 'brightness': 0.2, 'contrast': 0.25,
    'highlights': -0.3, 'shadows': 0.4, 'whites': 0.1, 'blacks': -0.2,
    'saturation': 0.3, 'vibrance': 0.4, 'clarity': 0.3, 'tint': 0.2,
    'denoise': 0.2, 'sharpness': 0.3,
}


def synth_level(n=4):
    """合成倾斜地平线图（≤480，避免降采样差异）：上天空/下地面 + 整体旋转倾角。"""
    angles = [3.0, 6.0, -4.0, 8.0]
    out = []
    for a in angles[:n]:
        w, h = 360, 270
        sky, ground = 205, 55
        base = Image.new('RGB', (w, h), (sky, sky, sky))
        px = base.load()
        for y in range(h // 2, h):
            for x in range(w):
                px[x, y] = (ground, ground, ground)
        out.append(base.rotate(a, expand=True, resample=Image.BICUBIC))
    return out


def main():
    os.makedirs(GOLD_DIR, exist_ok=True)
    inputs = pick_inputs(N)
    if not inputs:
        print('GOLD_GEN_FAIL no_inputs')
        return 1
    manifest = []
    for i, im in enumerate(inputs):
        in_path = os.path.join(GOLD_DIR, 'in_%d.png' % i)
        gold_path = os.path.join(GOLD_DIR, 'gold_%d.png' % i)
        meta_path = os.path.join(GOLD_DIR, 'steps_%d.json' % i)
        im.save(in_path)
        out, steps, reverted = auto_subset(im)
        out.save(gold_path)
        with open(meta_path, 'w', encoding='utf-8') as f:
            json.dump({'keys': steps, 'reverted': reverted}, f, ensure_ascii=False)
        manifest.append({'in': in_path, 'gold': gold_path, 'meta': meta_path})
        print('GOLD %d steps=%s reverted=%s' % (i, ','.join(steps), reverted))
        # P5-2 显式算子独立金样
        for name, oimg in op_gold(im).items():
            if name.startswith('_'):
                continue
            oimg.save(os.path.join(GOLD_DIR, 'op_%s_%d.png' % (name, i)))
        print('  OPS film/bw/glow/redeye/vignette/hdr/structure/dodge/dehaze/chromatic/portrait/lens ok')
        # P5-4 retouch_with_plan 金样（真实管线；复用已落盘的 in_path 避免 EXIF 方向差）
        outp, _ = retouch.retouch_with_plan(in_path, PLAN_PARAMS)
        Image.open(outp).convert('RGB').save(os.path.join(GOLD_DIR, 'op_plan_%d.png' % i))
        try:
            os.remove(outp)
        except OSError:
            pass
        print('  PLAN ok params=%d' % len(PLAN_PARAMS))
    # P5-4 合成倾斜地平线金样（Hough 检测 + 拉直）
    for i, tim in enumerate(synth_level(4)):
        in_lp = os.path.join(GOLD_DIR, 'in_level_%d.png' % i)
        tim.save(in_lp)
        det = retouch._detect_horizon(tim)
        ang = float(det[0]) if det else 0.0
        if det:
            st_img, _ = retouch._straighten(tim, det[0])
        else:
            st_img = tim
        st_img.save(os.path.join(GOLD_DIR, 'op_level_%d.png' % i))
        with open(os.path.join(GOLD_DIR, 'level_meta_%d.json' % i), 'w', encoding='utf-8') as f:
            json.dump({'angle': ang, 'votes': int(det[1]) if det else 0}, f)
        print('LEVEL %d angle=%.2f votes=%d' % (i, ang, det[1] if det else 0))
    with open(os.path.join(GOLD_DIR, 'manifest.json'), 'w', encoding='utf-8') as f:
        json.dump(manifest, f)
    print('GOLD_GEN_OK n=%d' % len(inputs))
    return 0


if __name__ == '__main__':
    sys.exit(main())
