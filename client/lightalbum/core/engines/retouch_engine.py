# -*- coding: utf-8 -*-
"""RetouchEngine —— 包「流明 Lumen」修图引擎（core/retouch.py）。参考引擎 #1。"""
from __future__ import annotations

from ..enginehub import Engine, EngineState


class RetouchEngine(Engine):
    name = 'retouch'
    kind = 'native'
    vram_weight = 0.0          # 纯 CPU / Pillow，不占显存
    max_jobs = 2
    priority = 1

    def init(self):
        from .. import retouch
        self._rt = retouch
        self._state = EngineState.READY

    # ---- 能力（task_* 由 Hub 派发）----
    def task_retouch_file(self, path, preset='auto', exif=None, quality=92):
        return self._rt.retouch_file(path, preset=preset, exif=exif, quality=quality)

    def task_retouch_many(self, paths, preset='auto', exif_map=None, progress=None):
        return self._rt.retouch_many(paths, preset=preset,
                                     exif_map=exif_map, progress=progress)

    def task_recommend_presets(self, path, exif=None, limit=4, quality=None):
        return self._rt.recommend_presets(path, exif=exif, limit=limit, quality=quality)

    def metrics(self):
        return {'name': self.name, 'vram_weight': self.vram_weight,
                'max_jobs': self.max_jobs}
