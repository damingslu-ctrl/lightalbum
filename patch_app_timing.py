# -*- coding: utf-8 -*-
"""给 MainWindow.__init__ 加细粒度启动计时标记，用于定位 window_built 那 1.6s 花在哪。
仅在 LIGHTALBUM_TIMING=1 时生效（_mark 内部已 gate）。"""
import io

TARGET = r'D:\Test\workbuddy\2026-09-18-09-00-13\client\lightalbum\app.py'

PAIRS = [
    # 本地库服务（DB + 服务 + hub）
    (r'''        self._start_library()
        # 本地 CLIP 分类服务随客户端自启（端口 8766，外部 venv 进程）。''',
     r'''        _mark('lib_before')
        self._start_library()
        _mark('lib_done')
        # 本地 CLIP 分类服务随客户端自启（端口 8766，外部 venv 进程）。'''),
    # AI 服务线程已 spawn（非阻塞，只记 spawn 开销）
    (r'''        self._start_face()
        # 页面加载完成后再补推一次库信息（首次推送时页面可能还在解析）''',
     r'''        self._start_face()
        _mark('ai_spawned')
        # 页面加载完成后再补推一次库信息（首次推送时页面可能还在解析）'''),
    # 菜单 / 状态栏
    (r'''        self._build_menu()
        self._build_status()
''',
     r'''        self._build_menu()
        self._build_status()
        _mark('ui_built')
'''),
    # 桌面集成 + 起 URL
    (r'''            self.desktop_init()      # 托盘 / 拖拽 / 几何记忆 / 任务栏进度（可用 LIGHTALBUM_NO_DESKTOP=1 跳过）
        self.view.setUrl(page_url)
''',
     r'''            self.desktop_init()      # 托盘 / 拖拽 / 几何记忆 / 任务栏进度（可用 LIGHTALBUM_NO_DESKTOP=1 跳过）
        _mark('desktop_done')
        self.view.setUrl(page_url)
        _mark('url_set')
'''),
]


def rep(path, old, new):
    with io.open(path, 'r', encoding='utf-8') as f:
        s = f.read()
    c = s.count(old)
    assert c == 1, 'count=%d (expect 1) for %r' % (c, old[:40])
    s = s.replace(old, new)
    with io.open(path, 'w', encoding='utf-8') as f:
        f.write(s)
    print('OK rep', old.strip().splitlines()[0][:44])


for old, new in PAIRS:
    rep(TARGET, old, new)
print('PATCH_APP_TIMING_DONE')
