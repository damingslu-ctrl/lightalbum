# -*- coding: utf-8 -*-
"""1.9.29 冒烟：起真实 LibraryServer，验证 /api/cull 与 /api/cull/export。"""
import io, os, sys, json, time, tempfile, threading, urllib.request, urllib.error

ROOT = r'D:\Test\workbuddy\2026-09-18-09-00-13'
sys.path.insert(0, os.path.join(ROOT, 'client'))
from lightalbum.core import server as S
from lightalbum.core.library import Library
from PIL import Image
import numpy as np

WORK = os.path.join(ROOT, '_smoke_cull')
os.makedirs(WORK, exist_ok=True)

# ---- 造 3 张图：p1/p2 高频噪声（清晰）；p2 与 p1 逐字节相同（MD5 重复）；p3 极小（低分辨率）----
def make(path, w, h, seed):
    rng = np.random.default_rng(seed)
    arr = rng.integers(0, 256, (h, w, 3), dtype=np.uint8)
    Image.fromarray(arr, 'RGB').save(path, 'JPEG')

p1 = os.path.join(WORK, 'a.jpg'); make(p1, 1200, 800, 1)
p2 = os.path.join(WORK, 'b.jpg'); make(p2, 1200, 800, 1)   # 与 p1 完全相同
p3 = os.path.join(WORK, 'c.jpg'); make(p3, 200, 150, 2)     # 小图

db = os.path.join(WORK, 'lib.db')
if os.path.exists(db): os.remove(db)
lib = Library(db)
recs = []
for p in (p1, p2, p3):
    st = os.stat(p)
    recs.append({'path': p, 'root': WORK, 'rel': os.path.basename(p),
                 'src': 'native', 'size': st.st_size, 'mtime': st.st_mtime,
                 'thumb': '', 'exif': {'t': os.path.basename(p), 'o': 'landscape',
                                        'r': 'china', 'cat': '风光'}})
lib._upsert_many(recs)

srv = S.LibraryServer(lib=lib, port=8799, static_dir=os.path.join(ROOT, 'client', 'resources'))
srv.start()
time.sleep(0.4)

def post(path, payload):
    req = urllib.request.Request('http://127.0.0.1:8799' + path,
        data=json.dumps(payload).encode('utf-8'),
        headers={'Content-Type': 'application/json'}, method='POST')
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            return r.status, json.loads(r.read().decode('utf-8'))
    except urllib.error.HTTPError as e:
        return e.code, json.loads(e.read().decode('utf-8'))

# 1) 选片（整个图库）
code, d = post('/api/cull', {'scope': 'all', 'options': {'dedup_level': '标准', 'top_n': 10}})
print('CULL', code, 'total=%s kept=%s rejected=%s dup_groups=%s top=%s'
      % (d.get('total'), d.get('kept'), d.get('rejected'),
         len(d.get('dup_groups', [])), len(d.get('top', []))))
assert code == 200 and d.get('ok'), '选片失败: %r' % d
# 预期：3 张里 b 与 a 重复被剔除、c 小图分辨率淘汰 → kept=1, rejected=2, 1 个去重组
assert d['total'] == 3, d['total']
assert d['kept'] == 1, 'kept 应为 1，实为 %s' % d['kept']
assert d['rejected'] == 2, 'rejected 应为 2，实为 %s' % d['rejected']
assert d['dup_groups'], '应有去重组'
print('  dup_kept=%s similar=%s' % (d['dup_groups'][0]['kept_name'],
      [s['name'] for s in d['dup_groups'][0]['similar']]))

# 2) 导出
code2, d2 = post('/api/cull/export', {'output_dir': os.path.join(WORK, 'out'), 'top_n': 10})
print('EXPORT', code2, d2)
assert code2 == 200 and d2.get('ok'), '导出失败: %r' % d2
out = d2['output_dir']
for label in ('精选', '普通', '废片', '去重'):
    print('  %s 目录存在=%s' % (label, os.path.isdir(os.path.join(out, label))))
assert all(os.path.isdir(os.path.join(out, l)) for l in ('精选', '普通', '废片', '去重'))

# 3) enhance：本环境 enhance_server 在线时返回真实结果（ok=True + out_path）
code3, d3 = post('/api/enhance', {'path': p1, 'cat': '风光'})
print('ENHANCE', code3, 'ok=%s route=%s out=%s'
      % (d3.get('ok'), d3.get('route'), d3.get('out_path') or d3.get('output')))
if d3.get('available') is False:
    print('ENHANCE_SKIP enhance_server :8768 未启动（环境依赖；'
          '增强逻辑由 test_enhance_mono.py 覆盖）')
else:
    assert code3 == 200 and d3.get('ok') is True, d3
    assert d3.get('route') == 'landscape', d3

print('SMOKE_CULL_OK')
