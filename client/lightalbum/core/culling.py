# -*- coding: utf-8 -*-
"""选片引擎（Culling）—— 从海量照片里本地筛出值得留下的好照片。

能力吸收自 `laoziocean-code/ai-photo-manager`（MIT License，
Copyright (c) 2026 AI摄影管家 Contributors）。按其许可要求保留版权声明。

针对本项目做了四处改造（原样照搬会踩坑）：
  1) **零云端**：原项目 8 维评分由云端视觉大模型（GPT/Gemini/Claude）产出，
     本项目铁律是修图/AI 零云端，故只填「本地可测」维度（清晰度/光影），
     主观维度（构图/主体/情绪/故事/发布价值）留 `None` —— 按数据诚实铁律
     **不编造默认值**，并按剩余权重重新归一化计总分，同时回报 `coverage`。
     主观维度可由本地 AI（musiq / Ollama）通过 `ai_scores` 注入补全。
  2) **零重依赖**：cv2 / imagehash / scipy 在本机客户端环境均不可用，
     故 pHash 用纯 numpy 的 DCT-II 实现（与 imagehash.phash 算法一致），
     模糊检测在无 cv2 时用 numpy 四邻域 Laplacian，功能完全等价。
  3) **RAW 豁免**（沿用原项目的真知灼见）：Laplacian 方差对夜空等暗场景
     系统性失灵（大片纯黑方差天然低），且 RAW 内嵌预览可能偏黑失真，
     故 RAW 只做去重与分辨率校验，**不做模糊/曝光淘汰**，避免误杀源文件。
  4) **失败即放行**：解码失败时模糊分记 999（不判模糊）、尺寸未知时放行分辨率，
     宁可多留也不误杀好照片。

流水线：
    读图 → MD5 精确去重 → pHash 相似聚类 → 模糊 → 曝光 → 分辨率
    → 候选集 / 废片集（附 reject_reason）
"""
from __future__ import annotations

import hashlib
import io as _io
import math
import os
import re
import shutil
import time
from typing import Any, Dict, List, Optional, Sequence, Tuple

import numpy as np
from PIL import Image

try:                                    # cv2 可选：没有就走纯 numpy 等价实现
    import cv2                          # type: ignore
except Exception:                       # pragma: no cover - 环境相关
    cv2 = None


# ---------------------------------------------------------------- 默认参数
DEFAULT_OPTIONS: Dict[str, Any] = {
    'dedup_level': '标准',      # 去重档位（DEDUP_LEVELS）
    'phash_threshold': 8,       # 兼容旧配置：显式阈值（dedup_level 优先）
    'blur_threshold': 80.0,     # Laplacian 方差低于此值判为模糊
    'min_dimension': 800,       # 最短边最小像素
    'min_megapixels': 0.5,      # 最小百万像素
    'phash_hash_size': 8,
}

# 去重档位：键为 UI 展示名，值为 pHash Hamming 距离阈值
# （越低越严格，只有极相似的才判重；越高越宽松，更多连拍/同场景会被去重）
DEDUP_LEVELS: Dict[str, Optional[int]] = {
    '关闭': None, '极严': 3, '严格': 5, '标准': 8, '宽松': 12, '极宽': 18,
}
DEFAULT_DEDUP_LEVEL = '标准'

# 8 维评分权重（源自 ai-photo-manager）
WEIGHTS: Dict[str, float] = {
    'composition': 0.15,    # 构图
    'lighting': 0.15,       # 光影
    'color': 0.13,          # 色彩
    'sharpness': 0.12,      # 清晰度
    'subject': 0.13,        # 主体突出度
    'emotion': 0.12,        # 情绪感染力
    'story': 0.10,          # 故事感
    'publish_value': 0.10,  # 发布价值
}

# 本项目本地可测的维度（其余需 AI 注入）
LOCAL_MEASURABLE = ('sharpness', 'lighting')

_GRADES: List[Tuple[str, float]] = [('S', 90), ('A', 80), ('B', 70), ('C', 60), ('D', 0)]

