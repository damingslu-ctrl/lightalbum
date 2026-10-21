# A1 · wave4 前置体验走查（Audit）— LightAlbum 私人记忆美术馆

- **生成时间**：2026-10-21T10:19+08:00
- **走查对象**：`native/src`（Qt 6.8.3 C++ 桌面应用），本地 `main` tip `3fb1ed2`，即 2.42.0 收口之后的真实树
- **走查页面数**：10（前厅 / 足迹 / 图库 / 人物馆 / 修图 / 日志 / 关于 / 凝视灯箱 / ⌘K 面板 / 去重对话框）
- **走查模式**：模式 A 自动走查（代码路径），三路并行取证 + 控制端逐条复核
- **框架**：Nielsen 10 + 两条自定义（responsive 按「窗口自适应 + 高 DPI」重释；performance 按「本机解码 / 缓存 / SQL」重释）
- **上游链路**：standalone——仓库内无 `spark-output/`，也没有 frame / scope / probe / signal / bench 上下文，本轮全部结论只来自代码本身与既有 V/D 设计文档

> 本文不打整体分。它只回答三件事：现在哪里在说谎、哪里在漏、下一波该动哪一块。

---

## 总览

| 严重度 | 数量 |
|---|---|
| 🔴 Blocker | 2 |
| 🟠 Major | 20 |
| 🟡 Minor | 14 |
| 合计 | **36** |

修复成本分布：`quick-win` 15 / `medium` 18 / `major-rework` 3。

**按维度分布**：

| 维度 | findings | 一句话诊断 |
|---|---|---|
| consistency 一致性 | 8 | 债务最密：动效实现违反自家禁则、四灯只落一半、棘轮测不到它声称测的东西 |
| visibility 状态可见 | 4 | 长操作有反馈但两套并行；策展与擦除这两条新路径反而最哑 |
| recognition 识别优于回忆 | 4 | **两条 blocker 都在这里**：说出去了的能力没有界面 |
| performance 性能感知 | 4 | 时间维度不可查是长廊的硬前置 |
| user-control 用户控制 | 2 | 永久静音无解除；凝视反悔只有 3 秒 |
| error-prevention 防错 | 2 | 全应用零二次确认；⌘K 空解析静默 |
| flexibility 灵活高效 | 3 | 19 组快捷键，界面上只写了 1 个 |
| aesthetic 美学简洁 | 2 | 前厅四区块超焦点上限，且 1366 窄屏自证破版 |
| error-recovery 错误恢复 | 2 | 关键自诊断文案手写三遍，导出失败不给原因 |
| responsive 窗口适配 | 2 | 23 个 .cpp 里只有 4 个理会窗口尺寸；高 DPI 只服务地图 |
| real-world-match 心智匹配 | 2 | 同一个页面两个名字 |
| help-docs 帮助文档 | 1 | 让用户「去发布包里看手册」 |

---

## 两条 Blocker（先说清它们为什么是 Blocker）

**B1 · 界面已经替产品许下了一个没有兑现的能力。**
2.42.0 的回执写的是「已把《%1》的 %2 张存为永久合集」——但合集**没有任何可浏览页面**。`Library::listAlbums()`（`core/Library.h:174` / `core/Library.cpp:760`）全仓 6 个引用点里，UI 调用点是 **0**（其余三个在 `src/main.cpp:3375/3388/3399` 的门禁里）；`core/CurateScheduler.cpp:220` 的注释自己承认「没有任何页面列它」。用户点了「收进合集」，东西进了库，然后在应用里消失。（`audit-20`）

**B2 · 不可逆操作没有任何回头路。**
「别再这类」写入的是永久静音：`kPermanentMuteUntil = Q_INT64_C(9999999999999)`（`core/CurateScheduler.cpp:17`），`muteTemplate()` 落库（`core/Library.cpp:707`），无确认（`ui/MainWindow.cpp:482-488`），**`unmute` 这个词在全仓 grep 为 0**，`loadMuteState()` 也没有 UI 调用点。回执自己写着「反悔入口下轮上线」（`core/CurateScheduler.cpp:230`），门禁腿 `M_ack_mute_honest`（`src/main.cpp:3462`）则把这句「必须含下轮」当成了断言——也就是说，**当前的绿色门禁正在保护一个未兑现的承诺**。波次一旦补上入口，那条腿必须同批翻面。（`audit-7`）

