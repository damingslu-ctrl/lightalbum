# CHANGELOG — LightAlbum native

遵循 SemVer：新功能 = MINOR，修复 = PATCH，破坏性 = MAJOR。
版本号单一真源：`CMakeLists.txt` 的 `project(... VERSION)`（打包脚本从此处解析）。

## [2.2.4] - 2026-10-02

### 功能（方向 C·相册管理与智能检索：第一步相册内搜索）
- **相册页新增搜索框（`GalleryPage.cpp`）**：构造函数 `personBar_` 之后插入
  `QLineEdit` 搜索栏（`searchLe`，圆角深色、聚焦描边 `#4fc08a`、带清除按钮），
  `textChanged` 信号 `connect` 到既有 `applyFilter()`（设 `searchText_` →
  `model_.setFilter(q)` + `updateCount()`）。
- **`GalleryModel::rebuild()` 搜索 haystack 由 title/rel/cam 扩展为全字段**：
  现含 `title / rel / cam / region / prov / city / date / rating`，覆盖
  地点（省/市/国家）/ 时间（年月）/ 评分 / 关键词，子串 `CaseInsensitive` 匹配。
- **新增门禁 `LA_GALLERY_SEARCH_QA`**：直接驱动 `GalleryModel` 断言 7 类过滤
  （北京市/北京=1、`china`=2、`2023`=1、裸`5`=2、`Canon`=1、`夜景`=1、清空=4），
  纯数据无需 UI。**实测 `LA_GALLERY_SEARCH_QA PASS marks=7 checks=7`**，退出码 0。
- **事故修复（同迭代）**：`runGallerySearchQa()` 初版在 `main()` 已构造 `QApplication`
  之后又建 `QCoreApplication` → 双实例退场 `SIGSEGV`（实测 `EXIT=139` 且 `marks=6`）。
  删除第二个实例，复用既有 `QApplication`；同步把测试数据中 `家猫` 日期
  `2024-01-15`→`2024-01-12`，消除裸串 `5` 误命中日期尾号（2→3）的噪声。版本 2.2.3→2.2.4。

## [2.2.3] - 2026-10-02

### 功能（方向 B·修图：非破坏式撤销/重做栈）
- **新增与 UI 无关的纯状态机 `RtHistory`（`src/core/RtHistory.h`）**：`RtEditState`
  聚合全部编辑状态（修调 `cur_`、AI 计划 `agentPlan_`/`agentMode_`、几何
  `rot_/flipH_/flipV_/angle_/aspect_/cropNorm_`）；`RtHistory` 提供
  `clear/push/canUndo/canRedo/undo/redo/current/size/pos`，`push` 截断 redo 尾且跳过无变化提交。
  原图 `geoSrc_` 只读、永不被改，撤销/重做 = 快照重套原图重算出图，**天然非破坏**。
- **`RetouchPage` 接入撤销/重做**：`doApply()` 末尾（非 `restoring_` 态）提交快照；
  `loadPhoto()` 建立基线并 `resetHistory()`；`onUndo/onRedo` 经 `restoring_` 标志防重入 push，
  `applyState()` 回写状态并 `rebuildGeo()` 重算；`updateUndoRedoButtons()` 同步按钮可用态。
  动作条新增 `↶ 撤销`/`↷ 重做` 按钮；`Ctrl+Z` 撤销、`Ctrl+Shift+Z`/`Ctrl+Y` 重做（`QShortcut`）。
- **新增门禁 `LA_RETOUCH_UNDO_QA`**：直接驱动 `RtHistory` 断言基线/推入/双级 undo/redo/
  push 截断 redo 尾/无变化不记，纯状态机无需 UI。**实测 `LA_RETOUCH_UNDO_QA PASS marks=9 checks=9`**；
  回归 `LA_RETOUCH_A_QA ok=1`（构造 RetouchPage 不崩）。版本 2.2.2→2.2.3。

## [2.2.2] - 2026-10-02

