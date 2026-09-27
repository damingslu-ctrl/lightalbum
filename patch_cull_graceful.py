# -*- coding: utf-8 -*-
"""test_cull_route.py：enhance 步骤依赖外部 :8768，离线时改为跳过而非硬失败。"""
import io

TARGET = r'D:\Test\workbuddy\2026-09-18-09-00-13\test_cull_route.py'

OLD = r"""assert code3 == 200 and d3.get('ok') is True, d3
assert d3.get('route') == 'landscape', d3"""

NEW = r"""if d3.get('available') is False:
    print('ENHANCE_SKIP enhance_server :8768 未启动（环境依赖；'
          '增强逻辑由 test_enhance_mono.py 覆盖）')
else:
    assert code3 == 200 and d3.get('ok') is True, d3
    assert d3.get('route') == 'landscape', d3"""


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
print('PATCH_CULL_GRACEFUL_DONE')