这两条不是「体验不够好」，而是本项目自己定的规矩——**界面上写出来的快捷键 / 计数 / 状态词就是承诺**——的违约面。其余 34 条都可以排期，这两条决定了 wave4 的正当性。

---

## 改版机会点（7 个，按优先级）

### 🥇 高优先级

**机会点 1 · 把说出去的承诺兑现：合集浏览页 + 静音反悔入口**
- 关联 findings：`audit-20` `audit-21` `audit-7` `audit-6` `audit-8` `audit-9` `audit-17` `audit-18` `audit-19` `audit-3` `audit-1` `audit-2`（12 条，含两条 Blocker）
- 影响面：前厅三手势回执 → 新·库房/合集页 → 展览历史 → ⌘K 空态 → 凝视撤销通道
- 为什么值得先做：唯一能同时清掉两条 Blocker 的一包。而且**引擎侧的数据与 API 大部分已经就位**（albums / album_items / exhibit_history / muteTemplate 都在库里），缺的是页面和一个 `unmuteTemplate()`。顺路把三套回执（`showAck` / `showToast` / `badge_`）收成同一个 `Receipt` 单元，再给策展生成与 AI 消除补上它们现在完全没有的进行态。
- 叙事红利：合集 + 展览历史就是 V1 五空间里**唯一还没落地、且数据已就位**的那间「库房」。

**机会点 2 · 时间长廊的落地前置：把时间变成可查的列**
- 关联 findings：`audit-34` `audit-35` `audit-31` `audit-32`
- 影响面：新·长廊页、SQLite schema 迁移、网格列数共用、高 DPI
- 为什么是前置不是装饰：V1/V3 最有想象力、也唯一没动工的主空间是长廊 + 时间滑轨。但库里**日期 / 年月 / 城市 / GPS 全部埋在 `exif_json` 这一段 TEXT 里**（`core/Library.cpp:87,108-128`），SQL 层没有任何时间索引或聚合（只有 `ORDER BY generated_at DESC`，`:678,:768`），每次筛选都是全量内存 JSON 解析（`ui/GalleryModel.cpp:65-76` + `core/CmdFilter.h:25-50`，前厅「往年今日」三桶全扫 `ui/LobbyPage.cpp:193-243`）。`core/Library.cpp:95` 的 reserve 是 7000 张起步——不做列 + 索引 + 回填，长廊的「纵深切时间」在万张规模下一定卡成幻灯片。
- 这正是 V 案自己承诺的「可分级止损：波 1 纯界面 / 引擎现成，一轮可落」的第一格。

**机会点 3 · 四灯真落地 + 棘轮补牙：让门禁测到它声称测的东西**
- 关联 findings：`audit-16` `audit-14` `audit-15` `audit-13` `audit-28` `audit-29`
- 影响面：`qa_palette_ratchet.ps1` C1/C3/C5（+新 C6）、5 个画布组件、Theme chip 单元、Icons 尺寸档、Elev E1/E3 决断
- 两个层次的问题叠在一起：
  1. **能力半成品**：切到 L2 玉照 / L3 素印（亮底）时，`CropCanvas.cpp:21`、`MaskCanvas.cpp:16`、`CompareWidget.cpp:37,39,57,60`、`EditorialView.cpp:89`、`VideoPlayerWidget.cpp:235,242` 仍是 L1 炭黑（逐文件核实：这三个画布文件的 Lumen **色**访问器命中为 0），灯箱影院层也写死 `rgba(6,8,6,.97)`（`ui/GazeLightbox.cpp:104,125,279`）。用户切灯看到的是一半界面在动。
  2. **治理假绿**：棘轮 C1 正则只匹配 `#rrggbb`（脚本 `:52`），看不见 `QColor(0x..)` 与 `rgba(...)`；C3 只判「值不在白名单 {4,6,10,14,20,999} 里才算债」（`:54-55`），所以 29 处硬写 `border-radius:10px` 全部合法通过。**实测输出 `C1..C4=0` 的同时，非豁免文件里有 37 处绕过 token 的直接色**（其中 11 处集中在那 5 个画布组件）。不补这一牙，每一波都会继续漏，而名册上永远写着 0。
- 顺路：C5 基线停在 30/30 而实测已是 28（省下的 2 格余量没钉住，加回来也不红；且计数单位是行不是字符，`ui/GazeLightbox.cpp:236,244` 一行两个 🔓🔒 只算 1）。

### 🥈 中优先级

