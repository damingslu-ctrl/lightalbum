# -*- coding: utf-8 -*-
"""ExifEngine —— 包 EXIF / GPS 真解析（core/exif.py::read_exif）。对齐现有子系统。"""
from __future__ import annotations

from ..enginehub import Engine, EngineState


class ExifEngine(Engine):
    name = 'exif'
    kind = 'native'
    vram_weight = 0.0
    max_jobs = 4
    priority = 1

    def init(self):
        from .. import exif
        self._ex = exif
        self._state = EngineState.READY

    def task_read(self, path, data=None):
        return self._ex.read_exif(path, data=data)

    def metrics(self):
        return {'name': self.name, 'vram_weight': self.vram_weight}
