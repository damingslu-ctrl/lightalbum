# -*- coding: utf-8 -*-
"""HeadlessRenderEngine —— darktable-cli / art-cli 无头渲染（ADR-0013 追记）。

定位：批量 Develop 内核。darktable / ART 把编辑参数存在 sidecar 文件
（.xmp / .arp）里，二者的 CLI 可以**完全无头**地按参数渲染出图——
这就是「LR Develop + Batch Export」的现成实现，GPL 独立进程零传染。

约定（与 darktable-cli / art-cli 一致）：
    <cli> <input> [<sidecar>] <output>

防御式设计：
  - 未配置二进制 → health.available=False；render 抛 RuntimeError（Hub 层 FAILED）；
  - 二进制路径经 task_set_binary 或环境变量 LIGHTALBUM_DARKTABLE_CLI /
    LIGHTALBUM_ART_CLI 配置；probe 会再扫一遍 PATH（shutil.which）。
"""
from __future__ import annotations

import os
import shutil
import subprocess
import threading
import time
from typing import Any, Dict, Optional

from ..enginehub import Engine

_CLIS = ('darktable', 'art')            # 支持的 CLI 种类
_ENV_KEYS = {'darktable': 'LIGHTALBUM_DARKTABLE_CLI',
             'art': 'LIGHTALBUM_ART_CLI'}
_WHICH_KEYS = {'darktable': 'darktable-cli', 'art': 'art-cli'}
# Windows 常见安装位置（darktable 官方安装器默认装 Program Files；
# 本机实证 D:\darktable\bin\darktable-cli.exe，不在 PATH 里）
_KNOWN_DIRS = [
    r'D:\darktable\bin', r'C:\darktable\bin',
    r'C:\Program Files\darktable\bin', r'C:\Program Files (x86)\darktable\bin',
    r'D:\Program Files\darktable\bin',
]