# 曝光判定阈值（源自原项目）
_EXP_UNDER_PX = 25
_EXP_OVER_PX = 230


# ---------------------------------------------------------------- 读图
def _raw_exts() -> set:
    """复用 develop_engine 的 RAW 扩展名集合，避免多处硬编码漂移。"""
    try:
        from .engines.develop_engine import _RAW_EXTS
        return set(_RAW_EXTS)
    except Exception:
        return {'.raw', '.cr2', '.cr3', '.nef', '.arw', '.dng', '.raf', '.orf',
                '.rw2', '.pef', '.sr2', '.x3f', '.3fr', '.mef', '.mrw', '.nrw'}


def is_raw(path: str) -> bool:
    return os.path.splitext(path or '')[1].lower() in _raw_exts()


def _load_pil(path: str) -> Image.Image:
    """加载为 PIL Image；RAW 走 rawpy（优先内嵌缩略图，避免整幅解码）。"""
    if is_raw(path):
        try:
            import rawpy                                   # type: ignore
            with rawpy.imread(path) as raw:
                try:
                    t = raw.extract_thumb()
                    data = getattr(t, 'data', None) if t is not None else None
                    if isinstance(data, (bytes, bytearray)):
                        im = Image.open(_io.BytesIO(bytes(data)))
                        im.load()
                        return im
                    if isinstance(data, np.ndarray):
                        return Image.fromarray(data)
                except Exception:
                    pass
                return Image.fromarray(
                    raw.postprocess(use_camera_wb=True, half_size=True))
        except Exception:
            pass
    im = Image.open(path)
    im.load()
    return im


def load_gray_small(path: str, longest: int = 512) -> np.ndarray:
    """解码为灰度 uint8，并把最长边缩到 `longest`（使经验阈值跨分辨率可用）。"""
    im = _load_pil(path)
    try:
        g = im.convert('L')
        w, h = g.size
        m = max(w, h)
        if m > longest:
            s = longest / float(m)
            g = g.resize((max(1, int(round(w * s))), max(1, int(round(h * s)))),
                         Image.LANCZOS)
        return np.asarray(g, dtype=np.uint8)
    finally:
        try:
            im.close()
        except Exception:
            pass


# ---------------------------------------------------------------- 模糊
def blur_score(path: str) -> float:
    """Laplacian 方差，越高越清晰。

    读取/处理失败返回 999.0（视为「无法评估」，不判模糊），避免把好照片误杀。
    """
    try:
        arr = load_gray_small(path)
        if cv2 is not None:
            return float(cv2.Laplacian(arr, cv2.CV_64F).var())
        # 无 cv2：四邻域 Laplacian（与 cv2 CV_64F 口径一致）
        a = arr.astype(np.float64)
        lap = (a[1:-1, 2:] + a[1:-1, :-2] + a[2:, 1:-1] + a[:-2, 1:-1]
               - 4.0 * a[1:-1, 1:-1])
        return float(np.var(lap))
    except Exception:
        return 999.0


def is_blurry(path: str, threshold: float = 80.0) -> bool:
    return blur_score(path) < threshold


def _sharpness_score(var: Optional[float]) -> Optional[float]:
    """Laplacian 方差 → 0-100 清晰度分（对数曲线）。

    标定：var=80（原项目模糊阈值）≈ 47 分（及格线附近），var>=600 记满分。
    """
    if var is None:
        return None
    try:
        v = float(var)
    except (TypeError, ValueError):
        return None
    if v <= 0:
        return 0.0
    if v >= 600:
        return 100.0
    s = 100.0 * (math.log10(v / 20.0 + 1.0) / math.log10(600.0 / 20.0 + 1.0))
    return round(max(0.0, min(100.0, s)), 1)


