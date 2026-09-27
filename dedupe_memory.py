# -*- coding: utf-8 -*-
"""清掉 2026-09-26.md 里被重复追加（且已损坏）的 1.9.27 条目。"""
import io

P = r'D:\Test\workbuddy\2026-09-18-09-00-13\.workbuddy\memory\2026-09-26.md'
MARK = '## 灯箱去黑边【1.9.27，已交付】'

with io.open(P, 'r', encoding='utf-8') as f:
    s = f.read()

first = s.find(MARK)
second = s.find(MARK, first + 1)
assert first != -1 and second != -1, '未找到重复条目 first=%d second=%d' % (first, second)

s2 = s[:second].rstrip() + '\n'
with io.open(P, 'w', encoding='utf-8') as f:
    f.write(s2)
print('DEDUPED removed_chars=%d' % (len(s) - len(s2)))
