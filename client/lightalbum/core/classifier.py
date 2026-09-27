# -*- coding: utf-8 -*-
"""AI 照片分类 —— 对接本地 CLIP 服务（默认 http://127.0.0.1:8766）。

为什么走 HTTP 而不内嵌
----------------------
CLIP 依赖 torch+CUDA（约 2GB+）。客户端 venv 会被 PyInstaller 整体打包，
塞进去会让 exe 变成几个 GB。所以沿用既有的本地 AI 思路（Pixel Cake / Jev）：
模型跑在 `D:\\lightalbum_ai\\venv_cake` 的独立服务里，客户端只做 HTTP 调用。

为什么用缩略图而不是原图
------------------------
iCloud「优化存储」的占位文件读图会触发按需下载并永久阻塞（历史上把同步卡死在
16% 就是这个）。这里只吃**已经生成好的缩略图**，完全绕开原盘 I/O。
没有缩略图的（视频 / 不支持的格式）跳过，不重试读写原图。

本地守卫
--------
默认 url 只允许回环地址（复用 brain.assert_local），防止把个人照片发到外网。
"""
from __future__ import annotations

import json
import os
import urllib.error
import urllib.request
import uuid

from .brain import assert_local

DEFAULT_URL = 'http://127.0.0.1:8766'
# 实测 RTX 2060：batch=32 约 244 img/s；再大收益不明显且显存压力上升
BATCH = 32
_TIMEOUT_PROBE = 2.0
_TIMEOUT_INFER = 60.0


# ---------------------------------------------------------------- 传输层
def _encode_multipart(files):
    """手写 multipart/form-data —— 项目统一用 urllib，不引入 requests。

    files: [(field, filename, bytes, content_type)]
    """
    boundary = '----LightAlbum' + uuid.uuid4().hex
    out = bytearray()
    crlf = b'\r\n'
    for field, fn, data, ct in files:
        out += b'--' + boundary.encode('ascii') + crlf
        out += ('Content-Disposition: form-data; name="%s"; filename="%s"'
                % (field, fn)).encode('utf-8') + crlf
        out += ('Content-Type: %s' % ct).encode('ascii') + crlf
        out += crlf
        out += data + crlf
    out += b'--' + boundary.encode('ascii') + b'--' + crlf
    return bytes(out), boundary


def _post_multipart(url, files, timeout=_TIMEOUT_INFER):
    body, boundary = _encode_multipart(files)
    req = urllib.request.Request(url, data=body, method='POST')
    req.add_header('Content-Type',
                   'multipart/form-data; boundary=%s' % boundary)
    req.add_header('Content-Length', str(len(body)))
    with _opener().open(req, timeout=timeout) as resp:
        raw = resp.read()
    try:
        return json.loads(raw.decode('utf-8'))
    except Exception:
        return {'ok': False, 'error': 'bad json from service'}


# ---------------------------------------------------------------- 探测
def _opener():
    """忽略系统代理。

    机器上配了http_proxy 时，urllib 会把 127.0.0.1 的请求也送去代理，
    导致「服务明明在跑却连不上」。本地服务一律直连。
    """
    return urllib.request.build_opener(urllib.request.ProxyHandler({}))


def probe(url=DEFAULT_URL, timeout=_TIMEOUT_PROBE):
    """返回服务可用性。不抛异常 —— UI 要靠它决定要不要显示「AI 分类」入口。"""
    out = {'ok': False, 'url': url, 'loaded': False, 'error': None,
           'device': None, 'gpu': None, 'cuda': False, 'labels': []}
    try:
        assert_local(url, 'AI 分类服务')
    except ValueError as e:
        out['error'] = str(e)
        return out
    try:
        with _opener().open(url + '/health', timeout=timeout) as resp:
            info = json.loads(resp.read().decode('utf-8'))
    except urllib.error.URLError as e:
        out['error'] = '服务未启动（%s）' % (getattr(e, 'reason', e) or e)
        return out
    except Exception as e:  # noqa: BLE001
        out['error'] = '%s: %s' % (type(e).__name__, e)
        return out
    out.update({'ok': True, 'loaded': bool(info.get('loaded')),
                'error': info.get('error'), 'device': info.get('device'),
                'gpu': info.get('gpu'), 'cuda': bool(info.get('cuda')),
                'labels': info.get('labels') or []})
    return out


# ---------------------------------------------------------------- 分类主流程
def classify_library(lib, url=DEFAULT_URL, batch=BATCH, limit=None,
                     progress=None, cancel=None):
    """给库里未分类的照片跑一遍模型推理，结果写回 SQLite。

    progress(done, total, name) —— 与 sync_root 保持同一调用习惯。
    cancel() 返回 True 则尽快停下（已在途的批次会跑完，避免半张 CONFIRM）。
    """
    st = probe(url)
    if not (st['ok'] and st['loaded']):
        return {'ok': False, 'error': st.get('error') or 'CLIP 服务不可用',
                'hint': '启动 D:\\lightalbum_ai\\start_classifier.bat',
                'done': 0, 'total': 0, 'counts': {}}

    rows = lib.photos_missing_category(limit=limit)
    total = len(rows)
    counts = {}
    skipped_no_thumb = 0
    failed = 0
    done = 0

    if progress:
        progress(0, total, '')

    i = 0
    while i < total:
        if cancel and cancel():
            break
        chunk = rows[i:i + batch]

        # 组装这一批：只取已存在的缩略图，缺的跳过（绝不碰原图）
        payload, paths = [], []
        for path, thumb in chunk:
            tp = lib.thumb_file(thumb) if thumb else ''
            if not tp:
                skipped_no_thumb += 1
                continue
            try:
                with open(tp, 'rb') as f:
                    data = f.read()
            except Exception:  # noqa: BLE001 - 单个坏图不该中断整批
                skipped_no_thumb += 1
                continue
            payload.append(('files', os.path.basename(tp), data, 'image/jpeg'))
            paths.append(path)

        if payload:
            try:
                res = _post_multipart(url + '/classify', payload)
            except Exception as e:  # noqa: BLE001
                failed += len(paths)
                res = None
            if res and res.get('ok'):
                items = []
                for r in res.get('results') or []:
                    idx = int(r.get('i', -1))
                    if idx < 0 or idx >= len(paths):
                        continue
                    label = r.get('label') or '其他'
                    score = float(r.get('score') or 0.0)
                    items.append((paths[idx], label, score))
                    counts[label] = counts.get(label, 0) + 1
                if items:
                    lib.set_categories(items)
                # 服务侧报的坏图
                failed += len(res.get('bad') or [])
            elif res:
                failed += len(paths)

        done += len(chunk)
        if progress:
            progress(done, total, '')
        i += batch

    return {
        'ok': True, 'done': done, 'total': total,
        'classified': sum(counts.values()),
        'skipped_no_thumb': skipped_no_thumb, 'failed': failed,
        'counts': counts,
        'device': st.get('device'), 'gpu': st.get('gpu'),
    }
