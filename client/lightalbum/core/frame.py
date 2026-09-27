# -*- coding: utf-8 -*-
"""光影集 · 相框 / EXIF 水印引擎（纯 Pillow，零新依赖）。

设计取向（对标 AfterFrame「相框 / EXIF 水印」但做减法）
─────────────────────────────────────────────────
AfterFrame 内置了各相机厂牌的真实 logo（哈苏 H、徕卡红点等），但那涉及商标授权
风险。本实现遵循既定决策：**用「文字字标 + 原创几何标记」呈现相机信息，绝不复制
任何品牌 logo**。具体：

*   「厂牌」用**相机品牌的英文名做字标**（如 SONY / CANON / 华为），按图像尺寸比例
    排版、字母间距拉开 —— 这是设计上的「字标」，不是商标图形；
*   厂牌取不到时，回退到**我们自己的原创字标「光影集 / LIGHTALBUM」**；
*   提供一个**原创几何标记**（细环 / 细线），不与任何品牌绑定；
*   所有 EXIF 参数（焦距 / 光圈 / 快门 / ISO / 日期）按统一风格排版。

家族（preset）
─────────────
*   bar       厂牌信息条（底条，左字标+机型/镜头，右 EXIF 参数）
*   margin    美术馆留白（四周厚边 + 底部居中字标/标题）
*   polaroid 拍立得（经典白边，底条更厚，可加标题）
*   editorial 杂志编辑风（衬线标题 + EXIF，细边）
*   filmstrip 胶片风（上下黑条 + 齿孔 + 帧号）
*   corner    极简角标（仅一角小字 EXIF，几乎无边框）

全部尺寸按图像尺寸的比例计算，任意分辨率都协调；主题黑/白一键切换。
导出写到原图同目录 `<名>_framed.jpg`，**绝不覆盖原片**，并完整保留 EXIF/GPS。
"""
from __future__ import annotations

import base64
import io
import math
import os
import re

from PIL import Image, ImageDraw, ImageFont, ImageOps

# ---- 引擎命名
try:
    from ..version import ENGINE_NAME, ENGINE_VERSION
except Exception:                                   # noqa: BLE001
    ENGINE_NAME, ENGINE_VERSION = 'Frame', '1.0.0'

# ------------------------------------------------------------ 字体
# 全部用系统自带字体（零新依赖）。Windows 优先，macOS / Linux 回退。
_FONT_DIRS = [
    r'C:\Windows\Fonts',
    '/System/Library/Fonts',
    '/usr/share/fonts',
    '/Library/Fonts',
]
_SANS = ['arial.ttf', 'segoeui.ttf', 'LiberationSans-Regular.ttf', 'DejaVuSans.ttf']
_SANS_B = ['arialbd.ttf', 'segoeb.ttf', 'LiberationSans-Bold.ttf', 'DejaVuSans-Bold.ttf']
_SERIF = ['cambria.ttc', 'times.ttf', 'Georgia.ttf', 'LiberationSerif-Regular.ttf',
          'DejaVuSerif.ttf']
# 中文字体（相机品牌中文名 / 用户自定义标题需要 CJK 字形）
_CJK = ['msyh.ttc', 'msyh.ttf', 'simhei.ttf', 'simsun.ttc', 'SourceHanSansSC-Regular.otf',
        'NotoSansCJK-Regular.ttc', 'wqy-zenhei.ttc']


def _find_font(names):
    for d in _FONT_DIRS:
        if not os.path.isdir(d):
            continue
        for n in names:
            p = os.path.join(d, n)
            if os.path.isfile(p):
                return p
    return None


_FONT_CACHE = {}


