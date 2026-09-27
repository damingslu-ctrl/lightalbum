# -*- coding: utf-8 -*-
"""CullingEngine —— 包「选片引擎」（core/culling.py）。

本地从海量照片筛出好照片：去重 → 模糊 → 曝光 → 分辨率 → 评分 → TopN → 归档。
零云端、纯 Pillow/numpy（cv2 可选），不占显存。
"""
from __future__ import annotations

from typing import Any, Dict, List, Optional, Sequence

from ..enginehub import Engine, EngineState


def _clean(rec: Dict[str, Any]) -> Dict[str, Any]:
    """去掉不可序列化的 PHash 对象，只保留 phash_str（跨桥接/JSON 安全）。"""
    d = dict(rec or {})
    d.pop('phash', None)
    return d


class CullingEngine(Engine):
    name = 'culling'
    kind = 'native'
    vram_weight = 0.0          # 纯 CPU / Pillow+numpy，不占显存
    max_jobs = 2
    priority = 2

    def init(self):
        from .. import culling
        self._cu = culling
        self._state = EngineState.READY

    # ---- 能力 ----
    def task_probe(self):
        """能力自检：去重档位、评分权重、本地可测维度。"""
        return {
            'name': self.name,
            'dedup_levels': list(self._cu.DEDUP_LEVELS.keys()),
            'weights': dict(self._cu.WEIGHTS),
            'local_measurable': list(self._cu.LOCAL_MEASURABLE),
            'defaults': dict(self._cu.DEFAULT_OPTIONS),
            'cv2': self._cu.cv2 is not None,
        }

    def task_assess(self, path: str, options: Optional[Dict[str, Any]] = None):
        """单张体检（含本地可测维度评分）。"""
        return _clean(self._cu.assess(path, options))

    def task_scan(self, paths: Sequence[str],
                  options: Optional[Dict[str, Any]] = None,
                  progress=None):
        """批量本地预处理 → (candidates, rejected)。"""
        cands, rejs = self._cu.filter_photos(list(paths), options=options,
                                             on_progress=progress)
        return {
            'total': len(paths),
            'candidates': [_clean(c) for c in cands],
            'rejected': [_clean(r) for r in rejs],
            'dedup_groups': self._cu.collect_dedup_groups(rejs),
        }

    def task_select(self, candidates: Sequence[Dict[str, Any]], n: int = 10):
        """按总分取 TopN。"""
        return [_clean(c) for c in self._cu.select_top(list(candidates), n=n)]

    def task_export(self, top: Sequence[Dict[str, Any]],
                    rest: Sequence[Dict[str, Any]],
                    rejected: Sequence[Dict[str, Any]],
                    output_dir: str, archive_all: bool = True):
        """归档到 精选/普通/废片/去重 四个文件夹。"""
        return self._cu.export_organized(list(top), list(rest), list(rejected),
                                         output_dir, archive_all=archive_all)

    def metrics(self):
        return {'name': self.name, 'vram_weight': self.vram_weight,
                'max_jobs': self.max_jobs}
