# -*- coding: utf-8 -*-
"""把 1.9.31（开场动画：客户端放后台 → 收拢成线 → 从线上拉起展开）写入今日日志。"""
import io, os

D = r'D:\Test\workbuddy\2026-09-18-09-00-13\.workbuddy\memory'
P = os.path.join(D, '2026-09-27.md')

ENTRY = u'''
## 开场动画真正顺序：动画在前 → 客户端后台 → 收拢成线 → 从线上拉起【1.9.31，已交付】
- **用户第三次澄清**：「先打开动画，**客户端放后台**，然后开场动画闭合成一条线，
  再**从线上展开客户端**，那时候**客户端从后台拉起**」。
- **为什么前两版「没理解」（根因，很关键）**：`app.py` 启动处就有 `w.show()`
  （原 1744 行）——**1440×940 的客户端窗口一开始就在屏幕上**；而 `SplashScreen`
  只是 **440×300 居中半透明浮层**。所以整个开场期间客户端**一直完整可见**，
  动画只是浮在它上面，「从中线展开」根本没东西可展开（客户端早就在那儿了）。
  此前只改了动画形态（淡出→径向→收拢成线），没动**窗口显示时机**，所以一直不对。
- **1.9.31 改法（顺序彻底反过来）**：
  1. 启动处**删掉 `w.show()`** → 客户端整个开场期间**未显示**（真·后台，任务栏也不出现）。
  2. `_finish_splash()` 里先 `splash.line_global_y()` 记下**收拢线的屏幕纵坐标**
     （此刻 splash 还活着，避免 deleteLater 后取不到）。
  3. `_reveal()` 才把客户端从后台拉起：`w.show() → raise_() → activateWindow() →
     processEvents()`，**随后** `LightTransition(w, seam=...).start()`，
     整屏暗幕立刻盖住它，再从中线展开 —— 全程看不到「客户端先出现、动画再盖上去」。
- **splash.py 配套**：
  - 新增 `SplashScreen.line_global_y()` = `self.y() + self._line_y()`（收拢线的屏幕 y）。
  - `LightTransition(target=None, seam=None)`：seam 传**全局** y，内部换算成本地
    （`- geo.y()`）并 clamp；不传则沿用屏幕中线。
  - 新增 `HOLD = 0.15`：起手约 120ms **暗幕保持全闭**，给刚从后台拉起的客户端完成
    首屏绘制，防止暗幕一开就露白底（这是隐藏窗口带来的新风险，必须留这一拍）。
- **握手不受影响**：READY_JS 判据是 DOM 级的（`__libHandshake===true` +
  `.g-item>0`），窗口隐藏时 QWebEngine 照样执行 JS，所以「数据就绪才展开」仍成立。
- **验证（无头探针 `probe_splash_1931.py`，offscreen + grab 抓帧）**：
  `BEFORE_REVEAL client_visible=False`（在后台 ✓）；
  `seam=370.0 == line_global 370.0`（严丝合缝对齐收拢线 ✓）；
  `HOLD t=0.10` 上/下暗幕 alpha=255/255（全闭 ✓）；
  `OPEN t=0.95` alpha=0/0（已退离 ✓）；
  `AFTER_REVEAL client_visible=True`（从后台拉起 ✓） → `PROBE_SPLASH_1931_OK`。
- **本轮两个坑**：
  1. 探针采样点取 `h-4` 会落在底幕退到最后**残留的约 4px** 里（te=0.99 时
     bot_h≈4.26）→ 误判成「没退离」；改成 `h-20` 即可。**是探针的错，不是代码的错**。
  2. `py_compile` 在本项目 `__pycache__` 上会撞 **WinError 5 拒绝访问**（改 pyc 名被拦）
     → 改用 `compile(open(p).read(), p, 'exec')` 做语法校验，不落 pyc。
- **验证链**：sync `IDENTICAL`（index.html 本轮未改）；app/splash 语法 OK；
  探针 `PROBE_SPLASH_1931_OK`。
- **构建**：1.9.30→1.9.31；BUILD_OK → 见下方 dist 路径。
'''

if not os.path.isdir(D):
    os.makedirs(D)
with io.open(P, 'a', encoding='utf-8') as f:
    f.write(ENTRY)
print('APPENDED', P)
