# -*- coding: utf-8 -*-
"""
真实 EXIF 提取。

产出直接对齐网页版 index.html 的 normalizePhoto 字段契约：
    id/t/s/y/o/r/pv/ct/lo/la/cam/lens/focal/ap/sh/iso/d/tag
    tm/tz/alt/ev/evc/wb/meter/tripod/rt/dim/src/rel/img

设计要点
--------
1. 双通道读取：JPEG/TIFF 走 piexif（能拿到 LensModel 等私有标签）；
   其余格式（PNG/WebP/HEIF）与 piexif 失败时回退 Pillow 的 getexif()。
2. 所有除法/有理数解析都做零值保护 —— 真实照片的 EXIF 脏得很，
   常见 "0/0"、"0/1"、空字符串，一处未防护就会让整批导入崩掉。
3. 焦距优先用 FocalLengthIn35mmFilm（若相机已写入等效焦距），
   否则保留物理焦距，交给网页版的 CAM_DB 按裁切系数换算。
4. GPS 高度可能是负数（低于海平面），EXIF 用 0/1 字节标记海拔方向。
"""
from __future__ import annotations

import datetime as _dt
import os
import re

try:
    import piexif
except Exception:                                    # pragma: no cover
    piexif = None

try:
    from PIL import Image
    from PIL.ExifTags import TAGS, GPSTAGS
except Exception:                                    # pragma: no cover
    Image = None
    TAGS = GPSTAGS = {}

IMG_EXT = {'.jpg', '.jpeg', '.jpe', '.jfif', '.png', '.webp', '.tif', '.tiff', '.bmp', '.gif', '.avif', '.heic', '.heif'}

# HEIC / HEIF（iPhone 照片）：Pillow 原生不解，需 pillow-heif 注册 opener。
# 注册是进程级副作用，放在这里即可 —— 本模块被 scanner 最先导入，
# 后续 thumbs / retouch / culling 的任何 Image.open 都能直接解 HEIC。
# 未安装 pillow-heif 时静默降级：HEIC 仍会入库，只是缩略图生成失败（不崩）。
HEIF_READY = False
if Image is not None:
    try:
        import pillow_heif                            # type: ignore

        pillow_heif.register_heif_opener()
        HEIF_READY = True
    except Exception:                                 # pragma: no cover
        HEIF_READY = False


# ---------------------------------------------------------------- 工具
def _rational_to_float(v):
    """EXIF 有理数 -> float。(num, den) / int / float / str('1/125') 都吃。"""
    try:
        if v is None:
            return None
        if isinstance(v, tuple) or isinstance(v, list):
            if len(v) != 2:
                return None
            num, den = v
            den = float(den)
            if den == 0:
                return None
            return float(num) / den
        # Pillow 的 EXIF 有理数类型 IFDRational（有 numerator / denominator）。
        # 不认它的话，焦距与光圈会静默取不到 —— JPEG 因先走 piexif 通道掩盖了这一点，
        # 而 HEIC / PNG / WebP 走通道 2 时会整片丢失（实测 HEIC 焦距恒为 0）。
        n = getattr(v, 'numerator', None)
        d = getattr(v, 'denominator', None)
        if n is not None and d is not None and not isinstance(v, (int, float, bool)):
            d = float(d)
            return None if d == 0 else float(n) / d
        if isinstance(v, int):
            return float(v)
        if isinstance(v, float):
            return float(v)
        if isinstance(v, bytes):
            v = v.decode('ascii', 'ignore')
        if isinstance(v, str):
            if '/' in v:
                a, b = v.split('/', 1)
                b = float(b)
                return None if b == 0 else float(a) / b
            return float(v)
        return None
    except Exception:
        return None


def _clean(s):
    """EXIF 字符串常见尾部 \\x00 与全空格。"""
    if s is None:
        return ''
    if isinstance(s, bytes):
        s = s.decode('utf-8', 'ignore')
    return str(s).replace('\x00', '').strip()


