# -*- coding: utf-8 -*-
"""1.9.29 修复：_last_cull 必须存在 LibraryServer 实例上（跨请求共享），
否则 /api/cull/export 取不到上次选片结果。
"""
import io

P = r'D:\Test\workbuddy\2026-09-18-09-00-13\client\lightalbum\core\server.py'


def rep(old, new):
    with io.open(P, 'r', encoding='utf-8') as f:
        s = f.read()
    c = s.count(old)
    assert c == 1, 'anchor %d 次: %r' % (c, old[:70])
    s = s.replace(old, new, 1)
    with io.open(P, 'w', encoding='utf-8') as f:
        f.write(s)
    print('OK (%d->%d)' % (len(old), len(new)))


# 1) __init__ 初始化 _last_cull
old1 = """        self._files_lock = threading.Lock()
        self.httpd = None"""
new1 = """        self._files_lock = threading.Lock()
        self._last_cull = None          # 上次选片结果（供 /api/cull/export 复用，跨请求共享）
        self.httpd = None"""
rep(old1, new1)

# 2) start() 里 class H 暴露 _last_cull 到 server
old2 = """            @property
            def vault(self):
                return server.vault()"""
new2 = """            @property
            def vault(self):
                return server.vault()

            @property
            def _last_cull(self):
                return server._last_cull
            @_last_cull.setter
            def _last_cull(self, v):
                server._last_cull = v"""
rep(old2, new2)

print('patch_server_lastcull: DONE')
