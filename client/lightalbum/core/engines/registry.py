# -*- coding: utf-8 -*-
"""引擎注册表工厂（ADR-0012 §2 / ADR-0013）。

build_default_hub(lib) 组装标准套件并 start_all()：
  - 始终注册：RetouchEngine / ExifEngine / AiEngine / CullingEngine（选片，
              零云端本地预处理 + 评分 + TopN 精选）
              DevelopEngine（rawpy 缺失时内部降级，不阻断）
              HeadlessRenderEngine（darktable-cli/art-cli 无头渲染，
              未配置二进制时降级）
              ExternalEditorEngine（编辑器路径经 task_set_editor 或
              环境变量 LIGHTALBUM_EXT_EDITOR 配置）
  - lib 非空时才注册：VaultEngine / ImportEngine（依赖本地库实例）
"""
from __future__ import annotations

from ..enginehub import EngineHub


def build_default_hub(lib=None, vram_budget: float = 1.0) -> EngineHub:
    from .retouch_engine import RetouchEngine
    from .exif_engine import ExifEngine
    from .ai_engine import AiEngine
    from .develop_engine import DevelopEngine
    from .external_editor_engine import ExternalEditorEngine
    from .headless_engine import HeadlessRenderEngine
    from .culling_engine import CullingEngine
    from .enhance_engine import EnhanceEngine

    hub = EngineHub(vram_budget=vram_budget)
    hub.register(RetouchEngine())
    hub.register(ExifEngine())
    hub.register(AiEngine())
    hub.register(DevelopEngine())
    hub.register(ExternalEditorEngine())
    hub.register(HeadlessRenderEngine())
    hub.register(CullingEngine())
    hub.register(EnhanceEngine())

    if lib is not None:
        from .vault_engine import VaultEngine
        from .import_engine import ImportEngine
        hub.register(VaultEngine(lib))
        hub.register(ImportEngine(lib))

    hub.start_all()
    return hub
