# -*- coding: utf-8 -*-
"""1.9.29 修复：/api/enhance 用到 register_file，但该方法在 LibraryServer 上，
handler 实例 H 没有，必须像 static_dir/files 一样委托到 server 实例。
"""
import io

P = r'D:\Test\workbuddy\2026-09-18-09-00-13\client\lightalbum\core\server.py'

old = """            static_dir = self.static_dir
            token = self.token
            files = self._files"""
new = """            static_dir = self.static_dir
            token = self.token
            files = self._files
            register_file = server.register_file   # 供 /api/enhance 登记产物预览"""

with io.open(P, 'r', encoding='utf-8') as f:
    s = f.read()
assert s.count(old) == 1, 'register delegate 出现 %d 次' % s.count(old)
s = s.replace(old, new, 1)
with io.open(P, 'w', encoding='utf-8') as f:
    f.write(s)
print('patch_server_register: DONE')
