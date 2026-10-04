# CHANGELOG — LightAlbum native

遵循 SemVer：新功能 = MINOR，修复 = PATCH，破坏性 = MAJOR。
版本号单一真源：`CMakeLists.txt` 的 `project(... VERSION)`（打包脚本从此处解析）。

## [2.14.0] - 2026-10-04

### 新增 - 按年筛选足迹（方向 A 第三步）

足迹地图此前只能看全量足迹，无法回答「某年我去了哪」。本步给三层（光点/轨迹/热力）加统一的年份维度筛选。

**数据诚实**：年份下拉只列**库中真实出现过的年份**（`computeFootYears()` 扫 `Photo.year` 去重降序），不按 `kpi.minYear..maxYear` 补齐中间空档 —— 没有照片的年份不作为可选项出现。`Photo.year` 在 `Library.cpp:84` 早已填充，本步零模型改动、零重复解析。

**★筛选位置铁律（本步最关键决策）**：筛选在**注入点**（`MapPage::computeGpsPoints`）完成，而非绘制层。原因是 `gpsPts_` / `gpsPaths_` / `gpsDates_` 三者**按下标严格对齐** —— `enterCountry` 里 `gpsPaths_[i]` 与 `gpsPts_[i]` 配对（`MapView.cpp:2304`）构造国家视图的照片路径。若只在绘制时过滤，三者长度将不一致，国家视图会把照片路径配到错误的足迹点上。故 `if (footYear_ > 0 && p.year != footYear_) continue;` 与其余过滤条件同处一个循环，`pts/paths/dates` 一起产出。

**同步入口收敛**：新增 `MapPage::syncFootData()` 作为足迹数据同步的**唯一入口**（注入 pts/dates/paths + 计数 + 记录筛选态）。构造、`setHidePrivate`（隐私门）、年份切换三处全部改走它 —— 铁律：任何改变**可见足迹集合**的操作都必须经此入口，否则三者的下标对齐与缓存失效（`densDirty_`/`arcTargetsDirty_`/`invalidateViewCache`）会各自漂移。

**UI**：顶栏新增年份下拉（`QComboBox`，墨韵深色样式 + 选中态翡翠），首项「全部年份」（值 0），切换时 `syncFootData()` + 航线目标同步 + `stopTrackPlayback()`（集合已变，旧播放进度失效）。筛选对三层同时生效，不因当前处于轨迹层而失效。

### 新增 - LA_MAP_YEAR_QA（第 38 门，full 层）

纯数据契约门（`png=$false`，不渲染）。注入 6 个跨年份确定性 GPS 点（2019/2021/2024 各 2），四类断言：

- **A 点数**：筛选 2024 后 `gpsPts_` 恰为该年点数（2）。
- **B 下标对齐（核心）**：`gpsPts_.size() == gpsPaths_.size() == gpsDates_.size()` —— 直接锁死国家视图依赖的那条不变量。**只断言「点数对」是弱断言**，对齐才是本步真正要防的回归。
- **C 幂等**：`setFootYearFilter` 重复设置同一年不改变态。
- **D 0=全部**：切回 0 恢复全量 6 点。

**反例（非空心）**：`LA_MAP_YEAR_QA_FORCE_BROKEN=1` 让注入点过滤失效 → 6 点全可见 → A 断言 `pts=6 expect=2` → RED。

### 修复 / 过程
- `src/ui/MapView.{h,cpp}`：`footYearFilter_` 成员 + `setFootYearFilter`/`footYearFilter`（幂等）+ `qaShootYear` 探针。
- `src/ui/MapPage.{h,cpp}`：`computeGpsPoints` 增年份过滤（与 pts/paths/dates 同循环）；新增 `computeFootYears()`/`syncFootData()`/`footYear_`/`yearBox_`；三处调用点收敛到 `syncFootData`；补 `QComboBox`/`QList`/`QSet`/`<algorithm>`/`<functional>` 头文件。
- `src/main.cpp`：`animOk` 排除列表加新门；新增 QA 派发。
- `gate_matrix.ps1`：新增第 38 门（full 层，ASCII 注释）；`CMakeLists.txt`：`VERSION 2.13.0 → 2.14.0`。

### 门数
38 门（新增 1）。fast 层 12 门不变，full 层 25 → 26 门。

## [2.13.0] - 2026-10-04

### 新增 - 轨迹方向箭头 + 播放动画（方向 A 第二步）

2.12.0 的轨迹层只有一条静态折线，无法表达「行进方向」与「按时间走过」。本步在不改动数据模型、不破坏性能预算的前提下补齐二者的表达。

**方向箭头（静态烘焙层）**：新增 `drawTrackArrows(p, pts)`，在每段中点绘制一个朝向下一段的三角（`QPainterPath` 三点闭合，随段方向旋转，退化段 `len<1e-6` 跳过）。世界层注入 `drawTrajectoryWorld`、下钻层注入 `drawTrajectory`，**均落在既有一次性栅格化缓存内** → 不增加每帧开销。

**播放头（动态层）**：新增 `drawTrackPlayback(p)` —— 翡翠实心芯 + 随进度收缩的脉冲环 + 身后 3 段递减拖尾。**铁律：播放头只在 `paintMap` 动态段调用，严禁进入 `bakeViewPix`/`buildWorldPixmap`** —— 否则每帧重栅格化整条轨迹，`LA_MAP_PERF` 预算会被击穿。

**位置唯一真源**：`trackPosAt(frac)` 为纯函数，按**累计弧长**（非点序号）插值，使播放头匀速沿折线前进，frac 超出 `[0,1]` 钳到端点。产品绘制与门禁断言**共用同一函数**（契约同源铁律）→ 门禁不可能与实现漂移。

**计时与降级**：`trackTimer_`(16ms) + `trackClock_`，仅播放期挂载、播完自停（不常驻空转）；`reduceMotion_` 为真时直接呈终态（末点脉冲驻留）而不空转。`startTrackPlayback()` 在非轨迹层或轨迹点 < 2 时拒绝起播；`setFootLayerMode` 离开轨迹层即 `stopTrackPlayback()`，防播放态残留到光点/热力层。

**UI**：MapPage 顶栏新增「▶ 播放足迹」按钮（复用既有 jade 深色样式），点击切换播放/停止；**仅轨迹层可用**，随三态分段按钮联动启禁。文案走 `translate("LightAlbum", …)` 字面量直调（遵 2.12.0 铁律 16）。

### 新增 - LA_MAP_TRACK_ANIM_QA（第 37 门，full 层）

注入 5 个确定性 GPS 点 + 递增日期，两类断言：
- **A 箭头存在性**：统计最亮一档青色像素（`b>235 && g>225 && r>170 && b>r+25`，比轨迹亮芯更亮）`> 0`。
- **B 三时刻位置（核心，区分器）**：播放头在 `t=0 / t=mid / t=end` 的**两两位移均 > 8px**。这是「真沿线移动」与「原地闪」的唯一区分器 —— 只断言「有像素」会是空心断言。位移经 `trackPosAt()` 求出，与产品渲染同源。

**反例（非空心）**：`LA_MAP_TRACK_ANIM_QA_FORCE_BROKEN=1` 把播放头钉死在首点 → 三时刻位置全等（位移全 0）→ `FAIL dPM=0.00 dEM=0.00 dPE=0.00` → 门红。

### 修复 / 过程
- `src/ui/MapView.{h,cpp}`：`trackTimer_`/`trackClock_`/`trackActive_`/`trackTOverride_`/`kTrackDurMs` 成员；`drawTrackArrows`/`drawTrackPlayback`/`trackPosAt`/`startTrackPlayback`/`stopTrackPlayback`/`qaShootTrackAnim`；构造函数接线播放时钟；`setFootLayerMode` 离层即停播；`paintMap` 动态段接入播放头。
- `src/ui/MapPage.{h,cpp}`：顶栏「▶ 播放足迹」按钮 + 播放/停止切换 + 随三态分段联动启禁。
- `src/main.cpp`：`animOk` 排除列表加新门；新增 QA 派发（镜像 `LA_MAP_HEAT_QA` 模板）。
- `gate_matrix.ps1`：新增第 37 门（full 层，ASCII 注释，实测 `measuredMs`）；`CMakeLists.txt`：`VERSION 2.12.0 → 2.13.0`。

### 门数
37 门（新增 1）。fast 层 12 门不变，full 层 24 → 25 门。

## [2.12.0] - 2026-10-04

### 新增 - 足迹轨迹 / 热力双模式（方向 A 第一步：数据时间维 + 双层绘制 + 三态开关）

**背景**：足迹地图此前只有「翡翠光点」一种呈现（`FL_Dots`），无法表达"按时间先后走过的路径"与"拍摄密度"这两类信息。本步在**不改动数据模型**的前提下补齐时间维并新增两个可切换层。

**数据时间维（零模型改动）**：`Photo.date` 早已存在（`core/Library.h:28`），本次只做提取与对齐——`MapPage::computeGpsPoints` 扩展出参 `QVector<QDate>* datesOut`，在**同一过滤循环内**（`hasGps && !priv && !(la==0&&lo==0)`）解析日期，保证与 `gpsPts_` 逐点严格对齐；解析失败回退为无效 `QDate`（数据诚实：不编造日期）。日期经 `MapView::setGpsDates()` 注入为平行成员 `gpsDates_`。

**双层绘制（烘焙静态层，不破 LA_MAP_PERF）**：新增 `drawTrajectoryWorld`/`drawHeatWorld`（世界层，`buildWorldPixmap` 内）与 `drawTrajectory`/`drawHeat`（下钻层，注入 `renderRegionStatic` / `renderProvinceStatic` / `renderCountryOnly` 的 `drawGpsDots` 之后）。二者**复用 `viewOrigin` 变换**（铁律：绘制与命中同源），且都落在既有一次性栅格化缓存内，因此帧耗时预算不受影响。轨迹按 `sortedTrackIdx()`（`gpsDates_` 升序 `std::stable_sort`，无日期者置后且保持原序）连折线；热力为密度分级的暖色径向渐变。

