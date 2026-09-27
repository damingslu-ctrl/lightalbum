# -*- coding: utf-8 -*-
"""引擎适配层（ADR-0012 / ADR-0013）：把各功能模块包成统一 Engine 契约的实现。

本包**必须**有 __init__.py（常规包，而非命名空间包），否则 PyInstaller
静态分析不会把引擎子模块收进打包产物，运行时才 ImportError。
这里显式导入调度器与全部引擎子模块，确保打包必然收集。
"""
from __future__ import annotations

from ..enginehub import (  # noqa: F401
    Engine, EngineHub, ViewEngine, EngineState, JobState)
from .retouch_engine import RetouchEngine           # noqa: F401
from .exif_engine import ExifEngine                 # noqa: F401
from .ai_engine import AiEngine                     # noqa: F401
from .develop_engine import DevelopEngine, is_raw_path  # noqa: F401
from .external_editor_engine import ExternalEditorEngine  # noqa: F401
from .headless_engine import HeadlessRenderEngine  # noqa: F401
from .vault_engine import VaultEngine               # noqa: F401
from .import_engine import ImportEngine             # noqa: F401
from .registry import build_default_hub             # noqa: F401

__all__ = [
    'Engine', 'EngineHub', 'ViewEngine', 'EngineState', 'JobState',
    'RetouchEngine', 'ExifEngine', 'AiEngine',
    'DevelopEngine', 'is_raw_path', 'ExternalEditorEngine',
    'HeadlessRenderEngine',
    'VaultEngine', 'ImportEngine',
    'build_default_hub',
]
