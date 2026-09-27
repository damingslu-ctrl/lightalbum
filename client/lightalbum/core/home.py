# -*- coding: utf-8 -*-
r"""数据目录解析 —— 全客户端唯一事实来源。

库索引 / 缩略图缓存 / 隐私库密文 / 导入原件 / 日志 全部挂在同一个「数据家目录」下，
默认是 `~/.lightalbum`。想把它整体搬到别的盘（例如 D:），按优先级依次判定：

1. 环境变量 `LIGHTALBUM_HOME`（最高优先级，适合脚本 / 临时切换）
2. 指针文件 `~/.lightalbum_home`（内容为一行目标目录）
3. 默认 `~/.lightalbum`

为什么用「指针文件」而不是写死路径：数据可以整块搬到 D 盘，主目录里只留一个
几字节的指针 —— 程序照旧能找到它，也方便随时改回去或再换盘。
"""
from __future__ import annotations

import os

MARKER_NAME = '.lightalbum_home'


def _expand(p):
    p = (p or '').strip()
    return os.path.abspath(os.path.expanduser(p)) if p else ''


def data_home():
    """返回数据家目录（绝对路径）。不负责创建目录。"""
    v = _expand(os.environ.get('LIGHTALBUM_HOME', ''))
    if v:
        return v
    try:
        marker = os.path.join(os.path.expanduser('~'), MARKER_NAME)
        if os.path.isfile(marker):
            with open(marker, 'r', encoding='utf-8') as f:
                p = _expand(f.readline())
            if p:
                return p
    except Exception:
        pass
    return os.path.join(os.path.expanduser('~'), '.lightalbum')
