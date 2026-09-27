# -*- coding: utf-8 -*-
"""1.9.30 index.html：增强完成提示加上「已保持黑白」。rep() 断言 count==1。"""
import io

TARGET = r'D:\Test\workbuddy\2026-09-18-09-00-13\index.html'

OLD = r"""      msg.textContent='增强完成（路由：'+(d.route||'')+'）'+(prev?'，已生成对比预览':'');"""

NEW = r"""      msg.textContent='增强完成（路由：'+(d.route||'')+'）'+(d.mono?'，已保持黑白':'')+(prev?'，已生成对比预览':'');"""


def rep(path, old, new):
    with io.open(path, 'r', encoding='utf-8') as f:
        s = f.read()
    c = s.count(old)
    assert c == 1, 'count=%d (expect 1)' % c
    s = s.replace(old, new)
    with io.open(path, 'w', encoding='utf-8') as f:
        f.write(s)
    print('OK rep', path)


rep(TARGET, OLD, NEW)
print('PATCH_INDEX_MONO_DONE')