def _font(kind, size, cjk=False):
    """kind ∈ sans/sans_b/serif；cjk 优先走中文字体（中英文都能画，只是拉丁没那么洋气）。"""
    key = (kind, cjk)
    names = (_CJK if cjk else None)
    if names is None:
        names = {'sans': _SANS, 'sans_b': _SANS_B, 'serif': _SERIF}[kind]
    cache_key = (tuple(names), size)
    if cache_key in _FONT_CACHE:
        return _FONT_CACHE[cache_key]
    path = _find_font(names)
    out = None
    if path:
        idx = 0
        # .ttc / .otc 需要字体索引（一般 0 即可）
        if path.lower().endswith(('.ttc', '.otc')):
            try:
                out = ImageFont.truetype(path, size, index=0)
            except Exception:
                out = None
        if out is None:
            try:
                out = ImageFont.truetype(path, size)
            except Exception:
                out = None
    if out is None:
        out = ImageFont.load_default()
    _FONT_CACHE[cache_key] = out
    return out


_CJK_RE = re.compile(r'[\u3000-\u9fff\uff00-\uffef]')


def _has_cjk(s):
    return bool(_CJK_RE.search(s or ''))


# ------------------------------------------------------------ EXIF 格式化
def _fmt_focal(v):
    try:
        f = float(v)
    except (TypeError, ValueError):
        return ''
    if f <= 0:
        return ''
    return ('%.1fmm' % f) if f < 10 else ('%.0fmm' % f)


def _fmt_ap(v):
    s = str(v or '').strip()
    if not s or s in ('0', '0.0'):
        return ''
    return ('f/' + s) if not s.startswith('f/') else s


def _fmt_shutter(v):
    s = str(v or '').strip()
    if not s:
        return ''
    if '/' in s:
        return (s + 's') if not s.endswith('s') else s
    try:
        f = float(s)
    except (TypeError, ValueError):
        return s
    if f <= 0:
        return ''
    if f >= 1:
        return ('%ds' % round(f)) if abs(f - round(f)) < 1e-6 else ('%.1fs' % f)
    return '1/%ds' % max(1, round(1.0 / f))


def _fmt_iso(v):
    try:
        i = int(float(v))
    except (TypeError, ValueError):
        return ''
    return ('ISO %d' % i) if i > 0 else ''


def _fmt_date(v):
    s = str(v or '').strip()
    if not s:
        return ''
    # 2026-09-20 → 2026.09.20
    return s.replace('-', '.').replace('/', '.')


def _exif_line(ex, opts):
    """按 opts['fields'] 组合 EXIF 参数行（缺字段整段隐藏）。"""
    fields = opts.get('fields') or {}
    parts = []
    if fields.get('focal', True):
        parts.append(_fmt_focal(ex.get('focal')))
    if fields.get('aperture', True):
        parts.append(_fmt_ap(ex.get('ap')))
    if fields.get('shutter', True):
        parts.append(_fmt_shutter(ex.get('sh')))
    if fields.get('iso', True):
        parts.append(_fmt_iso(ex.get('iso')))
    date = _fmt_date(ex.get('d')) if fields.get('date', False) else ''
    parts = [p for p in parts if p]
    line = '   '.join(parts)          # 三个空格分隔，克制留白
    if date:
        line = (line + '   ' + date).strip() if line else date
    return line


def _make_wordmark(ex):
    """相机品牌做字标；取不到回退到我们自己的原创字标（规避商标）。"""
    cam = (ex.get('cam') or '').strip()
    # 取首词做大写字标（Apple → APPLE；Canon EOS R5 → CANON；华为 → 华为）
    first = re.split(r'[\s/]+', cam)[0] if cam else ''
    if first:
        return first.upper() if _has_cjk(first) is False else first
    return '光影集'                     # 原创回退字标


# ------------------------------------------------------------ 文本绘制工具
def _text_w(draw, text, font):
    if hasattr(draw, 'textlength'):
        return draw.textlength(text, font=font)
    b = draw.textbbox((0, 0), text, font=font)
    return b[2] - b[0]


def _draw_spaced(draw, xy, text, font, fill, spacing=0, anchor=None):
    """带字母间距的绘制（字标用）。anchor 仅用于整体居中定位时的基线微调。"""
    x, y = xy
    if spacing <= 0:
        draw.text((x, y), text, font=font, fill=fill)
        return x + _text_w(draw, text, font)
    total = 0
    for ch in text:
        draw.text((x, y), ch, font=font, fill=fill)
        w = _text_w(draw, ch, font)
        x += w + spacing
        total += w + spacing
    return total


