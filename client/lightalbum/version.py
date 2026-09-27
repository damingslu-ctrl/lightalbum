# -*- coding: utf-8 -*-
"""光影集 · 版本与命名（单一事实源 single source of truth）。

要改版本号或名称，只改这一个文件：构建脚本、关于对话框、健康接口、
修图引擎的摘要输出都从这里读，避免多处硬编码漂移。
"""
from __future__ import annotations

# ---- 产品 ----
APP_NAME = '光影集'                 # 产品中文名
APP_NAME_EN = 'LightAlbum'          # 产品英文名（可执行文件名 / 包名）
APP_VERSION = '1.9.39'               # 产品版本（语义化：主.次.修订）
APP_CHANNEL = 'stable'              # 发布通道：stable / beta / dev
ORG_NAME = 'lightalbum'             # 组织名（QSettings / 注册表）
COPYRIGHT = '© 2026 光影集'

# ---- 修图引擎 ----
# 纯 Pillow、零 numpy/torch 依赖；哲学是「测量 → 规划 → 执行」，
# 宁可不改也不改坏（好图只做出图锐化）。所有强效果只在显式预设下触发。
ENGINE_NAME = 'Lumen'               # 引擎英文码名
ENGINE_NAME_CN = '流明'             # 引擎中文名
ENGINE_VERSION = '1.5.1'            # 引擎版本
ENGINE_TAGLINE = '测量 → 规划 → 执行，宁可不改也不改坏'


def app_version_string() -> str:
    """用于窗口标题 / 关于框的完整版本串，如 `光影集 1.1.0`。"""
    return '%s %s' % (APP_NAME, APP_VERSION)


def engine_version_string() -> str:
    """引擎的完整版本串，如 `Lumen 1.0.0`。"""
    return '%s %s' % (ENGINE_NAME, ENGINE_VERSION)


def engine_display() -> str:
    """引擎的中英混排显示名，如 `流明 Lumen 1.0.0`。"""
    return '%s %s %s' % (ENGINE_NAME_CN, ENGINE_NAME, ENGINE_VERSION)


def windows_version_tuple() -> tuple:
    """PyInstaller 版本信息文件用的 4 段版本号 (major, minor, patch, 0)。"""
    parts = (APP_VERSION.split('-')[0]).split('.')
    nums = []
    for i in range(3):
        try:
            nums.append(int(parts[i]))
        except Exception:
            nums.append(0)
    while len(nums) < 3:
        nums.append(0)
    return (nums[0], nums[1], nums[2], 0)
