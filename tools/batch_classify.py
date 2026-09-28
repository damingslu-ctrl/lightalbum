# -*- coding: utf-8 -*-
"""批量本地分类：thumbs → CLIP（复用 classifier_server 逻辑）→ library.db category 列。

- 零云端：模型从 D:\\lightalbum_ai\\hf_cache 离线加载（HF_HUB_OFFLINE=1）。
- 复用 classifier_server.load_model()/classify_batch()：8 类 prompt + 黑白饱和度启发式
  + 低置信度归「其他」+ 模型候选自动回退，与线上服务单张结果完全一致。
- 写回 photos.category / cat_score / cat_at；幂等（重复跑覆盖旧值）。
- 运行：D:/lightalbum_ai/venv_cake/Scripts/python.exe tools/batch_classify.py
"""
import os
import sys
import time
import sqlite3

os.environ.setdefault('HF_HUB_OFFLINE', '1')
# 铁律（I 节）：HF_HOME 与 HF_HUB_CACHE 须同设且指向缓存根（models--*/ 直接在其下）
os.environ.setdefault('HF_HOME', r'D:\lightalbum_ai\hf_cache')
os.environ.setdefault('HF_HUB_CACHE', r'D:\lightalbum_ai\hf_cache')

AI_DIR = r'D:\lightalbum_ai'
sys.path.insert(0, AI_DIR)

DB_PATH = r'D:\lightalbum\library.db'
THUMBS_DIR = r'D:\lightalbum\thumbs'
BATCH = 64          # 6GB 显存下 ViT-B-32 batch=64 稳妥
COMMIT_EVERY = 512  # 每 512 张落盘一次，中断可续（按 cat_at 判断跳过可选，这里直接全量覆盖）

import classifier_server as cs  # noqa: E402


def main():
    from PIL import Image

    t0 = time.time()
    st = cs.load_model()
    if not st.get('loaded'):
        print('MODEL_LOAD_FAIL %s' % st.get('error'))
        return 2
    print('MODEL_OK %s device=%s load_ms=%.0f' % (
        st.get('model_used'), st.get('device'), st.get('load_ms', 0)))

    con = sqlite3.connect(DB_PATH)
    cur = con.cursor()
    rows = cur.execute(
        "select path, thumb from photos "
        "where thumb is not null and thumb != '' and missing = 0 "
        "order by path").fetchall()
    total = len(rows)
    print('PHOTOS %d' % total)
    if total == 0:
        print('BATCH_CLASSIFY_OK n=0')
        return 0

    done = 0
    pending = []   # (category, score, cat_at, path)
    dist = {}

    def flush():
        if not pending:
            return
        cur.executemany(
            'update photos set category=?, cat_score=?, cat_at=? where path=?',
            pending)
        con.commit()
        pending.clear()

    # 顺序切块攒批：绝不重复/遗漏（v1 的 done+len 索引会重复分类 ~2 遍）
    i = 0
    while i < total:
        chunk = rows[i:i + BATCH]
        imgs = [Image.open(os.path.join(THUMBS_DIR, t)) for _, t in chunk]
        results = cs.classify_batch(imgs)
        now = time.time()
        for (p, _t), r in zip(chunk, results):
            pending.append((r['label'], float(r['score']), now, p))
            dist[r['label']] = dist.get(r['label'], 0) + 1
        i += len(chunk)
        done = i
        if done % COMMIT_EVERY < BATCH or done >= total:
            flush()
        if done % 640 < BATCH:
            print('PROGRESS %d/%d %.1fs' % (done, total, time.time() - t0),
                  flush=True)

    flush()
    print('== distribution ==')
    for k, v in sorted(dist.items(), key=lambda x: -x[1]):
        print('%s\t%d' % (k, v))
    n_db = cur.execute(
        "select count(*) from photos where category is not null and category != ''"
    ).fetchone()[0]
    print('DB_CATEGORIZED %d/%d' % (n_db, total))
    print('BATCH_CLASSIFY_OK n=%d secs=%.1f' % (done, time.time() - t0))
    return 0 if n_db == total else 1


if __name__ == '__main__':
    sys.exit(main())
