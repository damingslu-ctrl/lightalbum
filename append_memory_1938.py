# -*- coding: utf-8 -*-
import io

PATH = r"D:\Test\workbuddy\2026-09-18-09-00-13\.workbuddy\memory\2026-09-27.md"
with io.open(PATH, "r", encoding="utf-8") as f:
    data = f.read()

entry = r"""

## 性能：地图/图表按视口可见性重渲染 + 签名守卫【1.9.38，已交付】
- 触发：用户「还是很卡顿，尤其是图库、足迹那一块」。
- 根因审计（关键认知）：本 App 是**单页长滚动**（各 section 在 .main-col 同一滚动流，导航仅滚动定位+高亮，非 tab 切换 display:none；无 hashchange 监听、导航 click 不调 refreshAll）。
  - refreshAll 末尾的 scheduleHeavyRenders 无论当前在哪个区段，都**整体重建 SVG 世界地图**（buildWorld 含 233 省+国界 + renderPhotoMarkers + pins.innerHTML 重建并重新绑监听 ~数十 ms）+ 图表。
  - 用户在图库时，后台同步等事件反复触发 refreshAll → 地图在后台整体重绘 → 抢占主线程 → **全局掉帧**（图库、足迹都卡）。
  - 图库自身已有「结果集未变不重建 grid.innerHTML」守卫（renderGallery 的 sig/changed 判定）+ 缩略图 loading=lazy decoding=async + 加载更多走 insertBefore 增量；图库滚动处理器(8578)只做首页视差 transform，不触发 refreshAll。故图库卡顿主因是地图后台重绘的连带。
- 修法（脚本 patch_perf_1938.py，rep/计数断言）：replace scheduleHeavyRenders（7243-7248）为：
  1. `_setupSectionObservers()`：懒初始化，用 `document.querySelector('.main-col')` 作 root 的 IntersectionObserver（rootMargin 300px）观察 `#map`/`#log`，置 `_mapNear/_logNear` 并在进入视野时回调 scheduleHeavyRenders。
  2. scheduleHeavyRenders：仅当 `_mapNear||_logNear||下钻态(forceMap)` 才渲染；两块都不在视野直接 return（跳过整体重绘）。
  3. `renderMapGuarded()`：签名 `mapLevel|region|prov|PHOTOS.length|map.clientWidth`，未变则跳过整张 SVG 重建（DOM 持久，返回时无需重建）。
  4. `renderStatsGuarded()`：签名 `PHOTOS.length|有评分数`，未变则跳过图表重绘。
  5. 保留下钻态 forceMap 立即渲染（drill 必在地图可见时发生）+ 原 40ms 防抖。
- 效果：首屏/图库浏览时地图 SVG 不再后台整体重建；地图仅在接近视野（预渲染 300px）才构建；同一视图内重复 refreshAll 因签名守卫跳过重建。足迹/图库掉帧主因消除。
- 风险与守卫：无 resize 监听器依赖 renderMap（地图用 viewBox SVG 缩放），签名含 clientWidth 仅作未来兼容；sections 非 display:none，DOM 跨视野持久，跳过重建安全；下钻态 forceMap 保证 drill 一定渲染。
- 验证链全绿：node check.js SYNTAX_OK+EXTERNAL_REFS=none+SMOKE_DONE（FABRIC/ED_CSS 均 OK）；node test_viewer.js 19/0；sync_client.py IDENTICAL。
- 构建：1.9.37->1.9.38；BUILD_OK -> client/dist/LightAlbum-1.9.38-win64/LightAlbum/LightAlbum.exe（workpath w0927100739）。
  权威核验：包内 _internal/resources/index.html 含 renderMapGuarded/renderStatsGuarded/_setupSectionObservers 各 1、视口门控 return 1、旧 renderMap();renderStats(); 直接调用 0，全部符合预期。
- 说明：本轮是性能专项，未改任何视觉/布局铁律（单主 script、双主题、.main-col 滚动容器、reduced-motion 只压时长均未动）。若图库在「已加载数百张」后仍偶有滚动微顿，属大量 <img> 固有绘制开销，需另做（如 DOM 虚拟滚动），非本轮范围。
"""

with io.open(PATH, "a", encoding="utf-8") as f:
    f.write(entry)
print("APPENDED 1.9.38 entry, total bytes=%d" % (len(data) + len(entry)))