**三态开关**：`MapPage` 顶栏新增 `QButtonGroup` 独占三段按钮「光点 / 轨迹 / 热力」，沿用既有墨韵深色样式、`translate("LightAlbum", ...)` 包裹（lupdate 可抽取），连 `view_->setFootLayerMode(0/1/2)`。切换时 `buildWorldPixmap()` + `invalidateViewCache()` 双重重烘焙。

**零回归策略**：`footLayer_` 默认 `FL_Dots`，所有新增绘制均在 `footLayer_ != FL_Dots` 时才执行 → 未切换时输出与 2.11.1 逐像素一致，`LA_MAP_A_QA` / `LA_MAP_PERF` 不受影响。

**颜色分离（颜色校验铁律·实测）**：轨迹取青（蓝主导 `b>r+15 && b>g+15`）、热力取暖（红主导 `r-g>90 && r-b>90`），与既有翡翠光点（绿主导）、金色航线（`r≈g`）色相分离 → 三类像素统计互不污染，门禁可独立断言。

### 新增 - LA_MAP_TRACK_QA（第 35 门，full 层）

注入 5 个确定性 GPS 点（北京/上海/广州/成都/乌鲁木齐）+ 确定性递增日期，`footLayer=轨迹`，离屏渲染世界层并统计**青色轨迹像素**，断言 `>= 30`。经 `System.Diagnostics.Process` 直连自退，输出 `LA_MAP_TRACK_QA trackPx=…`。

**反例（非空心）**：`LA_MAP_TRACK_QA_FORCE_BROKEN=1` 强制回落 `FL_Dots` → 不画轨迹 → `trackPx=0 < 30` → 输出 `FAIL cyan=0` → 门红。

### 新增 - LA_MAP_HEAT_QA（第 36 门，full 层）

同构探针，`footLayer=热力`，统计**暖色热力像素**，断言 `>= 30`。

**反例（非空心）**：`LA_MAP_HEAT_QA_FORCE_BROKEN=1` 强制回落 `FL_Dots` → `warmPx=0 < 30` → 门红。

两门均属 map 类（构造 MainWindow、离屏渲染世界层），按矩阵分层依据落 **full 层**（不写 `tier` 键 = fail-safe 默认），不挤占 500ms fast 预算。

### 修复 / 过程
- `src/ui/MapView.{h,cpp}`：`FootLayer` 枚举 + `gpsDates_`/`footLayer_` 成员 + `setGpsDates`/`setFootLayerMode`/`footLayerMode` + `sortedTrackIdx` + 四个轨迹/热力绘制函数 + `qaShootTrack`/`qaShootHeat` 像素探针（含 FORCE_BROKEN 反例）。
- `src/ui/MapPage.{h,cpp}`：`computeGpsPoints` 增 `datesOut` 出参；两处调用点（构造 + `setHidePrivate`）注入日期；顶栏三态分段按钮 + `setFootLayerMode` 连线。
- `src/main.cpp`：`animOk` 排除列表加入两个新门；新增两处 QA 派发（镜像 `LA_MAP_COUNTRY_QA` 模板）。
- `gate_matrix.ps1`：新增 2 门（full 层，带注释与 `measuredMs`）。
- `CMakeLists.txt`：`VERSION 2.11.1 → 2.12.0`。

### 门数
36 门（新增 2）。fast 层 12 门不变，full 层 22 → 24 门。

## [2.11.1] - 2026-10-04

### 修复 - i18n 默认路径在中文 Windows 误载英文翻译器（debug 客户端「界面显示异常」根因）

**问题**：2.10.0 引入的中英双语 i18n 在部分中文 Windows 上**整屏 UI 变成英文**——「区域统计 / 已分析 / 摄影日志」等核心文案全部回退为 `No Image / Original / Edited / Photo Log` 英译，用户报告"好多之前没问题的现在出现问题了"。

**根因（铁证）**：原默认路径判定用 `QLocale::system().language() != QLocale::Chinese` 决定是否加载 `:/i18n/LightAlbum_en.qm`。Qt6 的 `language()` 返回的是**"区域格式"（locale for formatting）**语言，受 Windows「区域 → 管理 → 非 Unicode 程序的语言」/ 独立区域格式设置影响；在**显示语言为中文、区域格式被设为英语**的机器上会**误返 `English`**（本机实测 `lang=1` 即 English，而 `uiLanguages()` 居首为 `zh-Hans-CN`）。于是中文系统被误判为「非中文」→ 强制加载英文翻译器 → 全 UI 英文化。这是**纯判定缺陷**，与翻译文件本身无关。

**修复**：新增纯函数 `laI18nShouldLoadEn(forced, loc)` 作为语言决策唯一真源：
- 若 `LA_I18N_LANG` 显式设置（`en`/`zh`），**优先**于系统 locale（便于强制/测试）；
- 否则 `language()==Chinese` 或 `uiLanguages()` 任一含 `zh*` → 保持原生中文 UI（不加载）；
- 仅当系统既非中文 locale、`uiLanguages()` 也无 `zh*` 时，才加载英文。

`uiLanguages()` 对应 `GetUserPreferredUILanguages`，反映 OS **显示语言**列表，语义正确，弥补 `language()` 的盲区。中文默认不加载翻译器 → 原生 UI 零回归（与 2.10.0 设计一致）。

**门禁升级**：`LA_I18N_QA`（第 32 门，fast 层）由原 4 条「翻译正确性」断言**扩至 9 条**——在 4 条翻译断言之后，新增 5 条「构造-locale 决策」断言，把 2.10.0 漏测的默认路径焊进门禁（`zh_CN→不加载`、`zh_TW→不加载`、`en_US→加载`、`forced=en` 覆盖系统、`forced=zh` 显式保持中文）。反例 `LA_I18N_QA_FORCE_BROKEN=1` 仍非空心（不加载翻译器 → 4 串全 FAIL）。

### 修复 / 过程
- `src/main.cpp`：新增 `laI18nShouldLoadEn()` 纯函数；默认加载块改用该函数替代 `language()!=Chinese` 判定；`LA_I18N_QA` 门由 4 断言扩至 9 断言（含 5 条决策断言）。
- `CMakeLists.txt`：`VERSION 2.11.0 → 2.11.1`。
- `gate_matrix.ps1`：`LA_I18N_QA` 注释补记决策断言（`tier="fast"` / `measuredMs=210` 不变）。

### 门数
34 门（无新增门）。fast 层 12 门不变。回归：重建后 `LA_I18N_QA PASS entries=9`（含 5 决策断言全过）；修复前 exe 实测 `LA_I18N_DEBUG lang=1 ui=zh-Hans-CN,... loadEn=1 translator=en installed`（复现误载），修复后 `loadEn=0` 且无 `translator=en installed` 行（中文 UI 恢复）。`LA_SCALE_PERF ctrls=60` / `LA_GALLERY_E_QA featureOK=1` 全绿；图库离屏 grab 仅 2 缩略图为探针假象（虚拟化未触发布局），非真实回归。

## [2.11.0] - 2026-10-04

### 变更 - 无障碍 a11y（方向 F 第三步：键盘可达 + 对比度合规）

**问题**：取证发现三处可达性缺口——
- 地图 `MapView`、灯箱 `PhotoLightbox` 未设 `StrongFocus`，键盘无法聚焦或触发操作；
- 多处硬编码提示文字 `#5a6b60` 在深色背景上对比度不足（未达 WCAG 2.1 正文 4.5:1）；
- 「全局焦点环」子项已存在（`Lumen::focusRing()` + `main.cpp` 全局挂载），故免做。

**方案**：
- **键盘可达**：`MapView` 设 `StrongFocus` + `keyPressEvent`（`+`/`=` 放大、`-`/`_` 缩小改 `worldZoom_`、方向键平移、`Enter`/`Return` 下钻 `drillAtCenter`、`Esc` 返回），自绘翡翠焦点环（与全局 focusRing 同色 `0x5f,0xdc,0xa6`）；`PhotoLightbox` 设 `StrongFocus`、`showEvent→setFocus()`、`←`/`→` 基于 `lib_->photos()` 线性相邻下标翻图（端点不回绕）。`EditorialView::qaKeyboardCheck()`（#367 已落）断言 `GalleryTile` StrongFocus + `Enter` 激活。
- **对比度合规**：将 3 处硬编码提示色 `#5a6b60` → `#8da996`（与 `Dark::Muted`/`hint` 一致，满足 WCAG）；新增 `LA_A11Y_CONTRAST_QA` 审计 15 对 DarkPalette 固定色对（正文 4.5:1）。

### 新增 - LA_A11Y_CONTRAST_QA（第 33 门，fast 层）

纯函数 WCAG 2.1 相对亮度审计，覆盖 `Dark::Ink/Muted/Gold/Emerald/Bg/Card/Inset/Line` 与产品内固定硬编码色对的 15 对组合，逐对比对 4.5:1 阈值。纯 offscreen 自退、不构造 MainWindow、无外部 `need` 依赖。

**反例（非空心）**：`LA_A11Y_CONTRAST_QA_FORCE_BROKEN=1` 注入 `BROKEN Line on Inset (1.35:1)` → 门红。

### 新增 - LA_A11Y_KEYBOARD_QA（第 34 门，fast 层）

转发 `EditorialView::qaKeyboardCheck()` + 新增 `MapView::qaKeyboardCheck()`：断言瓦片/地图 `focusPolicy==StrongFocus` 且模拟 `Enter`/`Key_Plus` 触发激活/缩放（`worldZoom_ 1.0→1.25`）。纯 offscreen 自退、不构造 MainWindow、无外部 `need` 依赖。

**反例（非空心）**：`LA_A11Y_KEYBOARD_QA_FORCE_BROKEN=1` 强制 `NoFocus` → `tile not keyboard-focusable` + `mapview not keyboard-focusable` 双 `FAIL` 必红。

两门实测墙钟 **CONTRAST 18ms / KEYBOARD 17ms**（offscreen x3 取最优，2026-10-04），远低于 500ms fast 预算、比最便宜的 full 门（1017ms）还低 → 落 fast 层，使 a11y 获得 PR 层覆盖。

### 修复 / 过程

