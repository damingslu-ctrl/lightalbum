# -*- coding: utf-8 -*-
import io

p = r'D:\Test\workbuddy\2026-09-18-09-00-13\.workbuddy\memory\2026-09-26.md'
s = io.open(p, encoding='utf-8').read()

entry = u'''
## 三段式开场：握手就绪 → 收拢成线+粒子 → 从中线展开客户端【1.9.28，已交付】
- **用户澄清（此前只做了淡出+径向辉光，不是用户要的）**：开场动画结束前先确认
  「客户端初始化完、不卡顿、照片已加载」→ 然后动画**缩成一条线 + 粒子光效**
  → 再**从那条线展开客户端**。
- **握手（沿用并强化）**：app.py 轮询 READY_JS（`__libHandshake===true` 且
  （库空 或 `.g-item>0`））才触发收拢；COLLAPSE ~760ms 期间客户端持续渲染首屏，
  揭示出来不卡顿、不空画廊。15s 硬兜底也走同一路径。
- **splash.py 重写为相位机**：`_PH_INTRO`（原动效：呼吸环/旋转弧/轨道点/镜片/字标扫光）
  → `begin_exit(on_ready)` 进 `_PH_COLLAPSE`（环/弧向中心收拢且**纵向压扁成横线**、
  字标快速淡出、中线辉光 bloom、30 颗粒子沿线迸发横向漂散）→ 计时结束
  `QTimer.singleShot(0)` 回调 on_ready 并 hide/deleteLater。
- **LightTransition 重写为「从中线展开客户端」**：不再中心径向辉光，改为
  ① 两片暗幕（splash 底色）从屏幕竖直中线 seam 向上下退离（top 高度 seam*(1-te)、
  bottom 顶边 seam+(h-seam)*te）→ 客户端像从中线被拉开；
  ② 中线辉光 bloom（band + 实线芯 + 中心涌光）；
  ③ 46 颗粒子沿 seam 水平漂散。`_step_particles` 挂在 `_tick`（含 dt，暂停不掉帧）。
- **app.py 接线**：`_finish_splash()` 改调 `splash.begin_exit(_reveal)`；
  `_reveal()` = `LightTransition(w).start()`（splash 在 COLLAPSE 完成回调里自隐藏，
  LightTransition t=0 全屏暗幕+中线，与 splash 线无缝衔接，无闪帧）。
- **验证（无头探针 probe_splash.py，offscreen 抓帧）**：INTRO 环+镜片 OK；
  COLLAPSE 中段=压扁环+横线+粒子、后段=纯发光线+粒子；REVEAL=全宽中线辉光+
  粒子+上下暗幕；`flag_fired=True` 证明收拢完成回调链通。字标在探针里是方框
  （offscreen 缺 CJK 字体伪影，真机 Qt 回退中文字体，非 bug）。
- **构建**：py_compile OK；版本 1.9.27→1.9.28；BUILD_OK →
  `client/dist/LightAlbum-1.9.28-win64/LightAlbum/LightAlbum.exe`；
  Analysis-00.toc 含 `lightalbum.splash`；splash.py 5 处新标记、app.py 1 处 begin_exit。
- **注意**：offscreen 下 QWidget.grab() 可同步触发 paintEvent（无需事件循环跑 timer），
  是验证原生开场动画的有效手段（比 QWebEngine DOM 探针更直接）。
'''

assert u'## 灯箱去黑边【1.9.27' in s, '1.9.27 anchor missing'
s = s.rstrip() + u'\n' + entry
io.open(p, 'w', encoding='utf-8').write(s)
print('1.9.28 memory entry appended')
