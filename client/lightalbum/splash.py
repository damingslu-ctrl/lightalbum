"""LightAlbum 开机开场动画 + 与客户端之间的「光效衔接层」。

设计要点
--------
* 原生 PySide6 控件（不是 QWebEngine 页面），所以它能在「重浏览器引擎还没起来」
  之前就画出来，真正盖住 MainWindow.__init__ 里同步启动本地库服务 / 分类 /
  修图子进程、以及随后 QWebEngine 渲染首屏这段最卡的时间。
* 与 LightAlbum 桌面视觉一致：深炭灰底 (#11161f→#0a0e15) + 翡翠绿辉光。
* **纯动效、零文案**：只保留品牌字标，不放任何说明/备注文字。
* **三段式开场（1.9.28）**：
    ① INTRO  —— 呼吸光环 + 旋转弧 + 轨道光点 + 镜头镜片呼吸 + 字标扫光，
               一直播到「客户端就绪握手」完成；
    ② COLLAPSE —— 就绪后，整组动效压扁、向中心收拢成一条发光横线，
               横线两侧迸发粒子光效（确认「该亮起来了」的瞬间）；
    ③ 回调    —— COLLAPSE 结束即调用 on_ready（由 app.py 接管为 LightTransition
               的「从中线展开客户端」揭示层），并隐藏 splash。
  这样「开场动画结束前」系统已先确认初始化完成 + 照片加载好，且 COLLAPSE 这
  段持续渲染让客户端首屏已经铺好，揭示出来绝不卡顿、绝不空画廊。
* 纯 PySide6 + 标准库，零额外依赖，不影响启动期懒加载策略。
"""

import math
import random
import time

from PySide6.QtCore import Qt, QTimer, QRect, QRectF, QPointF, QPropertyAnimation, QEasingCurve
from PySide6.QtGui import (
    QPainter, QColor, QPen, QBrush, QLinearGradient, QRadialGradient,
    QPainterPath, QFont,
)
from PySide6.QtWidgets import QApplication, QWidget


# 主题色（与 index.html 深色玻璃拟态一致：charcoal + emerald）
_BG_TOP = QColor('#11161f')
_BG_BOTTOM = QColor('#0a0e15')
_EMERALD = QColor('#34d399')
_EMERALD_DIM = QColor(16, 185, 129, 120)
_TXT = QColor('#eef2f7')
_TXT_BASE = QColor(150, 170, 188)
_BORDER = QColor(16, 185, 129, 70)

# 相位
_PH_INTRO = 0
_PH_COLLAPSE = 1
_PH_DONE = 2

_COLLAPSE_MS = 760          # 压扁成线 + 粒子这一段时长
_LINE_Y_RATIO = 0.40        # 横线在控件内的纵向位置（与 INTRO 环心一致）