- `src/ui/MapView.{h,cpp}`：`StrongFocus` + `keyPressEvent` + `zoomBy/panBy/drillAtCenter` 辅助方法 + 翡翠焦点环 + `static int qaKeyboardCheck()`。
- `src/ui/MainWindow.cpp`（PhotoLightbox）：`StrongFocus`、`setPhoto` 存 `thumbsDir_`、`showEvent→setFocus()`、`←`/`→` 翻图 `navigate(int)`、`thumbsDir_` 成员。
- `src/ui/EditorialView.{h,cpp}`（#367）：`qaKeyboardCheck()` 瓦片键盘可达。
- `src/ui/CullPage.cpp` / `LogPage.cpp` / `MapPage.cpp`：提示色 `#5a6b60`→`#8da996`（对比度整改）。
- `src/main.cpp`：新增 `LA_A11Y_CONTRAST_QA` / `LA_A11Y_KEYBOARD_QA` 两门断言分支（位于 `int main` 启动前）。
- `CMakeLists.txt`：`VERSION 2.10.0 → 2.11.0`。
- `gate_matrix.ps1`：登记第 33/34 门，均 `tier="fast"`、`measuredMs=18/17`。**纯 ASCII**。

### 门数

32 → **34** 门。fast 层 10 → **12** 门。回归：fast 层 12 门全绿（wall 4216ms）；`LA_A11Y_CONTRAST_QA` 绿态 `PASS pairs=15`、红态 `BROKEN Line on Inset` 全中；`LA_A11Y_KEYBOARD_QA` 绿态 `PASS tile=keyboard-activatable` + `mapview=keyboard-navigable worldZoom 1.0000->1.2500`、红态双 `FAIL` 全中。全量 34 门 PASS=33 SKIP=1（LA_SCAN_SMOKE 需 env，预期跳过）KILL=0；`LA_AGENT_SMOKE` 受本地 Ollama 模型冷加载影响偶发超时（环境依赖，非代码回归，Ollama 热态 PASS）。

## [2.10.0] - 2026-10-03

### 变更 - 中英双语国际化（方向 F 第二步）

**问题**：核心 UI 文案（按钮、标签、状态、页标题）全部硬编码中文字面量，无法切换语言，
且未来接英文/多语会牵一发而动全身。

**方案**：
- 用 Python 脚本批量将 10 个源文件的 **280 处** `QStringLiteral("中文")` /
  `QString::fromUtf8("中文")` 统一改为 `QCoreApplication::translate("LightAlbum", "中文")`
  （context 固定 `"LightAlbum"`，零运行时成本、可被 `lupdate` 抽取）。
- **排除集（关键）**：过滤/查找/比较键（category、nav、`kNav`、`.value()`、`.contains()`、
  `pv.name` 等）若一侧来自 EXIF/数据而非 UI，对应 UI 串**保持 raw 不包裹**，否则
  过滤/选择逻辑断裂。已逐项核实并保留（CullPage 845-846、GalleryPage 19-21/285、
  MainWindow 66-68/1065、MapView 927/1169、LogPage 519）。
- 翻译文件：`lupdate` 抽取 **249** 条源串 → 策展英译字典填充（含 HTML 实体转义、emoji
  保留、`%1` 占位符完整）→ `lrelease` 编译出 `i18n/LightAlbum_en.qm`（25550 字节）。
- 资源嵌入：新建 `i18n.qrc` 将 `LightAlbum_en.qm` 编为 `:/i18n/LightAlbum_en.qm`，
  在 `CMakeLists.txt` 的 `LA_RESOURCES` 与 `resources.qrc` 并列登记，AUTORCC 嵌入 exe
  → Headless 门禁随取随用、零外链。
- 启动加载：`main.cpp` 在 `Dark::apply` 之后、`MainWindow` 构造之前，按
  `LA_I18N_LANG`（强制覆盖）或 `QLocale::system()`（默认）决定加载并 `installTranslator`：
  **中文（简/繁）默认不加载翻译器 → 原生 UI 零回归**；`en` 或非中文加载英文。

### 新增 - LA_I18N_QA（第 32 门，fast 层）

强制加载 `:/i18n/LightAlbum_en.qm`，断言 4 条核心 UI 串 `translate("LightAlbum", …)`
返回英文且**不等于**源码：

- `无图像` → `No Image`
- `原图` → `Original`
- `修后` → `Edited`
- `摄影日志` → `Photo Log`

实测墙钟 **165–246ms（取 210ms）**，纯 offscreen 自退、不构造 MainWindow、无外部 `need`
依赖 → 符合 fast 层判定（< 500ms 预算），与 `LA_HIDPI_QA`（构造 QWidget、762ms）归 full
形成对照，使翻译正确性获得 PR 层覆盖。

**反例（非空心）**：`LA_I18N_QA_FORCE_BROKEN=1` 跳过翻译器加载 → `translate` 回退源码
→ 4 串全部 `FAIL got=<中文> expect=<英文>`，实测变红（`[FAIL]`），证明门是真实断言。

### 修复 / 过程

- `src/main.cpp`：补 `#include <QTranslator>` / `<QLocale>`；插入 `LA_I18N_QA` 完整断言门
  （仿 `LA_HIDPI_QA`，位于 `int main` 分支前）；正常启动段加入翻译器加载块（`Dark::apply`
  之后、`MainWindow` 之前）。
- `native/i18n.qrc`（新增）：嵌入 `LightAlbum_en.qm` 为 `:/i18n/LightAlbum_en.qm`。
- `CMakeLists.txt`：`LA_RESOURCES` 追加 `i18n.qrc`；`VERSION 2.9.0 → 2.10.0`。
- `gate_matrix.ps1`：登记第 32 门 `LA_I18N_QA`，`tier="fast"`、`measuredMs=210`。**纯 ASCII**。

### 门数

31 → **32** 门。fast 层 9 → **10** 门。回归：fast 层 10 门全绿；`LA_I18N_QA` 绿态
`PASS entries=4`、红态 `FAIL` 4 串全中；中文默认 `LA_I18N_LANG=zh` 不加载翻译器且
`LA_LUMEN_SMOKE` 仍 PASS（零回归）。

## [2.9.0] - 2026-10-03

### 变更 - 高 DPR 渲染（方向 F 第一步）

**问题**：取证 `src` 内 `devicePixelRatio` 匹配数 **0** —— 全部自定义 `QImage` /
`QPixmap` 都以「逻辑像素」烘焙（典型 `viewPix_ = QPixmap(size())`、
`footDotPix_ = QPixmap(D, D)`、离屏 `QImage img(size(), ...)`）。在 125% / 150% /
200% 系统缩放下，OS 会对位图做**最近邻拉伸** → 地图/足迹点/静态层模糊发虚。

**方案**：新增 `native/src/ui/HiDpi.{h,cpp}` 作为**高 DPI 烘焙唯一真源**，把一切
「按 `size()` 烘焙位图」收敛到一处，统一 `设备像素尺寸 = 逻辑尺寸 × dpr` +
`setDevicePixelRatio(d)`，绘制仍用逻辑坐标 → 缩放后清晰。`MapView` 两处接入：

- 下钻静态层 `bakeViewPix`：`viewPix_ = HiDpi::bakePixmap(size())`（原 `QPixmap(size())`）；
- 足迹点精灵：`footDotPix_ = HiDpi::bakePixmap(QSize(D, D))`（原 `QPixmap(D, D)`，`D=120`）。
- `worldPix_`（3× 超采样）不涉及 devicePixelRatio，无需改。

`HiDpi::dpr()`：优先读 `LA_HIDPI_DPR` 覆盖（门禁/Headless 用，模拟指定 DPI 显示器），
否则走 `QGuiApplication::primaryScreen()->devicePixelRatio()`（已含 `QT_SCALE_FACTOR`），
无 GUI 实例回退 1。

### 新增 - LA_HIDPI_QA（第 31 门，full 层）

三断言：**A 机制**（`HiDpi::dpr()==2`）、**B 工具**（`HiDpi::bakeImage(1200,800)`
设备像素宽 == 2400 且 dpr == 2，唯一真源）、**C 产品**（真实 `MapView` 进入 china
大区烘焙静态层后 `viewPix_` 设备像素宽 == 逻辑宽 × 2，端到端依赖本修复）。

门内 `qputenv("LA_HIDPI_DPR","2")` 模拟 200% 显示器，与 offscreen 平台
`devicePixelRatio` 不稳定（被本机 200% 真显接管变 4）解耦 → 门禁**可复现**。

**关键实测修正（本轮最重要）**：首版断言 `qaViewPixDeviceWidth` 写成
`viewPix_.width() * viewPix_.devicePixelRatio()`，而 `QPixmap::width()` **已返回
设备像素**（实际缓冲宽），再乘 dpr → 2400×2 = 4800 假红。修复后直接返回
`viewPix_.width()`（设备像素）。根因是**测试侧双乘**，产品烘焙一直正确。

**反例（非空心）**：`LA_HIDPI_QA_FORCE_LQ=1` 让 `bake*` 故意返回逻辑尺寸
（`dpr=1`、`width()==逻辑宽`）→ B/C 必红。实测：
`LA_HIDPI_QA FAIL B imgDevW=1200 expected=2400 B imgDpr=1 ... C viewPixDevW=1200 ...`。

### 修复

- `src/ui/HiDpi.{h,cpp}`：新增，高 DPI 烘焙唯一真源（含 `LA_HIDPI_DPR` 覆盖与
  `LA_HIDPI_QA_FORCE_LQ` 反例分支）。
- `src/ui/MapView.cpp`：`bakeViewPix` 改用 `HiDpi::bakePixmap(size())`；`footDotPix_`
  改用 `HiDpi::bakePixmap(QSize(D,D))`；暴露 `qaBakeViewPix()` / `qaViewPixDeviceWidth()`
  （修正双乘）。
- `src/main.cpp`：引入 `ui/HiDpi.h`；在 `LA_EXIT_PROBE` 之后插入 `LA_HIDPI_QA` 完整
  断言门（强制 `LA_HIDPI_DPR=2`）。
- `CMakeLists.txt`：将 `HiDpi.{h,cpp}` 接入 `LA_SOURCES`。
- `gate_matrix.ps1`：登记第 31 门 `LA_HIDPI_QA`，`measuredMs=762`，落入 full 层
  （构造 QWidget 并离屏渲染，且 > 500ms 快层预算；无外部 `need` 依赖）。**纯 ASCII**。

