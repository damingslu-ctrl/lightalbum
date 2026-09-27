# -*- coding: utf-8 -*-
"""ExternalEditorEngine —— 外部编辑器跳转（ADR-0013，调研报告 §三「路径 A」）。

照搬 LR「Edit in External Editor」模式：
  1. task_plan   纯路径规划：工作目录 <src_dir>/_external_edit/<stem>/
  2. task_launch 复制工作副本 → 独立进程启动外部编辑器（darktable/ART/PhotoDemon…）
  3. task_scan   轮询输出目录，发现产物 → 调用方回灌入库（QFileSystemWatcher
                 的 GUI 接线后续在 app 侧做，本引擎只提供无 Qt 的轮询能力）

外部编辑器经环境变量 LIGHTALBUM_EXT_EDITOR 预配置（可选），
亦可用 task_set_editor 在运行时设置。独立进程调用 GPL 程序，无许可证传染。
"""
from __future__ import annotations

import os
import shlex
import shutil
import subprocess
import threading
import time
from typing import Any, Dict, List, Optional

from ..enginehub import Engine

_IMG_EXTS = {'.jpg', '.jpeg', '.png', '.tif', '.tiff', '.bmp', '.webp'}


class ExternalEditorEngine(Engine):
    """外部编辑器跳转引擎。task_set_editor / task_plan / task_launch / task_scan。"""

    name = 'extedit'
    kind = 'native'
    vram_weight = 0.0
    max_jobs = 4
    priority = 0

    def __init__(self) -> None:
        super().__init__()
        self._editor: Optional[str] = os.environ.get('LIGHTALBUM_EXT_EDITOR') or None
        self._editor_args: List[str] = []
        _ea = os.environ.get('LIGHTALBUM_EXT_EDITOR_ARGS')
        if _ea:
            try:
                self._editor_args = shlex.split(_ea, posix=False)
            except ValueError:
                self._editor_args = []
        self._lock = threading.Lock()
        self._jobs_done = 0

    # ---- 配置 ----
    def task_set_editor(self, path: str, args: Optional[list] = None) -> Dict[str, Any]:
        """设置外部编辑器可执行文件路径（含可选启动参数列表）。"""
        path = str(path or '').strip()
        if not path:
            raise ValueError('editor path 为空')
        if not os.path.isfile(path):
            raise FileNotFoundError(path)
        with self._lock:
            self._editor = path
            self._editor_args = list(args or [])
        return {'ok': True, 'editor': path, 'args': self._editor_args}

    def task_editor(self) -> Dict[str, Any]:
        """返回当前配置（供 health / 设置页展示）。"""
        return {'editor': self._editor, 'args': self._editor_args,
                'configured': bool(self._editor)}

    # ---- 纯规划（无副作用，可独立测试）----
    @staticmethod
    def task_plan(src: str, out_dir: str = None) -> Dict[str, Any]:
        if not os.path.isfile(src):
            raise FileNotFoundError(src)
        stem, ext = os.path.splitext(os.path.basename(src))
        work_dir = out_dir or os.path.join(os.path.dirname(src), '_external_edit', stem)
        out_sub = os.path.join(work_dir, 'output')
        return {
            'work_dir': work_dir,
            'work_src': os.path.join(work_dir, stem + '-edit' + ext.lower()),
            'out_dir': out_sub,
        }

    # ---- 启动 ----
    def task_launch(self, src: str, out_dir: str = None,
                    editor: Optional[str] = None,
                    args: Optional[list] = None,
                    wait: bool = False) -> Dict[str, Any]:
        """复制工作副本并以独立进程启动编辑器。返回工作副本与输出目录。"""
        plan = self.task_plan(src, out_dir)
        with self._lock:
            ed = editor or self._editor
            ed_args = list(args if args is not None else self._editor_args)
        if not ed:
            raise RuntimeError('未配置外部编辑器（task_set_editor 或 '
                               '环境变量 LIGHTALBUM_EXT_EDITOR）')
        if not os.path.isfile(ed):
            raise FileNotFoundError(ed)

        os.makedirs(plan['out_dir'], exist_ok=True)
        shutil.copy2(src, plan['work_src'])     # 每次全新副本，防外部改原件

        cmd = [ed] + ed_args + [plan['work_src']]
        flags = 0
        if os.name == 'nt':
            flags = 0x00000008 | 0x00000200     # DETACHED_PROCESS | NEW_PROCESS_GROUP
        if wait:
            proc = subprocess.run(cmd, creationflags=flags)  # noqa: PLW1509
            pid = proc.returncode
        else:
            proc = subprocess.Popen(cmd, creationflags=flags)  # noqa: PLW1509
            pid = proc.pid
        with self._lock:
            self._jobs_done += 1
        return {'ok': True, 'pid': pid, 'work_src': plan['work_src'],
                'out_dir': plan['out_dir']}

    # ---- 产物扫描（轮询；GUI 侧后续可换 QFileSystemWatcher）----
    @staticmethod
    def task_scan(out_dir: str, since: float = 0.0) -> List[Dict[str, Any]]:
        """列出 out_dir 中 mtime > since 的图片产物，供回灌入库。"""
        found: List[Dict[str, Any]] = []
        if not os.path.isdir(out_dir):
            return found
        for name in sorted(os.listdir(out_dir)):
            p = os.path.join(out_dir, name)
            if not os.path.isfile(p):
                continue
            if os.path.splitext(name)[1].lower() not in _IMG_EXTS:
                continue
            mt = os.path.getmtime(p)
            if mt > float(since):
                found.append({'path': p, 'mtime': mt, 'size': os.path.getsize(p)})
        return found

    # ---- 可观测 ----
    def health(self) -> Dict[str, Any]:
        base = super().health()
        base['editor_configured'] = bool(self._editor)
        base['editor'] = self._editor
        return base

    def metrics(self) -> Dict[str, Any]:
        return {'name': self.name, 'jobs': self._jobs_done,
                'ts': time.time()}
