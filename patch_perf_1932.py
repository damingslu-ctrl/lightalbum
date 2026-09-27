# -*- coding: utf-8 -*-
"""1.9.32 流畅性优化：
A) app.py   —— hub 构建移出主线程（启动少算一段 import 重模块的时间）
B) index.html —— 去掉 refreshAll 前冗余的 syncFilterUI()（refreshAll 内部已调用）
C) index.html —— 长列表 content-visibility，跳过视口外渲染
所有 rep 带 count 断言。"""
import io

APP = r'D:\Test\workbuddy\2026-09-18-09-00-13\client\lightalbum\app.py'
HTML = r'D:\Test\workbuddy\2026-09-18-09-00-13\index.html'


def rep(path, old, new, expect=1):
    with io.open(path, 'r', encoding='utf-8') as f:
        s = f.read()
    c = s.count(old)
    assert c == expect, 'count=%d (expect %d) for %r' % (c, expect, old[:60])
    s = s.replace(old, new)
    with io.open(path, 'w', encoding='utf-8') as f:
        f.write(s)
    print('OK rep(%d) %s :: %s' % (c, path.rsplit('\\', 1)[-1], old.strip().splitlines()[0][:50]))


# ---------------- A) hub 构建移出主线程 ----------------
OLD_A = r'''            # ADR-0012: mount unified engine hub (defensive; never block startup)
            try:
                from .core.engines.registry import build_default_hub
                self.hub = build_default_hub(self.lib_server.lib)
            except Exception as _hub_e:  # noqa: BLE001
                self.hub = None
                print('[hub] init failed (degraded): %s' % _hub_e)'''

NEW_A = r'''            # ADR-0012: mount unified engine hub。
            # 1.9.32 性能：build_default_hub 要 import 各引擎（重模块），同步构建会
            # 直接算进启动耗时；挪到后台线程，主线程不等。引擎只在用户真正发起任务
            # 时才用到，未就绪/构建失败仍按原逻辑降级为 None，不改变任何语义。
            def _build_hub():
                try:
                    from .core.engines.registry import build_default_hub
                    _hub = build_default_hub(self.lib_server.lib)
                    QTimer.singleShot(0, lambda: setattr(self, 'hub', _hub))
                except Exception as _hub_e:  # noqa: BLE001
                    print('[hub] init failed (degraded): %s' % _hub_e)
                _mark('hub_done')
            threading.Thread(target=_build_hub, name='hub-build', daemon=True).start()'''

rep(APP, OLD_A, NEW_A)

# ---------------- B) 去掉 refreshAll 前冗余 syncFilterUI ----------------
# refreshAll() 内部（约 7380 行）已经调用 syncFilterUI()，这里的前缀是纯重复渲染。
rep(HTML, '  syncFilterUI(); refreshAll();', '  refreshAll();', expect=3)

# ---------------- C) 长列表 content-visibility ----------------
CSS = '''
  /* 1.9.32 性能：长列表跳过视口外渲染 —— 画廊/胶片条项数上千时，滚动与重排
     只算视口内的，显著降 jank。contain-intrinsic-size 给未渲染项一个保守占位
     高度，避免列高塌陷导致滚动条跳动。 */
  .g-item{content-visibility:auto;contain-intrinsic-size:auto 320px}
  #filmstrip .ft{content-visibility:auto;contain-intrinsic-size:auto 84px}
'''

with io.open(HTML, 'r', encoding='utf-8') as f:
    s = f.read()
i_style = s.find('</style>')
i_fabric = s.find('<script id="fabric-lib">')
assert i_style > 0, '未找到 </style>'
assert i_fabric > 0, '未找到 fabric-lib script'
assert i_style < i_fabric, '第一个 </style> 必须在 fabric 之前（避免插进 fabric 源码里的字符串）'
s = s[:i_style] + CSS + s[i_style:]
with io.open(HTML, 'w', encoding='utf-8') as f:
    f.write(s)
print('OK css inserted at %d (before fabric at %d)' % (i_style, i_fabric))

print('PATCH_PERF_1932_DONE')
