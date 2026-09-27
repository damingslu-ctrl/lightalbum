# -*- coding: utf-8 -*-
"""AiEngine —— 包统一 AI 网关（端口 8788，ADR-0012 / memory H）。

瘦 HTTP 客户端（stdlib urllib，无新依赖）：
  /health 聚合、/matting /score → PixelCake、/classify → 分类器、
  /llm → Ollama、/unload 手动卸。vram_weight=1.0 保证重模型唯一同占
（LLM↔matting 互斥），由 Hub 的分时预算串行化。

防御式：网关未起时 health 返回 state='down'，绝不抛异常阻断启动。
"""
from __future__ import annotations

import json
import urllib.error
import urllib.request

from ..enginehub import Engine, EngineState


class AiEngine(Engine):
    name = 'ai'
    kind = 'native'
    vram_weight = 1.0          # 重模型唯一同占
    max_jobs = 1
    priority = 5
    GATEWAY = 'http://127.0.0.1:8788'

    def init(self):
        self._state = EngineState.READY

    def _url(self, path):
        return self.GATEWAY.rstrip('/') + path

    def health(self):
        try:
            with urllib.request.urlopen(self._url('/health'), timeout=2) as r:
                detail = json.loads(r.read().decode('utf-8', 'replace'))
            return {'name': self.name, 'kind': self.kind, 'state': 'ready',
                    'gateway': self.GATEWAY, 'detail': detail}
        except Exception as e:  # noqa: BLE001 - 网关未起属正常降级
            return {'name': self.name, 'kind': self.kind, 'state': 'down',
                    'gateway': self.GATEWAY, 'error': str(e)}

    def task_health(self):
        return self.health()

    def task_request(self, path, payload=None, timeout=30):
        url = self._url(path)
        data = json.dumps(payload or {}).encode('utf-8')
        req = urllib.request.Request(
            url, data=data, headers={'Content-Type': 'application/json'})
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return json.loads(r.read().decode('utf-8', 'replace'))

    def metrics(self):
        return {'name': self.name, 'vram_weight': self.vram_weight,
                'max_jobs': self.max_jobs}