### 门数

30 → **31** 门。全量回归两次：`PASS=29 FAIL=0 CHECK=0 SKIP=1 KILL=0`
（`gates=31`，墙钟 ~76s，`VERDICT: GO`）。
`LA_AGENT_SMOKE` 首次全量跑曾 `no-exit-request`（20109ms 略超 20s 预算），隔离复跑
两次均 `PASS`（19133ms / 3136ms），判定为**负载下偶发超时**，与本改动无关（改动不触及
agent 路径）。所有 `LA_MAP_*` 门保持 `[OK]`。

## [2.8.0] - 2026-10-03

### 变更 - 门禁矩阵化（方向 E 第三步）

**问题**：门表内联在 `qa_runner.ps1` 里，30 门在每个 PR 上全量跑。2026-10-03 实测
基线：**全量 30 门 = 73.6s 墙钟**，而门耗时的分布是天然两极的：

| 类别 | 门数 | 单门耗时 | 共因 |
|---|---|---|---|
| 纯逻辑门 | 9 | 56 - 250 ms | `main()` 早期 return，不构造 MainWindow |
| 构造 / fixture 门 | 21 | 338 - 8662 ms | 构造 MainWindow 或载入 6425 张 fixture |

也就是说 77% 的门只占 2% 的时间，而 68% 的 CI 时间花在少数几个必然要在 main 上
跑的门上。PR 每次都付这 73.6s，而其中绝大部分与"这次改动有没有打破东西"无关。

**方案**：新增 `native/gate_matrix.ps1`，作为**门表与分层的唯一真源**。
`qa_runner.ps1` 改为 dot-source 消费它，不再内嵌门表（此前若要在两处分层，必然
出现"门在一处、层在另一处"的漂移）。

**分层语义（fail-safe）**：

- `tier="fast"` —— 实测廉价，**每个 PR 都跑**；
- `tier="full"` —— 昂贵，push 到 main / tag 时跑，本地全量跑与 `release.ps1` 也跑；
- **tier 缺省 = full** —— 新门忘写 tier 只能落进最严的一层，绝不会因遗漏而
  静悄悄削弱 PR 层。

不传 `-Tier` 时是两层并集，与 2.7.0 行为逐字节一致 —— **打包门禁强度未被削弱**。

**实测收益**：fast 层 **9 门 1.74s**，全量 30 门 73.4s（**42 倍**）。

**两处刻意的排除**，都在 `gate_matrix.ps1` 头注释里写明理由：

- `LA_SCALE_PERF`（338 ms）虽快，但断言的是**墙钟预算**，放共享 CI runner 上必
  flaky。PR 层放一个会抖的门，等于教人无视红色 —— 比慢门更糟。
- `LA_RW_SMOKE`（1017 ms）临界，留在严层，让 vault / 加密往返只有一处能坏。

### 新增 - LA_GATE_MATRIX_QA（第 30 门，18 条断言）

它**住在 fast 层**：矩阵的看门狗自己必须在 PR 层被看。

1. E1 真表通过结构校验；E2 两层互斥且并集为全表；
2. E3 **fail-safe 实测**：剥掉所有 tier 后 fast 层为 0、full 层为 30；
3. E4 严层必须真的装着 ≥2000ms 的贵门（18 个）；
4. E5 fast 层无脚本门（矩阵自身除外）、无外部 env 依赖门；
5. E6 **成分而非比例**：fast 层每门 `measuredMs ≤ 500ms` 预算；
6. E7 5 个核心不变量门**必须**留在 fast 层；
7. E8 每门必须带 `measuredMs`（否则分层就成了猜）；
8. E9a-g 七组同源反例；E10 未知 tier 必须抛异常而非静默全跑。

### QA - 反例实测 6/6 变红（并因此改了两条断言）

外部脚本 `_ce324.py` 逐个注入真实缺陷、走**真 runner**、断言判红：

| 反例 | 注入 | 结果 |
|---|---|---|
| CE1 | 核心门 `LA_THUMB_CACHE_QA` 悄悄退出 PR 层 | RED |
| CE2 | 重复门名 | RED |
| CE3 | 外部 env 依赖门被拖进 fast 层 | RED |
| CE4 | 昂贵门（7.4s）被降级进 PR 层 | RED |
| CE5 | 混入未知 tier 值 `fas` | RED |
| CE6 | `measuredMs` 被吞进带引号的 `scriptArgs` | RED |

**首轮只有 3/5 变红，暴露了两条真盲区**：

1. **原 E6 只卡"fast 占比不超过一半"的比例**。把 3640 ms 的地图门塞进 fast 层
   后 fast=10/30，仍"不到一半" → 依然绿。**比例规则根本看不见成分退化**。已改为
   成员资格规则（成本预算 E6 + 核心门 E7）。
2. **CE1 最初写成"删掉一个 tier key 应当变红"，它保持绿色是正确的** —— 无 tier 的门
   本就该落严层。真正暴露的问题是：**没有任何规则能察觉核心门离开了 PR 层**。
   由此新增 E7 与其反例 E9f。

**同轮修掉一个真实数据损坏**：给门表批量追加 `measuredMs` 时，字段被拼进了
`LA_RELEASE_QA` 的**带引号 `scriptArgs` 字符串内部** ——
`scriptArgs="-SelfTest; measuredMs=3135"`，键消失、参数被改。E8 就是为这类"静默
吞字段"而设的。**教训：批量改结构化行必须逐行断言形状，不能只断言行首前缀。**

### 修复

- `qa_runner.ps1`：新增 `-Tier fast|full`、`-Timing`；门表改为消费 `gate_matrix.ps1`；
  摘要与 JSON 增加 `tier` / `totalMs` / 每门 `ms`。
- `iterate.ps1`：新增 `-Tier` 透传；Markdown 报告增加层级、墙钟、每门 ms 列。
  没有这一层，读者会把 9 门的 fast 报告误读成全套通过。
- `.github/workflows/qa.yml`：`pull_request` → fast 层，其余触发 → full 层，
  选择步骤写入 `$GITHUB_ENV`，层级出现在 CI 日志与 artifact 名前缀里。

### 门数

29 → **30** 门。全量回归：`PASS=29 FAIL=0 CHECK=0 SKIP=1 KILL=0`，
`tier=all gates=30`，墙钟 73379ms，`VERDICT: GO`。
（`LA_SCAN_SMOKE` 仍因缺 `LA_SCAN_ROOT` / `LA_SCAN_DB` 自动 `[SKIP]`。）

## [2.7.0] - 2026-10-03

### 修复（门禁自退时序缺陷 —— 此前被误记为「环境噪声」的真缺陷）
- **`LA_CULL_SMOKE` 永久挂死根因**：`CullPage::onFinished()` 的自测分支在**构造期**同步调用 `QApplication::quit()`，而 `app.exec()` 尚未进入。Qt 的 `quit()` 只给「当前事件循环」发退出信号，没有事件循环时它是 **no-op** —— exe 打印完所有断言后照常进入 `app.exec()` 永久空转，门禁表现为 `[KILL]`。历史上被归入「控制台依赖 / 环境族」，本次用最小探针**实证**为代码缺陷。
- 最小复现（探针对照实测，非推断）：`quit()` 在 `exec()` **之前**调用 → `RC=124` 挂死；在事件循环**内**（timer）调用 → `RC=0` 正常退出。
- 修复效果：`LA_CULL_SMOKE` 从 `RC=124` 挂死（曾需 25s 超时被 kill）变为 `RC=0` / 3.9s 自退，输出逐字节一致（250 bytes）。
- 同类审计：全库 9 处 `QApplication::quit()` **逐一核实时机**，确认仅 `CullPage.cpp:1003` 一处违规（其余 8 处本就在事件循环内）。为防复发，9 处**全部**收敛到新契约。

### 新增（自退契约 `src/core/qaExit.{h,cpp}`）
- `QaExit::requestExit(code)`：**幂等**、**无条件** `QTimer::singleShot(0,...)` 排队退出。铁律：不判断「是否在事件循环内」再决定同步/异步——Qt 无公开 API 判断该状态，猜错就是挂死；无条件排队则构造期注册也会在 `exec()` 启动后触发，事件循环内调用仅延迟一个事件迭代（~0ms），**一条路径覆盖两种时机**。
- `QaExit::armWatchdog()`：仅在检测到 `LA_*` 门禁变量时挂载（`activeGate()` 返回空串即直接 return，**真实交互启动绝不挂、绝不杀用户进程**）。超时未退出 → 打印 `LA_WATCHDOG FAIL gate=<门名> budget_ms=<ms> reason=no-exit-request` 并 `_exit(3)`。默认预算 20s，**必须 < runner 的 `-Timeout` 25s**，保证看门狗先于 runner 超时触发，从而拿到归因而非无信息的 `[KILL]`。
- `QaExit::activeGate()`：优先读 `LA_GATE`（运行器显式设），回退扫描环境变量找第一个非辅助 `LA_*`（**排除清单**而非白名单——两者都靠人工维护但失效方向相反：白名单漏新门禁→看门狗不挂→回到静默挂死；排除清单漏新辅助变量→仅归因消息不好看，退出行为不受影响）。
- 全库 `grep "QApplication::quit"` 现在仅在 `qaExit.cpp` 实现内部与注释中命中，`src/` 下裸调用为 0。