**机会点 4 · 动效宪章与代码合订**
- 关联：`audit-10` `audit-11` `audit-12` `audit-26`
- 最刺眼的一条是**自我矛盾**：D3 禁则写着「任何 bounce/elastic」`D3_motion_spec.html:51`、「全站禁用 spring/overshoot>2%」`:28`，而 `core/MotionKit.cpp:155-158` 的 StarBounce 用 `OutElastic` 把字号 12→15（+25%），调用点就在生产路径 `ui/GalleryPage.cpp:301`；ChipPop 用 `OutBack`（注释自述「过冲 ~+10%」），调用点 `:608`。`LA_MOTION_QA` 断的是曲线数学契约，不判「该不该用」，所以这个冲突**不会被任何门拦住**。
- 档位名实不符：D3 定 instant120/quick220/standard360/slow640 四档（`:31-35`），代码是 9 个具名预设、时长与任何档名都对不上（NavFade320、LightboxSlide380…），其中 6 个文档没有、2 档文档有而代码无同名实现；`kHeartBurst` / `kLightsDown` 在 UI 层零调用（`MotionKit.h:73` 自陈「本轮不接 UI」）。
- 宪章规定的编排没做：D3:43 人物墙进人 `standard360 + stagger 28ms`、D3:47 去重两图会拢 28px——`ui/PeoplePage.cpp`、`ui/DedupDialog.cpp` 零 Motion 引用。**12 个界面文件完全没有动效**，而消费减动效开关的只有 5 处，另有 5 处裸 `setDuration` 连系统开关都不理（`GazeLightbox.cpp:166,612`、`MainWindow.cpp:194`、`Splash.cpp:239,243`）。

**机会点 5 · 让键盘与帮助现身**
- 关联：`audit-24` `audit-25` `audit-30` `audit-23` `audit-4` `audit-5`
- 这套应用已经建起完整的键盘语言——19 组绑定（S/A/L 标记、`←/→` 夺权退展、`Ctrl/Shift` 多选、地图方向平移 + Enter 下钻、`+/-` 缩放、视频 Space）——但界面上**只写了 `Ctrl+K` 一个**。凝视模式的 S/A/L 含义要等你按下之后才在 toast 里第一次现身（`ui/GazeLightbox.cpp:329-332`），灯箱控件条只有三颗按钮、没有键位图例。
- 帮助不可达：关于页只写「使用说明：见发布包内 USER-MANUAL.md」（`ui/MainWindow.cpp:1342,1381`），界面里没有任何入口能打开它；`onboarding / welcome / firstRun / tutorial` 全仓 0 命中（`LobbyPage.cpp:548` 的 `makeEmptyGuide` 是空库引导卡，不记「已看过」）；全应用 0 个 inline 帮助。
- 这包和「馆主动开讲」的愿景同源：能力已经在了，缺的是**让人知道它在**。

**机会点 6 · 前厅重新呼吸**
- 关联：`audit-27` `audit-22` `audit-31` `audit-4`
- 前厅一屏四个并列区块（今日一展卡 / 今日展 7 卡 / 最新到馆 8 卡 / 指令栏），超出「不超 3 个焦点」；而那排 7 张 148px 定尺卡在 1366 笔电上会压扁叠字——**代码注释自己承认了**（`ui/LobbyPage.cpp:423-426`，并写明 `LA_UI_SHOT` 800 宽实测过）。`LobbyPage` 没有 `resizeEvent`。
- 若 wave4 做长廊，「今日展」条正好是长廊的入口卡，两者一起改版面才不重复劳动。顺路补上四个页面的空状态（图库 / 修图台 / 日志 / 足迹整库空），前厅与人物馆已经证明这个团队做得出好空态。

### 🥉 低优先级

**机会点 7 · 解码与缓存的账**
- 关联：`audit-33` `audit-36` `audit-35`
- 原图一律全尺寸解码（UI 侧 `setScaledSize` 命中为 0，全仓仅 `core/Scanner.cpp:152` 生成期在用）；缩略图缓存一页面一份实例、预算互不共享（各 L1 64MiB + L2 48MiB，三页并存峰值 336MiB，同一张图会被重复解码驻留）；地图爆炸卡绕开缓存同步读 10 张（`ui/MapPage.cpp:433,480,506`）。
- 不影响正确性，但决定「凝视」在万张库 + 高分屏上的手感；改动点集中在 FullCache / ThumbCache 两处，适合作为任意一波的顺路包。

---

## 完整 Findings 清单