def _draw_centered(draw, cx, y, text, font, fill):
    w = _text_w(draw, text, font)
    draw.text((cx - w / 2, y), text, font=font, fill=fill)


# ------------------------------------------------------------ 预设布局参数
# 每个家族给出：bg（默认主题色）、pad（相对照片短边的留白比例）、字体族、是否细边。
PRESETS = {
    'bar-dark':   {'family': 'bar',      'theme': 'dark',  'bg': '#0e0e0e', 'fg': '#f2f2f2',
                   'fg2': '#9a9a9a', 'accent': '#d8b46a'},
    'bar-light':  {'family': 'bar',      'theme': 'light', 'bg': '#f4f4f2', 'fg': '#161616',
                   'fg2': '#7a7a7a', 'accent': '#d8b46a'},
    'margin':     {'family': 'margin',   'theme': 'light', 'bg': '#ffffff', 'fg': '#1a1a1a',
                   'fg2': '#8a8a8a', 'accent': '#d8b46a'},
    'polaroid':   {'family': 'polaroid', 'theme': 'light', 'bg': '#fbfbf8', 'fg': '#222222',
                   'fg2': '#9a9a9a', 'accent': '#d8b46a'},
    'editorial':  {'family': 'editorial','theme': 'light', 'bg': '#f3f1ec', 'fg': '#1c1c1c',
                   'fg2': '#6e6e6e', 'accent': '#b08d57'},
    'filmstrip':  {'family': 'filmstrip','theme': 'dark',  'bg': '#0a0a0a', 'fg': '#f0f0f0',
                   'fg2': '#888888', 'accent': '#caa15a'},
    'corner':     {'family': 'corner',   'theme': 'dark',  'bg': '#000000', 'fg': '#f4f4f4',
                   'fg2': '#cfcfcf', 'accent': '#d8b46a'},
}


# ------------------------------------------------------------ 渲染
def _load_oriented(path):
    with Image.open(path) as im:
        im = ImageOps.exif_transpose(im)        # 应用 EXIF 旋转，避免横竖反
        return im.convert('RGB')


