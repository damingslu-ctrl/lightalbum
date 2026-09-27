# -*- coding: utf-8 -*-
"""无头探针：验证 1.9.28 三段式开场画面能正常绘制（不依赖完整应用启动）。

QT_QPA_PLATFORM=offscreen 下：
  - 构造 SplashScreen，抓 INTRO 帧；
  - begin_exit 后抓 COLLAPSE 中段（横线 + 粒子）；
  - 构造 LightTransition 抓「中线展开」帧。
抓到的 PNG 用 Read 工具肉眼核验，确认不是空白/崩溃。
"""
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'client'))

from PySide6.QtWidgets import QApplication
from PySide6.QtCore import Qt, QTimer
from PySide6.QtGui import QImage

from lightalbum.splash import SplashScreen, LightTransition

OUT = r'D:\Test\workbuddy\2026-09-18-09-00-13\probe_splash_out'
os.makedirs(OUT, exist_ok=True)

app = QApplication(sys.argv)
app.setAttribute(Qt.AA_ShareOpenGLContexts, True)

splash = SplashScreen(app_name='光影集')
splash.show()
app.processEvents()
time.sleep(0.25)
splash.grab().save(os.path.join(OUT, '01_intro.png'))
print('grabbed intro')

flag = {'fired': False}
splash.begin_exit(lambda: flag.__setitem__('fired', True))
time.sleep(0.34)
splash.grab().save(os.path.join(OUT, '02_collapse_mid.png'))
print('grabbed collapse-mid')
time.sleep(0.34)
try:
    splash.grab().save(os.path.join(OUT, '03_collapse_late.png'))
    print('grabbed collapse-late')
except Exception as e:
    print('collapse-late grab skipped:', e)

time.sleep(0.2)
lt = LightTransition(splash)
lt.start()
time.sleep(0.30)
app.processEvents()
lt.grab().save(os.path.join(OUT, '04_reveal.png'))
print('grabbed reveal')
time.sleep(0.2)
app.processEvents()

print('flag_fired=%s' % flag['fired'])
# 强制退出，避免事件循环空转
QTimer.singleShot(50, app.quit)
app.exec()
print('PROBE_DONE')
