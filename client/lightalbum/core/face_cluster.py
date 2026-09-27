# -*- coding: utf-8 -*-
"""人脸聚类（纯 numpy，零依赖模型）。

把一批「人脸 512 维 ArcFace 嵌入」按余弦相似度聚成若干「人物」簇。
不依赖任何外部服务：传入嵌入，返回每个人的簇标签。

聚类策略
--------
贪心单链（union-find）：任意两嵌入余弦相似度 ≥ 阈值即并到同一簇。
阈值取向：ArcFace 同一人通常 cos > 0.4，不同人 < 0.3，取 0.35 留余量。
单链对「同一人跨姿态」友好，代价是偶尔把极相似的两人并错（可接受 v1）。

为避免逐个两两比较 O(n²) 过慢，先按网格（分桶）只比较邻近桶——但个人相册
单次扫描的人脸数通常不过万，直接 O(n²) 在桌面端也就亚秒级，故用全量比较
保证质量（带早停）。
"""
from __future__ import annotations

import numpy as np

# 余弦相似度阈值：≥ 即判为同一人
DEFAULT_THRESHOLD = 0.35


def _cosine_matrix(emb):
    """返回 (n,n) 余弦相似度矩阵（已 L2 归一化的嵌入 → 直接点积）。"""
    n = emb.shape[0]
    sim = emb @ emb.T
    # 数值保护
    np.clip(sim, -1.0, 1.0, out=sim)
    return sim


def cluster_embeddings(embeddings, threshold=DEFAULT_THRESHOLD):
    """对一组嵌入聚类。

    embeddings: list[np.ndarray(512,)] 或 np.ndarray(n,512)（应已 L2 归一化）。
    返回 labels: list[int]，长度 = len(embeddings)，同一整数 = 同一人物。
    无输入返回空列表。
    """
    if len(embeddings) == 0:
        return []
    emb = np.asarray(embeddings, dtype=np.float64)
    if emb.ndim == 1:
        emb = emb.reshape(1, -1)
    n = emb.shape[0]
    if n == 1:
        return [0]

    # L2 归一化（防御性，若传入未归一化也能聚类）
    norms = np.linalg.norm(emb, axis=1, keepdims=True)
    norms[norms == 0] = 1.0
    emb = emb / norms

    sim = _cosine_matrix(emb)

    parent = list(range(n))

    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    def union(a, b):
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[ra] = rb

    # 上三角遍历，相似则合并
    for i in range(n):
        # 早停：本行全低于阈值则跳过后续比较（相似度随 i 变化无单调，仅微优化）
        row = sim[i, i + 1:]
        if row.size == 0:
            break
        hits = np.where(row >= threshold)[0]
        for j_off in hits:
            union(i, i + 1 + int(j_off))

    # 合并到根，得到连续标签
    roots = {}
    labels = []
    for i in range(n):
        r = find(i)
        if r not in roots:
            roots[r] = len(roots)
        labels.append(roots[r])
    return labels


def assign_person_names(labels):
    """把簇 id 映射成人类可读标签 '人物1' / '人物2' ...（按簇内数量降序命名）。

    返回 (labels, name_of_cluster)：name_of_cluster[k] = '人物K'（K 从 1 起，
    按该簇人脸数从多到少排序，最多的人叫 人物1）。labels 保持原顺序不变。
    """
    from collections import Counter
    cnt = Counter(labels)
    # 簇 id -> 序号（按数量降序，多者先）
    order = sorted(cnt.keys(), key=lambda k: -cnt[k])
    seq = {cid: idx + 1 for idx, cid in enumerate(order)}
    name_of = {cid: '人物%d' % seq[cid] for cid in order}
    return labels, name_of


def person_label_for_photo(face_labels, name_of_cluster):
    """一张照片可能有多个脸 → 返回去重后、按数量排序的人物标签串。

    返回逗号分隔的字符串，如 '人物1,人物3'；无脸返回 ''。
    """
    if not face_labels:
        return ''
    names = []
    seen = set()
    for lab in face_labels:
        nm = name_of_cluster.get(lab, '人物%d' % (lab + 1))
        if nm not in seen:
            seen.add(nm)
            names.append(nm)
    return ','.join(names)