class SplashScreen(QWidget):
    """开场动画控件。构造即显示，调用 begin_exit() 触发「收拢成线 → 回调」。

    画面只有品牌字标 —— 没有任何说明文字（用户明确要求）。
    """

    def __init__(self, app_name='LightAlbum'):
        super().__init__()
        self._app_name = app_name
        self._phase = _PH_INTRO
        self._t0 = time.perf_counter()
        self._last = self._t0
        self._exit_t0 = None
        self._on_exit = None
        self._min_ms = 900         # 最小展示时长，避免一闪而过
        self._particles = []

        self.setWindowFlags(
            Qt.SplashScreen | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
        )
        self.setAttribute(Qt.WA_TranslucentBackground, True)
        self.setFixedSize(440, 300)
        self._center_on_primary()

        # 动画心跳：~60fps。相位由真实时间推算，掉帧也不会走慢/走快。
        self._anim = QTimer(self)
        self._anim.setInterval(16)
        self._anim.timeout.connect(self.update)
        self._anim.start()

    # ---------------------------------------------------------- 外部接口
    def begin_exit(self, on_ready):
        """握手就绪后调用：收拢成线 + 粒子，结束后 on_ready() 并隐藏。"""
        if self._phase != _PH_INTRO:
            return
        self._phase = _PH_COLLAPSE
        self._exit_t0 = time.perf_counter()
        self._on_exit = on_ready
        self._init_line_particles()

    # ---------------------------------------------------------------- 布局
    def _center_on_primary(self):
        screen = self.screen() or QApplication.primaryScreen()
        geo = screen.availableGeometry() if screen else QRect(0, 0, 1280, 800)
        x = geo.x() + (geo.width() - self.width()) // 2
        y = geo.y() + (geo.height() - self.height()) // 2
        self.move(x, y)

    def _elapsed(self):
        return time.perf_counter() - self._t0

    def _line_y(self):
        return self.height() * _LINE_Y_RATIO

    def line_global_y(self):
        """收拢出的那条线在**屏幕**上的纵坐标。

        LightTransition 是整屏覆盖层，它自己的坐标原点在可用区顶部，
        所以这里给的是全局值，由 LightTransition 自行换算成本地 seam。
        """
        return self.y() + self._line_y()

    # ---------------------------------------------------------- 粒子系统
    def _init_line_particles(self):
        cx, cy = self.width() / 2, self._line_y()
        self._particles = []
        for _ in range(30):
            ang = random.uniform(-0.42, 0.42)           # 基本沿水平方向
            spd = random.uniform(26, 110)
            self._particles.append({
                'x': cx + random.uniform(-50, 50),
                'y': cy + random.uniform(-2.5, 2.5),
                'vx': math.cos(ang) * spd * (1 if random.random() < 0.5 else -1),
                'vy': math.sin(ang) * spd * 0.45,
                'life': 1.0,
                'fade': random.uniform(0.7, 1.5),
                'size': random.uniform(1.0, 2.8),
                'warm': random.random() < 0.35,          # 少量暖白粒子
            })

    def _step_particles(self, dt):
        for p in self._particles:
            p['x'] += p['vx'] * dt
            p['y'] += p['vy'] * dt
            p['life'] -= dt * p['fade']
        self._particles = [p for p in self._particles if p['life'] > 0]

    # ---------------------------------------------------------------- 绘制
    def paintEvent(self, _ev):
        now = time.perf_counter()
        dt = min(0.05, now - self._last)
        self._last = now

        if self._phase == _PH_INTRO:
            self._paint_intro(now - self._t0)
            return

        if self._phase == _PH_COLLAPSE:
            self._step_particles(dt)
            self._paint_collapse()
            el = (now - self._exit_t0) * 1000
            if el >= _COLLAPSE_MS:
                self._phase = _PH_DONE
                self._anim.stop()
                cb = self._on_exit
                self._on_exit = None
                QTimer.singleShot(0, lambda: self._fire(cb))
            return

    def _fire(self, cb):
        try:
            if cb:
                cb()
        finally:
            self.hide()
            self.deleteLater()

    # ---- ① INTRO：呼吸光环 + 旋转弧 + 轨道光点 + 镜片 + 字标扫光
    def _paint_intro(self, t):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        w, h = self.width(), self.height()
        radius = 18
        cx, cy = w / 2, h * _LINE_Y_RATIO
        R = 34

        # ---- 背景（圆角裁剪）
        path = QPainterPath()
        path.addRoundedRect(QRectF(0, 0, w, h), radius, radius)
        p.setClipPath(path)

        bg = QLinearGradient(0, 0, 0, h)
        bg.setColorAt(0, _BG_TOP)
        bg.setColorAt(1, _BG_BOTTOM)
        p.fillRect(self.rect(), bg)

        # 整体翡翠辉光（随呼吸轻微起伏）
        breath = 0.5 + 0.5 * math.sin(t * 1.6)
        glow = QRadialGradient(cx, cy, h * (0.58 + 0.06 * breath))
        glow.setColorAt(0, QColor(16, 185, 129, 40 + int(22 * breath)))
        glow.setColorAt(1, QColor(16, 185, 129, 0))
        p.fillRect(self.rect(), glow)

        # ---- ① 呼吸光环：三圈错峰向外扩散并淡出
        for i in range(3):
            ph = ((t * 0.55) + i / 3.0) % 1.0
            rr = R + 6 + ph * 48
            a = int(95 * (1.0 - ph) ** 1.5)
            if a <= 2:
                continue
            pen = QPen(QColor(52, 211, 153, a))
            pen.setWidth(max(1, int(3 * (1.0 - ph)) + 1))
            p.setPen(pen)
            p.setBrush(Qt.NoBrush)
            p.drawEllipse(QPointF(cx, cy), rr, rr)

        # ---- ② 静态外环
        pen = QPen(_EMERALD_DIM)
        pen.setWidth(3)
        p.setPen(pen)
        p.setBrush(Qt.NoBrush)
        p.drawEllipse(QPointF(cx, cy), R, R)

        # ---- ③ 旋转外弧（270°）+ ④ 反向内弧（130°）
        pen2 = QPen(_EMERALD)
        pen2.setWidth(4)
        pen2.setCapStyle(Qt.RoundCap)
        p.setPen(pen2)
        p.drawArc(QRectF(cx - R, cy - R, 2 * R, 2 * R),
                  int((t * 150.0) % 360.0 * 16), 270 * 16)

        inner_pen = QPen(QColor(52, 211, 153, 150))
        inner_pen.setWidth(2)
        inner_pen.setCapStyle(Qt.RoundCap)
        p.setPen(inner_pen)
        p.drawArc(QRectF(cx - R * 0.62, cy - R * 0.62, 2 * R * 0.62, 2 * R * 0.62),
                  int((-t * 220.0) % 360.0 * 16), 130 * 16)

        # ---- ⑤ 轨道光点（沿外环公转，带辉光）
        ang = math.radians(t * 150.0 - 90.0)
        ox, oy = cx + math.cos(ang) * R, cy + math.sin(ang) * R
        dot = QRadialGradient(ox, oy, 9)
        dot.setColorAt(0, QColor(230, 255, 244, 235))
        dot.setColorAt(0.4, QColor(52, 211, 153, 120))
        dot.setColorAt(1, QColor(52, 211, 153, 0))
        p.setBrush(dot)
        p.setPen(Qt.NoPen)
        p.drawEllipse(QPointF(ox, oy), 9, 9)

        # ---- ⑥ 镜片：玻璃渐变 + 呼吸内辉光 + 高光
        lens = QRadialGradient(cx - R * 0.3, cy - R * 0.35, R * 1.15)
        lens.setColorAt(0, QColor(255, 255, 255, 55 + int(30 * breath)))
        lens.setColorAt(0.55, QColor(20, 42, 56, 210))
        lens.setColorAt(1, QColor(8, 14, 20, 240))
        p.setBrush(lens)
        p.drawEllipse(QPointF(cx, cy), R - 5, R - 5)

        hl = QRadialGradient(cx - R * 0.34, cy - R * 0.36, R * 0.5)
        hl.setColorAt(0, QColor(255, 255, 255, 95))
        hl.setColorAt(1, QColor(255, 255, 255, 0))
        p.setBrush(hl)
        p.drawEllipse(QPointF(cx - R * 0.34, cy - R * 0.36), R * 0.3, R * 0.3)

        # ---- ⑦ 品牌字标：一道光周期性扫过（无任何其他文案）
        ty = int(cy + R + 26)
        f = QFont('Segoe UI', 22)
        f.setBold(True)
        p.setFont(f)
        p.setPen(_TXT_BASE)
        p.drawText(QRect(0, ty, w, 34), Qt.AlignHCenter, self._app_name)

        sweep = ((t * 0.55) % 1.6) / 1.6          # 0→1 循环，含停顿
        if sweep <= 1.0:
            gx = -0.35 + sweep * 1.7              # 光带中心（相对字标宽度比例）
            grad = QLinearGradient(w * (gx - 0.22), 0, w * (gx + 0.22), 0)
            grad.setColorAt(0, QColor(238, 242, 247, 0))
            grad.setColorAt(0.5, QColor(255, 255, 255, 235))
            grad.setColorAt(1, QColor(238, 242, 247, 0))
            pen_t = QPen()
            pen_t.setBrush(QBrush(grad))
            p.setPen(pen_t)
            p.drawText(QRect(0, ty, w, 34), Qt.AlignHCenter, self._app_name)

        # ---- 圆角描边
        p.setClipping(False)
        stroke = QPainterPath()
        stroke.addRoundedRect(QRectF(0.5, 0.5, w - 1, h - 1), radius, radius)
        p.setPen(QPen(_BORDER, 1))
        p.setBrush(Qt.NoBrush)
        p.drawPath(stroke)

    # ---- ② COLLAPSE：压扁成横线 + 粒子光效
    def _paint_collapse(self):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        w, h = self.width(), self.height()
        radius = 18
        cx, cy = w / 2, self._line_y()
        R = 34
        el = (time.perf_counter() - self._exit_t0) * 1000
        prog = min(1.0, el / _COLLAPSE_MS)          # 0→1
        ease = prog * prog * (3 - 2 * prog)          # smoothstep

        # ---- 背景（圆角裁剪）
        path = QPainterPath()
        path.addRoundedRect(QRectF(0, 0, w, h), radius, radius)
        p.setClipPath(path)

        bg = QLinearGradient(0, 0, 0, h)
        bg.setColorAt(0, _BG_TOP)
        bg.setColorAt(1, _BG_BOTTOM)
        p.fillRect(self.rect(), bg)

        # 背景整体辉光（随收拢渐隐）
        breath = 0.5 + 0.5 * math.sin(time.perf_counter() * 1.6)
        g = QRadialGradient(cx, cy, h * 0.6)
        g.setColorAt(0, QColor(16, 185, 129, int((40 + 22 * breath) * (1 - ease))))
        g.setColorAt(1, QColor(16, 185, 129, 0))
        p.fillRect(self.rect(), g)

        # 环 / 弧向中心收拢、纵向压扁成线
        a_ring = int(255 * (1 - ease))
        if a_ring > 4:
            rx = R * (1 - 0.12 * ease)
            ry = R * (1 - ease)                      # 压扁 → 横线
            # 外环压扁
            pen = QPen(QColor(52, 211, 153, a_ring))
            pen.setWidth(3)
            p.setPen(pen)
            p.setBrush(Qt.NoBrush)
            p.drawEllipse(QPointF(cx, cy), rx, max(0.5, ry))
            # 旋转外弧压扁
            pen2 = QPen(QColor(52, 211, 153, int(a_ring * 0.95)))
            pen2.setWidth(4)
            pen2.setCapStyle(Qt.RoundCap)
            p.setPen(pen2)
            p.drawArc(QRectF(cx - rx, cy - ry, 2 * rx, 2 * ry),
                      int((time.perf_counter() * 150.0) % 360.0 * 16), 270 * 16)
            # 轨道光点收到线上
            ang = math.radians(time.perf_counter() * 150.0 - 90.0)
            ox, oy = cx + math.cos(ang) * rx, cy + math.sin(ang) * ry
            dot = QRadialGradient(ox, oy, 9)
            dot.setColorAt(0, QColor(230, 255, 244, int(235 * (1 - ease))))
            dot.setColorAt(0.4, QColor(52, 211, 153, int(120 * (1 - ease))))
            dot.setColorAt(1, QColor(52, 211, 153, 0))
            p.setBrush(dot)
            p.setPen(Qt.NoPen)
            p.drawEllipse(QPointF(ox, oy), 9, 9)

        # 字标快速淡出
        a_txt = int(255 * (1 - ease * ease))
        if a_txt > 3:
            ty = int(cy + R + 26)
            f = QFont('Segoe UI', 22)
            f.setBold(True)
            p.setFont(f)
            p.setPen(QColor(150, 170, 188, a_txt))
            p.drawText(QRect(0, ty, w, 34), Qt.AlignHCenter, self._app_name)

        # ---- 中线：发光线（前段成形、中段最亮、末段淡出交给 LightTransition）
        line_w = 120 + (w * 0.82 - 120) * ease
        bloom = math.sin(math.pi * min(1.0, prog * 1.1))   # 中段最亮
        ly = cy
        # 柔光带
        band = QLinearGradient(0, ly - 26, 0, ly + 26)
        band.setColorAt(0.0, QColor(52, 211, 153, 0))
        band.setColorAt(0.5, QColor(120, 255, 210, int(120 * bloom)))
        band.setColorAt(1.0, QColor(52, 211, 153, 0))
        p.fillRect(QRectF(cx - line_w / 2, ly - 26, line_w, 52), band)
        # 实线芯
        core = QLinearGradient(cx - line_w / 2, 0, cx + line_w / 2, 0)
        core.setColorAt(0.0, QColor(52, 211, 153, 0))
        core.setColorAt(0.5, QColor(240, 255, 248, int(230 * (0.3 + 0.7 * bloom))))
        core.setColorAt(1.0, QColor(52, 211, 153, 0))
        p.setPen(QPen(QBrush(core), 2.2))
        p.drawLine(int(cx - line_w / 2), int(ly), int(cx + line_w / 2), int(ly))

        # ---- 粒子光效（沿横线两侧迸发、横向漂散后淡出）
        for pt in self._particles:
            la = max(0.0, pt['life'])
            col = QColor(255, 246, 224, int(220 * la)) if pt['warm'] else QColor(120, 255, 210, int(220 * la))
            r = max(0.5, pt['size'] * (0.5 + 0.5 * la))
            pg = QRadialGradient(pt['x'], pt['y'], r * 2.4)
            pg.setColorAt(0, col)
            pg.setColorAt(1, QColor(col.red(), col.green(), col.blue(), 0))
            p.setBrush(pg)
            p.setPen(Qt.NoPen)
            p.drawEllipse(QPointF(pt['x'], pt['y']), r * 2.4, r * 2.4)

        # ---- 圆角描边
        p.setClipping(False)
        stroke = QPainterPath()
        stroke.addRoundedRect(QRectF(0.5, 0.5, w - 1, h - 1), radius, radius)
        p.setPen(QPen(_BORDER, 1))
        p.setBrush(Qt.NoBrush)
        p.drawPath(stroke)


