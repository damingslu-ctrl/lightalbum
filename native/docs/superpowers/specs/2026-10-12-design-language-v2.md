# 光影集 · 设计语言 v2（P1）规格说明书

**日期**：2026-10-12 · **状态**：待用户评审 · **上游**：`2026-10-11-ui-premium-audit.md`（子轨 0 审视报告）
**范围**：本文档只覆盖升级路线中的 **P1（设计语言 tokens + 组件单源 + 棘轮门禁）**。P0 缺陷修复、P2 图标铺开、P3 逐页精修（含新设置页）、P4 收口各自独立立项。
**评审记录**：五节（语义色板 / 字阶 / 间距·圆角·elevation / 组件单源 / 棘轮门禁）已于 2026-10-12 逐节呈现并全部获用户批准（"都接受，按你推荐选择"）。

---

## 0. 总纲与两处勘误

**总判断**：19 枚 `Lumen::` token 的设计本身成立；失败在采用率——`src/ui` 有 434 处 hex 字面量（75 个不同值、约 53 个 off-token），RetouchPage 90 hex/0 token 引用。设计语言 v2 = **保留曜石基底 + 语义角色写死 + 给散落颜色一个回家地址 + 棘轮门禁守卫**。不重造风格，把首页「Quiet Frame」已达到的水准提炼为全应用可执行契约。

### 勘误 K1（订正审视报告 D5）

审视报告称修图页紫色按钮"跳出翡翠+金色板、off-brand"。核实：`RetouchPage.cpp:333/525/530` 的 `#a88be5`/`#6d54a8` 恰为 `VIOLET`/`VIOLET_DK` token 值——VIOLET 本就是 Theme.h 注释钦定的"AI 智能体专属"强调色，**色合规，真缺陷是字面量粘贴**（该页未 include Theme.h）。修复 = 改引用 + 新增 `btnViolet()` 组件化（§4.2）。日志页 ISO 系列色 `#4b3f7a` 才是真 off-token，归入系列色板收编（§1.4）。

### 勘误 K2（订正 §4.4 评审时的图标推荐）

评审时推荐了 Segoe Fluent/MDL2 系统图标字体，**本节改为 qrc 内嵌 SVG**，依据两条实证：
1. `iconengines/qsvgicon.dll` + `Qt6Svg.dll` 自 2.0.0 起已随每个发布包分发（本次核实 `LightAlbum-2.36.0-win64/` 与 `native/build/` 均在），SVG 路线零新增依赖、zip 体积不变；
2. 审视 D4 的豆腐块正是"图标依赖环境字体"的系统性后果——offscreen QA 里系统字体回退已经失败过一次；SVG 由 QtSvg 自绘，跨 Win10/Win11、跨 offscreen 完全确定，且线宽风格可控（高级感需要的细描边单色线性图标，字体 glyph 给不了）。

系统字体方案的全部优势（零资产）抵不过一条"渲染不确定"。图标机制在本 spec 定版（§4.4），34 处铺开属 P2。

---

## 1. 语义色板

### 1.1 分层基底（保留，不动）

| 角色 | Token | 值 | 用途锁死 |
|---|---|---|---|
| 画布 | BG | #060806 | 仅窗口底 |
| 面板 | PANEL | #0c100d | 侧栏/工具条/弹层容器 |
| 卡片 | CARD | #131a15 | 一切可交互容器 |
| 卡片悬停 | CARD_HI | #1a231d | hover/active，禁止做默认底色 |
| 内凹 | INSET | #0e1410 | 输入框/时间轴轨道 |
| 描线 | LINE | #26312a | 唯一分隔线色，禁止暖灰替代 |

### 1.2 文本层级（保留）+ typo 清退

INK #f4f7f4 / MUTED #8da396 / FAINT #5a6b60 三级不变。`#8da996`（MUTED 的抄写变形，22 处）为错别字，机械替换为 `Lumen::MUTED`，不属设计决策。

### 1.3 强调色职责表（v2 核心增量）

| 强调 | 主 / 亮 / 暗 | 唯一职责 | 禁止 |
|---|---|---|---|
| 翡翠 EMERALD | #4fc08a / #6adba6 / #2f7d57 | 确认、选中、进度、主操作 | 做数据分类色（CAT 板除外，见 1.4 裁定） |
| 金 GOLD | #d9a94e / #f0d08a / #b0863a | 星级、荣誉、稀缺标记 | 做报错色 |
| 紫 VIOLET | #a88be5 / — / #6d54a8 | **AI 专属**（智能体、AI 按钮、AI 徽章） | 非 AI 功能一律禁用 |
| 珊瑚 CORAL | #e0705f / — / CORAL_DK（新增） | 破坏性/删除 | 做分类色（CAT 板除外） |

