# -*- coding: utf-8 -*-
"""1.9.30 冒烟：单色（黑白）照片增强路由 —— 改走 landscape 且结果转回灰度。

不依赖外部 enhance_server :8768：只把「转发到 :8768」的那次 urlopen 换成假响应，
测试客户端自身到 LibraryServer(:8797) 的 POST 仍是真 HTTP。
"""
import io, os, sys, json, time, urllib.request, urllib.error

ROOT = r'D:\Test\workbuddy\2026-09-18-09-00-13'
sys.path.insert(0, os.path.join(ROOT, 'client'))
from lightalbum.core import server as S          # noqa: E402
from lightalbum.core.library import Library      # noqa: E402
from lightalbum.core import enhance as E         # noqa: E402
from PIL import Image                            # noqa: E402
import numpy as np                               # noqa: E402

WORK = os.path.join(ROOT, '_smoke_mono')
os.makedirs(WORK, exist_ok=True)
PORT = 8797

# 假「增强产物」：一张彩色图（server 在 mono 时应把它转灰度）
fake_out = os.path.join(WORK, 'fake_enh.png')
grad = np.zeros((64, 64, 3), dtype=np.uint8)
grad[..., 0] = 220          # 强红
grad[..., 2] = 60           # 强蓝
Image.fromarray(grad, 'RGB').save(fake_out, 'PNG')

# 单色源（纯灰度）
mono_src = os.path.join(WORK, 'mono_src.jpg')
Image.fromarray(np.full((64, 64, 3), 128, dtype=np.uint8), 'RGB').save(mono_src, 'JPEG')
# 彩色源（强绿）
color_src = os.path.join(WORK, 'color_src.jpg')
c = np.zeros((64, 64, 3), dtype=np.uint8)
c[..., 1] = 200
Image.fromarray(c, 'RGB').save(color_src, 'JPEG')


class FakeResp:
    def __init__(self, payload):
        self._b = json.dumps(payload).encode('utf-8')

    def read(self):
        return self._b

    def __enter__(self):
        return self

    def __exit__(self, *a):
        return False


_real_urlopen = urllib.request.urlopen


def patched_urlopen(req, *args, **kwargs):
    """只劫持转发到外部美图服务(:8768)的那次调用，其余走真实网络。"""
    url = getattr(req, 'full_url', None) or str(req)
    if ':%d/' % E.ENHANCE_PORT in url or ':' + str(E.ENHANCE_PORT) in url:
        body = json.loads(req.data.decode('utf-8'))
        return FakeResp({'ok': True, 'route': body.get('route'),
                         'model': 'Image-Adaptive-3DLUT',
                         'out_path': fake_out, 'elapsed_ms': 1})
    return _real_urlopen(req, *args, **kwargs)


urllib.request.urlopen = patched_urlopen

db = os.path.join(WORK, 'lib.db')
if os.path.exists(db):
    os.remove(db)
lib = Library(db)
srv = S.LibraryServer(lib=lib, port=PORT,
                      static_dir=os.path.join(ROOT, 'client', 'resources'))
srv.start()
time.sleep(0.4)


def post(path, payload):
    req = urllib.request.Request('http://127.0.0.1:%d%s' % (PORT, path),
                                 data=json.dumps(payload).encode('utf-8'),
                                 headers={'Content-Type': 'application/json'},
                                 method='POST')
    try:
        with _real_urlopen(req, timeout=30) as r:
            return r.status, json.loads(r.read().decode('utf-8'))
    except urllib.error.HTTPError as e:
        return e.code, json.loads(e.read().decode('utf-8'))


def chroma_of(path):
    arr = np.asarray(Image.open(path).convert('RGB'), dtype=np.float32)
    return float((arr.max(-1) - arr.min(-1)).mean())


# ---- 1) 单色源 + 人像分类 → 应改走 landscape，且输出是灰度 ----
code, d = post('/api/enhance', {'path': mono_src, 'cat': '人像'})
print('MONO/PORTRAIT', code, 'ok=%s route=%s mono=%s out=%s'
      % (d.get('ok'), d.get('route'), d.get('mono'), d.get('output')))
assert code == 200 and d.get('ok') is True, d
assert d.get('route') == 'landscape', '单色人像应改走 landscape: %r' % d
assert d.get('mono') is True, d
outp = d.get('output') or ''
assert outp and os.path.isfile(outp), '应产出灰度文件: %r' % outp
ch = chroma_of(outp)
assert ch < 1.0, '输出应为灰度（chroma≈0），实为 %g' % ch
print('  mono output chroma=%.4f (灰度 OK)' % ch)

# ---- 2) 彩色源 + 人像分类 → 仍走 portrait，且不转灰 ----
code2, d2 = post('/api/enhance', {'path': color_src, 'cat': '人像'})
print('COLOR/PORTRAIT', code2, 'route=%s mono=%s out=%s'
      % (d2.get('route'), d2.get('mono'), d2.get('output')))
assert d2.get('route') == 'portrait', d2
assert d2.get('mono') is False, d2
assert '.mono.png' not in (d2.get('output') or ''), '彩色不应生成 .mono.png: %r' % d2

# ---- 3) 单色源 + 风光分类 → landscape，转灰 ----
code3, d3 = post('/api/enhance', {'path': mono_src, 'cat': '风光'})
print('MONO/LANDSCAPE', code3, 'route=%s mono=%s' % (d3.get('route'), d3.get('mono')))
assert d3.get('route') == 'landscape' and d3.get('mono') is True, d3
assert os.path.isfile(d3.get('output') or ''), d3
assert chroma_of(d3['output']) < 1.0, d3

print('SMOKE_MONO_OK')