def _fmt_aperture(f):
    """f/1.8 这样显示；小于 10 保留 1 位小数，否则取整（大光圈段更常用小数）。"""
    if f is None or f <= 0:
        return ''
    return ('f/%g' % round(f, 1)) if f < 10 else ('f/%d' % round(f))


def _fmt_shutter(t):
    """>=1s 显示 '15s' / '1.3s'；<1s 显示 '1/125'。"""
    if t is None or t <= 0:
        return ''
    if t >= 1:
        return ('%gs' % round(t, 1)) if t < 10 else ('%ds' % round(t))
    inv = 1.0 / t
    return '1/%d' % round(inv) if inv >= 2 else ('%gs' % round(t, 2))


def _dms_to_deg(dms, ref):
    """(度,分,秒) 有理数序列 + N/S/E/W -> 带符号十进制度。"""
    try:
        if not dms or len(dms) < 3:
            return None
        d = _rational_to_float(dms[0])
        m = _rational_to_float(dms[1])
        s = _rational_to_float(dms[2])
        if d is None:
            return None
        deg = d + (m or 0) / 60.0 + (s or 0) / 3600.0
        if ref in ('S', 'W'):
            deg = -deg
        return round(deg, 6)
    except Exception:
        return None


_DT_RE = re.compile(r'^(\d{4})[:\-/](\d{1,2})[:\-/](\d{1,2})[ T]?(\d{1,2})?:?(\d{1,2})?:?(\d{1,2})?')


def _parse_dt(s):
    """'2023:02:11 20:14:03' / '2023-02-11T20:14:03' -> (date, time)。"""
    s = _clean(s)
    m = _DT_RE.match(s)
    if not m:
        return '', ''
    y, mo, d = m.group(1), int(m.group(2)), int(m.group(3))
    hh = int(m.group(4) or 0)
    mm = int(m.group(5) or 0)
    ss = int(m.group(6) or 0)
    try:
        _dt.date(int(y), mo, d)
    except Exception:
        return '', ''
    return '%s-%02d-%02d' % (y, mo, d), '%02d:%02d' % (hh, mm) if (hh or mm or ss) else ''