### 1.4 数据分类色板 CAT1..CAT8（新增）

现有两处私自调色板全部收编：LogPage 图表 6 色（`LogPage.cpp:465-467`：#3f6098 #c19a4b #4b3f7a #c76b3a #5a8f6b #b06a8a）晋升为命名序列；GalleryPage 分类 chips 的 8 色 hash 盘（`GalleryPage.cpp:38-44`，FNV hash 取模）**砍掉随机分配，改类目固定映射**——同一文件夹换版本即变色是"低廉感"真实来源。

| 位 | 名 | 值 | 来源 |
|---|---|---|---|
| CAT1 | jade | #4fc08a（=EMERALD） | 收编 #5a8f6b |
| CAT2 | sky | #5b9cd6 | 新 token，收编 #3f6098 |
| CAT3 | gold | #d9a94e（=GOLD） | 收编 #c19a4b |
| CAT4 | violet | #a88be5（=VIOLET） | 收编 #4b3f7a |
| CAT5 | coral | #e0705f（=CORAL） | 收编 #c76b3a |
| CAT6 | rose | #cf86a8 | 新 token，收编 #b06a8a |
| CAT7 | teal | #57b6a4 | 新 token |
| CAT8 | olive | #9fb75a | 新 token |

**裁定**：CAT 板是数据编码语言，不受"VIOLET=AI 专属"控制面职责约束；AI-ness 只由控件（按钮/徽章）表达。四枚新色标称 AA 图形对比（≥3:1 vs CARD），**P4 用对比度工具全量复检**，此处值以真机截图校准为准。

### 1.5 照片上文字板 INK_IMG 三档（新增）

HomePage 暖灰三阶（#e8e4da/#98a29a/#6d7a6f + chip 文字 #7d887f）是刻意的编辑手法——叠加在照片内容上，而非 UI 表面。**豁免为命名 token**：`INK_IMG` #e8e4da、`MUTED_IMG` #98a29a、`FAINT_IMG` #6d7a6f，**仅允许用于照片/影像叠加层**；#7d887f → MUTED_IMG。规则：离开照片表面即违规。

### 1.6 发光 token（首次成文）

全仓唯一手写 glow `#5fdca6`（`MapView.cpp:1651+`）收编为预设：`Glow::jade` = EMERALD_HI @45% alpha、`Glow::gold` = GOLD_HI @40% alpha。glow 是翡翠/金的**荣誉性** elevation，与 §3.4 中性阴影分轨；动效辉光一律用 palette color 实现（CSS 会钉死 font-size，禁止字号途径——既有引擎约束）。

### 1.7 off-token 回家映射表（全量）

| 现状字面量 | 站点 | 去处 |
|---|---|---|
| #8da996 ×22 | GalleryPage:119/139/148/157/166/247/334/455/523、CullPage:655/861、DedupDialog:52/205、LogPage:282/298/512 等 | MUTED |
| #d9e4dc ×17 | DedupDialog:186、MapPage:518、菜单族 | INK |
| #0e130f ×8 / #1d2a22 ×8 | 菜单底/边族 | PANEL / LINE |
| #0f1512 | LogPage:239/258/514 | INSET |
| #7fe6ac | LogPage:511 | EMERALD_HI |
| #9db2a5 | MainWindow:187 | MUTED |
| #3a4a40 | MainWindow:413（淡出态） | FAINT |
| #9fa8a3 | RetouchPage:175 | MUTED |
| #d2f0e0 | RetouchPage:1318 | INK_IMG（照片叠加提示）或 INK，按承载面判定 |
| #1c2620 | DedupDialog:58/172 | LINE |
| #101710 / #232d26 | GalleryPage:454/522（chip 克隆盘） | CARD / LINE |
| #0a0e0b | MainWindow:507 | PANEL |
| #4a2630 / #e6a3a3 | MainWindow:675（破坏按钮私有盘） | btnOutline(CORAL)（§4.1） |
| #24312a | Theme.h 内滚动条把手 | 归并 LINE（改 Theme 自身） |
| #5fdca6 | MapView glow | Glow::jade |
| #a88be5/#6d54a8/#4fc08a/#f4f7f4/#d9a94e/#26312a/#131a15/#0e1410/#6adba6/#2f7d57/#e0705f 等 token 值字面量 | 全仓 | 改 `Lumen::` 引用 + `hex()`，色不变 |
| 8 色 hash chip 盘 | GalleryPage:38-44 | CAT1..8 固定映射 |
| 图表 6 色 | LogPage:465-467 | CAT 板（1.4 对应关系） |
| HomePage 暖灰 #e8e4da/#98a29a/#6d7a6f/#7d887f | HomePage:727/732/743/863 | INK_IMG/MUTED_IMG/FAINT_IMG/MUTED_IMG |