# ---------------------------------------------------------------- 曝光
def exposure_stats(path: str) -> Dict[str, Any]:
    """返回 {mean, under, over, verdict}；失败返回安全默认（verdict='未知'）。

    under/over 为暗(<25)/亮(>230) 像素占比。
    """
    try:
        arr = load_gray_small(path, longest=512)
        mean = float(arr.mean())
        under = float((arr < _EXP_UNDER_PX).mean())
        over = float((arr > _EXP_OVER_PX).mean())
        if under > 0.45 and mean < 70:
            verdict = '严重欠曝'
        elif over > 0.45 and mean > 185:
            verdict = '严重过曝'
        elif under > 0.25:
            verdict = '欠曝'
        elif over > 0.25:
            verdict = '过曝'
        else:
            verdict = '正常'
        return {'mean': round(mean, 1), 'under': round(under, 3),
                'over': round(over, 3), 'verdict': verdict}
    except Exception:
        return {'mean': 0.0, 'under': 0.0, 'over': 0.0, 'verdict': '未知'}


_LIGHTING_BY_VERDICT = {'正常': 85.0, '欠曝': 60.0, '过曝': 60.0,
                        '严重欠曝': 25.0, '严重过曝': 25.0}


def _lighting_score(verdict: Optional[str]) -> Optional[float]:
    """曝光判定 → 0-100 光影分。无法判定（'未知'）返回 None，不编造。"""
    return _LIGHTING_BY_VERDICT.get(verdict or '')


# ---------------------------------------------------------------- 去重 / 感知哈希
class PHash:
    """轻量感知哈希（替代 imagehash.ImageHash，避免额外依赖）。

    支持 `-`（Hamming 距离）、`==`、`str()`；算法与 imagehash.phash 一致。
    """
    __slots__ = ('_bits',)

    def __init__(self, bits):
        self._bits = np.asarray(bits, dtype=bool).flatten()

    def __sub__(self, other: 'PHash') -> int:
        return int(np.count_nonzero(self._bits != other._bits))

    def __eq__(self, other: object) -> bool:
        if not isinstance(other, PHash):
            return NotImplemented
        return self.__sub__(other) == 0

    def __hash__(self) -> int:
        return hash(self._bits.tobytes())

    def __str__(self) -> str:
        return ''.join('1' if b else '0' for b in self._bits)


def _dct2(a: np.ndarray) -> np.ndarray:
    """2D DCT-II（未归一化），等价 scipy.fftpack.dct(type=2, norm=None)。"""
    n = a.shape[0]
    idx = np.arange(n)
    k = idx[:, None]
    c = 2.0 * np.cos(np.pi * k * (2 * idx + 1) / (2 * n))
    return c @ a @ c.T


def compute_md5(path: str, chunk: int = 65536) -> str:
    h = hashlib.md5()
    with open(path, 'rb') as f:
        while True:
            b = f.read(chunk)
            if not b:
                break
            h.update(b)
    return h.hexdigest()


def compute_phash(path: str, hash_size: int = 8) -> Optional[PHash]:
    """感知哈希：灰度 → 缩到 (hash_size*4)² → DCT-II → 低频 hash_size² 与中位数比较。

    失败返回 None（调用方需按「无法评估」处理）。
    """
    try:
        im = _load_pil(path)
        try:
            gray = im.convert('L').resize((hash_size * 4, hash_size * 4),
                                          Image.LANCZOS)
            pixels = np.asarray(gray, dtype=np.float64)
            dct = _dct2(pixels)
            low = dct[:hash_size, :hash_size]
            med = float(np.median(low))
            return PHash(low > med)
        finally:
            try:
                im.close()
            except Exception:
                pass
    except Exception:
        return None


def resolve_dedup_threshold(options: Dict[str, Any]) -> Optional[int]:
    """解析去重阈值；None 表示不做相似去重（MD5 精确去重仍会执行）。"""
    level = options.get('dedup_level')
    if level is None:
        pt = options.get('phash_threshold')
        return int(pt) if pt is not None else DEDUP_LEVELS[DEFAULT_DEDUP_LEVEL]
    return DEDUP_LEVELS.get(level, DEDUP_LEVELS[DEFAULT_DEDUP_LEVEL])