### QA（新增 `LA_EXIT_QA`，9 断言全绿 + 同源反例）
- 设计要点：门禁**自身**用 `QProcess` 拉起同一 exe 的三种子进程模式（`LA_EXIT_PROBE=early|deferred|hang`），把「Qt 的 quit 时序语义」这一外部事实**固化为可断言契约**。
- (A) 契约 2 项：`early`（exec 前裸 `quit()`，**预期挂死**）必须 `waitForFinished` 返回 false；`deferred`（`QaExit::requestExit`）必须 `rc=0` 且带 `deferred PASS` 标记。
- (B) **同源反例 E4**：断言两模式 `finished` **必须不同**。事故的整个教训就是「两种时序行为不同」；若哪天变成相同，契约即失去意义 → FAIL「assertion is vacuous」。
- (C) 看门狗 4 项：故意不请求退出时，必须在预算内以 `rc=3` 退出，且归因行同时含 `LA_WATCHDOG FAIL`（E7）与 `gate=<门名>`（E8，**指名道姓**，而非笼统「有个门挂了」）。
- (D) 纯函数 2 项：`activeGate()` 在本进程应返回 `LA_EXIT_QA`（E9）；`requestExit` 连调三次仍只触发一次退出（幂等）。
- **反例有效性已实测（双重验证，防断言空心）**：把 `requestExit` 实现改回同步 `QCoreApplication::exit(code)`（精确复现事故形态）后重建 → ① `LA_EXIT_QA` `RC=1` RED，`fails=2`（E3 + E4 双重命中）；② 真实门 `LA_CULL_SMOKE` 被看门狗抓住，`RC=3` + `LA_WATCHDOG FAIL gate=LA_CULL_SMOKE`。随后恢复正确实现。
- **本门在设计过程中当场抓到自身的第二个盲区**：看门狗原挂在 `MainWindow` 构造前，而 `LA_EXIT_PROBE` 子进程入口在 `main()` 顶部就 `return app.exec()`，走不到挂载点 → E5 报「watchdog never fired」。据此把挂载点前移到 `QApplication` 构造之后、任何分支之前。**这正是「先写断言再实现」的价值：盲区是门禁自己发现的，不是靠复盘。**
- 门数 26 → **27**；运行器新增设 `LA_GATE`，使看门狗归因能指名门名。

### 门数
- 26 → **27** 门。历史遗留的 `LA_CULL_SMOKE [KILL]`（此前被当作「已知环境族」放过）现已修复为正常绿门。

## [2.6.0] - 2026-10-03

### 功能（导入 iCloud 相册）
- 顶栏「＋」导入钮改为**下拉菜单**：`导入目录…` / `导入 iCloud 相册` / （分隔线）/ 探测状态行。空库空态同步给出并排双按钮——新用户第一次接触应用的入口不能把 iCloud 藏在下拉里。
- 新增 `src/core/ICloudSource.{h,cpp}`：iCloud 本地落盘根**多候选探测**（`iCloud Photos/Photos` → `iCloud Photos` → `iCloud Drive/Photos`，相对 `QStandardPaths::PicturesLocation`）。写死一条会在别的机器上静默「导入 0 张」，故按优先级取第一个真实存在者。
- **空壳拒绝**：`detectRoot()` 要求候选目录里至少有一个媒体文件，只有 `desktop.ini` 的空壳不算命中——避免虚假「已找到 iCloud」。探测不到时菜单项**禁用**并说明原因，绝不静默导入 0 张后报「完成」。
- 确认弹窗展示真实根路径 + 真实文件数，并说明「云端未下载的占位文件会跳过，在 iCloud 下载完成后重跑即可增量补齐」。
- **写入路径不新建**：iCloud 与普通目录两个入口都收敛到新抽出的 `runImportInto(dir, title)`，缩略图缓存键、增量规则（size+mtime 未变则跳过）、missing 标记语义完全同源（铁律：缓存/写入入口必须唯一）。

### 取证（先测再改，本机实测而非推断）
- iCloud 落盘形态：`%USERPROFILE%\Pictures\iCloud Photos\Photos` 实测存在，**平铺** 6778 个文件（仅 1 个子目录 `untitled.afcatalog`），不按相册分目录。扩展名分布：`.jpg` 5088 / `.heic` 777 / `.png` 561 / `.mp4` 120 / `.mov` 102 / `.dng` 58 / `.arw` 39。
- **基线先行**：改动前先用现有 `LA_SCAN_SMOKE` 门跑 iCloud 风格沙盒，得 `added=6 placeholders=2 total=8` —— 证明 `Scanner::syncRoot` **本就能正确处理 iCloud 目录**（含占位符跳过）。故本次不动扫描器核心，缺口在**入口可发现性**。
- **格式支持实测**（自建 `QImageReader` 探针，逐格式 read() 实测，禁猜）：`.jpg` ok 4656×3496；`.heic` **ok 3840×2160**（Qt 能解，白名单已有）；`.png` ok；`.dng`/`.arw` **NULL `Unsupported image format`**；`.mp4`/`.mov` 同。→ RAW 与视频**不纳入**白名单（纳入只会产出空缩略图卡片），`.dng/.arw` 保持被 `kImgExt` 静默忽略。
- 占位符判定为**两条独立判据**（实测覆盖）：`< 24KB`（`isIcloud` 时）与 `.icloud` sidecar / `RECALL_ON_OPEN` 文件属性。门禁对两条分别构造样本。

### QA（新增 `LA_ICLOUD_QA`，15 断言全绿 + 反例）
- (A) 探测纯函数 8 项：候选数=3、首选路径、默认 PicturesLocation 非空、命中沙盒根、文件数=9、**空壳拒绝**、根下 true / 无关目录 false。
- (B) 端到端 `syncRoot` 5 项：`total=5`（3 png + 2 占位；DNG/AAE/ini/afcatalog 全部不计）、`added=3`、`placeholders=2`、`errors=0`、**幂等** `added=0 unchanged=3`。
- (C) **反例 2 项**：另建**不含 "icloud" 子串**的独立临时根跑同样内容 → `placeholders=0 added=4`，与正例 `(2, 3)` 显著不同；C2 断言正反例**必须不同**，若相同则 B3 是空心断言。实测踩到两个坑并修掉：① 1px PNG 仅 ~70 字节，会被 `isIcloud && size<24KB` 判成占位（`placeholders=6 added=0`）→ 改用撑到 ~30KB 的合法 PNG；② 反例根若沿用 `la_icloud_qa` 沙盒名，`contains("icloud")` 路径子串判定同样命中 → 反例自身失效（`placeholders=1`）→ 反例根改用 `la_plainctl_qa`。
- 门数 25 → **26**。

### 修复
- **★ 启动性能：iCloud 探测改懒构建**。`buildImportMenu()` 原在顶栏构造期**同步**调 `detectRoot()`，而 `detectRoot()` 会对候选目录做 `entryInfoList`（本机实测 iCloud 目录 **6778 个 `QFileInfo`**）—— 等于把这笔 IO 压进 UI 启动主线程。改法：构造期只建菜单骨架（**零 IO**，「导入 iCloud 相册」先置灰 + 状态行显示「正在检测…」），首次 `QMenu::aboutToShow`（用户已点开、正在等）时才探测并启用。
- `isUnderICloudRoot()` 首轮用 `p` 与 `path` 自比（恒真）→ 改为纯「逐级上剥目录链比对候选根」。
- `title` 未被 `QFutureWatcher::finished` lambda 捕获 → 编译错误 `'title' is not captured`，已加入捕获列表。

## [2.5.0] - 2026-10-02

### 工程（方向 E·发布 CI 强化：第二步 依赖与产物溯源 SBOM + 可复现性验证）
- **取证：发布链路存在 3 个真实缺陷**（本步把「能打包」推进到「产物可溯源、构建可复现」）：
  1. **许可证合规缺口（真实缺陷）**：`docs/THIRD_PARTY_NOTICE.md` 只声明了 Python 侧（历史资产）的来源，而当前优化重心 `native/src/core/CullEngine.cpp/.h` 是从 `ai-photo-manager`（MIT）派生的 pHash / DCT-II / Hamming 近邻去重实现 —— `grep -rn "MIT License|Copyright (c)|SPDX|third-party" native/src/` 命中 **0 行**，两个源文件零版权声明；更严重的是 **dist 内完全没有 license/notice 文件**，MIT 要求的版权声明随二进制分发根本没被满足。NOTICE 文件写着「已归属」，代码里却可能随时被摘掉声明且无人察觉。
  2. **产物不可溯源（SBOM缺失）**：zip 里只有 exe + Qt 运行库 + 手册 + MANIFEST，**无法回答「这个包是哪次提交、哪个 Qt、哪个 GCC 编出来的」**。版本号只出现在文件名里，包内无任何构建元数据。
  3. **★「可复现」声明是假的（locale-dependent 排序）**：`New-ReleaseManifest` 原用 `Sort-Object FullName`，而 PowerShell `Sort-Object` 默认是**区域文化序（culture-sensitive, locale-dependent）**而非序数序。实测证据：同一批文件 `Sort-Object` 排成 `a_b.txt | ab.txt | a-b.txt | zz.txt | Zz.txt`（`zz` 在 `Zz` 前），而 `StringComparer::Ordinal` 排成 `Zz.txt | a-b.txt | a_b.txt | ab.txt | zz.txt`。后果：同一棵文件树在 zh-CN / en-US Windows 上 MANIFEST 顺序不同 → 跨机器 diff 全是噪声，两次构建「不一致」其实是 locale 差异而非产物差异。