def render_frame(img, exif, preset_id, opts=None):
    """核心渲染：返回加好相框/水印的新 Image（不写盘）。

    img   : PIL.Image（已 RGB）
    exif  : 我们的 EXIF 字典（cam/lens/focal/ap/sh/iso/d/tm …）
    preset_id: PRESETS 的键
    opts  : {theme, pad(倍率), fields, caption, showMark, mark}
    """
    opts = opts or {}
    ex = exif or {}
    preset = PRESETS.get(preset_id) or PRESETS['bar-light']
    theme = opts.get('theme') or preset['theme']
    # 颜色：默认用预设自带；主题与预设不符时翻转（bg 取同族另一主题，fg 按主题定）
    bg, fg, fg2, accent = preset['bg'], preset['fg'], preset['fg2'], preset['accent']
    if theme != preset['theme']:
        other = next((v for v in PRESETS.values()
                      if v['family'] == preset['family'] and v['theme'] == theme), None)
        if other:
            bg = other['bg']
        else:
            # 同族没有另一主题的预设（如 margin 只有浅色版）：直接给回退底色，
            # 否则会出现「白底白字」不可读
            bg = '#0e0e0e' if theme == 'dark' else '#f4f4f2'
        fg = '#1a1a1a' if theme == 'light' else '#f2f2f2'
        fg2 = '#7a7a7a' if theme == 'light' else '#9a9a9a'
    # opts 显式覆盖优先
    if opts.get('bg'):
        bg = opts['bg']
    if opts.get('fg'):
        fg = opts['fg']
    pad_scale = max(0.4, min(2.4, float(opts.get('pad', 1.0))))

    w, h = img.size
    short = min(w, h)
    fam = preset['family']

    # ---- 画布尺寸与留白
    if fam == 'bar':
        band = int(short * 0.135 * pad_scale)
        cw, ch = w, h + band
        px = py = 0
    elif fam == 'margin':
        m = int(short * 0.10 * pad_scale)
        cw, ch = w + 2 * m, h + 2 * m
        px, py = m, m
    elif fam == 'polaroid':
        side = int(short * 0.045 * pad_scale)
        bottom = int(short * 0.16 * pad_scale)
        cw, ch = w + 2 * side, h + side + bottom
        px, py = side, side
    elif fam == 'editorial':
        m = int(short * 0.055 * pad_scale)
        # 顶部留白要容得下标题（否则 y 为负被裁掉）
        size_title = max(18, int(min(w, h) * 0.045))
        m_top = max(m, int(size_title * 1.9))
        cw, ch = w + 2 * m, h + m_top + m
        px, py = m, m_top
        top_m = m_top
    elif fam == 'filmstrip':
        band = int(short * 0.085 * pad_scale)
        cw, ch = w, h + 2 * band
        px, py = 0, band
    else:  # corner：极小留白
        cw, ch = w, h
        px, py = 0, 0

    bg_rgb = _hex(bg)
    canvas = Image.new('RGB', (cw, ch), bg_rgb)
    canvas.paste(img, (px, py))
    draw = ImageDraw.Draw(canvas)

    wordmark = _make_wordmark(ex)
    line = _exif_line(ex, opts)
    caption = (opts.get('caption') or '').strip()
    show_mark = bool(opts.get('showMark', False))

    # ---- 各家族绘制
    if fam == 'bar':
        _draw_bar(draw, cw, ch, w, h, px, py, band, ex, wordmark, line, caption,
                  fg, fg2, accent, opts)
    elif fam == 'margin':
        _draw_margin(draw, cw, ch, w, h, px, py, wordmark, line, caption,
                     fg, fg2, accent, opts)
    elif fam == 'polaroid':
        _draw_polaroid(draw, cw, ch, w, h, px, py, side, bottom, caption or wordmark,
                       fg, fg2, opts)
    elif fam == 'editorial':
        _draw_editorial(draw, cw, ch, w, h, px, py, wordmark, line, caption,
                        fg, fg2, accent, opts)
    elif fam == 'filmstrip':
        _draw_filmstrip(draw, cw, ch, w, h, px, py, band, line, fg, fg2, opts)
    else:  # corner
        _draw_corner(draw, cw, ch, w, h, line, fg, fg2, opts)

    if show_mark and fam in ('margin', 'polaroid', 'editorial'):
        # 原创几何标记：只放在边框留白的右下角，绝不盖在照片上
        r = max(4, int(short * 0.012))
        if fam == 'polaroid':
            mx, my = cw - side * 1.5, ch - bottom * 0.5
        else:                                   # margin / editorial
            mx, my = cw - m * 0.55, ch - m * 0.5
        if (opts.get('mark') or 'ring') == 'line':
            half = int(r * 1.6)
            draw.line([(mx - half, my), (mx + half, my)], fill=_hex(accent),
                      width=max(1, int(short * 0.003)))
        else:
            draw.ellipse([mx - r, my - r, mx + r, my + r], outline=_hex(accent),
                         width=max(1, int(short * 0.003)))
    return canvas


