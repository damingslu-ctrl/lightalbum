# -*- coding: utf-8 -*-
"""ImportEngine —— 包本地库导入（core/library.py::Library.sync_root/import_photos）。
对齐现有子系统（app.py 现有散落 QThread 导入流程的后续统一入口）。"""
from __future__ import annotations

from ..enginehub import Engine, EngineState


class ImportEngine(Engine):
    name = 'import'
    kind = 'native'
    vram_weight = 0.0
    max_jobs = 1

    def __init__(self, lib):
        super().__init__()
        self._lib = lib

    def init(self):
        self._state = EngineState.READY

    def task_sync_root(self, root, progress=None, prune=True, want_thumbs=True):
        if self._lib is None:
            raise RuntimeError('import 未初始化（lib 为空）')
        return self._lib.sync_root(root, progress=progress,
                                   prune=prune, want_thumbs=want_thumbs)

    def task_import_photos(self, root, items, src='native'):
        if self._lib is None:
            raise RuntimeError('import 未初始化（lib 为空）')
        return self._lib.import_photos(root, items, src=src)

    def health(self):
        return {'name': self.name, 'kind': self.kind, 'state': 'ready'}

    def metrics(self):
        return {'name': self.name, 'vram_weight': self.vram_weight}