长尾残值（75 distinct 中未列名者）在实施计划中逐条归并；**归并原则：就近语义，不许发明新值**——新值只能以新 token 形式进 Theme.h 并走评审。

---

## 2. 字阶

**病灶**：19 档字号混用（8.5–46），QSS px 与 QFont pt 两套单位并行（LogPage 标题 20pt vs MainWindow 标题 18px），font-weight bold/600/700 混写，衬线体从 HomePage 泄漏（CullPage:631 Georgia 14px）。

### 2.1 六档阶梯（pt 为唯一法源）

| 档 | 名称 | 取值 | 字体/字重 | 用途锁死 |
|---|---|---|---|---|
| T1 | Display | 20pt | Serif Bold | 首页英雄区；Splash 46pt 唯一豁免 |
| T2 | Title | 15pt | Serif 或 YaHei Bold | 页面主标题（每页至多 1） |
| T3 | Head | 12pt | YaHei Bold | 区块/弹层标题 |
| T4 | Body | 9.5pt | YaHei Normal | 全局基线（applyAppFont 现值，不动） |
| T5 | Small | 9pt | YaHei Normal | 卡片元数据、图注、按钮副文案 |
| T6 | Micro | 8.5pt | YaHei Normal/Bold（画布场景而定） | 画布内徽章/版本号（Painter 专用，UI 控件禁用） |

归并：10/10.5/11/11.5→T5/T4；13/14/16/17/18→T3/T2。阶梯刻意紧于 1.25 等比——深色 UI 中 ≤1pt 差不可辨，19 档收敛到 6 档层级反而清晰。

### 2.2 单源 API

```cpp
namespace Type {
  QFont display(); QFont title(bool serif = true); QFont head();
  QFont body();    QFont small();               QFont mono(qreal pt = 9.5);
  enum class Tier { Display, Title, Head, Body, Small };
  QString css(Tier t);   // 内部统一 pt→px(×4/3) 换算，输出 stylesheet 片段
}
```

页面禁止再手调 `setPointSizeF` / 在 QSS 写 `font-size:`（豁免区同 §5.1）。`mono()` 收编 LogPage 两处 Consolas（:298/:511）与 CullPage 雷达数值。

### 2.3 衬线 = 编辑特权

Georgia+NotoSerifSC（`serifFont`，Theme.h:82）只出现在 T1/T2；T3 及以下一律 YaHei。一处出现两次就两次都掉价——CullPage:631 降为 T3。

### 2.4 字重与字距

- 字重三档：Normal(400)/Demibold(600，仅由 helper 生成)/Bold(700)；散落的 `font-weight:600/700/bold` 走 helper。
- letter-spacing 只属于小号大写拉丁（small-caps 风味）：T5/T6 场景 1–3px；中文正文一律 0。HomePage:234 的 3px 保留为签名用法，MainWindow:187 的 1px 归并 helper。

---

## 3. 间距 · 圆角 · elevation

**病灶**：圆角 16 档（2/4/5/6/7/8/9/10/11/12/14/16/17/22/24/999）；spacing/margin 字面量 153 处/11 文件无网格；描边 1px/1.5px/2px 混用；**全仓 0 处阴影**——所有表面浮在同一平面。

### 3.1 圆角五档 + 嵌套律

| 档 | 值 | 用途 | 收编来源 |
|---|---|---|---|
| r-micro | 4px | 图例色块、滚动条把手、微型角标 | 2/4 |
| r-inset | 6px | 输入框、进度条、滑轨（把手=高/2 自动） | 5/6/7 |
| r-ctl | 10px | 按钮、下拉、工具块 | 8/9/11 |
| r-card | 14px | 卡片、框容器、弹层 | 12/14/16 |
| r-feature | 20px | 首页英雄件、Splash、Toast | 17/22/24 |
| r-pill | 999px | 徽章、筛选 chip（唯一胶囊来源） | 999 |

