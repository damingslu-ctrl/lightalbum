# -*- coding: utf-8 -*-
"""1.9.30 server.py /api/enhance 单色（黑白）路由处理。rep() 断言 count==1。"""
import io

TARGET = r'D:\Test\workbuddy\2026-09-18-09-00-13\client\lightalbum\core\server.py'

# ---- A) 单色检测 + portrait→landscape 改写 ----
OLD_A = """                self._send(200, {'ok': False, 'skipped': True,
                                 'reason': '照片未分类（cat 缺失或「其他」），'
                                           '按数据诚实铁律不猜测路由'})
                return
            try:
                strength = float(payload.get('strength') or E.DEFAULT_STRENGTH)
            except Exception:
                strength = E.DEFAULT_STRENGTH
            url = 'http://127.0.0.1:%d/enhance' % E.ENHANCE_PORT
            body = json.dumps({'path': fp, 'route': r, 'strength': strength,
                               'variant': 'paired'}).encode('utf-8')"""

NEW_A = """                self._send(200, {'ok': False, 'skipped': True,
                                 'reason': '照片未分类（cat 缺失或「其他」），'
                                           '按数据诚实铁律不猜测路由'})
                return
            # 像素级单色检测：真·黑白照片即使被分类为人像，也改走 landscape 色调增强
            # （避免 portrait 上色器给黑白照染色），结果再转回灰度保持黑白。
            mono = False
            try:
                from PIL import Image
                import numpy as _npy
                _im = Image.open(fp).convert('RGB')
                _arr = _npy.asarray(_im, dtype=_npy.float32)
                mono = E.is_monochrome(_arr)
            except Exception:
                mono = False
            if mono and r == 'portrait':
                r = 'landscape'
            try:
                strength = float(payload.get('strength') or E.DEFAULT_STRENGTH)
            except Exception:
                strength = E.DEFAULT_STRENGTH
            url = 'http://127.0.0.1:%d/enhance' % E.ENHANCE_PORT
            body = json.dumps({'path': fp, 'route': r, 'strength': strength,
                               'variant': 'paired'}).encode('utf-8')"""

# ---- B) 成功分支：单色照片把增强产物转回灰度 ----
OLD_B = """                if not _err:
                    srv['ok'] = True
                    out = srv.get('output') or srv.get('out_path') or ''
                    if out and os.path.isfile(out):
                        srv['preview_url'] = '/api/file/%s' % self.register_file(out)"""

NEW_B = """                if not _err:
                    srv['ok'] = True
                    out = srv.get('output') or srv.get('out_path') or ''
                    if mono and out and os.path.isfile(out):
                        try:
                            from PIL import Image as _PIL
                            _g = _PIL.open(out).convert('L').convert('RGB')
                            _gpath = out.rsplit('.', 1)[0] + '.mono.png'
                            _g.save(_gpath)
                            out = _gpath
                        except Exception:
                            pass
                    if out and os.path.isfile(out):
                        srv['output'] = out
                        srv['preview_url'] = '/api/file/%s' % self.register_file(out)
                srv['mono'] = bool(mono)"""


def rep(path, old, new):
    with io.open(path, 'r', encoding='utf-8') as f:
        s = f.read()
    c = s.count(old)
    assert c == 1, 'count=%d (期望 1)' % c
    s = s.replace(old, new)
    with io.open(path, 'w', encoding='utf-8') as f:
        f.write(s)
    print('OK rep', path)


rep(TARGET, OLD_A, NEW_A)
rep(TARGET, OLD_B, NEW_B)
print('PATCH_SERVER_MONO_DONE')