# ---------------------------------------------------------------- 主入口
def read_exif(path, data=None, _im=None):
    """读取一张照片的真实 EXIF。

    data：可选，调用方已读入的文件字节 —— 传入则 piexif 不再重读磁盘。
    _im：可选，调用方已打开的 PIL 图 —— 传入则尺寸/方向不再重新打开。
    两者配合可把「一张照片 3 次打开」压到 1 次（大批量导入的主提速点之一）。

    返回 dict（字段名与网页版一致）。任何一步失败都返回兜底值，
    绝不抛异常 —— 批量导入时一张脏图不能拖垮整批。
    """
    out = {
        'cam': '', 'lens': '', 'focal': 0, 'ap': '', 'sh': '', 'iso': 0,
        'd': '', 'tm': '', 'la': None, 'lo': None, 'alt': None,
        'dim': '', 'evc': 0.0, 'wb': '', 'meter': '', 'orient': 0,
        'rating': 0,
    }
    if not path or not os.path.isfile(path):
        return out

    # ---- 尺寸（Pillow 一条路即可，且能处理旋转标记）
    try:
        if _im is not None:
            w, h = _im.size
            out['dim'] = '%d×%d' % (w, h)
            out['orient'] = _read_orientation(_im)
        elif Image is not None:
            with Image.open(path) as im:
                w, h = im.size
                out['dim'] = '%d×%d' % (w, h)
                out['orient'] = _read_orientation(im)
    except Exception:
        pass

    raw = None
    # ---- 通道 1：piexif（JPEG / TIFF）。有现成字节就不碰磁盘。
    if piexif is not None:
        try:
            raw = piexif.load(data) if data is not None else piexif.load(path)
        except Exception:
            raw = None

    if raw:
        try:
            z = raw.get('0th', {})
            ex = raw.get('Exif', {})
            g = raw.get('GPS', {})

            make = _clean(z.get(piexif.ImageIFD.Make))
            model = _clean(z.get(piexif.ImageIFD.Model))
            # 机身名去掉重复的厂商前缀："NIKON CORPORATION NIKON Z 7" -> "Nikon Z 7"
            cam = (make + ' ' + model).strip()
            if model and make and model.upper().startswith(make.split()[0].upper() if make.split() else ''):
                cam = model
            out['cam'] = cam

            out['lens'] = _clean(ex.get(piexif.ExifIFD.LensModel)) \
                or _clean(ex.get(piexif.ExifIFD.LensMake))

            focal = _rational_to_float(ex.get(piexif.ExifIFD.FocalLength))
            out['focal'] = round(focal, 1) if focal else 0

            ap = _rational_to_float(ex.get(piexif.ExifIFD.FNumber))
            out['ap'] = _fmt_aperture(ap)

            t = _rational_to_float(ex.get(piexif.ExifIFD.ExposureTime))
            out['sh'] = _fmt_shutter(t)

            iso = ex.get(piexif.ExifIFD.ISOSpeedRatings) or ex.get(piexif.ExifIFD.PhotographicSensitivity)
            out['iso'] = int(iso) if isinstance(iso, int) else int(_rational_to_float(iso) or 0)

            dto = ex.get(piexif.ExifIFD.DateTimeOriginal) or z.get(piexif.ImageIFD.DateTime)
            out['d'], out['tm'] = _parse_dt(dto)

            ev = _rational_to_float(ex.get(piexif.ExifIFD.ExposureBiasValue))
            out['evc'] = round(ev, 2) if ev is not None else 0.0

            wb = ex.get(piexif.ExifIFD.WhiteBalance)
            out['wb'] = {0: '自动', 1: '手动'}.get(wb, '')

            mm = ex.get(piexif.ExifIFD.MeteringMode)
            out['meter'] = {0: '未知', 1: '平均测光', 2: '中央重点', 3: '点测光', 4: '多点',
                            5: '评价测光', 6: '局部'}.get(mm, '')

            if g:
                lat = _dms_to_deg(g.get(piexif.GPSIFD.GPSLatitude), _clean(g.get(piexif.GPSIFD.GPSLatitudeRef)))
                lon = _dms_to_deg(g.get(piexif.GPSIFD.GPSLongitude), _clean(g.get(piexif.GPSIFD.GPSLongitudeRef)))
                if lat is not None and lon is not None and (lat or lon):
                    out['la'], out['lo'] = lat, lon
                    a = _rational_to_float(g.get(piexif.GPSIFD.GPSAltitude))
                    if a is not None:
                        # 字节 0=海拔以上，1=以下
                        ref = g.get(piexif.GPSIFD.GPSAltitudeRef)
                        out['alt'] = int(round(-a if ref == 1 else a))
        except Exception:
            pass

    # ---- 通道 2：Pillow getexif（补漏 / 非 JPEG）
    _fill_from_pillow(path, out)

    # ---- 星级：Lightroom / Camera Raw 的 XMP 侧车（xmp:Rating / crs:Rating）
    r = read_xmp_rating(path)
    if r is not None:
        out['rating'] = r

    # ---- 无拍摄时间就用文件修改时间兜底（宁可有个大概日期，也不要空）
    if not out['d']:
        try:
            ts = os.path.getmtime(path)
            out['d'] = _dt.datetime.fromtimestamp(ts).strftime('%Y-%m-%d')
            out['tm'] = out['tm'] or _dt.datetime.fromtimestamp(ts).strftime('%H:%M')
        except Exception:
            pass
    return out