**嵌套律**：外层圆角 = 内层圆角 + 内层 padding，容差 ±2px。违反例处置：CullPage:630（22px+1.5px 金边）→ r-pill + 1px；MainWindow:1179（24px 徽章）→ r-card。

### 3.2 描边：1px 唯一宽度

1.5px/2px 全废（CullPage:630、RetouchPage:71-72）。强调靠颜色不靠加粗边：选中态 = 1px EMERALD + CARD_HI 底。深底上 Fusion 渲染 1.5px 会抖在 1–2px 之间，粗边只会糊。

### 3.3 间距：4px 基网格

`Space::` 常量 **4 / 8 / 12 / 16 / 24 / 32**（xs/s/m/l/xl/2xl）。页面外边距 24、区块 16–24、卡片内 12–16、控件间 4–8；153 处字面量就近归格（10→12、14→16、18→16、20→24）。**不对称留白是编辑手法，仅 HomePage 允许**；其余页面对称走格点。

### 3.4 elevation 三级（首次引入）

深底黑阴影肉眼极限低，主语言是"表面台阶 + 1px 顶缘亮线"，阴影只做压舱石：

| 级 | 表面 | 描边 | 阴影 | 适用 |
|---|---|---|---|---|
| E0 | CARD | LINE 1px | 无 | 常驻内容卡 |
| E1 | CARD | 顶缘 rgba(244,247,244,.05) 1px | 0 6px 20px rgba(0,0,0,.50) | hover 浮起、下拉、Toast |
| E2 | CARD_HI/PANEL | LINE 1px | 0 14px 36px rgba(0,0,0,.62) | 模态、lightbox、右键菜单 |

```cpp
namespace Elev { void apply(QWidget*, Level); }  // 内部挂 QGraphicsDropShadowEffect
```

弹层与背景零深度差是"平"感最大单一来源，DedupDialog 等 E2 化是首批受益者。glow（§1.6）继续作为荣誉性 elevation 分轨存在。

---

## 4. 组件单源

**口号：组件只有工厂出货，页面不许自缝。** 除画布绘制（GalleryCardPainter、MapView）与 Splash 外，页面 QSS 出现 background/border/radius/font-size 即违规。

### 4.1 组件定编

| helper | 状态 | 职责 |
|---|---|---|
| btnJade()/btnGold() | 已有（Theme.h:102-128） | 唯一实心渐变操作按钮 |
| btnGhost() | 已有（:137） | 默认中性按钮 |
| **btnViolet()** | **新增** | VIOLET→VIOLET_DK 渐变，AI 动作第四形态（替代 RetouchPage:333 字面量粘贴） |
| **btnOutline(accent)** | **新增** | 描边按钮参数化单源（jade/coral/gold/line 四例：CullPage:535-541、DedupDialog:204、MainWindow:675） |
| chip(accent) | 已有（:175） | 唯一胶囊徽章 |
| input()/combo() | input 已有（:201） | QLineEdit/QComboBox 单源（GalleryPage:190、RetouchPage:529 收编） |
| slider(accent) | 已有（:227） | 把手尺寸按槽高推导（RetouchPage:440/486/757 三处手写收编；:757 与 :486 的 14/12px 差异纯手抖） |
| progress() | 已有（:236） | RetouchPage:273 手写版收编 |
| menu()/toast() | 已有 | 右键菜单、通知 |

### 4.2 六套按钮配方归并路线

- RetouchPage:337/420/452/476/479/490/509（"131a15+26312a+8px"家族）→ `btnGhost()`；
- GalleryPage:138-167 + MapPage:46-105（btnGhost 仿制品，9 vs 8px）→ `btnGhost()`；
- RetouchPage:326-334（AI 紫渐变）→ `btnViolet()`；
- DedupDialog:204、MainWindow:675（破坏色私有盘）→ `btnOutline(CORAL)`;
- 特殊尺寸件（HomePage:135 等）→ helper + Radius/Elev 参数覆盖，不重写配方。

### 4.3 迁移顺序（风险低→高）

slider/input/progress（纯换调用）→ 按钮配方收编（含两个新 helper）→ 图标（P2）。全程每步过 `LA_UI_SHOT` 七页比对。

### 4.4 图标机制（定版：qrc SVG；铺开属 P2）

依据勘误 K2。契约：

```cpp
namespace Icon {
  QIcon get(const char* name, QColor tint, qreal px);  // QSvgRenderer→QPixmap@2x dpr，带缓存
}
```