### 功能（方向 A·地图：清偿 GeoIndex 中文省名债务）
- **`kCities` 中国条目省名由英文改为精确中文省名**（逐字对齐 `:/data/mapdata.json`
  `provinces.china` 的 DataV 34 省）：北京→北京市、上海→上海市、广州/深圳→广东省、
  成都→四川省、昆明/大理/丽江→云南省、杭州→浙江省、西安→陕西省、武汉→湖北省、
  长沙/张家界→湖南省、乌鲁木齐→新疆维吾尔自治区、拉萨→西藏自治区、哈尔滨→黑龙江省、
  大连/沈阳→辽宁省、厦门→福建省、桂林/南宁→广西壮族自治区、西宁→青海省、兰州→甘肃省、
  贵阳/遵义→贵州省、南京→江苏省、青岛→山东省、长春→吉林省、呼和浩特→内蒙古自治区、
  海口→海南省、台北→台湾省、香港→香港特别行政区、澳门→澳门特别行政区。
  多边形主路径本就返回中文省名，此修使**兜底（最近城市）路径与之完全一致**，
  彻底消除中英文混用（同一城市：多边形命中显示"辽宁省"、走城市兜底却显示
  "Liaoning Province" 的残留不一致）。
- **新增 `GeoIndex::verifyChinaProvinces()` 数据完整性自检**：遍历城市表，凡
  `region=="china"` 的条目，`prov` 须无 ASCII 字母且落在已知 34 省白名单内，否则返回
  违规项；`runGeoSmoke`（`LA_GEO_SMOKE`）在门禁内调用并据其 FAIL，使该债务**门禁可断言、
  不可回退**。
- `runGeoSmoke` 新增 成都 已知点（30.57,104.07 → 四川省/成都，主路径命中多边形），
  已知点断言扩至 6。

### 实测
- `LA_GEO_SMOKE PASS loaded=1`：6 已知点全 `ok`（雷克雅未克 / 北京 / 广州 / 大连 /
  成都 / 南海诚实空），无 `GEO badprov` 违规行 → `verifyChinaProvinces()` 零命中。
- 增量构建通过（`main.cpp` + `GeoIndex.cpp` 编译 + 链接，`LightAlbum.exe` 100% Built）。
- 注：本机 git-bash/headless 上下文下 `qa_runner.ps1` 因安全策略禁用
  `.NET Process.Start` 且缺真实控制台无法自退，故全 19 门自动执行交由 CI
  (`qa.yml` 真实 Windows runner + 控制台) 与 `release.ps1` 卡点负责；本轮门禁以
  Git-Bash 文件重定向直跑 `LA_GEO_SMOKE` 实测通过。

## [2.2.1] - 2026-10-02

### 修复（QA 门修正，无用户态行为变化）
- **`LA_GEO_SMOKE` 由红转绿**：DataV 34 省切换后，多边形主路径返回**中文省名**
  （北京市 / 广东省 / 辽宁省），但测试仍期望英文（`Beijing Municipality` 等），
  该门自切换后一直 RED 且因历史未跑 + Git Bash 吞输出长期未被发现。
  将 3 处测试期望对齐到设计内中文真值。

### 变更
- 新增 `docs/客户端迭代指导书.md`（全模块 SOP：模块总览 + 全门禁目录 + 工具链 +
  铁律 + CheckList + 迭代 4 实例）。
- **关键运维发现写入 SOP**：原生 Windows Qt exe 在 Git Bash(MSYS) 下输出被吞、
  表现为「门禁无输出/秒退」假阴性；跑全部门禁**必须用 PowerShell**（`& $exe 2>&1`）。

### 已知债务（已于 2.2.2 清偿）
- `src/core/GeoIndex.cpp` 的 `kCities` 中国条目省名原为英文（兜底路径），与多边形
  中文不一致；**已在 2.2.2 逐条改为 DataV 34 省精确中文名**，并由
  `verifyChinaProvinces()` 在 `LA_GEO_SMOKE` 门禁内断言、不可回退。

### 实测
- `LA_GEO_SMOKE PASS loaded=1`（5 已知点全 ok：雷克雅未克 / 北京 / 广州 / 大连 / 南海诚实空）
- 构建零 error；产物 `LightAlbum-2.2.1-win64.zip`（版本号由 `release.ps1` 从 CMakeLists 解析）

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
