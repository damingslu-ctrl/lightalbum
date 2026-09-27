# -*- coding: utf-8 -*-
"""VaultEngine —— 包隐私库（core/vault.py::Vault）。对齐现有子系统。"""
from __future__ import annotations

from ..enginehub import Engine, EngineState


class VaultEngine(Engine):
    name = 'vault'
    kind = 'native'
    vram_weight = 0.0
    max_jobs = 1

    def __init__(self, lib):
        super().__init__()
        from .. import vault
        # lib 为空（降级模式）时只占位，不建 Vault，任务调用会显式报错
        self._vault = vault.Vault(lib) if lib is not None else None

    def init(self):
        self._state = EngineState.READY

    def task_unlock(self, password):
        if self._vault is None:
            raise RuntimeError('vault 未初始化（lib 为空）')
        return self._vault.unlock(password)

    def task_lock(self):
        if self._vault is not None:
            self._vault.lock()
        return True

    def task_set_password(self, password):
        if self._vault is None:
            raise RuntimeError('vault 未初始化（lib 为空）')
        self._vault.set_password(password)
        return True

    def task_status(self):
        if self._vault is None:
            return {'available': False, 'unlocked': False}
        return {'available': True, 'unlocked': self._vault.unlocked(),
                'seconds_left': self._vault.seconds_left()}

    def health(self):
        return {'name': self.name, 'kind': self.kind, 'state': 'ready',
                'detail': self.task_status()}

    def metrics(self):
        return {'name': self.name, 'vram_weight': self.vram_weight}