class LightTransition(QWidget):
    """开场「横线」与客户端之间的光效衔接层（1.9.28：从中线展开客户端）。

    客户端窗口此时已经显示，但被这层轻纱盖住。动画分三层叠加：
      ① 两片暗幕从屏幕竖直中线（seam）向上下退离 —— 客户端像从中线「拉开」；
      ② 中线辉光 bloom：开场那条线的光在中线最亮、随后收敛淡出；
      ③ 粒子光效：自中线沿水平方向漂散、淡出，呼应 splash 的粒子。
    结束时暗幕完全退离、整体透明，客户端从中线被「展开」出来，而非硬切。

    鼠标事件完全穿透，不影响用户立刻操作。
    """

    DURATION = 0.80          # 秒
    HOLD = 0.15              # 前 15%（约 120ms）暗幕保持全闭：客户端刚从后台被拉起，
                             # 留一两帧让它完成首屏绘制，避免暗幕一开就露出白底。

    def __init__(self, target=None, seam=None):
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

        self._anim = QTimer(self)
        self._anim.setInterval(16)
        self._anim.timeout.connect(self._tick)

    def start(self):
        self._t0 = time.perf_counter()
        self._init_particles()
        self.show()
        self.update()
        self._anim.start()

    def _init_particles(self):
        w, h = self.width(), self.height()
        cx, cy = w / 2, self._seam
        self._particles = []
        for _ in range(46):
            ang = random.uniform(-0.32, 0.32)
            spd = random.uniform(80, 320)
            self._particles.append({
                'x': cx + random.uniform(-w * 0.30, w * 0.30),
                'y': cy + random.uniform(-6, 6),
                'vx': math.cos(ang) * spd * (1 if random.random() < 0.5 else -1),
                'vy': math.sin(ang) * spd * 0.5,
                'life': 1.0,
                'fade': random.uniform(0.8, 1.7),
                'size': random.uniform(1.0, 3.0),
                'warm': random.random() < 0.3,
            })

    def _step_particles(self, dt):
        for p in self._particles:
            p['x'] += p['vx'] * dt
            p['y'] += p['vy'] * dt
            p['life'] -= dt * p['fade']
        self._particles = [pp for pp in self._particles if pp['life'] > 0]

    def _tick(self):
        if self._t0 is None:
            return
        now = time.perf_counter()
        dt = min(0.05, now - (self._last if hasattr(self, '_last') else now))
        self._last = now
        self._step_particles(dt)
        self._phase = (now - self._t0) / self.DURATION
        if self._phase >= 1.0:
            self._anim.stop()
            self._close()
            return
        self.update()

    def paintEvent(self, _ev):
        t = max(0.0, min(1.0, self._phase))
        # 1.9.31：起手 HOLD 段内暗幕保持全闭，客户端在幕后完成首屏绘制。
        tr = max(0.0, (t - self.HOLD) / max(1e-6, (1.0 - self.HOLD)))
        te = tr * tr * (3 - 2 * tr)           # 暗幕退离缓动
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        w, h = self.width(), self.height()
        seam = self._seam

        # ① 两片暗幕从中线向上下退离（盖住客户端；退离即揭示）
        top_h = seam * (1 - te)
        bot_top = seam + (h - seam) * te
        bot_h = (h - seam) * (1 - te)
        if top_h > 0.5:
            g = QLinearGradient(0, 0, 0, seam)
            g.setColorAt(0, _BG_TOP)
            g.setColorAt(1, _BG_BOTTOM)
            p.fillRect(QRectF(0, 0, w, top_h), g)
        if bot_h > 0.5:
            g = QLinearGradient(0, seam, 0, h)
            g.setColorAt(0, _BG_BOTTOM)
            g.setColorAt(1, _BG_TOP)
            p.fillRect(QRectF(0, bot_top, w, bot_h), g)

        # ② 中线辉光 bloom（开场那条线的光，中段最亮、随后收敛）
        bloom = math.sin(math.pi * min(1.0, t * 1.15))
        if bloom > 0.01:
            band = QLinearGradient(0, seam - 40, 0, seam + 40)
            band.setColorAt(0.0, QColor(52, 211, 153, 0))
            band.setColorAt(0.5, QColor(150, 255, 220, int(150 * bloom)))
            band.setColorAt(1.0, QColor(52, 211, 153, 0))
            p.fillRect(QRectF(0, seam - 40, w, 80), band)
            core = QLinearGradient(0, 0, w, 0)
            core.setColorAt(0.0, QColor(52, 211, 153, 0))
            core.setColorAt(0.5, QColor(240, 255, 248, int(235 * (0.3 + 0.7 * bloom))))
            core.setColorAt(1.0, QColor(52, 211, 153, 0))
            p.setPen(QPen(QBrush(core), 2.4))
            p.drawLine(0, int(seam), w, int(seam))
            # 中心一束更亮的涌光
            r = (0.1 + 0.5 * t) * min(w, h) * 0.5
            rg = QRadialGradient(w / 2, seam, r)
            rg.setColorAt(0.0, QColor(255, 255, 255, int(120 * bloom)))
            rg.setColorAt(0.4, QColor(52, 211, 153, int(70 * bloom)))
            rg.setColorAt(1.0, QColor(52, 211, 153, 0))
            p.fillRect(self.rect(), rg)

        # ③ 粒子光效：自中线沿水平方向漂散、淡出
        for pt in self._particles:
            la = max(0.0, pt['life'])
            col = QColor(255, 246, 224, int(220 * la)) if pt['warm'] else QColor(120, 255, 210, int(220 * la))
            r = max(0.5, pt['size'] * (0.5 + 0.5 * la))
            pg = QRadialGradient(pt['x'], pt['y'], r * 2.4)
            pg.setColorAt(0, col)
            pg.setColorAt(1, QColor(col.red(), col.green(), col.blue(), 0))
            p.setBrush(pg)
            p.setPen(Qt.NoPen)
            p.drawEllipse(QPointF(pt['x'], pt['y']), r * 2.4, r * 2.4)

    def _close(self):
        self.hide()
        self.deleteLater()
