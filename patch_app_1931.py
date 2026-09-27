# -*- coding: utf-8 -*-
"""1.9.31：app.py —— 开场期间客户端待在后台（不显示），收拢成线后才从后台拉起。"""
import io

TARGET = r'D:\Test\workbuddy\2026-09-18-09-00-13\client\lightalbum\app.py'

# ---- 1) 启动时不再显示主窗口 ----
OLD_1 = r'''    w = MainWindow()
    _mark('window_built')
    w.show()
    _mark('window_shown')
'''

NEW_1 = r'''    w = MainWindow()
    _mark('window_built')
    # 1.9.31：**这里不显示客户端** —— 整个开场期间它都待在后台（未显示），
    # 屏幕上只有开场动画（440x300 的居中浮层）。等动画收拢成线、要从线上把
    # 客户端展开时，才在 _reveal() 里把它从后台拉起（show + raise + activate），
    # 随即被 LightTransition 的整屏暗幕盖住，再从中线展开 —— 全程看不到
    # 「客户端先出现、动画再盖上去」的反向顺序。
    _mark('window_bg_hidden')
'''

# ---- 2) _reveal：拉起客户端 + 对齐收拢线 ----
OLD_2 = r'''    def _reveal():
        # 1.9.28：splash 已「收拢成线 + 粒子」并隐藏，由 LightTransition 从中线展开客户端。
        LightTransition(w).start()
'''

NEW_2 = r'''    def _reveal():
        # 1.9.31：此刻才把客户端从后台拉起 —— 此前它一直未显示，屏幕上只有开场动画。
        # seam 用 splash 收拢出的那条线的屏幕纵坐标，保证「从那条线展开」严丝合缝。
        w.show()
        w.raise_()
        w.activateWindow()
        app.processEvents()
        LightTransition(w, seam=_splash_state.get('seam')).start()
'''

# ---- 3) _finish_splash：先记下收拢线的屏幕纵坐标 ----
OLD_3 = r'''        # 1.9.28：握手就绪（__libHandshake + 画廊卡片已渲染）后再收拢成线，
        # 结束即从中线展开客户端；15s 硬兜底同样走这条路，绝不挡界面。
        splash.begin_exit(_reveal)
'''

NEW_3 = r'''        # 1.9.28：握手就绪（__libHandshake + 画廊卡片已渲染）后再收拢成线，
        # 结束即从中线展开客户端；15s 硬兜底同样走这条路，绝不挡界面。
        # 1.9.31：先记下 splash 收拢成线时的屏幕纵坐标（此刻 splash 还活着），
        # 供 LightTransition 把 seam 对到同一条线上。
        try:
            _splash_state['seam'] = splash.line_global_y()
        except Exception:
            _splash_state['seam'] = None
        splash.begin_exit(_reveal)
'''


def rep(path, old, new):
    with io.open(path, 'r', encoding='utf-8') as f:
        s = f.read()
    c = s.count(old)
    assert c == 1, 'count=%d (expect 1)' % c
    s = s.replace(old, new)
    with io.open(path, 'w', encoding='utf-8') as f:
        f.write(s)
    print('OK rep', old.splitlines()[0][:48])


rep(TARGET, OLD_1, NEW_1)
rep(TARGET, OLD_2, NEW_2)
rep(TARGET, OLD_3, NEW_3)
print('PATCH_APP_1931_DONE')