- **修复 3（序数排序）**：`New-ReleaseManifest` 改用 `[Array]::Sort($paths, [System.Comparison[System.Object]] { param($a,$b) [System.StringComparer]::Ordinal.Compare($a.rel, $b.rel) })`，并在沙盒里放入 `Zz.txt` + `zz.txt` 作为**文化序与序数序结果相反的判别对** —— 只有真正按序数排，改回 `Sort-Object` 才会转红。
- **新增 C6 `Assert-LicenseCompliance($staging, $noticeName, $srcRoot, $derivedRel)`（许可证合规）**，两半：① dist 内 `THIRD_PARTY-NOTICE.md` 存在且非空；② 声明的每个派生源文件**前 12 行内**必须有 `// SPDX-License-Identifier:` 头 —— 防 NOTICE 文件继续声称「已归属」而代码里的声明被悄悄摘掉。
- **新增 `BUILDINFO.txt`（SBOM / 构建溯源）**：由 `New-BuildInfo($staging, $name, $version, $srcRoot)` 生成，**纯 ASCII**，写入 `version / git_commit / build_type / qt_version / qt_dir / compiler / compiler_version / packaged_at / packaged_by / packaged_on_host / file_count / total_bytes / note`。**全部字段从既有文件读出，无任何手工同步项**：`build/CMakeCache.txt`（`CMAKE_BUILD_TYPE` / `CMAKE_CXX_COMPILER` / `Qt6_DIR`）、`git rev-parse --short HEAD`、`g++ --version`。生成顺序为 **trim 之后、MANIFEST 之前**（trim 改变文件集，MANIFEST 必须覆盖 BUILDINFO）。
- **BUILDINFO 被纳入 C3 断言**：`$need` 增至 `@("LightAlbum.exe", "platforms/qoffscreen.dll", "platforms/qwindows.dll", $BUILDINFO_NAME)` —— **不可追溯的 zip 不是可出货产物**。由此真实路径顺序调整为：C2/C6 在 3.7 步跑，BUILDINFO 生成后（4.5）才跑 C3（4.7）。
- **新增 C7 `Assert-ManifestReproducible($staging, $name)`（可复现性）**，两半：
  - 半 (a) **先读磁盘上现有 MANIFEST**，校验条目按 `StringComparer::Ordinal` 严格升序且 path 无重复。**顺序至关重要**：若先重生成再校验，会把被篡改/乱序的 manifest「修好」，注入的缺陷永远抓不到（这是本轮实测踩到并修复的**设计缺陷**：C7 初版自证清白，10 个注入里唯独 C7 的两条 100% 漏抓）。
  - 半 (b) 连续两次 `New-ReleaseManifest` 逐字节比对（`h2 -eq h3`），并与 on-disk `h1` 一致。
- **C7 的反例设计方法论**：「两次生成是否字节一致」无法被确定性注入（同树枚举两次必然相同），故额外引入**产物级不变量**「序数严格升序 + 无重复」作为可确定性注入的失败源：注入 ① `[array]::Reverse($ent)` 制造乱序；② 重复首条 entry。
- **SPDX 头落地**：`native/src/core/CullEngine.cpp`（+19 行）/`.h`（+3 行）加**纯 ASCII 英文** SPDX 块，含上游 URL、MIT 归属、派生点说明（pHash / DCT-II / Hamming 去重）、指向 `THIRD_PARTY-NOTICE.md`，并写明「删除此头会违反 C6 并使 `LA_RELEASE_QA` 失败」。`docs/THIRD_PARTY_NOTICE.md` 补全 C++/Qt 原生侧（能力表逐行补 `CullEngine::` 对应符号、新增第 5 条改造说明、新增「随包分发」章节）。
- **契约自检从 5 条扩到 7 条，注入从 5 条扩到 10 条**：C1×1、C2×1、C3×2（含删 BUILDINFO）、C4×1、C5×1、C6×2（含删 NOTICE / 剥 SPDX 头）、C7×2（含乱序 / 重复 entry）。**沙盒增强**：写 `THIRD_PARTY-NOTICE.md`、造假带 SPDX 头的 `src/core/CullEngine.cpp/.h`、调 `New-BuildInfo` 生成沙盒 BUILDINFO、放入 `Zz.txt` + `zz.txt` 判别对。
- **实测**：
  - `LA_RELEASE_QA PASS contracts=7 injected=10 caught=10 baseline=7 zip=9`（EXIT=0，10 个注入缺陷全部被对应契约抓住）。
  - **反例 A（断言非空心）** `release.ps1 -SelfTest -NoInject` → `FAIL reason=NOINJECT contracts=7 ... -- contract assertions unproven`（EXIT=1）。
  - **反例 B（真实 C6）**：用 Python 字节级精确删除 `CullEngine.cpp` 的 SPDX 行（`removed 1 line(s)`，`grep -c` = 0）后跑真实打包 → `throw C6 no SPDX-License-Identifier header in derived source: src/core/CullEngine.cpp`，**打包在 C6 被阻断**（EXIT=1）；已还原并核验（391 行 CRLF、SPDX 计数 1、无残留 .bak）。
  - **反例 C（真实排序缺陷）**：把序数排序改回 `Sort-Object` → `baseline:C7 did not pass on a clean sandbox`（EXIT=1）；还原后 EXIT=0 —— **证明 C7 真能抓住 locale 缺陷**。
  - **真实打包冒烟** `release.ps1 -SkipGate`：`C1 ok (2.5.0)` / `C2/C6 ok: manual present, license attribution shipped` / `SBOM: BUILDINFO.txt (14 lines)` / `C3 ok: exe + qoffscreen + qwindows + BUILDINFO all present and non-empty` / `MANIFEST: 30 files, self-verified, reproducible=E26AA669099B` / `C5 ok 17.2 MB, 31 entries` / `RELEASE OK`。（`dxcompiler.dll` 缺失 warning 属既有无害告警。）
  - **BUILDINFO 真实取值核验**：`version 2.5.0` / `git_commit e581d87` / `build_type Release` / `qt_version 6.8.3` / `qt_dir D:/qt/6.8.3/mingw_64/lib/cmake/Qt6` / `compiler D:/qt/Tools/mingw1310_64/bin/g++.exe` / `compiler_version ... 13.1.0` / `packaged_at 2026-10-02T23:20:54` / `packaged_by damin` / `packaged_on_host LUC` / `file_count 29` / `total_bytes 42219170`。（首轮 `qt_version : unknown` 系 `Get-QtVersionFromCache` 误用 `Select-String -Path $qt6Dir`（该参数是文件路径而 Qt6_DIR 是目录值）→ 静默返回空，已改用 `[regex]::Match` 修好。）
  - **MANIFEST 真实内容核验**：可解析条目 `grep -c "^[0-9a-f]{64}"` = 30；含 `BUILDINFO.txt`(525 B) 与 `THIRD_PARTY-NOTICE.md`(4869 B) 两条；首行序数为 `BUILDINFO.txt` 在 `D3Dcompiler_47.dll` 之前。
  - **产物独立可跑**：`cd native/LightAlbum-*-win64 && export QT_QPA_PLATFORM=offscreen LA_SMOKE=1 && ./LightAlbum.exe > f 2>&1` → `EXIT=0`、`174` 字节、`LA_P1_SMOKE_OK photos=6425 years=8 provs=11 span=2019-2026`（脱离 `build/` 目录）。
  - **Runner 端到端**：`qa_runner.ps1 -Only LA_RELEASE_QA` → `LA_RELEASE_QA [OK]`、`RUNNER_EXIT=0`、JSON `{"summary":{"skip":0,"pass":1,"fail":0,"check":0},"gates":[{"detail":"","mark":"[OK]","name":"LA_RELEASE_QA"}]}`。
- **纯 ASCII 铁律自纠**：`Get-QtVersionFromCache` 注释里的中文省略号 `…`（`e2 80 a6`）破坏纯 ASCII 约束 → 改`...`，复核 `nonascii 0`。
- 版本 2.4.0→**2.5.0**；门数仍 25（`LA_RELEASE_QA` 由 5 契约升级为 7 契约，非新增门）。

## [2.4.0] - 2026-10-02

### 工程（方向 E·发布 CI 强化：第一步 发布契约化 + CI 覆盖修复）
- **取证：发布链路存在 6 个真实缺陷**（本步不新增用户可见功能，而是把「能打包」变成「打包可验证」）：
  1. **`$ts` 作用域缺陷**：`release.ps1` 里 `$ts` 只在 `if (Test-Path $staging)` 分支内定义，而 `-TrimNetwork` 分支也用它给裁剪文件打时间戳 → **首次打包（staging 尚不存在）时 `$ts` 为 `$null`**，裁剪产物退化为 `Qt6Network.dll_`（无时间戳），下次 trim 静默覆盖。
  2. **CHANGELOG 与版本零校验**：版本真源在 `CMakeLists.txt`，但 `release.ps1`/`iterate.ps1` 中 `CHANGELOG` 出现次数为 **0** —— 改了版本号却没写 CHANGELOG，照样产出「可发布」zip。
  3. **手册 glob 硬编码** `LightAlbum-2.0.0-*.md`：当前版本已 2.3.0，仅因手册文件名恰好还是 2.0.0 才碰巧命中；一旦手册改名即静默不打进包。
  4. **分发物无清单**：zip 打完只打印体积，无任何 manifest —— 分发完整性不可校验、两次构建不可比对。
  5. **CI 覆盖漏洞**：`.github/workflows/qa.yml` 的 `paths` 只含 `src`/`resources`/`data` → **改动门禁运行器本身（`qa_runner.ps1`/`release.ps1`/`iterate.ps1`）完全不触发 CI**，门禁可被悄悄削弱而无人察觉。
  6. **门禁判定无留痕**：结论只在 runner 日志里，`iterate.ps1` 的 Markdown 报告写在 `TEMP` 的时间戳文件中，无人可打开。
