# -*- coding: utf-8 -*-
"""版本组关联（ADR-0013 §版本组）——外部编辑/无头渲染产物 ↔ 原图。

设计：
  - 持久层是 `data_home()/versions.json`（**不动 SQLite schema**，回归面最小）；
  - 以**绝对路径**（规范化为小写正斜杠）为键：页面侧用
    `p.path || __nativeAbs[p.rel]` 还原 abs 后即可匹配，本地库/网页两模式通用；
  - 纯 stdlib、文件损坏时自愈（当作空表返回，绝不抛出阻断）。
"""
from __future__ import annotations

import json
import os
import threading
from typing import Any, Dict

from .home import data_home

_PATH = os.path.join(data_home(), 'versions.json')
_LOCK = threading.Lock()


def norm(p: str) -> str:
    """路径规范化：绝对路径 + 正斜杠 + 小写（Windows 盘符不区分大小写）。"""
    try:
        p = os.path.abspath(str(p or ''))
    except Exception:  # noqa: BLE001
        p = str(p or '')
    return p.replace('\\', '/').lower()


def _load_raw() -> Dict[str, Any]:
    try:
        with open(_PATH, 'r', encoding='utf-8') as fh:
            d = json.load(fh)
        if isinstance(d, dict) and isinstance(d.get('products'), dict):
            return d
    except Exception:  # noqa: BLE001  缺失/损坏一律自愈为空表
        pass
    return {'v': 1, 'products': {}}


def products() -> Dict[str, Dict[str, Any]]:
    """返回全量映射 {productAbs(norm): {'orig','kind','ts'}}。"""
    with _LOCK:
        return dict(_load_raw()['products'])


def record(product: str, orig: str, kind: str, ts: float = None) -> Dict[str, Any]:
    """登记一条「产物 → 原图」关联。键与值都经 norm()。"""
    with _LOCK:
        d = _load_raw()
        d['products'][norm(product)] = {
            'orig': norm(orig), 'kind': str(kind), 'ts': float(ts or 0)}
        os.makedirs(os.path.dirname(_PATH), exist_ok=True)
        with open(_PATH, 'w', encoding='utf-8') as fh:
            json.dump(d, fh, ensure_ascii=False)
    return products()


def orig_of(product: str) -> Dict[str, Any]:
    """查某产物的关联；无则 {'orig': norm(自身)}（自身即原图）。"""
    key = norm(product)
    with _LOCK:
        hit = _load_raw()['products'].get(key)
    if hit:
        return {'orig': hit.get('orig'), 'kind': hit.get('kind'),
                'ts': hit.get('ts'), 'self_is_product': True}
    return {'orig': key, 'kind': None, 'ts': None, 'self_is_product': False}


def path() -> str:
    return _PATH