class HeadlessRenderEngine(Engine):
    """无头渲染引擎。task_probe / task_set_binary / task_binaries / task_render。"""

    name = 'headless'
    kind = 'native'
    vram_weight = 0.0        # 外部进程自带 GPU/内存治理，不占显存预算
    max_jobs = 1             # darktable-cli 单实例吞吐即可，串行防内存峰值
    priority = 1

    def __init__(self) -> None:
        super().__init__()
        self._bins: Dict[str, Optional[str]] = {
            cli: os.environ.get(_ENV_KEYS[cli]) or None for cli in _CLIS}
        self._lock = threading.Lock()
        self._jobs_done = 0

    # ---- 配置 ----
    def task_set_binary(self, cli: str, path: str) -> Dict[str, Any]:
        """设置某 CLI 的可执行文件路径（darktable-cli.exe / art-cli.exe）。"""
        cli = str(cli or '').lower()
        if cli not in _CLIS:
            raise ValueError('未知 CLI：%s（支持 %s）' % (cli, '/'.join(_CLIS)))
        path = str(path or '').strip()
        if not path or not os.path.isfile(path):
            raise FileNotFoundError(path or cli)
        with self._lock:
            self._bins[cli] = path
        return {'ok': True, 'cli': cli, 'binary': path}

    def task_binaries(self) -> Dict[str, Any]:
        """返回当前配置（供 health / 设置页展示）。"""
        with self._lock:
            return {cli: self._bins[cli] for cli in _CLIS}

    # ---- 探测 ----
    def task_probe(self) -> Dict[str, Any]:
        """逐 CLI 报可用性：显式配置优先，否则扫 PATH。"""
        out: Dict[str, Any] = {}
        for cli in _CLIS:
            with self._lock:
                path = self._bins[cli]
            found = path if (path and os.path.isfile(path)) else shutil.which(
                _WHICH_KEYS[cli])
            if not found:
                for d in _KNOWN_DIRS:
                    cand = os.path.join(d, _WHICH_KEYS[cli] + '.exe')
                    if os.path.isfile(cand):
                        found = cand
                        break
            out[cli] = {'available': bool(found), 'binary': found or None}
        return out

    # ---- 渲染 ----
    def task_render(self, src: str, out_path: str, sidecar: Optional[str] = None,
                    cli: str = 'darktable', timeout: int = 600) -> Dict[str, Any]:
        """按 sidecar 参数无头渲染：<cli> <input> [<sidecar>] <output>。

        sidecar 为 None 时用 CLI 的默认渲染（等同 darktable 的初始显影）。
        成功判定：退出码 0 且输出文件存在。
        """
        if not os.path.isfile(src):
            raise FileNotFoundError(src)
        cli = str(cli or '').lower()
        if cli not in _CLIS:
            raise ValueError('未知 CLI：%s' % cli)
        with self._lock:
            binary = self._bins[cli]
        if not binary or not os.path.isfile(binary):
            # 未显式配置时自动探测（PATH + 常见安装目录），并落位复用——
            # 此前 probe 能发现而 render 不认，导致「探测可用、渲染报未配置」（真机实证）
            found = shutil.which(_WHICH_KEYS[cli])
            if not found:
                for d in _KNOWN_DIRS:
                    cand = os.path.join(d, _WHICH_KEYS[cli] + '.exe')
                    if os.path.isfile(cand):
                        found = cand
                        break
            if not found:
                raise RuntimeError(
                    '%s 未配置（task_set_binary 或环境变量 %s）' % (cli, _ENV_KEYS[cli]))
            with self._lock:
                self._bins[cli] = binary = found

        os.makedirs(os.path.dirname(os.path.abspath(out_path)) or '.', exist_ok=True)
        cmd = [binary, src]
        if sidecar:
            if not os.path.isfile(sidecar):
                raise FileNotFoundError(sidecar)
            cmd.append(sidecar)
        cmd.append(out_path)
        cmd = self._norm_cmd(cmd)

        flags = 0x08000000 if os.name == 'nt' else 0    # CREATE_NO_WINDOW
        t0 = time.time()
        proc = subprocess.run(cmd, timeout=int(timeout), creationflags=flags,
                              capture_output=True)
        with self._lock:
            self._jobs_done += 1
        if proc.returncode != 0 or not os.path.isfile(out_path):
            tail = (proc.stderr or b'').decode('utf-8', 'replace')[-400:]
            raise RuntimeError('%s 渲染失败 rc=%s：%s' % (cli, proc.returncode, tail))
        return {'ok': True, 'out': out_path, 'cli': cli,
                'sidecar': sidecar or None, 'size': os.path.getsize(out_path),
                'elapsed': round(time.time() - t0, 2)}

    @staticmethod
    def _norm_cmd(cmd):
        """路径实参统一转正斜杠。

        darktable-cli 在 Windows 下会把实参里的反斜杠当转义符吃掉，
        导致 rc=0 却不产出文件（实证）；正斜杠则一切正常。art 同源，
        统一处理无害。
        """
        out = [cmd[0]]
        for a in cmd[1:]:
            out.append(str(a).replace('\\', '/'))
        return out

    # ---- 批量收集（纯逻辑，供 act_headlessRender 复用与单测）----
    def task_collect(self, folder: str, exclude: str = None,
                     cap: int = 100) -> list:
        """收集 folder 下可渲染文件（图片 + RAW），排除 exclude 自身。"""
        from .develop_engine import _RAW_EXTS
        exts = {'.jpg', '.jpeg', '.png', '.tif', '.tiff'} | set(_RAW_EXTS)
        out = []
        for name in sorted(os.listdir(folder)):
            fp = os.path.join(folder, name)
            if not os.path.isfile(fp):
                continue
            if os.path.splitext(name)[1].lower() not in exts:
                continue
            if exclude and os.path.abspath(fp) == os.path.abspath(exclude):
                continue
            out.append(fp)
        return out[:int(cap)]

    # ---- 可观测 ----
    def health(self) -> Dict[str, Any]:
        base = super().health()
        base['bins'] = self.task_binaries()
        return base

    def metrics(self) -> Dict[str, Any]:
        return {'name': self.name, 'jobs': self._jobs_done}
