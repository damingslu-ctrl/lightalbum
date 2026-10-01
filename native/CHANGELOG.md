# CHANGELOG — LightAlbum native

遵循 SemVer：新功能 = MINOR，修复 = PATCH，破坏性 = MAJOR。
版本号单一真源：`CMakeLists.txt` 的 `project(... VERSION)`（打包脚本从此处解析）。

## [2.2.0] - 2026-10-02

### 新增
- **下钻视图滚轮缩放（1–8x，光标锚定）+ 拖拽平移**：大区 / 省 / 国家视图都能放大看密集照片簇并拖动。
  变换统一到唯一真源 `viewOrigin()`（= 拟合 scale × `viewZoom_` + `viewPan_`），
  绘制与命中同源 → 放大后点省/点国/点卡片依然准确。
- 进入/退出下钻视图自动重置缩放与平移；平移带边界夹取，不会拖成全空白。

### 变更
- 交互语义统一：世界层与下钻层的「点空白」都改为**按下待命、原地松开才返回上一级**
  （拖动即平移，不再一按就跳走）。

### 实测
- `LA_MAP_CLICK_QA` 十三断言全 OK（新增 `vzoom=OK(zoom=1.56)`；`blank` 断言改为
  按下+松开合成，因语义变更）
- `LA_MAP_PERF`：world=2.20 / region=0.75 / province=0.70 / country=0.55 ms
- `LA_MAP_REGION_QA`：china provinces=34 land=157234；`LA_MAP_SMOKE` 不变

## [2.1.0] - 2026-10-02

### 新增
- **「▶ 播放本区作品」**：省 / 大区 / 国家三层级通用，一键用爆炸弹层播放当前区域全部作品（最多 10 张，尊重隐私门）。
- **世界层导航**：顶部「⌂ 回到中国」按钮（缩放归 1 + 中国居中），底部显示当前缩放倍数 `缩放 ×N`。
- 国家视图：世界层任意国家可点（含亚洲全境），无专辑国家进入国界轮廓视图 + 该国 GPS 作品爆炸弹层。
- 世界层滚轮缩放（1–4x，光标锚定）与拖拽平移；3x 超采样烘焙；源数据抽稀 0.18/0.08（2608 环 + 12 湖泊镂空）。

### 修复
- 国家视图爆炸弹层从未播放：`startBurst` 的 `level_` 守卫只认省视图。
- 世界层点击/悬停热区与绘制错位：命中测试与绘制的 scale 基准不一致。
- DataV 中文省名 + 扁平环导致 GeoIndex 静默丢省（`photo.prov` 全空）→ 环数据统一为双层包裹。
- 足迹光点过大（blit 倍率 10→5→3.5）；下钻放大后光晕变巨斑（改 `sqrt(scale)`）。

### 变更
- 流畅性：下钻视图静态层缓存 + 航线批量合并 + 密度/目标集/文字 halo 三级缓存。
  region 帧耗 52.90ms → 0.35ms（1200×800，20 帧均值）。
- QA：`LA_MAP_CLICK_QA` 十二断言；新增 `LA_MAP_PERF` 帧耗时门、`LA_MAP_COUNTRY_QA` 国家视图像素门。

### 实测（离屏门）
- `LA_MAP_CLICK_QA`：country/anchor/province/burst/card/blank/zoom/all/ctry/cburst/reset/regBurst 全 OK
- `LA_MAP_PERF`：world≈2.35 / region≈0.35 / province≈0.35 / country≈0.30 ms
- `LA_MAP_SMOKE`：rings=2608 regions=10 provinces=233 zh=233 names=2596
- `LA_MAP_REGION_QA`：china provinces=34 land≈157234；`LA_MAP_COUNTRY_QA`：Vietnam slate=27182

## [2.0.0] - 2026-09（基线）
- native C++/Qt 桌面端基线版本：图库 / 地图 / 精选 / 修图 / 统计五大页。