# ---- 子绘制
def _draw_bar(draw, cw, ch, w, h, px, py, band, ex, wordmark, line, caption,
              fg, fg2, accent, opts):
    y0 = py + h                      # 底条起点
    inset = int(min(w, h) * 0.05)
    size_main = max(14, int(band * 0.42))
    size_sub = max(11, int(band * 0.30))
    size_ex = max(11, int(band * 0.30))
    # 左：字标（大写、间距拉开）
    fmain = _font('sans_b', size_main, _has_cjk(wordmark))
    cy = y0 + band / 2
    ty = cy - size_main * 0.62
    _draw_spaced(draw, (inset, ty), wordmark, fmain, fg, spacing=max(1, int(size_main * 0.14)))
    # 左：机型/镜头 第二行（若有）
    model = (ex.get('lens') or '').strip()
    if not model:
        model = (ex.get('cam') or '').strip()
    if model:
        fsub = _font('sans', size_sub, _has_cjk(model))
        _draw_spaced(draw, (inset, ty + size_main * 0.92), model, fsub, fg2,
                     spacing=max(1, int(size_sub * 0.06)))
    # 右：EXIF 参数行
    if line:
        fex = _font('sans', size_ex, _has_cjk(line))
        lw = _text_w(draw, line, fex)
        draw.text((cw - inset - lw, cy - size_ex * 0.62), line, font=fex, fill=fg)
    # accent 细线分隔（可选）
    draw.line([(inset, y0 + band * 0.18), (cw - inset, y0 + band * 0.18)],
              fill=accent, width=max(1, int(band * 0.02)))


def _draw_margin(draw, cw, ch, w, h, px, py, wordmark, line, caption,
                 fg, fg2, accent, opts):
    m = px
    size_wm = max(16, int(min(w, h) * 0.040))
    size_ex = max(12, int(min(w, h) * 0.024))
    cx = cw / 2
    fwm = _font('sans_b', size_wm, _has_cjk(wordmark))
    y = ch - m * 0.52 - size_wm
    _draw_centered(draw, cx, y, wordmark, fwm, fg)
    if caption:
        fc = _font('serif', size_ex, _has_cjk(caption))
        _draw_centered(draw, cx, y + size_wm * 1.25, caption, fc, fg2)
    elif line:
        fe = _font('sans', size_ex, _has_cjk(line))
        _draw_centered(draw, cx, y + size_wm * 1.25, line, fe, fg2)


def _draw_polaroid(draw, cw, ch, w, h, px, py, side, bottom, caption,
                   fg, fg2, opts):
    size = max(14, int(bottom * 0.34))
    cx = cw / 2
    if caption:
        fc = _font('serif', size, _has_cjk(caption))
        _draw_centered(draw, cx, ch - bottom * 0.62 - size * 0.6, caption, fc, fg)
    else:
        fwm = _font('sans_b', size, False)
        _draw_centered(draw, cx, ch - bottom * 0.62 - size * 0.6, 'LIGHTALBUM', fwm, fg)


def _draw_editorial(draw, cw, ch, w, h, px, py, wordmark, line, caption,
                    fg, fg2, accent, opts):
    m = px
    title = caption or wordmark
    size_title = max(18, int(min(w, h) * 0.045))
    size_ex = max(12, int(min(w, h) * 0.024))
    cx = cw / 2
    ft = _font('serif', size_title, _has_cjk(title))
    # 标题居中于顶部留白（py 即 m_top，标题高度不会超出）
    _draw_centered(draw, cx, (py - size_title) / 2, title, ft, fg)
    if line:
        fe = _font('sans', size_ex, _has_cjk(line))
        bot_m = ch - py - h            # 底部留白
        _draw_centered(draw, cx, py + h + (bot_m - size_ex) / 2, line, fe, fg2)


def _draw_filmstrip(draw, cw, ch, w, h, px, py, band, line, fg, fg2, opts):
    # 齿孔：上下黑条上规律排列的小矩孔（深色家族，用更亮的点表示孔）
    hole = max(4, int(band * 0.16))
    gap = max(10, int(band * 0.62))
    y_top = py - band + (band - hole) / 2
    y_bot = py + h + (band - hole) / 2
    x = gap
    while x < cw - gap / 2:
        draw.rectangle([x, y_top, x + hole, y_top + hole], fill=_hex('#2a2a2a'))
        draw.rectangle([x, y_bot, x + hole, y_bot + hole], fill=_hex('#2a2a2a'))
        x += gap + hole
    # 帧号（右下）
    size = max(11, int(band * 0.32))
    f = _font('sans', size, False)
    label = 'FRAME  %s' % (line or '')
    lw = _text_w(draw, label, f)
    draw.text((cw - gap / 2 - lw, py + h + (band - size) / 2), label, font=f, fill=fg)