- **5 条发布契约（C1-C5）抽为接受显式路径的独立函数**：C1 `Assert-VersionSync`（CHANGELOG 顶部 `## [x.y.z]` 必须 == CMakeLists `project(VERSION)` == 待打包版本）、C2 `Assert-ManualPresent`、C3 `Assert-CoreArtifacts`（`LightAlbum.exe` + `platforms/qoffscreen.dll` + `qwindows.dll` 非空）、C4 `Assert-ManifestConsistent`、C5 `Assert-ZipIntegrity`。**这些契约在真实打包路径里真的执行**（抛错即中止），同时被 `-SelfTest` 在一次性沙盒上驱动 —— **生产代码零 seam、无测试专用分支**。
- **`MANIFEST.txt`**：staging 内生成 `sha256 <TAB> 字节数 <TAB> 相对路径`清单，**在 trim 之后生成**以描述真正出货的内容；随包分发。
- **新增第 25 门 `LA_RELEASE_QA`（脚本门）**：`qa_runner.ps1` 新增 `kind="ps1"` 门类型（跑脚本而非 exe，KILLED 文案区分；**不依赖 exe 自退**，故不受「真实控制台」约束）。`-SelfTest` 在临时沙盒建假 CMakeLists/CHANGELOG/dist/zip，先确认干净基线 5 条全绿，再**逐个注入 5 类缺陷**并断言对应契约是唯一被抓到的那条。
- **CI 加固**：`paths` 补 `native/*.ps1`、`native/docs/**` 及 workflow 自身；新增 `Collect machine-readable gate result`（把 `TEMP` 下时间戳 JSON 拷进工作区）+ `upload-artifact` 留痕；新增**串行 `package` job**（`needs: qa`）在每次 push 真跑 `release.ps1` —— 打包器从此不再只在人的机器上运行。
- **脚本编码治理**：`release.ps1` 原含 452 个非 ASCII 字节且无 BOM → PS 5.1 按 GBK 解析，中文字符串必然乱码。**全脚本转纯 ASCII**（注释亦然），与 `qa_runner.ps1`/`iterate.ps1` 对齐。
- **实测**：
  - `LA_RELEASE_QA PASS contracts=5 injected=5 caught=5 baseline=5 zip=6`（EXIT=0，5 个注入缺陷全部被对应契约抓住）。
  - **反例** `release.ps1 -SelfTest -NoInject` → `FAIL reason=NOINJECT contracts=5 injected=0 caught=0 -- contract assertions unproven`（EXIT=1）；经 `qa_runner` 判定为 `[FAIL]`（端到端非空心）。
  - **真实 C1 反例**：把 CHANGELOG 顶部改为 `2.9.9` → `C1 version mismatch: CMakeLists=2.3.0 CHANGELOG=2.9.9`，打包**前**即中止（EXIT=1），文件已还原。
  - **真实打包冒烟** `release.ps1 -SkipGate`：`C1 ok: CHANGELOG top entry matches CMakeLists (2.4.0)` / `C2/C3 ok` / `MANIFEST: MANIFEST.txt (28 files, self-verified)` / `C5 ok: ...LightAlbum-2.4.0-win64.zip (17.2 MB, 29 entries)` / `RELEASE OK`。
  - **产物独立可跑**：在 dist 目录内 `QT_QPA_PLATFORM=offscreen LA_SMOKE=1 ./LightAlbum.exe` → `LA_P1_SMOKE_OK photos=6425 years=8 provs=11 span=2019-2026`（EXIT=0，脱离 `build/`）。
  - `iterate.ps1 -NoBuild -Only LA_RELEASE_QA` → `ITERATION VERDICT: GO`；workflow YAML 校验通过（`qa` 8 步 → `package` 7 步 `needs: qa`）。
  - 回归：`LA_GALLERY_BATCH_QA` / `LA_BATCH_TX_QA` / `LA_THUMB_CACHE_QA` / `LA_SMOKE` / `LA_SCALE_SMOKE` / `LA_SCALE_PERF` / `LA_GALLERY_E_QA` 全绿。
- 版本 2.3.0→**2.4.0**；`qa_runner` 24 门 → **25 门**。

## [2.3.0] - 2026-10-02

### 功能（方向 D·规模化与流畅性：第五步 b 图库多选 + 批量工具条）
- **交付「批量操作」的用户可见入口**（2.2.9 只建了原子批量写层，本步接线到 UI，方向 D至此完整闭环）。
- **`EditorialView` 多选状态机**（沿用主流图库语义，**不破坏**既有单击开灯箱）：
  - 无修饰 → 单选（清多选集）+ 打开灯箱；**Ctrl** → 切换该行选中；**Shift** → 以焦点行为锚点做范围选择。
  - `GalleryTile::activated` 改为携带 `Qt::KeyboardModifiers`，点击统一走`clickRow(row, mods)`；多选手势**不触发灯箱**。
  - 选中态改由 `selRows_`（`QSet<int>` 模型行号）驱动，池化瓦片重绑（`updateWindow`）与 `refreshSelectionVisual()` 共用同一判定——虚拟化下滚出再滚回选中态不丢。
  - 新增 API：`selectedRows`/`selectedPaths`/`selectAll`/`clearSelection`/`toggleRow`/`clickRow`；新增信号 `selectionChanged(count)`。
- **`GalleryPage` 批量工具条**：位于计数与视图之间，**选中为空时整条隐藏**（不占视觉空间）；含 `已选 N 张` + `★1..★5` + `🔒 私密` + `取消选择`。计数文案追加「已选 N」注记。
- **架构铁律：视图层不持有写能力**——`GalleryPage` 只有 `const QVector<Photo>*`（只读借用），按钮只发 `batchRatingRequested` / `batchPrivateRequested` 信号，**库写入一律在 `MainWindow`（持 `lib_`）执行**，复用 `Library::setRatings`/`setPrivates` 的单事务原子语义；结果以 `QMessageBox` 反馈「请求 N 张 / 成功 M 张 / 已原子提交 或 已整体回滚: 原因」。批量私密后额外 `setHidePrivate(hidePrivate())` 重跑隐私过滤。
- **新增第 24 门 `LA_GALLERY_BATCH_QA`**（headless 自建临时库，不碰真实 db）：断言 ① 工具条初始隐藏/有选中显示/取消回隐 + 5 个星级按钮 ② Ctrl 累加与再点取消，**且不触发灯箱** ③ Shift 范围（2→5 得 4 张、反向 2→1 得 2 张）④ 无修饰点击必触发灯箱（既有行为守护）⑤ 越界 row 不改变选中 / 全选 / 取消边界 ⑥ 批量 signal 载荷（paths 数与评分值正确、触发后自动清选中）⑦ **端到端**：signal → `Library::setRatings` → DB 与内存副本一致（各 3 张 rating=4）。
- **实测**：
  - `LA_GALLERY_BATCH_QA PASS rows=8 ctrl=3 range=4 lamp=1 selectAll=8 stars=5 ratingSig=1 privSig=1 e2e=3 bar=1`（EXIT=0）。
  - **反例** `LA_BATCH_QA_LAMPASS=1`（强制多选手势也开灯箱）正确 `FAIL reason=LAMPASS: ctrl click must NOT open lightbox, but gate forced it open`（EXIT=1，非空心）。
  - 回归全绿：`LA_GALLERY_E_QA featureOK=1` / `LA_SCALE_SMOKE PASS photos=6425 galleryCtrls=60`（51→60 为批量条 8 子控件 +1 bar）/ `LA_SCALE_PERF PASS firstMs=40 scrollMaxMs=18` / `LA_BATCH_TX_QA PASS` / `LA_THUMB_CACHE_QA PASS` / `LA_GALLERY_SEARCH_QA marks=7` / `LA_P1_SMOKE_OK` / `LA_MAP_CLICK_QA all=OK(10/10)`。
- **版本 2.2.9→2.3.0（MINOR：新用户可见功能）**；`qa_runner` 23 门 → **24 门**。至此方向 D（规模化与流畅性）六向全部落地。
## [2.2.9] - 2026-10-02

### 功能（方向 D·规模化与流畅性：第五步批量写回事务化·第一阶段数据层）
- **取证：native 侧写回面根本没有批量入口**——`Library` 只有单条 `setRating`/`setPrivate`，唯一调用方是灯箱点星（`MainWindow.cpp:208`）。所谓「批量操作」零实现，故本步先补**原子批量写层 + 首个可用批量 API**。同时定位一个隐性复杂度问题：`findMutable` 原为**线性扫描 6425 项**，批量场景会退化成 O(N²)（6425 条 × 6425 次比较 ≈ 4×10⁷）。
- **`BatchResult` + 原子批量 API**：`setRatings(paths, rating)` / `setPrivates(paths, priv)`，单条 API 改为其特例（长度为 1 的批量）——**统一唯一事务入口，禁旁路直写**。
  - **单事务原子**：任一条 `exec` 失败或 `commit` 失败 → 整体 `rollback()`，`done` 归零并给出 `failedIndex`（首个失败下标）+ `error`（SQL 原文）。
  - **prepared statement 只prepare 一次**，循环 `addBindValue`+`exec` → 6425 条也只解析一次 SQL。
  - **内存副本仅在 commit 成功后同步**——否则中途失败会出现「DB 已回滚、内存已改」的撕裂（本步刻意防御点）。
  - 路径不存在 → `numRowsAffected()==0`，`done` 少但 `ok=true`（**不是错误**）；空批 → 成功空操作。
- **O(1) path 索引**：`pathIx_`（`QHash<QString,int>`）+ `rebuildPathIndex()`（`photos_` 排序完成后建，保证下标与最终顺序一致）+ `verifyPathIndex()`（QA 自检双向一致）；`findMutable` 改走索引，**禁线性扫描**。
- **新增第 23 门 `LA_BATCH_TX_QA`**（headless 自建临时库，**不碰真实 library.db**）：断言 ① 索引双向一致 ② 空批=成功空操作 ③ 批量打星原子成功且 DB↔内存同步 ④ 不存在路径→`done` 少但仍成功 ⑤ **失败整体回滚**（DB 与内存都保持原值、**无撕裂**——核心红线）⑥ rating 越界 `qBound` 收敛（9→5, -3→0）⑦ 批量隐私标记同语义 ⑧ 触发器注入下 `failedIndex` 定位精度。
  - 失败源用**生产代码零 seam** 的两种真实手段：默认 `BEGIN EXCLUSIVE` 独占写锁（UPDATE 必SQLITE_BUSY）；`LA_BATCH_TX_QA_FAIL_AT=<n>` 用 QA 自建 `BEFORE UPDATE ... RAISE(ABORT)` 触发器在第 n 条精确失败。
- **实测**：
  - `LA_BATCH_TX_QA PASS rows=6 failAt=-1 idx=1 empty=1 atomic=1 rollback=1 clamp=1 priv=1`（EXIT=0）。
  - `LA_BATCH_TX_QA_FAIL_AT=3` 触发器精确注入 → `PASS ... failAt=3`（EXIT=0，断言`failedIndex==3` 且整体回滚）。
  - **反例** `LA_BATCH_TX_QA_NOLOCK=1`（不制造失败源）正确 `FAIL reason=NOLOCK: expected FAIL but batch succeeded (assertion is vacuous)`（EXIT=1）——**证明回滚断言非恒真**。
  - 回归全绿：`LA_THUMB_CACHE_QA PASS` / `LA_SCALE_SMOKE PASS photos=6425 galleryCtrls=51` / `LA_SCALE_PERF PASS firstMs=42 scrollMaxMs=19` / `LA_P1_SMOKE_OK` / `LA_GALLERY_SEARCH_QA PASS marks=7`。
- 版本 2.2.8→2.2.9；`qa_runner` 22 门 → **23 门**。
## [2.2.8] - 2026-10-02

