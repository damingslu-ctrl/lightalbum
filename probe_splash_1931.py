# -*- coding: utf-8 -*-
"""1.9.31 无头探针：验证「客户端开场期间在后台 → 收拢成线 → 从该线拉起并展开」。

offscreen 下 QWidget.grab() 可同步触发 paintEvent（见记忆 P 节），无需事件循环。
"""
import os, sys

os.environ['QT_QPA_PLATFORM'] = 'offscreen'
ROOT = r'D:\Test\workbuddy\2026-09-18-09-00-13'
sys.path.insert(0, os.path.join(ROOT, 'client'))

from PySide6.QtWidgets import QApplication, QWidget   # noqa: E402
from lightalbum.splash import SplashScreen, LightTransition   # noqa: E402

app = QApplication([])

# ---- ① 开场动画：只有它在屏幕上 ----
sp = SplashScreen(app_name='光影集')
sp.show()
app.processEvents()
line_g = sp.line_global_y()
print('SPLASH y=%s line_local=%s line_global=%s' % (sp.y(), sp._line_y(), line_g))
assert sp.isVisible(), '开场动画应可见'

# ---- ② 客户端：整个开场期间在后台（未显示）----
client = QWidget()
client.resize(1440, 940)
client.setWindowTitle('client-stub')
assert not client.isVisible(), '开场期间客户端必须在后台（未显示）'
print('BEFORE_REVEAL client_visible=%s  (在后台 OK)' % client.isVisible())

# ---- ③ seam 应对齐收拢线 ----
lt = LightTransition(client, seam=line_g)
geo = lt.geometry()
expected = line_g - geo.y()
print('TRANSITION geo=(%s,%s,%sx%s) seam=%s expected=%s'
      % (geo.x(), geo.y(), geo.width(), geo.height(), round(lt._seam, 2), round(expected, 2)))
assert abs(lt._seam - expected) < 0.6, 'seam 未对齐开场收拢线'

w, h = lt.width(), lt.height()


def alpha_at(y):
    img = lt.grab().toImage()
    c = img.pixelColor(w // 2, max(0, min(h - 1, y)))
    return c.alpha()


lt.start()

# ---- ④ HOLD 段（t=0.10 < HOLD=0.15）：暗幕应仍全闭 ----
lt._phase = 0.10
lt.update()
a_top, a_bot = alpha_at(4), alpha_at(h - 4)
print('HOLD t=0.10  top_alpha=%s bottom_alpha=%s' % (a_top, a_bot))
assert a_top > 10 and a_bot > 10, 'HOLD 段暗幕应全闭（给客户端首屏绘制时间）'

# ---- ⑤ 展开末段（t=0.95）：暗幕应已退离 ----
lt._phase = 0.95
lt.update()
# 采样点要避开暗幕退到最后仍残留的边缘几像素：底幕末段只剩 seam+h 附近约 4px
a_top2, a_bot2 = alpha_at(4), alpha_at(h - 20)
print('OPEN t=0.95  top_alpha=%s bottom_alpha=%s' % (a_top2, a_bot2))
assert a_top2 < 10 and a_bot2 < 10, '末段暗幕应已退离，露出客户端'

# ---- ⑥ 从后台拉起客户端（与 app.py _reveal 同序）----
client.show()
client.raise_()
client.activateWindow()
app.processEvents()
assert client.isVisible(), 'reveal 后客户端应已从后台拉起'
print('AFTER_REVEAL client_visible=%s  (从后台拉起 OK)' % client.isVisible())

print('PROBE_SPLASH_1931_OK')
