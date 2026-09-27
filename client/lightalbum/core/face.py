# -*- coding: utf-8 -*-
"""人脸 / 人物识别 —— 对接本地人脸服务（默认 http://127.0.0.1:8767）。

流程
----
1. 取库里「尚未跑过人物识别」的照片（缺 person 列）；
2. 对每张的**缩略图**调用本地人脸服务检测 + 取 512 维嵌入（绕开原盘 I/O，
   与 AI 分类同样规避 iCloud 占位阻塞）；
3. 全局聚类（余弦单链）→ 每个簇一个「人物 N」标签；
4. 写回 SQLite 的 person 列（一张照片可能有多人，逗号分隔）。

本地守卫：默认 url 只允许回环地址（复用 brain.assert_local），绝不在外网跑。
降级：服务不可用 → 返回明确错误，不抛异常中断主程序。
"""
from __future__ import annotations

import json
import urllib.error
import urllib.request

from .brain import assert_local
from . import face_cluster as FC

DEFAULT_URL = 'http://127.0.0.1:8767'
_TIMEOUT_PROBE = 2.0
_TIMEOUT_INFER = 60.0
_CLUSTER_THRESHOLD = 0.4     # 比默认 0.35 严一点：人物分群宁可少并，避免张冠李戴


def _opener():
    """忽略系统代理，本地服务一律直连。"""
    return urllib.request.build_opener(urllib.request.ProxyHandler({}))


def probe(url=DEFAULT_URL, timeout=_TIMEOUT_PROBE):
    """返回服务可用性。不抛异常 —— UI 靠它决定要不要显示「识别人物」入口。"""
    out = {'ok': False, 'url': url, 'loaded': False, 'error': None, 'device': None}
    try:
        assert_local(url, '人脸服务')
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
                'error': info.get('error'), 'device': info.get('device')})
    return out


def _post_json(url, payload, timeout=_TIMEOUT_INFER):
    req = urllib.request.Request(url, data=json.dumps(payload).encode('utf-8'),
                                 method='POST',
                                 headers={'Content-Type': 'application/json'})
    with _opener().open(req, timeout=timeout) as resp:
        return json.loads(resp.read().decode('utf-8'))


def face_scan_library(lib, url=DEFAULT_URL, limit=None, progress=None,
                      cancel=None):
    """对未识别人物的人脸跑一遍检测 + 聚类，结果写回 SQLite。

    progress(done, total, name) 与 sync_root 同习惯；cancel() 返回 True 尽快停。
    """
    st = probe(url)
    if not (st['ok'] and st['loaded']):
        return {'ok': False, 'error': st.get('error') or '人脸服务不可用',
                'hint': '启动 D:\\lightalbum_ai\\face_server.py（客户端应已自动拉起）',
                'done': 0, 'total': 0, 'persons': 0, 'faces': 0, 'skipped': 0}

    rows = lib.photos_missing_person(limit=limit)
    total = len(rows)
    done = 0
    skipped = 0
    faces_total = 0

    # 收集：每张照片的 [ (path, embedding np.array) ... ]，以及扁平 embedding 列表
    photo_faces = []          # per photo: list of cluster-label (filled after cluster)
    flat_emb = []             # np arrays
    flat_ref = []             # (photo_idx, face_idx) 反向索引

    if progress:
        progress(0, total, '')

    for pi, (path, thumb) in enumerate(rows):
        if cancel and cancel():
            break
        tp = lib.thumb_file(thumb) if thumb else ''
        if not tp:
            skipped += 1
            done += 1
            if progress:
                progress(done, total, '')
            continue
        try:
            res = _post_json(url + '/face/detect', {'path': tp})
        except Exception as e:  # noqa: BLE001
            skipped += 1
            done += 1
            if progress:
                progress(done, total, '')
            continue
        faces = (res or {}).get('faces') or []
        emb_for_photo = []
        for f in faces:
            emb = f.get('embedding')
            if not emb:
                continue
            arr = __import__('numpy').array(emb, dtype='float64')
            flat_emb.append(arr)
            flat_ref.append((pi, len(emb_for_photo)))
            emb_for_photo.append(arr)
        photo_faces.append(emb_for_photo)
        faces_total += len(emb_for_photo)
        done += 1
        if progress:
            progress(done, total, '')

    # 全局聚类
    n_persons = 0
    if flat_emb:
        labels = FC.cluster_embeddings(flat_emb, threshold=_CLUSTER_THRESHOLD)
        _, name_of = FC.assign_person_names(labels)
        # 回填每张照片的脸→人物标签
        per_photo_cluster = [[] for _ in photo_faces]
        for (pi, _fi), lab in zip(flat_ref, labels):
            per_photo_cluster[pi].append(lab)
        items = []
        for pi, (path, _thumb) in enumerate(rows):
            if cancel and cancel():
                break
            person_str = FC.person_label_for_photo(per_photo_cluster[pi], name_of)
            items.append((path, person_str))
        if items:
            lib.set_persons(items)
        n_persons = len(name_of)

    return {
        'ok': True, 'done': done, 'total': total,
        'faces': faces_total, 'persons': n_persons,
        'skipped': skipped, 'device': st.get('device'),
    }
