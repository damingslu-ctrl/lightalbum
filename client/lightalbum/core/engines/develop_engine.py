# -*- coding: utf-8 -*-
"""DevelopEngine —— RAW 开发内核（ADR-0013，调研报告 §三「路径 B」）。

职责边界（符合 ADR-0012）：
  - RAW 解码 / 白平衡 / demosaic / 基础后处理 → LibRaw（经 rawpy 包装）；
  - 输出 16-bit TIFF（可选 tifffile）或 8-bit JPEG/PNG 预览；
  - 后续影调 / 风格 / 预设仍由 Lumen（RetouchEngine）负责，两者串联。

防御式设计：
  - rawpy 缺失时 probe() 返回 available=False，health 降级，绝不抛出阻断启动；
  - 非 RAW 输入（JPEG 等）交给 rawpy 时会抛 LibRaw 错误 → Hub 层 FAILED，
    属预期行为（调用方应先经 ExifEngine/扩展名判断）。
"""
from __future__ import annotations

import os
import threading
from typing import Any, Dict

from ..enginehub import Engine

# RAW 扩展名判断（stdlib imghdr 在 3.13 被移除，自写）
_RAW_EXTS = {'.raw', '.cr2', '.cr3', '.nef', '.arw', '.dng', '.raf',
             '.orf', '.rw2', '.pef', '.srw', '.nrw', '.kdc', '.dcr',
             '.mos', '.erf', '.3fr', '.fff', '.iiq', '.x3f'}


def is_raw_path(path: str) -> bool:
    """按扩展名判断是否 RAW（引擎内自用；网页侧不得依赖本判断）。"""
    return os.path.splitext(path or '')[1].lower() in _RAW_EXTS


class DevelopEngine(Engine):
    """RAW 开发内核引擎。task_probe / task_develop。"""

    name = 'develop'
    kind = 'native'
    vram_weight = 0.0        # LibRaw 走 CPU，不占 6GB 显存预算
    max_jobs = 2
    priority = 3

    def __init__(self) -> None:
        super().__init__()
        self._probe: Dict[str, Any] = {'available': False, 'reason': 'not probed'}
        self._lock = threading.Lock()
        self._jobs_done = 0

    # ---- 生命周期 ----
    def init(self) -> None:
        super().init()
        try:
            self._probe = self._do_probe()
        except Exception as e:  # noqa: BLE001
            self._probe = {'available': False, 'reason': str(e)}

    def start(self) -> None:
        super().start()
        if not self._probe.get('available'):
            try:
                self._probe = self._do_probe()
            except Exception as e:  # noqa: BLE001
                self._probe = {'available': False, 'reason': str(e)}

    # ---- 可观测 ----
    def health(self) -> Dict[str, Any]:
        base = super().health()
        base['develop_available'] = bool(self._probe.get('available'))
        if not self._probe.get('available'):
            base['degraded_reason'] = self._probe.get('reason')
        return base

    def metrics(self) -> Dict[str, Any]:
        return {'name': self.name, 'jobs': self._jobs_done}

    # ---- 探测（无副作用）----
    def _do_probe(self) -> Dict[str, Any]:
        import rawpy  # noqa: F401  函数内 import：PyInstaller 可静态分析
        import numpy
        info: Dict[str, Any] = {
            'available': True,
            'rawpy': getattr(rawpy, '__version__', 'unknown'),
            'libraw': '.'.join(str(x) for x in rawpy.libraw_version),
            'numpy': getattr(numpy, '__version__', 'unknown'),
            'tifffile': False,
        }
        try:
            import tifffile  # noqa: F401
            info['tifffile'] = True
        except Exception:  # noqa: BLE001
            pass
        return info

    def task_probe(self) -> Dict[str, Any]:
        """返回 RAW 内核可用性（available / 版本 / 可选依赖）。"""
        return dict(self._probe)

    # ---- 核心任务 ----
    def task_develop(self, src: str, out_dir: str = None,
                     bit_depth: int = 16, quality: int = 95,
                     use_camera_wb: bool = True, no_auto_bright: bool = False,
                     demosaic: str = None, fbdd_noise_reduction: int = 0,
                     fmt: str = None) -> Dict[str, Any]:
        """RAW → 开发后位图。返回 {'ok', 'out', 'depth', 'fmt', ...}。

        bit_depth=16 → TIFF（有 tifffile 为真 16bit，否则降级 8bit 并注明）；
        bit_depth=8  → JPEG（fmt 可改 'PNG'）。out_dir 缺省为 src 同目录。
        """
        import rawpy
        import numpy as np  # noqa: F401  rawpy 依赖
        if not os.path.isfile(src):
            raise FileNotFoundError(src)

        params: Dict[str, Any] = dict(
            use_camera_wb=bool(use_camera_wb),
            no_auto_bright=bool(no_auto_bright),
            fbdd_noise_reduction=int(fbdd_noise_reduction),
        )
        if demosaic:
            params['demosaic_algorithm'] = getattr(
                rawpy.DemosaicAlgorithm, str(demosaic).upper())
        if int(bit_depth) == 16:
            params['output_bps'] = 16

        with rawpy.imread(src) as raw:
            rgb = raw.postprocess(**params)   # HxWx3 uint16（16bit）或 uint8
        with self._lock:
            self._jobs_done += 1
        return self._encode(rgb, src, out_dir, int(bit_depth), int(quality), fmt)

    # ---- 纯编码（可独立测试；不触 rawpy）----
    def _encode(self, arr, src: str, out_dir: str, bit_depth: int,
                quality: int, fmt: str = None) -> Dict[str, Any]:
        from PIL import Image
        stem = os.path.splitext(os.path.basename(src))[0] + '-develop'
        out_dir = out_dir or os.path.dirname(src) or '.'
        os.makedirs(out_dir, exist_ok=True)

        # 按 dtype 判定：uint16 全暗图（max<=255）也算 16bit，
        # 否则 _to_u8 右移 8 位会把整幅图写成全黑（自查发现的真 bug）
        is16 = arr.dtype.itemsize == 2
        if fmt is None:
            fmt = 'TIFF' if bit_depth == 16 else 'JPEG'
        ext = '.tif' if fmt == 'TIFF' else '.png' if fmt == 'PNG' else '.jpg'
        out = os.path.join(out_dir, stem + ext)

        depth_note = None
        if is16 and fmt == 'TIFF':
            try:
                import tifffile
                tifffile.imwrite(out, arr)           # 真 16-bit
            except Exception:
                Image.fromarray(self._to_u8(arr)).save(out, 'TIFF')
                depth_note = 'downgraded-to-8bit (tifffile unavailable)'
        else:
            img = Image.fromarray(self._to_u8(arr))
            if fmt == 'JPEG':
                img.save(out, 'JPEG', quality=quality)
            else:
                img.save(out, fmt)
            if is16:
                depth_note = 'downgraded-to-8bit (%s)' % fmt
        return {'ok': True, 'out': out, 'depth': 16 if is16 else 8,
                'fmt': fmt, 'note': depth_note,
                'size': os.path.getsize(out)}

    @staticmethod
    def _to_u8(arr):
        if arr.dtype.itemsize == 2:
            return (arr >> 8).astype('uint8')
        return arr