def find_groups(phashes: Sequence[Optional[PHash]], threshold: int) -> List[List[int]]:
    """对 phash 序列做相似聚类，返回下标分组（Hamming <= threshold 归一组）。"""
    valid = [i for i, p in enumerate(phashes) if p is not None]
    used: set = set()
    groups: List[List[int]] = []
    n = len(valid)
    for a in range(n):
        i = valid[a]
        if i in used:
            continue
        grp = [i]
        used.add(i)
        for b in range(a + 1, n):
            j = valid[b]
            if j in used:
                continue
            if (phashes[i] - phashes[j]) <= threshold:       # type: ignore[operator]
                grp.append(j)
                used.add(j)
        groups.append(grp)
    return groups


# ---------------------------------------------------------------- 评分
def compute_total(scores: Dict[str, Any], ai_scores: Optional[Dict[str, Any]] = None) -> float:
    """加权总分（0-100）。

    缺失维度（None / 非数值）会被**排除**并按剩余权重重新归一化 —— 这既避免了
    「把未知当 0 分」拉低好照片，也遵守数据诚实铁律。全部缺失时返回 0.0。
    """
    merged = dict(scores or {})
    if ai_scores:
        for k, v in ai_scores.items():
            if merged.get(k) is None and v is not None:
                merged[k] = v
    num = 0.0
    den = 0.0
    for k, w in WEIGHTS.items():
        v = merged.get(k)
        try:
            v = float(v)
        except (TypeError, ValueError):
            continue
        v = max(0.0, min(100.0, v))
        num += v * w
        den += w
    if den <= 0:
        return 0.0
    return round(num / den, 2)


