# -*- coding: utf-8 -*-
"""EnhanceEngine —— 「先分类后路由」本地 AI 美图（core/enhance.py 路由 + lut3d 服务）。

路由判定零猜测：cat（库内 AI 分类）→ portrait / landscape；缺失 → skipped。
执行走本机 enhance_server（127.0.0.1:8767，零云端）；人像模型未装时服务端
诚实返回 501，引擎原样透传 reason。
"""
from __future__ import annotations

import json
import urllib.request
from typing import Any, Dict, Optional

from .. import enhance
from ..enginehub import Engine, EngineState


class EnhanceEngine(Engine):
    name = 'enhance'
    kind = 'hybrid'            # 路由/融合本地 + lut3d 推理本机 GPU 服务
    vram_weight = 0.1          # <600K 参数 CNN，显存占用可忽略
    max_jobs = 1

    def __init__(self, port: int = enhance.ENHANCE_PORT):
        self._port = port
        self._state = EngineState.READY

    # ---- 内部 ----
    def _url(self, p: str) -> str:
        return 'http://127.0.0.1:%d%s' % (self._port, p)

    def _post(self, path: str, payload: Dict[str, Any], timeout: float = 120.0):
        req = urllib.request.Request(
            self._url(path), data=json.dumps(payload).encode('utf-8'),
            headers={'Content-Type': 'application/json'}, method='POST')
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.status, json.loads(r.read().decode('utf-8'))

    # ---- 能力 ----
    def task_probe(self):
        info = {
            'name': self.name,
            'routes': {k: v for k, v in enhance.ROUTE_TABLE.items()},
            'service': self._url('/health'),
        }
        try:
            with urllib.request.urlopen(self._url('/health'), timeout=2) as r:
                info['service_ok'] = True
                info['service'] = json.loads(r.read().decode('utf-8'))
        except Exception as e:
            info['service_ok'] = False
            info['service_error'] = str(e)
        return info

    def task_enhance(self, path: str, cat: Optional[str] = None,
                     route: Optional[str] = None,
                     strength: float = enhance.DEFAULT_STRENGTH,
                     variant: str = 'paired'):
        """单图增强。route 显式指定优先；否则由 cat 路由；都缺 → skipped（不猜）。"""
        r = route or enhance.route_for(cat)
        if not r:
            return {'ok': False, 'skipped': True,
                    'reason': '照片未分类（cat 缺失或「其他」），按数据诚实铁律不猜测路由'}
        code, resp = self._post('/enhance', {
            'path': path, 'route': r, 'strength': strength, 'variant': variant})
        resp.setdefault('route', r)
        resp['http_status'] = code
        return resp
