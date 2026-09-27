# -*- coding: utf-8 -*-
"""1.9.29 修复：enhance_server 成功响应不含 'ok' 键，且产物键可能是
out_path 而非 output。服务端归一化：成功置 ok=True 并补 preview_url，
否则原样透传（前端按 detail/error 提示）。否则前端会把成功当失败。
"""
import io

P = r'D:\Test\workbuddy\2026-09-18-09-00-13\client\lightalbum\core\server.py'

old = """                srv.setdefault('route', r)
                if srv.get('ok'):
                    out = srv.get('output') or ''
                    if out and os.path.isfile(out):
                        srv['preview_url'] = '/api/file/%s' % self.register_file(out)
                self._send(200, srv)"""
new = """                srv.setdefault('route', r)
                _err = srv.get('error') or srv.get('detail')
                if not _err:
                    srv['ok'] = True
                    out = srv.get('output') or srv.get('out_path') or ''
                    if out and os.path.isfile(out):
                        srv['preview_url'] = '/api/file/%s' % self.register_file(out)
                self._send(200, srv)"""

with io.open(P, 'r', encoding='utf-8') as f:
    s = f.read()
assert s.count(old) == 1, 'enhance ok-branch 出现 %d 次' % s.count(old)
s = s.replace(old, new, 1)
with io.open(P, 'w', encoding='utf-8') as f:
    f.write(s)
print('patch_server_enhance_ok: DONE')