def score_detail(scores: Dict[str, Any],
                 ai_scores: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
    """返回 {total, grade, used, missing, coverage} —— coverage 为参与计分的权重占比。"""
    merged = dict(scores or {})
    if ai_scores:
        for k, v in ai_scores.items():
            if merged.get(k) is None and v is not None:
                merged[k] = v
    used: List[str] = []
    missing: List[str] = []
    cov = 0.0
    for k in WEIGHTS:
        v = merged.get(k)
        try:
            float(v)
        except (TypeError, ValueError):
            missing.append(k)
            continue
        used.append(k)
        cov += WEIGHTS[k]
    total = compute_total(scores, ai_scores=ai_scores)
    return {'total': total, 'grade': grade(total), 'used': used,
            'missing': missing, 'coverage': round(cov, 3)}


def grade(total: float) -> str:
    for name, lower in _GRADES:
        if total >= lower:
            return name
    return 'D'


# ---------------------------------------------------------------- 预处理编排
def assess(path: str, options: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
    """单张体检：返回 md5 / phash / 尺寸 / 模糊 / 曝光 / 分辨率 / 本地评分。"""
    opts = {**DEFAULT_OPTIONS, **(options or {})}
    rec: Dict[str, Any] = {'path': path, 'status': 'candidate', 'reject_reason': None,
                           'dup_of': None, 'dup_group': None}
    try:
        im = _load_pil(path)
        try:
            rec['width'], rec['height'] = im.size
        finally:
            try:
                im.close()
            except Exception:
                pass
    except Exception as e:
        rec['width'] = rec['height'] = 0
        rec['load_error'] = str(e)
    try:
        rec['size_bytes'] = os.path.getsize(path)
    except Exception:
        rec['size_bytes'] = 0
    try:
        rec['md5'] = compute_md5(path)
    except Exception:
        rec['md5'] = None
    rec['phash'] = compute_phash(path, int(opts['phash_hash_size']))
    rec['phash_str'] = str(rec['phash']) if rec['phash'] is not None else None
    rec['blur'] = blur_score(path)
    rec['is_blurry'] = rec['blur'] < float(opts['blur_threshold'])
    rec['exposure'] = exposure_stats(path)
    rec['is_raw'] = is_raw(path)

    w, h = rec['width'], rec['height']
    if w and h:
        mp = (w * h) / 1_000_000
        rec['resolution_ok'] = (mp >= float(opts['min_megapixels'])
                                and min(w, h) >= int(opts['min_dimension']))
    else:
        # 尺寸未知：不做分辨率淘汰，避免好照片被误杀
        rec['resolution_ok'] = True

    rec['scores'] = {
        'sharpness': _sharpness_score(rec['blur']),
        'lighting': _lighting_score(rec['exposure'].get('verdict')),
    }
    return rec


def _sharpest(recs: Sequence[Dict[str, Any]]) -> Dict[str, Any]:
    """在相似组里挑「最清晰者」作为保留代表。"""
    return max(recs, key=lambda r: (r.get('blur') if not r.get('is_blurry') else -1.0)) or recs[0]


def filter_photos(paths: Sequence[str], options: Optional[Dict[str, Any]] = None,
                  on_progress=None) -> Tuple[List[Dict[str, Any]], List[Dict[str, Any]]]:
    """本地预处理，返回 (candidates, rejected)。

    on_progress(cur, total, msg) 可选回调，便于界面显示实时进度。
    """
    opts = {**DEFAULT_OPTIONS, **(options or {})}
    records: List[Dict[str, Any]] = []
    n = len(paths)
    for idx, p in enumerate(paths):
        if on_progress:
            on_progress(idx, n, '预处理 %d/%d：%s' % (idx + 1, n, os.path.basename(p)))
        try:
            records.append(assess(p, opts))
        except Exception as e:                                # 单张失败不影响整体
            r = {'path': p, 'status': 'rejected', 'reject_reason': '无法读取：%s' % e,
                 'dup_of': None, 'dup_group': None, 'blur': 999.0, 'is_blurry': False,
                 'exposure': {'verdict': '未知'}, 'resolution_ok': True, 'is_raw': False,
                 'width': 0, 'height': 0, 'md5': None, 'phash': None, 'scores': {}}
            records.append(r)

    _dedupe_exact(records)
    _dedupe_similar(records, resolve_dedup_threshold(opts))
    _filter_quality(records)

    for r in records:
        if r['status'] == 'candidate':
            r['score'] = score_detail(r.get('scores') or {})
            r['total_score'] = r['score']['total']

    candidates = [r for r in records if r['status'] == 'candidate']
    rejected = [r for r in records if r['status'] == 'rejected']
    return candidates, rejected


def _dedupe_exact(records: List[Dict[str, Any]]) -> None:
    by_md5: Dict[str, List[Dict[str, Any]]] = {}
    for r in records:
        if r['status'] == 'rejected' or not r.get('md5'):
            continue
        by_md5.setdefault(r['md5'], []).append(r)
    group_no = 0
    for grp in by_md5.values():
        if len(grp) > 1:
            group_no += 1
            keep = _sharpest(grp)
            keep['dup_group'] = group_no
            for r in grp:
                if r is not keep:
                    r['status'] = 'rejected'
                    r['reject_reason'] = '完全重复（MD5 相同）'
                    r['dup_group'] = group_no
                    r['dup_of'] = keep['path']


def _dedupe_similar(records: List[Dict[str, Any]], threshold: Optional[int]) -> None:
    if threshold is None:
        return
    valid = [r for r in records if r['status'] != 'rejected' and r.get('phash') is not None]
    if not valid:
        return
    groups = find_groups([r['phash'] for r in valid], threshold)
    group_no = 1000                       # 与精确去重组号错开
    for grp in groups:
        if len(grp) > 1:
            group_no += 1
            grp_recs = [valid[i] for i in grp]
            keep = _sharpest(grp_recs)
            keep['dup_group'] = group_no
            for r in grp_recs:
                if r is not keep:
                    r['status'] = 'rejected'
                    r['reject_reason'] = '高度相似（感知哈希）'
                    r['dup_group'] = group_no
                    r['dup_of'] = keep['path']


def _filter_quality(records: List[Dict[str, Any]]) -> None:
    for r in records:
        if r['status'] == 'rejected':
            continue
        if r.get('is_raw'):
            # RAW 不做模糊/曝光淘汰（见模块 docstring 第 3 点）
            if not r.get('resolution_ok', True):
                r['status'] = 'rejected'
                r['reject_reason'] = '分辨率过低'
                continue
            r['status'] = 'candidate'
            continue
        if r.get('is_blurry'):
            r['status'] = 'rejected'
            r['reject_reason'] = '疑似模糊/对焦不实'
            continue
        verdict = (r.get('exposure') or {}).get('verdict', '未知')
        if verdict in ('严重欠曝', '严重过曝'):
            r['status'] = 'rejected'
            r['reject_reason'] = verdict
            continue
        if not r.get('resolution_ok', True):
            r['status'] = 'rejected'
            r['reject_reason'] = '分辨率过低'
            continue
        r['status'] = 'candidate'


def collect_dedup_groups(rejected: Sequence[Dict[str, Any]]) -> List[Dict[str, Any]]:
    """汇总被去重照片 → 报告用结构：[{kept, reason, similar:[...]}]。"""
    by_kept: Dict[str, Dict[str, Any]] = {}
    for r in rejected:
        if not r.get('dup_of'):
            continue
        g = by_kept.get(r['dup_of'])
        if g is None:
            g = {'kept': r['dup_of'], 'reason': r.get('reject_reason'), 'similar': []}
            by_kept[r['dup_of']] = g
        g['similar'].append(r['path'])
    groups = list(by_kept.values())
    groups.sort(key=lambda g: len(g['similar']), reverse=True)
    return groups


# ---------------------------------------------------------------- 精选 / 导出
def select_top(candidates: Sequence[Dict[str, Any]], n: int = 10) -> List[Dict[str, Any]]:
    """按总分降序取前 n 张（总分缺失按 0 参与排序）。"""
    ranked = sorted(candidates, key=lambda r: float(r.get('total_score') or 0.0),
                    reverse=True)
    return ranked[:max(0, n)]


_EXT_MAP = {'.jpg': '.jpg', '.jpeg': '.jpg', '.png': '.png', '.webp': '.webp',
            '.bmp': '.jpg', '.tif': '.jpg', '.tiff': '.jpg'}
FOLDER_PICK, FOLDER_NORMAL, FOLDER_WASTE, FOLDER_DUP = '精选', '普通', '废片', '去重'


def _safe_filename(name: str) -> str:
    for c in '<>:"/\\|?*':
        name = name.replace(c, '_')
    return name.strip() or '照片'


def export_organized(top: Sequence[Dict[str, Any]],
                     rest: Sequence[Dict[str, Any]],
                     rejected: Sequence[Dict[str, Any]],
                     output_dir: str,
                     archive_all: bool = True) -> str:
    """归档到 output_dir 下的四个文件夹，返回 output_dir。"""
    groups: List[Tuple[str, List[Dict[str, Any]], bool]] = [
        (FOLDER_PICK, list(top), True),
        (FOLDER_NORMAL, list(rest), False),
        (FOLDER_WASTE, [r for r in rejected if not r.get('dup_of')], False),
        (FOLDER_DUP, [r for r in rejected if r.get('dup_of')], False),
    ]
    if not archive_all:
        groups = [(FOLDER_PICK, list(top), True)]
    for label, recs, _ren in groups:
        dir_ = os.path.join(output_dir, label)
        os.makedirs(dir_, exist_ok=True)
        used: set = set()
        for i, rec in enumerate(recs, start=1):
            ext = os.path.splitext(rec['path'])[1].lower() or '.jpg'
            ext = _EXT_MAP.get(ext, ext)
            name = ('TOP%d%s' % (i, ext)) if label == FOLDER_PICK \
                else _uniq(os.path.basename(rec['path']), used)
            if label == FOLDER_PICK:
                used.add(name)
            try:
                shutil.copy2(rec['path'], os.path.join(dir_, name))
            except Exception:
                pass
    return output_dir


def _uniq(name: str, used: set) -> str:
    if name not in used:
        used.add(name)
        return name
    stem, ext = os.path.splitext(name)
    i = 2
    while True:
        cand = '%s_%d%s' % (stem, i, ext)
        if cand not in used:
            used.add(cand)
            return cand
        i += 1