- 资产：`native/src/ui/icons/*.svg` 经 qrc 内嵌；**24×24 网格、2px 描边、圆头端点、单色 currentColor**（tint 由调用方传 token 色），线性风格与「Quiet Frame」一致；
- 文案净化：emoji 一律移出 `tr()` 串（RetouchPage:490 `"🧹 AI消除"` 现状污染翻译语料且 lupdate 禁用区不可重跑）；图标经 `setIcon`/helper 前缀拼接，新代码禁止字形进翻译串；
- Theme.h:350/366 的 ▾✓ 为 Qt 原生菜单/勾选机制所需，保留；
- 34 处/12 文件逐站映射清单在 P2 实施计划中枚举，预计收敛为约 20 枚图标资产。

---

## 5. 棘轮门禁 LA_PALETTE_QA

### 5.1 形态

- **PowerShell 脚本门，FULL tier**（名册既有约束 E9d：脚本门不得入 fast）；注册进 `gate_matrix.ps1` 门表——注释行**纯 ASCII**（铁律 37），名册 71→72（fast 18 + full 54）；E11/E12 无需改动（其守卫对象是名册本身）。
- 扫描范围：`src/ui/*.cpp|h` + `src/core/*.cpp|h`；豁免名单写死基线文件：`Theme.h`、`DarkPalette.h`、`GalleryCardPainter.cpp`、`MapView.cpp`、`Splash.cpp`（改动豁免需评审）。

### 5.2 五条计数（只降不升）

| # | 计数项 | 起算 ceiling | 归零后终态 |
|---|---|---|---|
| C1 | hex 字面量（非豁免） | 434 | 非豁免文件出现任何 `#[0-9a-fA-F]{6}` 即红 |
| C2 | `font-size:` 字面量 | ~90 | 只许 `Type::css()` 输出 |
| C3 | `border-radius:` 越格值（∉4/6/10/14/20/999） | 全混用值 | 白名单制，越格即红 |
| C4 | spacing/margin 越格值（∉4/8/12/16/24/32） | 153 采样 | 白名单制 |
| C5 | emoji/PUA 字形在 `tr()` 串内 | 34 | 字形与文案分离 |

基线：`native/qa/palette_baseline.json`，每计数一条 `{current, ceiling}`。`current > ceiling` 即红；每轮收敛提交下调 ceiling——改进显形为 diff，回潮显形为红门。**门禁先于收敛落地**（实施计划第 0 任务）：初始 ceiling=现值注册即绿，之后所有 PR 天然被守卫。

### 5.3 反例挂钩

`qa_counterexample_matrix.py` 加 **CE9**：向临时副本页面塞一个 hex 字面量 + 一个越格 radius，断言门红且报出正确计数项——防"扫描器空转也绿"。

---

## 6. 验证与收口

- **每任务**：`LA_UI_SHOT` offscreen 七页截图 + 人工比对；E2 弹层打开态补专项截图（此前镜头从未覆盖）；CullPage/首页照片叠加层需真库截图复核 INK_IMG 归并效果。
- **对比度**：CAT 板与文本层在 P4 用工具全量复检（文本 ≥4.5:1，UI 图形 ≥3:1，目标 CARD/PANEL 底）；本 spec 色值为标称值，允许 ±ΔE 校准但不得换色相语义。
- **回归**：既有 71 门全绿是每批提交的硬前提；新门入册后 `-Tier full` 72 门。
- **不动骨**：数据契约、零云端立场、i18n 单源、`lupdate` 禁用、AI 单写者线程模型（AiWriter/Qt::QueuedConnection）全部不破——视觉改造只改层。

## 7. 硬约束

MinGW/Qt6.8.3 既有工具链（不新增依赖）；发布 zip 资产不增（SVG 插件已在包内）；本机无管理员权限、C 盘近满——一切验证走既有 offscreen QA 线路；改造范围排除 `native/LightAlbum-2.32.0-win64/`（用户安装目录）。

## 8. 与后续子轨的接口

- P0（D1 足迹图注叠压 / D2 日志标签裁切 / D3 选片溢出）可先于本 spec 实施，互不依赖；
- P2 = §4.4 机制的 34 站点铺开 + C5 归零；
- P3 批A 新设置页（已批准：导航第九项、AI 连接+常规、core/AppSettings env>ini>default、LA_SETTINGS_QA）**按本语言出生**，不欠旧债；
- P4 = 对比度/高 DPI/减动效复检 + 全量门 + 发版。