取证与复核方法见文末。`evidence` 均可按 `文件:行号` 直接跳转。

### 维度 1 · visibility（系统状态可见性）

| # | 严重度 | 问题 | 证据 | 建议 | 成本 |
|---|---|---|---|---|---|
| audit-1 | 🟠 major | 策展「换一展 / 开展」在整个后台生成区间零反馈，只有结果落地时一句浮字 | `core/CurateScheduler.cpp:104-130`（QtConcurrent::run，无进度回调）；`ui/MainWindow.cpp:210-215`（generated→showAck）；`:174` swapPending_ 只是旗标 | swapPending_ 转成可见态：展卡位骨架 + 「馆长正在选题」呼吸字 | medium |
| audit-2 | 🟠 major | AI 消除提交后只有一行文字变化，页内唯一进度条被显式清零留给别的用途 | `ui/RetouchPage.cpp:1186-1187`；`:1196` prog_ 置 0；prog_ 归 AI 决策 `:357-360,948,1010` | 按 D3:44 的「AI 进度」编排给一条玉绿进度线 | quick-win |
| audit-3 | 🟡 minor | 回执三套各自实现，且全应用没有状态栏通道；六个页面做任何事都不给回执 | `ui/MainWindow.cpp:1208-1232`；`ui/GazeLightbox.cpp:339-357`；RetouchPage 21 处 badge_；grep `statusBar(` 全仓 **0 命中** | 抽一个 Receipt 单元（同 QSS、同时长、可带动作槽） | medium |
| audit-4 | 🟡 minor | 前厅里用户看不到自己在哪个空间：顶栏整条隐藏，导航轨只有 68px 图标、文字仅在 tooltip | `ui/MainWindow.cpp:682`；`:263-264`；`:291`；`:918-919` | 保留一枚「在馆位置」五空间胶囊，当前格点亮 | quick-win |

### 维度 2 · real-world-match（与用户语言匹配）

| # | 严重度 | 问题 | 证据 | 建议 | 成本 |
|---|---|---|---|---|---|
| audit-5 | 🟡 minor | 同一个东西两个名字：导航「图库」vs 页内「全部藏品」；修图台整个页面没有页内标题 | `ui/MainWindow.cpp:115-119`；`ui/GalleryPage.cpp:228`；RetouchPage 标题 token 0 命中 | 定「空间名 ↔ 页内名」对照表进 V4 语汇表 | quick-win |
| audit-6 | 🟡 minor | 「收藏」这个动作两入口两种说法，其中一种还是一颗永久禁用的按钮 | `ui/LobbyPage.cpp:366`「收进合集」；`ui/CmdKPanel.cpp:385-389`「暂存为合集」setEnabled(false)+tooltip「下轮上线」 | 让它真能建，或撤下按钮只留说明——留着灰按钮就是教用户忽略界面 | quick-win |

### 维度 3 · user-control（用户控制与自由）

| # | 严重度 | 问题 | 证据 | 建议 | 成本 |
|---|---|---|---|---|---|
| audit-7 | 🔴 **blocker** | 「别再这类」不可逆：看不见静音了什么，没有解除入口，连 API 都不存在 | `CurateScheduler.cpp:17,213`；`Library.cpp:707`；grep `unmute` **0 命中**；`loadMuteState` UI 调用点 0；`MainWindow.cpp:482-488` 无确认；`CurateScheduler.cpp:230` 自认「反悔入口下轮上线」 | 加 `unmuteTemplate()` + 「安静下来的选题」列表单元，回执措辞同批改 | medium |
| audit-9 | 🟠 major | 凝视 S/A/L 标记只有 3 秒同键反悔，窗口过期或灯箱收起即永久失效——而这是逐帧停留近 2.4 秒的模式 | `ui/GazeLightbox.cpp:313-334`（判定 `:317`、提示 `:332`）；`:357` 收起失效；kExhibitBreath=2400ms | 反悔不该靠记秒数：常驻「撤销上次标记」小按钮 + Ctrl+Z | quick-win |

### 维度 4 · consistency（一致性与标准）