### 功能（方向 D·规模化与流畅性：第四步缩略图多级缓存）
- **`ThumbCache` 两级 +按字节预算 LRU（替代单级 360px / 按张数计费）**：
  - **取证根因**：旧实现单一 360px 级 + `QCache::setMaxCost(4096)` 按**张数**计费，每张 360×360×4B ≈ 506KB → 上限 **4096×506KB ≈ 1.98 GiB**；且 216px 普通瓦片与 feature 大图（~468px）共用同一张 360px 图——大图不够大、小图白白占内存。
  - 改为 **L1 `kTile` 256px**（普通瓦片）/ **L2 `kFeature` 512px**（feature 2×2）两级，**按字节预算**淘汰：默认 `LA_THUMB_BUDGET1`=64MiB + `LA_THUMB_BUDGET2`=48MiB，合计 ~112MiB（**远低于旧 1.98GiB**）。
  - **单次解码一次派生两级**（`LoadTask` 复用同一份源像素，避免二次读盘/解码）。
  - **刻意保留的旧行为**：① 负缓存（`negative_` 跨级共享，防重试风暴；旧实现用「空 QPixmap 存进 QCache」表达同语义）；② `pending_` 按文件名去重 + `finish()` 经 `QueuedConnection` 回主线程；③ `QPointer` 生命周期守卫；④ 某级因预算淘汰时 `cached()` **回落另一级**（宁可给大图也不给空图）且不重复触发后台加载。
  - 实现要点：`ByteLru` = `QHash<QString,QPixmap>` + `mutable QList<QString> order_`（front=MRU，淘汰从尾取）；`cost(pm)=w*h*(depth/8)`（**`QPixmap` 无 `sizeInBytes()`，那是 `QImage` 的 API**）。
- **接线（API 向后兼容）**：`GalleryModel::pixmapAt(row, level=kTile)` 增level 默认参数并透传；`GalleryTile::paintEvent` 按 `feature_` 选级、`prefetchVisible()` 按 row==0 选级。既有调用点零改动。
- **新增第 22 门 `LA_THUMB_CACHE_QA`**（headless 无需 db，现场合成 8 张 512×512 样图）：断言①一次解码派生两级且两级像素不同 ② L1 字节 < L2 字节 ③ 同级重复 `request` 按文件名去重（任务数 == 文件数）④ 负缓存（缺文件重复 request 只触发一次后台任务、两回调皆空图）⑤ 字节预算生效（压小预算→该级张数受限且 `bytes<=budget`）⑥ 跨级回落（淘汰后 `cached()` 回落另一级、不重触发加载）。
- **实测**：`LA_THUMB_CACHE_QA PASS files=8 px=256/512 l1=8 l2=8 bytes=2097152/8388608 derived=16 req=8 evictL1=2 evictL2=1`（EXIT=0）；8 图 L1 仅 2MiB、L2 8MiB。**反例** `LA_THUMB_TILE_PX=32`（预算放得下全部→不淘汰）正确 `FAIL reason=B_evict l1count=8`（EXIT=1，非空心）。回归：`LA_GALLERY_E_QA featureOK=1`、`LA_SCALE_SMOKE PASS photos=6425 galleryCtrls=51`、`LA_SCALE_PERF PASS firstMs=46 scrollMaxMs=14`、`LA_SMOKE LA_P1_SMOKE_OK`、`LA_MAP_A_QA jade=17`、`LA_GALLERY_SEARCH_QA PASS marks=7` 全绿。
- 版本 2.2.7→2.2.8；`qa_runner` 21 门 → **22 门**。
## [2.2.7] - 2026-10-02

### 功能（方向 D·规模化与流畅性：第三步列表虚拟化）
- **`EditorialView` 窗口化虚拟化（列表虚拟化落地）**：彻底移除「`rebuild()` 为每张照片 `new GalleryTile` 控件（6425 个）」的非虚拟化实现——这正是 D 第二步 `LA_SCALE_PERF` 基线指出的 `firstMs≈815–994ms` 主因。改为：
  - 去 `QGridLayout`，`content_` 设为**虚拟总高**（feature 2×2 + 流式网格），由该高度驱动滚动条；
  - **池化 `GalleryTile`**（`pool_`/`free_`/`bound_`）：仅实例化「可见视口 + 上下各 2 行缓冲」的瓦片；`scroll`/`resize` 时按可见模型行 `recycle` + `setRow(row,feature)` 重绑复用，杜绝逐图 new/delete；`GalleryTile` 新增 `setRow()` 支持池化重绑。
  - `relayout()` 重算列数/单元格/虚拟高并 `updateWindow()`；`resizeEvent`(60ms 去抖)/`valueChanged` 触发重算与重定位；`prefetchVisible()` 改为仅预取当前绑定（可见）行 ±1。
  - **对外 API 语义零破坏**：`featureGeometry()`/`tileGeometry(int)`/`tileCount()`(=模型行数)/`columnCount()`/`prefetchVisible()`/`photoActivated` 全部保留等价行为，确保 `LA_GALLERY_E_QA`(几何/颜色)、`LA_SCALE_SMOKE`、`LA_SCALE_PERF` 不破。
- **实测收益（D 第二步基线守护兑现「虚拟化落地后数值显著下降」）**：`LA_SCALE_PERF` 首屏 `firstMs` 由 ~841ms **降至 41ms（约 20×）**，滚动帧 `scrollMaxMs` 保持 15ms（≤50）；`galleryCtrls` 由 6470 **降至 51**（仅可见池化瓦片 + chrome）。`LA_GALLERY_E_QA` `featureOK=1` 不变；`LA_SCALE_SMOKE` 仍 `PASS photos=6425`。
- **反例确证非空心**：`LA_SCALE_PERF_FIRST=30`（<实测 38/41）正确 `FAIL`（`reason=firstMs=38>budget=30`），回归红线真实有效。
- 版本 2.2.6→2.2.7；`qa_runner` 21 门不受影响（`galleryCtrls` 断言仅要求 ≥0）。

## [2.2.6] - 2026-10-02

### 功能（方向 D·规模化与流畅性：第二步流畅性基线门禁）
- **新增门禁 `LA_SCALE_PERF`（规模化流畅性基线）**：复用 `LA_SCALE_SMOKE` 库定位逻辑（env
  `LA_SCALE_DB` → `LA_DB` → 向上搜 `_scale_fixture.db` 合成 6425 张），构造 `GalleryPage`
  量化 ① 首屏耗时（`firstMs`：构造→首绘，含 6425 瓦片实例化 + 布局 + 可见区预取）② N 次滚动跳变帧
  paint 均值/峰值（`scrollAvgMs`/`scrollMaxMs`，保持 <120ms 节流、隔离纯 paint、不触发缩略图预取）。
  预算经 env 覆盖：`LA_SCALE_PERF_FIRST`（默认 3000ms）/ `LA_SCALE_PERF_SCROLL`（默认 50ms）/
  `LA_SCALE_PERF_JUMPS`（默认 20）。`GalleryPage` 新增公开访问器 `qaScrollBar()` 暴露
  `EditorialView` 滚动条供 offscreen 模拟滚动。断言 `photos≥1000` 且两项 ≤ 预算。
- **取证结论（先测再改基线）**：当前 `EditorialView` 在 `rebuild()` 实例化**全部 6425 个 `GalleryTile`
  控件**（非虚拟化），`firstMs≈815–994ms`、滚动帧 `≤12ms`；`ctrls=6470`（6425 瓦片 + ~45 chrome）。
  本门为后续「列表虚拟化」优化（§8.2）建立可回归守护——虚拟化落地后数值将显著下降，届时收紧预算。
- **双 QApplication 铁律再守**：`runScalePerf()` 复用 `main()` 既有 `QApplication`，仅自建 `Library`，
  不新建 `QCoreApplication`（避免退场 `SIGSEGV`）。
- **实测**：`LA_SCALE_PERF PASS photos=6425 firstMs=841 scrollAvgMs=10 scrollMaxMs=12`
  `ctrls=6470`（EXIT=0）；`LA_SCALE_PERF_FIRST=500` 时正确 `FAIL`（阈值断言生效，非空心 PASS）；
  `qa_runner -Only` 报告 `[OK]`。`qa_runner.ps1` 登记第 21 门。版本 2.2.5→2.2.6。

## [2.2.5] - 2026-10-02

### 功能（方向 D·规模化与流畅性：第一步真实库回归门禁）
- **新增门禁 `LA_SCALE_SMOKE`（真实库规模化回归）**：加载真实库（env
  `LA_SCALE_DB` 优先 → `LA_DB` → 从 exe 目录向上搜索提交于仓库根的
  `_scale_fixture.db`，合成 6425 张镜像真库规模），断言 `open` 成功、照片数
  `≥ LA_SCALE_MIN`（默认 1000）、`kpi` 已填充（`kpi.total==n` / `yearCount≥1` /
  `provCount≥1`）；构造 `GalleryPage`（真实图库网格）+ `MapView`（真实 GPS 足迹）
  offscreen 不崩、首屏渲染不超时（`mapMs ≤ LA_SCALE_MAP_MS` 默认 15000）。
- **合成 scale fixture `_scale_fixture.db`（6425 张，3.18MB）**：`_gen_scale_fixture.py`
  生成，覆盖 18 年份 / 15 省份 / ~80% GPS / 8 分类 / 人物；缩略图故意指向不存在文件
  （验证 `ThumbCache` 负缓存兜底缺失缩略图不崩）。`LA_SCALE_DB` 可指向用户真库覆盖。
- **双 QApplication 铁律再守**：`runScaleSmoke()` 复用 `main()` 既有 `QApplication`，
  仅自建 `Library` 打开真实库，不新建 `QCoreApplication`（避免退场 `SIGSEGV`）。
- **实测**：`LA_SCALE_SMOKE PASS photos=6425 years=18 provs=15 span=2008-2025 gps=5140`
  `galleryCtrls=6470 mapMs=4`，退出码 0；`LA_SCALE_MIN=10000` 时正确 `FAIL`（阈值断言生效，
  非空心 PASS）。版本 2.2.4→2.2.5。

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