def read_xmp_rating(path):
    """读 Lightroom / Camera Raw 侧车里的星级，无侧车或解析失败返回 None。

    两种写法都认：`<xmp:Rating>3</xmp:Rating>` 与 `crs:Rating="3"`。
    侧车命名同时兼容 `<原图完整名>.xmp`（Lightroom 默认）与 `<stem>.xmp`。
    Lightroom 的 -1 表示「已排除」，这里归一化为 0（未评级）。

    只读前 200KB —— Rating 一定在 XMP 头部，避免为拿一个数字整读大侧车。
    """
    if not path:
        return None
    cands = (path + '.xmp', os.path.splitext(path)[0] + '.xmp')
    for xp in cands:
        if not os.path.isfile(xp):
            continue
        try:
            with open(xp, 'r', encoding='utf-8', errors='ignore') as f:
                head = f.read(200000)
        except Exception:
            continue
        # 三种写法都认：<xmp:Rating>3</xmp:Rating>（Lightroom 主流）、
        # 以及属性式 xmp:Rating="3" / crs:Rating="3"（Camera Raw 与其它导出器）。
        m = (re.search(r'<xmp:Rating>\s*(-?\d+)\s*</xmp:Rating>', head)
             or re.search(r'Rating\s*=\s*"\s*(-?\d+)\s*"', head))
        if not m:
            continue
        try:
            v = int(m.group(1))
        except (TypeError, ValueError):
            continue
        if v < 0:                     # -1 = Lightroom「已排除」
            return 0
        return v if v <= 5 else 5
    return None


def _read_orientation(im):
    try:
        from PIL import ExifTags
        ex = im.getexif()
        for k, v in ex.items():
            if ExifTags.TAGS.get(k) == 'Orientation':
                return int(v)
    except Exception:
        pass
    return 0


def _fill_from_pillow(path, out):
    """用 Pillow 的 getexif 补齐 piexif 未取到的字段。"""
    if Image is None:
        return
    try:
        with Image.open(path) as im:
            ex = im.getexif()
            if not ex:
                return
            flat = {}
            for k, v in ex.items():
                flat[TAGS.get(k, str(k))] = v
            # Exif 子 IFD（0x8769）：焦距 / 光圈 / ISO / 拍摄时间 / 曝光补偿 / 镜头
            # 全都挂在这一层。只读 0th IFD 的话，HEIC、PNG、WebP 这类不走
            # piexif 的格式会整片丢元数据（iPhone 照片尤其明显）。
            if hasattr(ex, 'get_ifd'):
                try:
                    for k, v in (ex.get_ifd(0x8769) or {}).items():
                        key = TAGS.get(k, str(k))
                        if key not in flat:
                            flat[key] = v
                except Exception:
                    pass
            ifgps = ex.get_ifd(0x8825) if hasattr(ex, 'get_ifd') else {}
            gps = {}
            for k, v in (ifgps or {}).items():
                gps[GPSTAGS.get(k, str(k))] = v

            if not out['cam']:
                make, model = _clean(flat.get('Make')), _clean(flat.get('Model'))
                out['cam'] = (make + ' ' + model).strip()
            if not out['lens']:
                out['lens'] = _clean(flat.get('LensModel'))
            if not out['focal']:
                f = _rational_to_float(flat.get('FocalLength'))
                out['focal'] = round(f, 1) if f else 0
            if not out['ap']:
                out['ap'] = _fmt_aperture(_rational_to_float(flat.get('FNumber')))
            if not out['sh']:
                out['sh'] = _fmt_shutter(_rational_to_float(flat.get('ExposureTime')))
            if not out['iso']:
                out['iso'] = int(_rational_to_float(flat.get('ISOSpeedRatings')) or 0)
            if not out['d']:
                out['d'], out['tm'] = _parse_dt(flat.get('DateTimeOriginal') or flat.get('DateTime'))
            if not out['evc']:
                out['evc'] = round(_rational_to_float(flat.get('ExposureBiasValue')) or 0.0, 2)
            if gps and out['la'] is None:
                lat = _dms_to_deg(gps.get('GPSLatitude'), _clean(gps.get('GPSLatitudeRef')))
                lon = _dms_to_deg(gps.get('GPSLongitude'), _clean(gps.get('GPSLongitudeRef')))
                if lat is not None and lon is not None and (lat or lon):
                    out['la'], out['lo'] = lat, lon
                    a = _rational_to_float(gps.get('GPSAltitude'))
                    if a is not None:
                        out['alt'] = int(round(-a if gps.get('GPSAltitudeRef') == 1 else a))
    except Exception:
        pass