| # | 严重度 | 问题 | 证据 | 建议 | 成本 |
|---|---|---|---|---|---|
| audit-10 | 🟠 major | 实现违反自家动效禁则：禁 bounce/elastic、禁 overshoot>2%，而生产路径用 OutElastic 把字号冲到 +25% | `D3_motion_spec.html:51`、`:28`；`core/MotionKit.cpp:155-158`；`ui/GalleryPage.cpp:301`；ChipPop `:143-148` + `:608` | 改宪章承认例外，或换 quint-out——一次裁决，一提交落地 | quick-win |
| audit-11 | 🟠 major | 档位名实不符：文档四档，代码九个具名预设且时长对不上任何档名 | `D3:31-35` vs `MotionKit.cpp:124,134,143,155,164,172,182,191,202`；`kHeartBurst`/`kLightsDown` UI 零调用 | 每预设标注归属档位；未接线的明确标储备 | medium |
| audit-12 | 🟠 major | 宪章规定的编排没实现，12 个界面文件完全没有动效 | `D3:43,45,46,47` vs `ui/PeoplePage.cpp`、`ui/DedupDialog.cpp`、`ui/CompareWidget.cpp` 零 Motion 引用 | 做人物馆/长廊那波时一次性补 D3:43 | medium |
| audit-13 | 🟡 minor | chip 有 5 份平行样式实现；四组 QSS 串跨文件逐字复制 | `Theme.h:472` / `GalleryPage.cpp:50` / `CmdKPanel.cpp:86` / `LogPage.cpp:328` / `MapPage.cpp:74`；`CmdKPanel.cpp:90`==`GalleryPage.cpp:54`；`GazeLightbox.cpp:347`==`MainWindow.cpp:1217` | 样式串收进 Theme / 并入 Receipt | medium |
| audit-14 | 🟠 major | 棘轮测「在不在格上」而非「走不走 token」，C1..C4=0 的同时 37 处直接色 + 29 处硬写圆角全部隐身 | `qa_palette_ratchet.ps1:52`（C1 仅 `#rrggbb`）、`:54-55`+`:48`（C3 白名单）；命中清单见机会点 3 | 加 C6（QColor(0x..) / rgba( 计债），C3 升级为「必须来自 Radius::」 | medium |
| audit-15 | 🟡 minor | C5 基线停在 30/30 而实测 28，省下的余量没钉住；计数单位是行不是字符 | `qa/palette_baseline.json`；脚本 `:73-74`；`ui/GazeLightbox.cpp:236,244` | 跑 `-Update` 下调到 28；单位改字符级或注记 | quick-win |
| audit-16 | 🟠 major | 四灯只落一半：五个画布组件在亮灯下仍是 L1 炭黑 | `CropCanvas.cpp:21`、`MaskCanvas.cpp:16`、`CompareWidget.cpp:37,39,57,60`、`EditorialView.cpp:89`、`VideoPlayerWidget.cpp:235,242`、`GazeLightbox.cpp:104,125,279` | 定夺：给 L2/L3 定义 canvas 层 token（暗房在亮灯里也该暗），或跟随 lamp | medium |
| audit-29 | 🟡 minor | 图标已矢量化但尺寸失控（14 个图标 / 5 种尺寸 / 6 个调用点）；新接生的 E1、E3 层级产品代码零使用 | `ui/Icons.h:154-155,21,15-19`；尺寸 16/18/22/24/44；`Elev::apply` 产品命中 5 处，E1/E3 只在 `src/main.cpp:3080-3179` 门禁自测 | 尺寸收三档；E1/E3 要么在新空间用起来要么删 | quick-win |

### 维度 5 · error-prevention（错误预防）

| # | 严重度 | 问题 | 证据 | 建议 | 成本 |
|---|---|---|---|---|---|
| audit-8 | 🟠 major | 全应用零二次确认：批量改私密、隐私库重新上锁、修图一键重置都是点下即生效 | grep `QMessageBox::question` 在 `native/src/ui` **0 命中**（仅 2 处事后 warning `MainWindow.cpp:1069,1093`）；`:601-612`、`:1096-1099`、`RetouchPage.cpp:1239-1244` | 用带撤销槽的回执代替模态框，更符合美术馆语气 | medium |
| audit-17 | 🟠 major | ⌘K 把话解析成空筛选时直接静默 return：不关面板、不提示、什么都不动 | `ui/CmdKPanel.cpp:554`（前置 clear `:550-553`）；对照词典为空的降级文案 `:587,:591` | 一句「这句我没能翻成筛选」+ 指向已有 chip 行 | quick-win |

> 如实说明降低了一包风险面：**删除照片这条最高危路径本身不存在**（`ui/` 内无删除用户数据的路径；策展侧「只打包不删除」`core/CurateEngine.cpp:496`）。修图重置那行注释称撤销点在 `doApply` 末尾自然提交，「只重置不应用」这条路径能否撤销**未经实测证实**——留给 wave4 的验收腿。

### 维度 6 · recognition（识别优于回忆）

| # | 严重度 | 问题 | 证据 | 建议 | 成本 |
|---|---|---|---|---|---|
| audit-20 | 🔴 **blocker** | 承诺了「永久合集」却没有合集页面，收藏的东西在应用里找不到 | `Library.h:174` / `Library.cpp:760`；UI 调用点 0；`CurateScheduler.cpp:220` 自认；`MainWindow.cpp:1302` makePlaceholder（含「即将推出」胶囊 `:1324`）**全仓零调用点 = 死代码**，连占位都没接上 | 新·库房/合集页 + 点进去复用 GazeLightbox 展内播放；同批撤「下轮上线」措辞 | major-rework |
| audit-21 | 🟠 major | 展过的展览不留痕：表里有历史，界面只看得见当下这一版 | `Library.h:169` / `Library.cpp:671`；调用点仅 `CurateScheduler.cpp:115,185` + 门禁 5 处；`MainWindow.cpp:207` restoreLast | 「已展出的」列表（长廊的记忆层） | medium |
| audit-22 | 🟠 major | 四个页面没数据时是一片空白，一句话都没有 | `ui/GalleryPage.cpp`（仅计数 `:676`）、`RetouchPage.cpp:670-675`、`LogPage.cpp` 0 命中、`MapPage.cpp` 只有区域级 `:187,394,461`；对照已有好空态 `LobbyPage.cpp:548-589`、`PeoplePage.cpp:165-169,335,342,350` | 按前厅语气补四句空馆状态 | quick-win |
| audit-23 | 🟡 minor | 应用不记得用户找过什么：设置里只有两枚灯的状态 | `ui/Theme.h:86,93,96,102`（仅 lamp/active、lamp/mode）；`CmdKPanel` 无历史；`GalleryPage.cpp:217` 搜索框每次重建 | 「最近找过的四条」铺在 ⌘K 空态（与 audit-17 同一块版面） | quick-win |

### 维度 7 · flexibility（灵活与效率）

| # | 严重度 | 问题 | 证据 | 建议 | 成本 |
|---|---|---|---|---|---|
| audit-24 | 🟠 major | 19 组按键绑定里界面只写了 Ctrl+K 一处；S/A/L 的含义按键后才第一次现身 | 显示中的仅 `MainWindow.cpp:364`、`CmdKPanel.cpp:392`、`LobbyPage.cpp:531`；未显示：`GazeLightbox.cpp:301-309`、`EditorialView.cpp:105-112,363-364`、`MapView.cpp:3175-3188`、`RetouchPage.cpp:665-668`、`VideoLightbox.cpp:46-50`；toast 首现 `:329-332`；ctl_ 无图例 `:235-257` | 凝视灯箱常驻键位带，首次入场 640ms 后淡到 40% | medium |
| audit-25 | 🟡 minor | 撤销/重做按钮上没有键注 | `ui/RetouchPage.cpp:421`「↶ 撤销」；绑定 `:665-668` | 按钮统一带键注后缀（写了就要实测） | quick-win |
| audit-26 | 🟠 major | 降低动效只能靠系统/环境变量，应用内无开关；5 处裸时长连系统开关都不理 | `core/MotionPref.h:24-43`（env `:27` > SPI `:37`）；QSettings 无 motion 键；`GazeLightbox.cpp:166,612`、`MainWindow.cpp:194`、`Splash.cpp:239,243`；消费点仅 5 处 | 设置页三态「跟随系统/全开/收敛」+ 裸时长收进 Motion::Driver | medium |

### 维度 8 · aesthetic（美学与简洁）

| # | 严重度 | 问题 | 证据 | 建议 | 成本 |
|---|---|---|---|---|---|
| audit-27 | 🟠 major | 前厅一屏 4 区块超焦点上限；那排 7 张定尺卡在 1366 笔电压扁叠字（代码注释自证并写明 800 宽实测） | 区块 `LobbyPage.cpp:331,418,426,454,461-464,505-546`；自证注释 `:423-426`；LobbyPage 无 resizeEvent | 今日展条收成 5 幅或可横滚，卡宽 token 化；与长廊同批改 | major-rework |
| audit-28 | 🟡 minor | 样式以逐控件内联 QSS 为主：181 次散在 13 个文件，修图台一文件 39 次 | RetouchPage 39 / MainWindow 27 / GalleryPage 19 / LobbyPage 18 / MapPage 17 / PeoplePage 15 / GazeLightbox 13 / LogPage 11；padding 字面量含非网格值（`LogPage.cpp:500` margin-right:5px、`:215,330,333` padding:0 14px） | 重复串收进 Theme；内联次数量基线后进棘轮 | medium |

### 维度 9 · error-recovery（错误识别 / 诊断 / 修复）

| # | 严重度 | 问题 | 证据 | 建议 | 成本 |
|---|---|---|---|---|---|
| audit-18 | 🟡 minor | 「导出失败」三个字既无原因也无重试；同类还有「局部应用失败」 | `ui/RetouchPage.cpp:1283`、`:1128`；对照 `:969` 是唯一带 `%1` 原因的 | 错误串一律带原因槽 + 「再试一次」 | quick-win |
| audit-19 | 🟡 minor | 最关键的自诊断文案「引擎未就绪」手写复制三遍，只换尾钮名字 | `ui/MainWindow.cpp:1630 / :1705 / :1816`；`:1636`「分类过程中断（可能已部分写入）」只报状态不报下一步 | 抽 `engineNotReady(tail)`；`:1636` 补「已写入 N 条，可继续」 | quick-win |

### 维度 10 · help-docs（帮助与文档）

| # | 严重度 | 问题 | 证据 | 建议 | 成本 |
|---|---|---|---|---|---|
| audit-30 | 🟠 major | 帮助在应用里不可达：关于页让用户去发布包里看手册；无首次引导；全应用 0 个 inline 帮助 | `ui/MainWindow.cpp:1342,1381`；grep `onboarding\|welcome\|firstRun\|tutorial` 0 命中（`LobbyPage.cpp:548` 是空库引导，不记已看过） | 手册做真入口 + 首次进前厅一段 3 帧馆长导览（复用 kExhibitBreath） | medium |

### 维度 11 · responsive（桌面重释：窗口自适应 + 高 DPI）

| # | 严重度 | 问题 | 证据 | 建议 | 成本 |
|---|---|---|---|---|---|
| audit-31 | 🟠 major | 23 个 .cpp 里只有 4 个理会窗口尺寸；人物馆固定 4 列不随宽、展开条硬排 12×88px；主窗无最小尺寸 | grep resizeEvent 仅 `EditorialView/GazeLightbox/MainWindow/MapView`；列数只在 `EditorialView.cpp:154` qBound(3,W/230,5)+`:324-330` 去抖；`PeoplePage.cpp:174`、`:277-289`；`main.cpp:11208-11209` 下限 320×240 | 抽共用「按宽算列」函数，人物馆/库房/长廊共用；定诚实最小窗宽并进门禁 | medium |
| audit-32 | 🟠 major | 高 DPI 只服务地图：缩略图缩放不乘 devicePixelRatio，2.4K/4K 屏上所有卡片图是糊的 | `ui/HiDpi.h:8` 自述「全应用此前从未调用 devicePixelRatio」，调用点仅 `MapView.cpp:253,362`；`core/ThumbCache.cpp:24-25` | scaled 尺寸按 dpr 取值 + pixmap 打 dpr 标签 | medium |

### 维度 12 · performance（性能感知）

| # | 严重度 | 问题 | 证据 | 建议 | 成本 |
|---|---|---|---|---|---|
| audit-33 | 🟠 major | 原图一律全尺寸解码，UI 侧按需缩放解码命中为 0 | grep `setScaledSize` 仅 `core/Scanner.cpp:152`；`core/FullCache.cpp:67-72` 全尺寸、`:23` 140MB、`:30` 40MP 闸、`:132` 全局池。（**更正**：灯箱首帧是同步读缩略图 + 异步解原图 `ui/GazeLightbox.cpp:180-184`，故不指控「同步解原图」） | FullCache 加 setScaledSize(viewport*dpr)，与 audit-32 同处代码一起做 | medium |
| audit-34 | 🟠 major | 长廊要用的时间/地点维度不是列，是埋在 exif_json TEXT 里；SQL 层零时间索引或聚合 | `core/Scanner.cpp:34-51`（5 索引均非时间/地理）、`:54-61`、`core/Library.cpp:36-49`；日期/年/城市/省/GPS 在 `exif_json`（`Library.cpp:87,108-128`）；SQL 只有 `ORDER BY generated_at DESC`（`:678,:768`）；内存全扫 `GalleryModel.cpp:65-76`、`CmdFilter.h:25-50`、`LobbyPage.cpp:193-243`；规模 `Library.cpp:95` reserve(7000) | 补 shot_ts/year/month/city 列 + 索引 + 一次性迁移回填（老库 fail-open），聚合交 SQL | major-rework |
| audit-35 | 🟡 minor | 足迹爆炸卡绕开缩略图缓存，同步读 10 张并整图缩放 | `ui/MapPage.cpp:433,480,506` | 接 ThumbCache | quick-win |
| audit-36 | 🟡 minor | 缩略图缓存一页面一份实例、预算互不共享，三页并存峰值 336MiB 且重复解码 | `core/ThumbCache.cpp:42-43,51-56,57`（每实例独立线程池）、`:89` pending 去重、`:109` 负缓存；实例 `GalleryPage.h:71`、`LobbyPage.cpp:154`、`PeoplePage.cpp:50` | 改成 MainWindow 持单实例，页面借用 | medium |

---

## 走查纪律：被复核推翻的三条断言

自动取证给出过这三条，逐条读码后**判为不成立，故未收入 findings**。记下来是为了防止下一波再被端上来：

1. ~~「凝视灯箱每次换图同步解码原图，阻塞 GUI」~~ → 实际是同步读**缩略图**文件作即时占位、再异步解原图，解码失败保持缩略图（`ui/GazeLightbox.cpp:180-184` 有完整注释与 HEIC 实测降级说明）。
2. ~~「修图台一键重置无确认且不可逆，用户工作会丢」~~ → 该行注释称撤销点在 `doApply` 末尾自然提交（`ui/RetouchPage.cpp:1244`）。「只重置不应用」这条路径是否可撤销**尚未实测**，因此 audit-8 只把它列为「无确认」，不宣称数据丢失。
3. ~~「L2 玉照与 L3 素印近乎同档，是冗余」~~ → `ui/Theme.h:55` 的注释写明 L3 是灰阶灯、唯一彩点即 `gold==emerald`，这是 V4 的有意设定，不是债。中性列（bg/card/line）确实接近，但强调色与 ink 阶完全不同（`Theme.h:50-59`）。

另外一处**当前绿色门禁正在保护未兑现能力**的结构性风险，值得单独记住：`M_ack_mute_honest`（`src/main.cpp:3462`）断言静音回执**必须同时含「反悔」+「下轮」**。这条腿在 wave4 补上入口时必须同批翻面，否则「反悔入口已上线」这句正确的话会被同一道门判红。

---

## 下一步

1. **建议进 `/brief`**：把上面 7 个机会点收敛成一页纸策略——尤其要先钉住「wave4 只拿机会点 1 + 2 的高优先级三包，还是把 3 的治理补牙也算进去」。这个边界决定 2.43.0 的体量。
2. **Blocker 不等改版**：`audit-7`（静音不可解）与 `audit-20`（合集无处看）严格来说是**已上线措辞与已有能力的违约**，不是「体验不够好」。若要更快，可以先出一个极小补丁：撤下 ⌘K 那颗灰按钮 + 把两句回执改成不含未兑现承诺的措辞（门禁腿同批改），把完整版留进 wave4。
3. **工程前置的顺序是硬的**：`audit-34`（时间列 + 索引）必须在长廊界面之前，否则「纵深时间轴」在万张库上无法验收；`audit-32` + `audit-33` 是同一处缓存/解码代码，拆开会做两遍。
4. **纪律延续**：色彩走棘轮且**只准降不准升**（`qa_palette_ratchet.ps1:105,:110`）；动效只引用档位；新机制配新门（wave3 的先例是 `LA_CURATE_QA`）；界面写出来的句子必须实测——这三条在本轮已经被 `audit-14`、`audit-10`、`audit-20` 分别证明过一次。

---

### 附：方法与局限

- 模式 A（代码走查）能覆盖静态层面：调用点计数、token 采纳、文案落点、schema 与查询路径、快捷键绑定。**无法覆盖**：实际视觉密度观感、焦点初始态、真实宽屏/窄屏截图比对、长库（>10k 张）下的帧率。前厅破版（audit-27）与四灯适配（audit-16）两条应在 wave4 用 `LA_UI_SHOT` 截图复核后再定案。
- 本走查是专家启发式，不是真实用户行为。样本是产品本身，不含工单/访谈。
- 数据事实来自当前工作树（`3fb1ed2`），不是任何历史快照。
