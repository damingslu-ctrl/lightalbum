# -*- coding: utf-8 -*-
"""1.9.31：splash.py —— ① 暴露收拢线的屏幕纵坐标 ② 暗幕 seam 可对齐该线
③ 起手留一段「暗幕全闭」给刚从后台拉起的客户端完成首屏绘制。"""
import io

TARGET = r'D:\Test\workbuddy\2026-09-18-09-00-13\client\lightalbum\splash.py'

# ---- 1) 收拢线的屏幕纵坐标 ----
OLD_1 = r'''    def _elapsed(self):
        return time.perf_counter() - self._t0

    def _line_y(self):
        return self.height() * _LINE_Y_RATIO
'''

NEW_1 = r'''    def _elapsed(self):
        return time.perf_counter() - self._t0

    def _line_y(self):
        return self.height() * _LINE_Y_RATIO

    def line_global_y(self):
        """收拢出的那条线在**屏幕**上的纵坐标。

        LightTransition 是整屏覆盖层，它自己的坐标原点在可用区顶部，
        所以这里给的是全局值，由 LightTransition 自行换算成本地 seam。
        """
        return self.y() + self._line_y()
'''

# ---- 2) HOLD 常量 ----
OLD_2 = r'''    DURATION = 0.80          # 秒
'''

NEW_2 = r'''    DURATION = 0.80          # 秒
    HOLD = 0.15              # 前 15%（约 120ms）暗幕保持全闭：客户端刚从后台被拉起，
                             # 留一两帧让它完成首屏绘制，避免暗幕一开就露出白底。
'''

# ---- 3) seam 参数 ----
OLD_3 = r'''    def __init__(self, target=None):
        super().__init__()
        self._phase = 0.0
        self._t0 = None
        self.setAttribute(Qt.WA_TranslucentBackground, True)
        self.setAttribute(Qt.WA_TransparentForMouseEvents, True)
        self.setWindowFlags(
            Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool
        )
        screen = None
        if target is not None:
            try:
                screen = target.screen()
            except Exception:
                screen = None
        screen = screen or QApplication.primaryScreen()
        geo = screen.availableGeometry() if screen else QRect(0, 0, 1280, 800)
        self.setGeometry(geo)
        self._seam = geo.height() * 0.5
        self._particles = []
'''

NEW_3 = r'''    def __init__(self, target=None, seam=None):
        """target: 客户端主窗口；seam: 开场收拢线的**屏幕**纵坐标（None 则取屏幕中线）。"""
        super().__init__()
        self._phase = 0.0
        self._t0 = None
        self.setAttribute(Qt.WA_TranslucentBackground, True)
        self.setAttribute(Qt.WA_TransparentForMouseEvents, True)
        self.setWindowFlags(
            Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool
        )
        screen = None
        if target is not None:
            try:
                screen = target.screen()
            except Exception:
                screen = None
        screen = screen or QApplication.primaryScreen()
        geo = screen.availableGeometry() if screen else QRect(0, 0, 1280, 800)
        self.setGeometry(geo)
        if seam is None:
            self._seam = geo.height() * 0.5
        else:
            # 全局纵坐标 → 本覆盖层本地坐标（覆盖层原点在 geo.y()）
            self._seam = max(0.0, min(float(geo.height()),
                                      float(seam) - float(geo.y())))
        self._particles = []
'''

# ---- 4) 暗幕退离加 HOLD ----
OLD_4 = r'''    def paintEvent(self, _ev):
        t = max(0.0, min(1.0, self._phase))
        te = t * t * (3 - 2 * t)              # 暗幕退离缓动
'''

NEW_4 = r'''    def paintEvent(self, _ev):
        t = max(0.0, min(1.0, self._phase))
        # 1.9.31：起手 HOLD 段内暗幕保持全闭，客户端在幕后完成首屏绘制。
        tr = max(0.0, (t - self.HOLD) / max(1e-6, (1.0 - self.HOLD)))
        te = tr * tr * (3 - 2 * tr)           # 暗幕退离缓动
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
rep(TARGET, OLD_4, NEW_4)
print('PATCH_SPLASH_1931_DONE')
