# -*- coding: utf-8 -*-
"""
缩略图生成与磁盘缓存。

设计要点
--------
1. 缓存键 = sha1(绝对路径 | mtime | size)。mtime 参与意味着原图被覆盖后
   缓存自动失效，不会一直显示旧缩略图。
2. 统一输出 JPEG（quality 82）：比 PNG 小一个数量级，网页端解码也更快。
3. **必须处理 EXIF Orientation**：手机竖拍的照片在 EXIF 里是"横图 + 旋转标记"，
   不转的话网页上会看到躺倒的图（这是最容易漏的一步）。
4. 透明 PNG/WebP 转 JPEG 前要合成到白底，否则透明区变黑。
"""
from __future__ import annotations

import hashlib
import os

from .home import data_home

try:
    from PIL import Image, ImageOps
except Exception:                                    # pragma: no cover
    Image = ImageOps = None

MAX_PX = 512          # 缩略图长边
JPEG_Q = 82
# 缓存上限（高水位）。正常库的缩略图数远小于此，_evict 几乎不触发。
# 早先设为 4000，导致库规模超过后「按 atime 删最旧一半」会持续删掉大量
# 缩略图（库里 5931 张却只留 ~2900 个文件）→ 画廊成片裂图。缩略图按
# sha1(路径|mtime|size) 内容寻址，源文件变化即换新键，旧条目天生失效，
# 根本不需要激进 LRU，这里只保留一个极高的安全网。
MAX_CACHE = 200000


def cache_dir():
    """缓存目录放在用户家目录下，避免污染安装目录（打包后 exe 目录可能只读）。"""
    base = os.path.join(data_home(), 'thumbs')
    try:
        os.makedirs(base, exist_ok=True)
    except Exception:
        base = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '.thumbs')
        base = os.path.abspath(base)
        os.makedirs(base, exist_ok=True)
    return base


def _key(path):
    try:
        st = os.stat(path)
        raw = '%s|%s|%s' % (os.path.abspath(path), int(st.st_mtime), st.st_size)
    except Exception:
        raw = os.path.abspath(path)
    return hashlib.sha1(raw.encode('utf-8', 'ignore')).hexdigest()


_LAST_EVICT = [0.0]      # 模块级节流：批量导入时绝不能每张图都跑一次 listdir+sort


def _maybe_evict(d, min_interval=600.0):
    """节流版 LRU 清理：最快每 10 分钟跑一次。

    早先 thumb_path 每张图都调 _evict（listdir 4000 个文件 + getatime + sort），
    单是清理的文件系统开销就能盖过缩略图本身（实测踩过）。
    """
    import time
    now = time.time()
    if now - _LAST_EVICT[0] < min_interval:
        return
    _LAST_EVICT[0] = now
    _evict(d)


def _evict(d, limit=MAX_CACHE):
    """简易 LRU：文件数超限时按 atime 删掉最旧的一半。"""
    try:
        files = [os.path.join(d, f) for f in os.listdir(d) if f.endswith('.jpg')]
        if len(files) <= limit:
            return
        files.sort(key=lambda p: os.path.getatime(p))
        for p in files[: len(files) // 2]:
            try:
                os.remove(p)
            except Exception:
                pass
    except Exception:
        pass


def thumb_path(src, size=MAX_PX):
    """返回缩略图绝对路径；不存在则生成。失败返回 ''。

    兼容入口（单张/外部调用用）；扫描管线请走 cached_thumb_path +
    thumb_from_open，避免同一文件被打开多次。
    """
    if Image is None or not src or not os.path.isfile(src):
        return ''
    hit = cached_thumb_path(src, size)
    if hit:
        return hit
    try:
        with Image.open(src) as im:
            return _make(im, src, size)
    except Exception:
        return ''


def cached_thumb_path(src, size=MAX_PX):
    """只查缓存、不生成。命中返回路径（并刷新 atime），未命中返回 ''。

    扫描管线的「缓存优先」入口：命中时连文件都不用打开，
    重扫上千张已入库照片就是纯 stat + sha1，毫秒级。
    """
    if Image is None or not src or not os.path.isfile(src):
        return ''
    d = cache_dir()
    ext = os.path.splitext(src)[1].lower()
    out = os.path.join(d, _key(src) + ('.png' if ext == '.png' else '.jpg'))
    if os.path.isfile(out):
        try:
            os.utime(out, None)
            return out
        except Exception:
            pass
    return ''


def thumb_from_open(im, src, size=MAX_PX):
    """从一个**已打开、未 load** 的 PIL 图生成缩略图。

    供扫描管线复用同一次磁盘读取（EXIF 与缩略图共用一次打开）。
    """
    if Image is None or im is None:
        return ''
    d = cache_dir()
    ext = os.path.splitext(src)[1].lower()
    out = os.path.join(d, _key(src) + ('.png' if ext == '.png' else '.jpg'))
    if os.path.isfile(out):
        try:
            os.utime(out, None)
            return out
        except Exception:
            pass
    return _make(im, src, size, out)


def _make(im, src, size=MAX_PX, out=None):
    """实际生成逻辑。im 可能已被 draft() 缩到 1/8 —— 这是提速的核心。"""
    d = cache_dir()
    _maybe_evict(d)
    if out is None:
        ext = os.path.splitext(src)[1].lower()
        out = os.path.join(d, _key(src) + ('.png' if ext == '.png' else '.jpg'))
    try:
        # JPEG 专属加速：draft 让解码器只解 1/2、1/4 或 1/8 分辨率，
        # 20MP 照片解成 ~2600px 再缩到 512，省掉约 94% 的像素解码。
        # 早先版本 im.load() 全量解码，为 512px 缩略图花掉整张原图的时间（实测踩过）。
        try:
            im.draft('RGB', (size, size))
        except Exception:
            pass
        im.load()
        # 先按 EXIF 摆正，再缩放 —— 顺序反了会得到错误的长宽比
        try:
            im = ImageOps.exif_transpose(im)
        except Exception:
            pass
        w, h = im.size
        if max(w, h) > size:
            im = im.resize((int(w * size / max(w, h)) or 1,
                            int(h * size / max(w, h)) or 1), Image.LANCZOS)
        if im.mode in ('RGBA', 'LA', 'P'):
            bg = Image.new('RGB', im.size, (255, 255, 255))
            im = im.convert('RGBA')
            bg.paste(im, mask=im.split()[-1])
            im = bg
        elif im.mode != 'RGB':
            im = im.convert('RGB')
        # optimize=True 对 512px 小图收益趋零、却明显拖慢编码，不开
        im.save(out, 'JPEG', quality=JPEG_Q)
        return out
    except Exception:
        return ''


def to_file_url(p):
    """本地路径 -> file:/// URL（网页版 img 字段用的就是这种格式）。"""
    if not p:
        return ''
    p = os.path.abspath(p).replace('\\', '/')
    # 路径里的空格/中文/井号必须转义，否则 URL 会被截断
    try:
        from urllib.request import pathname2url
        return 'file://' + pathname2url(p)
    except Exception:
        return 'file:///' + p.replace(' ', '%20')
