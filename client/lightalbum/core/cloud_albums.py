# -*- coding: utf-8 -*-
r"""云相册本地同步目录的探测与引导。

现实约束（先说清楚，避免误解）
────────────────────────────
iCloud 与一刻相册都**没有开放 API**，无法在客户端里直接拉云端相册。
但两者都有 Windows 同步客户端会把照片落到本地磁盘：

*   iCloud for Windows：照片存到 `C:\Users\<u>\Pictures\iCloud Photos\Photos`
    （OneDrive 接管 Pictures 时会挪到 `C:\Users\<u>\iCloud Photos\Photos`；
    旧版 7.x 用 `...\iCloud Photos\Downloads`）。
*   一刻相册 PC 版：备份路径由用户在「设置 → 备份设置」自选，无固定位置，
    只能探测常见默认点 + 让用户手动指定。

因此「接入」= 探测这些本地同步目录 → 一键交给既有扫描管线（缓存优先 +
并行 + draft 解码都自动生效）。iCloud 开着「优化存储」时本地可能只有
缩略图占位文件 —— 用小文件占比检测出来并提示用户先在 iCloud 设置里
勾选「下载并保留原件」，否则导入的全是缩略图。
"""
from __future__ import annotations

import os

from .exif import IMG_EXT
from .scanner import is_photo

# 单个照片文件低于该字节数视为「占位缩略图」而非可导入原图
PLACEHOLDER_BYTES = 32 * 1024
# 探测占位占比时的采样上限（大目录不全扫，够统计用）
SAMPLE_LIMIT = 400

_PROVIDERS = [
    {
        'id': 'icloud',
        'name': 'iCloud 照片',
        # 按可能性排序；存在即采用
        'candidates': [
            os.path.join('~', 'Pictures', 'iCloud Photos', 'Photos'),
            os.path.join('~', 'Pictures', 'iCloud Photos'),
            os.path.join('~', 'iCloud Photos', 'Photos'),       # OneDrive 接管 Pictures 时
            os.path.join('~', 'Pictures', 'iCloud Photos', 'Downloads'),
        ],
        'install_hint': '安装「iCloud for Windows」（微软商店）并登录 Apple ID，'
                        '在 iCloud 设置 → 照片 → 勾选「iCloud 照片」与「下载新照片和视频」，'
                        '同步完成后本页会自动识别。',
    },
    {
        'id': 'yike',
        'name': '一刻相册',
        # PC 版备份路径用户自选，这里只探测常见默认点
        'candidates': [
            os.path.join('~', '一刻相册'),
            os.path.join('~', 'Pictures', '一刻相册'),
            os.path.join('~', 'Documents', '一刻相册'),
            'D:\\一刻相册',
            os.path.join('~', 'BaiduNetdiskDownload', '一刻相册'),
        ],
        'install_hint': '安装「一刻相册 PC 版」，登录后在 设置 → 备份设置 里开启备份并选择'
                        '本地文件夹；然后把该文件夹路径填到下方输入框，或直接用「手动指定」。',
    },
]


def _expand(p):
    return os.path.abspath(os.path.expanduser(p))


def _probe_photo_dir(path):
    """探测一个候选目录是否为可用照片库。

    返回 dict（含 photo_count / placeholder_ratio / newest_mtime），
    目录不存在或无照片时返回 None。
    """
    if not path or not os.path.isdir(path):
        return None
    count = 0
    tiny = 0
    sampled = 0
    newest = 0.0
    for dirpath, dirnames, filenames in os.walk(path):
        dirnames[:] = [d for d in dirnames if not d.startswith('.')]
        for fn in filenames:
            if not is_photo(fn):
                continue
            count += 1
            p = os.path.join(dirpath, fn)
            try:
                st = os.stat(p)
                newest = max(newest, st.st_mtime)
                if sampled < SAMPLE_LIMIT:
                    sampled += 1
                    if st.st_size < PLACEHOLDER_BYTES:
                        tiny += 1
            except Exception:
                pass
    if not count:
        return None
    ratio = (tiny / sampled) if sampled else 0.0
    return {
        'path': path,
        'photo_count': count,
        # 占位（仅缩略图未下载原图）占比：iCloud「优化存储」时常见
        'placeholder_ratio': round(ratio, 2),
        'newest_mtime': newest,
    }


def detect(extra_map=None):
    """探测所有云相册的本地同步目录。

    extra_map：{provider_id: [目录, ...]}，用户手动指定的补充目录。
    只归属到对应 provider（早先版本套给所有 provider，
    导致 fake iCloud 目录被同时当成一刻相册的库，实测踩过）；
    同一 provider 下与真实候选择优（取照片数更多者）。

    返回 list[dict]：id/name/path/photo_count/placeholder_ratio/warn/hint。
    只返回「确实存在且有照片」的；没装的用户会从 hint 里拿到安装引导。
    """
    extra_map = extra_map or {}
    out = []
    for prov in _PROVIDERS:
        hit = None
        for cand in prov['candidates']:
            hit = _probe_photo_dir(_expand(cand))
            if hit:
                break
        for p in extra_map.get(prov['id'], []):
            if p and os.path.isdir(p):
                r = _probe_photo_dir(p)
                if r and (not hit or r['photo_count'] > hit['photo_count']):
                    hit = r
        if not hit:
            out.append({'id': prov['id'], 'name': prov['name'],
                        'found': False, 'hint': prov['install_hint']})
            continue
        warn = ''
        if hit['placeholder_ratio'] >= 0.5:
            if prov['id'] == 'icloud':
                warn = ('本地文件里 %.0f%% 是缩略图占位（开了「优化存储」）。'
                        '请在 iCloud 设置 → 照片 选择「下载并保留原件」后再导入，'
                        '否则入库的全是缩略图。' % (hit['placeholder_ratio'] * 100))
            else:
                warn = ('本地文件里 %.0f%% 是小体积缩略图，请确认同步已下载原图。'
                        % (hit['placeholder_ratio'] * 100))
        out.append({
            'id': prov['id'], 'name': prov['name'], 'found': True,
            'path': hit['path'], 'photo_count': hit['photo_count'],
            'placeholder_ratio': hit['placeholder_ratio'], 'warn': warn,
        })
    return out