def _draw_corner(draw, cw, ch, w, h, line, fg, fg2, opts):
    if not line:
        return
    size = max(11, int(min(w, h) * 0.026))
    f = _font('sans', size, _has_cjk(line))
    lw = _text_w(draw, line, f)
    pad = int(size * 0.6)
    # 右下角，压在画面内（半透明黑底保证可读）
    bx = w - lw - pad * 2
    by = h - size - pad * 2
    draw.rectangle([bx - pad, by - pad, bx + lw + pad, by + size + pad],
                   fill=(0, 0, 0, 120) if draw.im.mode == 'RGBA' else (0, 0, 0))
    draw.text((bx, by), line, font=f, fill=fg)


def _draw_mark(draw, cw, ch, px, py, w, h, fam, accent, fg, opts):
    """原创几何标记（细环 / 细线），不与任何品牌绑定。"""
    kind = (opts.get('mark') or 'ring')
    cx, cy = px + w / 2, py + h / 2
    if kind == 'line':
        half = int(min(w, h) * 0.06)
        draw.line([(cx - half, cy), (cx + half, cy)], fill=_hex(accent),
                  width=max(1, int(min(w, h) * 0.004)))
    else:  # ring
        r = int(min(w, h) * 0.05)
        draw.ellipse([cx - r, cy - r, cx + r, cy + r], outline=_hex(accent),
                     width=max(1, int(min(w, h) * 0.004)))


def _hex(c):
    c = c.lstrip('#')
    if len(c) == 3:
        c = ''.join(x * 2 for x in c)
    return tuple(int(c[i:i + 2], 16) for i in (0, 2, 4))


# ------------------------------------------------------------ 对外 API
def apply_frame(path, preset_id='bar-light', opts=None, exif=None, quality=92):
    """渲染并导出到 `<名>_framed.jpg`（不覆盖原片）。返回输出路径。"""
    if exif is None:
        from . import exif as exif_mod
        exif = exif_mod.read_exif(path)
    img = _load_oriented(path)
    out = render_frame(img, exif, preset_id, opts)
    base, _ = os.path.splitext(path)
    out_path = base + '_framed.jpg'
    out.save(out_path, 'JPEG', quality=quality)
    # 保留 EXIF/GPS（pillow 默认不写，需手动移植）
    try:
        with Image.open(path) as src:
            raw = src.info.get('exif')
        if raw:
            out2 = Image.open(out_path)
            out2.save(out_path, 'JPEG', quality=quality, exif=raw)
    except Exception:
        pass
    return out_path


def preview_frame(path, preset_id='bar-light', opts=None, max_edge=900):
    """返回 base64 PNG（缩小预览，供 UI 实时预览）。"""
    from . import exif as exif_mod
    ex = exif_mod.read_exif(path) if not opts else (opts.pop('_exif', None) or exif_mod.read_exif(path))
    img = _load_oriented(path)
    if max(img.size) > max_edge:
        img.thumbnail((max_edge, max_edge))
    out = render_frame(img, ex, preset_id, opts)
    buf = io.BytesIO()
    out.save(buf, 'PNG')
    return base64.b64encode(buf.getvalue()).decode('ascii')


def list_presets():
    """前端预设清单（家族 + 主题 + 中文名）。"""
    names = {
        'bar-dark': '厂牌信息条 · 黑', 'bar-light': '厂牌信息条 · 白',
        'margin': '美术馆留白', 'polaroid': '拍立得', 'editorial': '杂志编辑风',
        'filmstrip': '胶片风', 'corner': '极简角标',
    }
    return [{'id': k, 'name': names.get(k, k), 'family': v['family'], 'theme': v['theme']}
            for k, v in PRESETS.items()]


__all__ = ['apply_frame', 'preview_frame', 'render_frame', 'list_presets', 'PRESETS']
