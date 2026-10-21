# 波4「兑现」· 典藏补齐 + 时间地基 + 色债治理 · 设计规格 · 2026-10-21

> 来源：`native/docs/design_studio/2026-10-21-wave4-audit/`（体验走查 B0）与同批**设计简报 B1**
> （`native/docs/design_studio/2026-10-21-wave4-audit/B1_brief.html`，6 维策略 / 4 业务目标 / 5 约束 / 5 不做）；
> 愿景口径见 `2026-10-14-vision/`（V4 token 全文 / V5 五波次 / D 系列今日纪律）。
> 前序：`specs/2026-10-19-wave3-curation-engine-design.md`（波3 策展引擎，已随 2.42.0 交付）。
> 冲突裁决优先级：本文 > 简报 > 愿景包文档 > 现有代码注释。

## 0 · 范围、红线

**本波一句话：把上两轮界面上已经说出口的话兑现成真的，然后才允许碰长廊。**

触发本波的不是「不够美」，而是**三句越了能力线的承诺**与**测不到的真债**：

1. `CurateScheduler::keepAck()`（`src/core/CurateScheduler.cpp:216-225`）写「合集浏览**下轮上线**」——
   数据真落库（`albums`/`album_items`，`Library.cpp:731-758`），但 `Library::listAlbums()`（`:760-781`）
   在全仓**只有门禁在调**（`src/main.cpp:3375/3388/3399`），`ui/` 目录 0 引用 ⇒ 用户按下「收进合集」后，
   那份永久合集**在本应用里没有任何去处**。
2. `CurateScheduler::muteAck()`（`:227-231`）写「反悔入口**下轮上线**」——而 `unmute` / `clearMute` /
   `DELETE FROM template_mutes` 在全仓**0 命中**：连 API 都不存在。永久静音键
   `kPermanentMuteUntil = 9999999999999`（`CurateScheduler.cpp:17`）把模板**判了无期**，用户唯一的解法是重装数据库。
3. ⌘K 面板里的 **「暂存为合集」是一枚死键**：`CmdKPanel.cpp:385-390` 造了按钮、`setEnabled(false)`、
   tooltip 写「下轮上线」。它与第 1 条是同一件事的两半——**合集的写入方**（`createAlbum`）早就存在，
   缺的正是「让检索结果也能进合集」这个入口。

这三条按 `brief` 的 B1/B2 归类为 **blocker**：界面文字即承诺（用户级记忆
`feedback-advertised-ui-text-is-a-contract`）。

**三条红线（继承，不可破）**
1. **零云端、零遥测、不新增模型**（V 案戒律）。一切口径只能是门禁读数与 `-SelfTest` 读数。
2. **GUI 线程唯一写者**（用户级记忆 `ai-single-writer-gui-thread`）：新增的任何库写入必须在 GUI 线程，
   后台只算不写。
3. **迁移必须 fail-open**：老库（2.3x 的 `photos` 表）不可因本波迁移而打不开；打不开就什么都不动。

### 0.1 命名冲突（本波第一处必须裁决的事实）

简报与走查把「让合集有去处」写作「补齐**库房**」。核实代码：`Hall::Vault` **已存在且不是空的**——
它是**隐私库**（`src/ui/Theme.h:38`；`src/core/Vault.h` = PBKDF2 口令散列；`MainWindow::onVaultButton()`
`src/ui/MainWindow.cpp:1054-1100` 顶栏锁钮，`MainWindow.cpp:121` 注释明写「Vault 不经导航，只由顶栏锁钮进」），
并被门禁钉死语义：`B5_vault_jade`（`src/main.cpp:1271`）。

**裁决 R1**：本波新空间**不叫 Vault、不叫库房**，定名 **`Hall::Collection` / 导航标签「典藏」**，页面类名
`CollectionPage`。理由：`Vault` 一词在本代码库已被「口令门 + `photos.private`」占用，复用同一个词做第二件事
= 违反 Nielsen #4.2（同一概念必须同一个词）。简报里的「库房」是隐喻，落到 IA 上是「典藏」。
`Hall::Vault` 与其 `B5_vault_jade` 腿**一律不动**。

### 0.2 裁决表（全部集中在此，正文只引用编号，不另立口径）

| # | 冲突 | 双方原文（核实过） | 裁决 |
|---|---|---|---|
| **R2** | 兑现页面会**弄红**两条正在守护「诚实」的门禁腿 | `M_ack_mute_honest`（`main.cpp:3461-3463`）要求回执**含**「反悔」**且含**「下轮」；`M_ack_keep_honest`（`:3470-3472`）要求**含「下轮」且**不含**「随时」「再看」 | **同一个提交里改线**：文案改成真实入口后，这两条腿的断言方向必须**整体翻转**为「含`典藏`、**不含**`下轮`」。这两条腿当初的用途是「不许假装可用」，兑现之后若留着，就变成「**不许真的可用**」——守护的对象消失了，牙齿必须换口径。新腿仍保留同一形状（变异证明：把文案改回「下轮」必须变红）。 |
| **R3** | C3 收紧撞上「上限只降不升」的棘轮铁律 | `qa_palette_ratchet.ps1:105` 对 `-Update` 抬上限直接报错；`:110` `if ($now -gt $ceil)` 判红。把 C3 定义收紧为「裸 `border-radius` 数字必须旁边出现 `Radius::`」后实测 **C3 0 → 8**（`DedupDialog.cpp:63,179`、`GalleryPage.cpp:450`、`LogPage.cpp:330,333`、`MainWindow.cpp:1326,1353`、`RetouchPage.cpp:1518`） | **先清零、后收紧，两步走且顺序不可换**：任务序里先提交这 8 处改走 `Radius::*`（棘轮口径不变，C3 仍 0），再在同批提交里改 C3 的**正则定义**并把上限重新定义为 0。定义收紧后的首跑必须**实测为 0** 才允许落基线。禁止「先抬上限再慢慢修」——那等于把棘轮倒着拨。 |
| **R4** | 动效宪章与代码互相判死刑 | D3 宪章原文 `:28`「全站禁用 spring/overshoot>2%」、`:51`「任何 bounce/elastic」；而 `LA_MOTION_QA` **正向断言** `OutBack` 中段值 >1（`main.cpp:6785-6786`）与 `StarBounce` 的 glow 键「f=0.5 必须离开起点（真弹）」（`:6870-6882`），名册里这条门是全绿 fast | **改宪章，不改代码**。理由：`StarBounce`（加星）与 `OutBack`（卡片入位）已随 2.41.0 上线并被真实用户看见，属于**已存在的品牌动作**；D3 的禁令是「未实现先立宪」的产物，而 `MotionKit.cpp:155` 的 420ms/overshoot 是量过的、有减动效兜底的实现。裁决 = 在 D3 增补「**例外白名单**」段（`kStarBounce` / `kHeartBurst` 两档 bounce 属庆祝动作，允许 overshoot≤8%，且必须吃 `prefers-reduced-motion`），门禁与宪章第一次说同一句话。**动 `LA_MOTION_QA` 的断言是禁区**——它会为了配合文档而弄红一条已绿的门。 |
| **R5** | 导航插行 vs 6 处裸行号 | `MainWindow.cpp` 现有 `setCurrentRow` 字面量 `:219/507/526/531/537/543/641/1129`，动效预设谓词 `:1265-1267` 用 `row == 0 / row == 3` | **先归一为具名常量，再插行**（顺序不可换）。且新增一条源扫腿禁止 `setCurrentRow(` 后跟数字字面量——魔数不除净，下一次插行还会错位（详见 §9）。 |
| **R6** | `Layout.h` 要不要单开一道门 | 名册已到 80 门，纯算术函数单开门会让名册虚胖（本项目记过「门数膨胀但牙齿没变」的判例类型） | **不单开门**，`gridCols` 的腿并入 `LA_COLLECTION_QA` 的 G 组（§4）。 |
| **R7** | ⌘K「暂存为合集」怎么兑现 | 按钮是 `setEnabled(false)` 占位（`CmdKPanel.cpp:388`），而写入方 `Library::createAlbum(name, paths, source)` 已存在且被策展轨用着（`CurateScheduler.cpp:198`） | **本波点亮它**：有选中项时可点，走 `CurateScheduler` 侧新增的 `keepSelection(name, paths)`（同一写入路径，`source='cmdk'`），落库后回执并让典藏页 tab1 立刻可见；无选中项时**禁用但不写「下轮」**，tooltip 改说清缺什么（「先在结果里勾选照片」）。若不做，§6.3「`下轮` 0 命中」这条硬腿过不去。 |

### 0.3 明确不做（写清以免越界）

- **长廊 UI 本体**（时间滑轨 / 地理互切 / 生涯条）→ 留 wave5。本波只交付它需要的**数据地基**（§3.1）与**地基的调用方**（§3.2）。
- **前厅版面重构** → 与长廊同批（前厅与长廊共用版面语言，分开做必返工）。
- **按需缩放解码 / 缓存单实例化**（audit-33/36）→ 性能轨，不做。
- **地图爆炸卡缓存链路** → 不做。
- **扩语种 / WCAG 深化** → 英文目录只补本波新增串，不扩第三语种；无障碍只补键位带与焦点，不做完整审计。
- **不 push、不打包、不打 tag**（推送授权已随 2.40.0 耗尽）。

---

## 1 · 总体架构

两轨并行，A 轨面向用户、B 轨面向代码；**A 轨先于 B 轨**（先兑现说过的话，再动地基）。

```
┌ A 轨 · 兑现（用户可见）────────────────────────────────────────┐
│ CollectionPage「典藏」（新，导航第 4 行）                        │
│   ├ tab1 合集：Library::listAlbums() → 卡片 → 点进 = 凝视序列     │
│   ├ tab2 展览史：Library::loadExhibitHistory() → 逐展可回看      │
│   └ tab3 安静下来的选题：Library::listMutes() → 逐条唤醒 ★新 API │
│ LobbyPage：生成期骨架（Pending 态 + 呼吸字）                     │
│ GazeLightbox：常驻键位带（legend strip）                        │
│ CmdKPanel：空解析必须有话说 + 时间筛选改走索引                   │
│ 四页空态：Gallery / Retouch / Log / Map（前厅语气）              │
└────────────────────────────────────────────────────────────────┘
        ▲ 只读查询（GUI 线程）           │ 唤醒/回看 → 意图
        │                               ▼
┌ 持久层（SQLite）──────────────────────────────────────────────┐
│ Library：+unmuteTemplate() +listMutes()          （A 轨）      │
│ photos：+shot_ts +shot_year +shot_month +city 四列 +三索引（B 轨）│
│ Scanner：EXIF 解析同处写四列（唯一写点）                        │
└────────────────────────────────────────────────────────────────┘

┌ B 轨 · 地基与治理（用户暂不可见，但可测）──────────────────────┐
│ §3.1 时间地点落列 + 索引 + 一次性 fail-open 回填                 │
│ §3.2 ⌘K 时间筛选 = 四列的唯一消费方（parity 腿 + EXPLAIN 腿）    │
│ §3.3 列数函数三网格共用（EditorialView / PeoplePage / 典藏）     │
│ §3.4 画布层 lamp token（5 个自绘 widget）                       │
│ §3.5 棘轮 C6 新维度 + C3 定义收紧（见 R3）                       │
│ §3.7 诚实的最小窗宽                                              │
└────────────────────────────────────────────────────────────────┘
```

**文件清单**

| 动作 | 文件 | 职责 |
|---|---|---|
| 新建 | `src/ui/CollectionPage.{h,cpp}` | 典藏三 tab；只读 `Library`，零写库（唤醒除外，见 §2.3 走 Scheduler） |
| 改 | `src/ui/MainWindow.cpp` | 导航第 8 行、栈第 8 页、`kRow*` 常量归一、§2.5 pending 接线 |
| 改 | `src/ui/Theme.h` | `Hall::Collection` + `follow[]` 扩一格；画布层 4 个访问器 |
| 改 | `src/core/Library.{h,cpp}` | `unmuteTemplate` / `listMutes` / `MuteRow` / **`albumPaths`（合集取路径，现无读口）**；四列迁移 + 三索引 + 一次性回填；`pathsForShotRange` / `pathsForCity` |
| 改 | `src/core/Scanner.cpp` | 解析处（`:412-450` 同批）写四列 |
| 改 | `src/core/CurateScheduler.{h,cpp}` | `unmuteExhibit()` + `keepSelection()` + 三条 ack 文案改线 |
| 改 | `src/ui/GazeLightbox.cpp` | 键位带；cinema 层随灯 |
| 改 | `src/ui/CmdKPanel.cpp` | 空态文案 + 索引化候选源 |
| 改 | `src/ui/LobbyPage.cpp` | Pending 骨架 |
| 改 | 5 个自绘 widget + 4 个页面空态 | §3.4 / §2.9 |
| 改 | `src/main.cpp` | 新门 `runCollectionQa()` + 各门改线/加腿 |
| 改 | `native/gate_matrix.ps1` | 名册 80 → 81（**纯 ASCII**） |
| 改 | `native/qa_palette_ratchet.ps1` + `qa/palette_baseline.json` | C6 新增 + C3 收紧 |
| 改 | `native/i18n/LightAlbum_en.ts` + `.qm` | 手工 append + `lrelease`（`lupdate` 禁用） |
| 改 | `native/CHANGELOG.md`、`docs/迭代指导书.md`、`docs/LightAlbum-2.43.0-使用说明书.md` | 收口 |

---

## 2 · A 轨详设

### 2.1 `CollectionPage`（典藏）

**入口**：`kNav` 增第 8 行，标签「典藏」，图标复用现有 `curate`（`Icons.h` 键表：cull/curate/enhance/gallery/
home/lamp/lobby/log/map/people/reject/retouch/star/swap —— **不新增图标**）。位置插在「图库」之后、「人物馆」之前
（浏览组内聚），即新行序 `前厅0 足迹1 图库2 典藏3 人物馆4 修图5 日志6 关于7`。

**代价必须写清**：`MainWindow.cpp` 现有 **6 处**裸行号（`:507/526/537/641` = 2 图库、`:531` = 3 人物馆、
`:1129` = 4 修图，另有 `:219/543` = 0 前厅）与 `:1265-1267` 的 `row == 0 / row == 3` 动效预设谓词。插入行会把它们全部错位。
**按 R5**：先做**行号归一**（§9）再插行，顺序不可换——先改常量再动表，插入本身才是机械操作。

**空间语义**：`Hall::Collection` 追加在枚举**末尾**（`{ Lobby, Gallery, People, Studio, Vault, Common, Collection }`），
不插入中间——`lampFor()` 按 `int(h)` 索引 `follow[]`，中间插值会静默改掉 Vault/Common 的灯。
`follow[6]` 同步扩为 `follow[7]`，`Collection → Lamp::Jade`（典藏是亮底画廊语汇，与图库/人物馆同族）。
`lampFor` 的边界判断 `i < 6` 改写为 `i < int(sizeof(follow)/sizeof(follow[0]))`——同一魔数不允许有两处。

**三 tab 内容**（每 tab 一个 `QListWidget`/自绘卡，不引入新容器框架）：

- **tab1 合集**：`listAlbums()`（`Library.cpp:760-781`，返回 `AlbumRow{id,name,source,createdAt,count}`）。
  卡片 = `LobbyPage::makeThumbCard` 同一张尺子（`LobbyPage.cpp:482`）的第一帧缩略 + 名称 + 「N 张」+ 来源徽记
  （`source=='curate'` → 「策展收入」，`'cmdk'` → 「检索收入」）。点卡片 → §2.2 凝视。空态走 §2.9。
  **同名合集不去重**（`createAlbum` 每次 `INSERT`，`:739-744`；两次收同一展 = 两份事实，不是重复数据），
  卡片显示 `created_at` 日期以区分——**不写「合并合集」这种本波不存在的功能名**。
- **tab2 展览史**：`loadExhibitHistory()`（`:671-695`，`ORDER BY generated_at DESC`）。逐行 = 展名 + 小记 +
  日期 + 帧数 + 「已收进 / 已换走 / 已静音」徽记（读 `feedback`）。点行 → §2.2 凝视该展路径。
  **不做编辑、不做删除**（本波只给去处，不给维护）。
- **tab3 安静下来的选题**：新 API `listMutes(now)`（§2.3）。逐行 = 模板可读名 + 静音时长（永久 / 至某日）+
  「唤醒」按钮。

**数据可见性**：`CollectionPage` 构造签名沿用兄弟页范式 `CollectionPage(Library*, const QString& thumbsDir, QWidget* parent)`，
但**只**在 GUI 线程调 `const` 查询方法。隐私门沿用兄弟页同一机制（构造期传 `hidePrivate`，解锁/上锁由
`MainWindow.cpp:594-618` 那一族 `refreshPrivateState()` 汇合刷新）。
**这里有一处必须处理的口径陷阱**：`AlbumRow::count` 是 `listAlbums()`（`Library.cpp:760-781`）里的
**相关子查询全量计数**，它不知道隐私门。未解锁时若直接显示这个数，界面上的「N 张」就是一个**说谎的数字**
（点进去却看不到）。裁决：`hidePrivate=true` 时卡片计数由页面**按可见路径重算**，
门禁腿断言「含私密照的合集在未解锁态下，显示数 ≠ `AlbumRow::count` 且 = 可见数」。

### 2.2 播放已有合集 = 复用凝视，零新播放器

`GazeLightbox` 的公开面已支持任意列表回放，**不需要** curation：
`setScope(const QStringList&)` + `setPhoto(const Photo*, thumbsDir)` + `enter(Mode, startPath)`
（`src/ui/GazeLightbox.h:57-121`）。

**但路径本身现在取不出来**：核实全仓，`album_items` 只有**写入**（`Library.cpp:748`
`INSERT INTO album_items(album_id,path,ord)`）与**计数**（`:767` 相关子查询），
**没有任何按合集取 path 列表的读口**。故本波必须新增：

```cpp
QStringList Library::albumPaths(qint64 albumId) const;   // SELECT path ... WHERE album_id=? ORDER BY ord
```

`ord` 是策展时的展帧序（主图恒在首位；`createAlbum` 按传入序写 `:748-754`），**必须按 `ord` 返回**——
回放顺序就是收藏顺序，这条有门禁腿（同一合集两次取回序一致，且首元素 = 建集时的第一路径）。

因此典藏页点击 → `MainWindow` 侧一条既有路径：取合集的 `paths`（经 `Library`）→
`openGaze(startPath, paths)`（声明 `MainWindow.h:53`，定义 `:1176`；人物馆展室已用同一重载，`:501`）。
**禁止**为合集新开 lightbox 实例或复刻第二份翻页逻辑（同源纪律）。

顺序纪律照抄 `MainWindow.cpp:1191` 的门禁注释：`enter()` 会 `interruptCuration()`，故策展入口必须
`enter` → `setCuration`；**非策展入口（本波合集）不涉及 setCuration，不受该序约束**，但 `setScope` 必须在
`enter` 之前完成——门禁腿要断言这一点（§4）。

### 2.3 反悔入口：`unmuteTemplate` + 逐条唤醒（B2 blocker）

**持久层**（`Library.{h,cpp}`，紧邻 `muteTemplate` `:707-717` / `loadMuteState` `:719-729`）：

```cpp
struct MuteRow { QString tmpl; qint64 until; };                       // until = 绝对秒；永久哨兵见 Scheduler
QVector<MuteRow> listMutes(qint64 now) const;                         // WHERE until > ? ORDER BY until DESC
bool unmuteTemplate(const QString& tmpl);                              // DELETE FROM template_mutes WHERE template = ?
```

`loadMuteState()`（引擎消费方）**不动**——它返回 `QStringList`，语义正确且被 `CurateEngine` 吃；
新 API 是给 UI 的**带时长视图**，两者同源一张表，不构成双真源（铁律 20）。

**编排层**：`CurateScheduler::unmuteExhibit(const QString& token)` → `lib_->unmuteTemplate(token)`，
返回 `bool`。**必须在 GUI 线程调用**（写库），与 `muteExhibit`（`:210-214`）对称。
MainWindow 只接线、不直写：`M_no_direct_window_writes`（`main.cpp:3550-3553`）现在禁
`lib_->createAlbum(` / `lib_->muteTemplate(` / `lib_->updateExhibitFeedback(`，本波**加入
`lib_->unmuteTemplate(`**——新写口子必须当场纳入同一条腿，否则「唯一写者」出现旁路。

**UI**：tab3 每行「唤醒」→ `unmuteExhibit` → 成功后 ① 列表就地移除该行 ② `showAck()` 真实回执 ③ 立刻
`maybeGenerate(..., /*force=*/true)` 换一版（与「别再这类」当场撤下对称：唤醒应当**当场可见地**让这类重新可被选中）。

**回执改线（触发 R2）**：

| 现文 | 新文 | 门禁改线 |
|---|---|---|
| 「这类展已安静下来。反悔入口下轮上线。」 | 「这类展已安静下来。可在**典藏 · 安静下来的选题**里唤醒。」 | `M_ack_mute_honest` 改为 `contains("反悔") \|\| contains("唤醒")` **且** `!contains("下轮")` |
| 「已把《%1》的 %2 张存为永久合集，合集浏览下轮上线。」 | 「已把《%1》的 %2 张存为永久合集，可在**典藏**里再看。」 | `M_ack_keep_honest` 改为 `contains("典藏")` **且** `!contains("下轮")`；原 `!contains("随时")&&!contains("再看")` 的**禁令方向翻转为要求**（见 R2） |

两条腿的**变异证明**必须在计划里写明：把任一句改回含「下轮」→ 该腿必须红。

### 2.4 死代码一并清掉

`MainWindow::makePlaceholder()`（`:1302-1340`）**0 调用点**。本波「占位死代码」随 §4 门禁腿一并删除；
删除后加一条源扫腿断言 `makePlaceholder` 在 `ui/` 目录 0 命中（防止未来又造占位页而不接线——这正是本波要治的病）。

### 2.5 长操作反馈：生成期骨架 + 呼吸字（audit-1/2）

策展生成走 `QtConcurrent::run`，`generated(id)` 恒在 GUI 线程发射（`CurateScheduler.h:9-14` 注释）。
现状：按下「换一展」到 `generated` 之间，前厅卡片**静止不动**，用户无从判断是在算还是没算。

- `LobbyPage::setPending(bool)`：pending 期间今日一展卡换成**骨架**（`Lumen::inset()` 底 + `line()` 描边 +
  一行呼吸字「正在为本机选展…」），走**既有** `Motion::kExhibitBreath`（2400ms，`MotionKit.cpp:202`），
  **不新增时长档**。
- 三处 `maybeGenerate` 调用点：ctor `MainWindow.cpp:208`、「换一展」`:479`、「别再这类」`:486`——
  进入前置 pending，`generated` 落地即撤。`force` 与非 `force` 同处理。
- 减动效（`prefers-reduced-motion`）下骨架**仍要出现**，只是不呼吸（静态 + 文案）。

### 2.6 ⌘K 空解析必须有话说（audit-17）

`CmdKPanel` 现有谓词 `isBare`（`:559/562`）判定「无筛选条件」。本波要求：**解析不出任何实体时**，面板必须回一句
说明性文案（「没听懂这个词。可以试：人物名 / 2019 / 杭州 / 去年」）而不是静默空列表。文案必须含**三个真实可解析的样例**，
门禁断言样例串逐个能被 `parse()` 解析成功（**不许写自己解析不了的提示**——这是本波对「界面上的每句话都是承诺」的新解）。

### 2.7 凝视键位带（audit-24）

`GazeLightbox` 现无 legend（全仓 0 命中），键位只写在《使用说明书》里。控件条现有 3 枚按钮
（`GazeLightbox.cpp:232-257`）。本波在控件条右侧加**常驻键位带**：`← → 翻｜空格 凝视｜Enter 收藏｜Esc 退出`。
**硬性纪律**：带内每一枚键都必须实测有效，且**焦点初始态**（刚 `enter` 未点击任何子控件）也必须有效——
按用户级记忆 `feedback-advertised-ui-text-is-a-contract`，验收要逐条实测后才允许写进 legend。
读屏语义：legend 用 `QLabel` + `setAccessibleName`，不进 Tab 序（不可点，故不给焦点）。

### 2.8 ⌘K「暂存为合集」点亮（R7，第 3 条承诺）

`CmdKPanel.cpp:385-390` 的死键必须随典藏页一起活过来：有勾选 → 可点 → `CurateScheduler::keepSelection(name, paths)`
（**复用 `createAlbum` 同一写入路径**，`source='cmdk'`；禁止在 UI 层直调 `lib_->createAlbum(`，见 §2.3 的
`M_no_direct_window_writes` 扩项）→ 回执「已存为合集，典藏里能再看」→ 典藏 tab1 下帧可见。
无勾选 → 仍禁用，但 tooltip **不再写「下轮」**，改说当前缺什么（「先在结果里勾选照片」）。
门禁腿：① `stash->setEnabled(false)` 后不再跟随「下轮」tooltip（源扫切片到 `stash` 构造段）；
② `keepSelection` 空 paths / 空标题 → 返回 0 且**不建空壳合集**（与 `keepExhibit` 同判据，逐字对称）。

### 2.9 四页空态（audit-22/30）

按 `LobbyPage::makeEmptyGuide`（`LobbyPage.cpp:548-589`）的语气与 token，补 4 处 else 分支：
`GalleryPage.cpp:652-679`、`RetouchPage.cpp:670-675`、`LogPage.cpp:366/371-381`、`MapPage.cpp:266/458-461`。
每处 = 一句「这里为什么是空的」+ 一句「往哪走」，**不许出现本波不存在的入口名**。典藏页自身三个 tab 各需空态（共 7 处）。

---

## 3 · B 轨详设

### 3.1 时间地点落列 + 索引（audit-34）

现状：`exif_json` 是唯一时间/地点载体，`Library::open` 每行做一次 `QJsonDocument::fromJson`
（`Library.cpp:108-132` 单点解析），SQL 侧无年/月/城市列 ⇒ 任何时间维度查询必须先全表载入再解析。

**新增列**（`photos`）：`shot_ts REAL NOT NULL DEFAULT 0`（拍摄时刻 epoch 秒，date+time 合成，无时间分量则取当日 00:00）、
`shot_year INTEGER NOT NULL DEFAULT 0`、`shot_month INTEGER NOT NULL DEFAULT 0`（1–12）、`city TEXT`。
`NOT NULL DEFAULT` 形状照抄 `seen_at` 的既有先例（`Library.cpp:49`）——无 EXIF 的老库行必须落 0 而不是 NULL，
否则索引查询与 `= 0` 谓词两套语义会分叉。

**三条索引**：`idx_photos_shot_ts`、`idx_photos_year_month (shot_year, shot_month)`、`idx_photos_city`。

**写入点唯一**：`Scanner.cpp` 已在 `:412-450` 写 `d/tm/y/ct` 进 `exif_json`，并在 `:292-299` upsert——
四列在同一处绑定写入，**不允许**出现第二处写点（否则回填与新扫描分叉）。

**迁移范式**（逐字照抄 `Library.cpp:30-70` 的既有 `la_mig` 独立连接写法）：
- 用**独立连接** `la_mig`，`ALTER TABLE photos ADD COLUMN …`，**返回值丢弃**（重复 ADD 报错无害）；
- 索引用 `CREATE INDEX IF NOT EXISTS`；
- 不开事务包裹 DDL；真正的成败门是既有大 SELECT（`:86-91`）——它打不开才算迁移失败；
- **fail-open**：任一 ALTER 失败即视为「列已存在或库不可写」，后续 SELECT 照旧，程序正常启动。

**一次性回填**：新迁移标记 `meta('ex_cols_backfill')`。缺失时执行一次 `SELECT path, exif_json FROM photos`
→ 内存解析 → 批量 `UPDATE`（单事务、GUI 线程）。完成后写标记。**幂等**：有标记直接跳过。
5k–50k 量级实测一次即可；`measuredMs` 回填门。**回填只在四列存在后执行**，否则 UPDATE 报错被 fail-open 吞掉。

### 3.2 唯一消费方 = ⌘K 时间筛选（B4 的诚实性所在）

只落地基、没有调用方 = 投机代码。本波给它的调用方是 `CmdKPanel`：
`candidatesFor(chip, f)`（`CmdKPanel.cpp:463`）与筛选谓词（`:559/562` 一带）现在遍历全量 `QVector<Photo>`。

改造：当 filter 含 `yearMin/yearMax/month/day` 或城市时，先向 `Library` 取**索引化的 path 集合**，再只对命中的
`Photo` 做其余谓词：

```cpp
QVector<QString> Library::pathsForShotRange(qint64 fromTs, qint64 toTs, bool hidePrivate) const;  // BETWEEN shot_ts
QVector<QString> Library::pathsForCity(const QString& city, bool hidePrivate) const;              // = city
```

`CmdKPanel` 增 `void setTimeSource(Library*)`（可空；为空 → 走原全量路径，门禁两种形态都要绿，且**结果必须逐字节相同**）。

**隐私门不许被索引绕过**（本波唯一的越权风险）：两个查询带 `hidePrivate` 谓词，未解锁时 SQL 里就必须
`AND private = 0`（列名核实为 `private`，`Library.cpp:39`），**不允许**「先全取再在 UI 过滤」——
一旦 path 集合进了内存，它就成了第二份可能泄漏的真相。
门禁腿：造一份含 `private=1` 的 fixture，`hidePrivate=true` 的返回值必须**不含**该路径，且
`EXPLAIN` 仍走索引（谓词写在 `WHERE` 里不影响索引选择，用这条腿证明两件事不打架）。

**两条硬腿**（§4）：① **parity**——一组查询在「索引源」与「全量源」下的结果集必须完全一致
（沿用 `clip-parity-tolerance-contract` 的对称篡改做法：每个判定分支配自己的反例）；
② **EXPLAIN**——对两条查询跑 `EXPLAIN QUERY PLAN`，断言输出含 `USING INDEX`，且**无** `SCAN photos`
（若优化器选全表扫，这条腿必须红，而不是静默退化）。

### 3.3 列数函数三网格共用（audit-31）

现状两套：`EditorialView.cpp:152-157` `cols_ = qBound(3, int(W/230), 5)`（带 60ms 防抖 resizeEvent）
与 `PeoplePage.cpp:172-179` 死写 `const int cols = 4`。无 `Layout.h` 工具（全仓 0 命中）。

**新增** `src/ui/Layout.h`：`inline int gridCols(int widthPx, int targetPx = 230, int lo = 3, int hi = 5)`
（纯函数、无 Qt 依赖、门禁直测）。三处消费：`EditorialView`、`PeoplePage`、`CollectionPage`。
**防抖只在一处**：`Layout.h` 只做算术，resize 节流归各页既有机制，工具函数不许长出状态。

### 3.4 画布层 lamp token（audit-15/16，B3）

现状：切灯后 5 个自绘 widget 保持硬写色 ⇒ 同一屏两套底色（L4 暮金底 + 硬编码玉照绿遮罩这类）。
`Theme.h` 无 canvas / mask / focus-ring token（0 命中）。

`Lumen` 新增 4 个访问器（读活跃灯表，与既有 11 个同族）：
`canvasBg()`（比 `bg()` 深一档的工作区）、`canvasLine()`、`maskColor()`（半透明遮罩基色）、`focusRing()`。
`LampTokens` 扩 4 字段 × 4 灯定值，**两两任列相异**由 `LA_LAMP_QA` A 组既有口径覆盖（扩列后必须重跑）。

逐点替换（行号为核实值）：`CropCanvas.cpp:21,42,45,101,105`、`MaskCanvas.cpp:10`
（`kMaskColor QColor(0x5a,0x8f,0x6b)`）、`:16,57,64`、`EditorialView.cpp:89`、`CompareWidget.cpp:37,39,57,60`、
`VideoPlayerWidget.cpp:235,242`、`LogPage.cpp:158-160`。
另：`GazeLightbox.cpp:104,125,279,346` 的影院层 `rgba()` 改由 `maskColor()` 派生 alpha。

### 3.5 棘轮新维度 C6 + C3 收紧（治理）

**C6 = 画布/遮罩层裸色**。`qa_palette_ratchet.ps1` 现有 C1..C5（`:52-74`），扫描根 `src\ui` + `src\core`
（`:35`，**`main.cpp` 不在内**）。新维度口径实测（不是抄走查里的「37」）：
`QColor(0x` 在扫描根 = **18**，`rgba(` = **15**。裁决：**C6 = 两者并集，基线取脚本首跑实测值，上限只降不升**。
白名单沿用 `:30` + `:48` 的既有豁免机制；`Theme.h` 的四灯定值表本身必须豁免（那是 token 源，不是债）。
`compare` 循环（`:101-112`）现在硬编码 5 个键 —— 加 C6 必须**同时**把它改成遍历键表，否则新维度静默不参评
（「加了但没比」= 假绿，这是本项目记过的判例类型）。

**C3 收紧**按 R3 的两步走执行，改完定义后 `-Update` 首跑必须实测 0 才允许落基线。

### 3.6 动效宪章与代码对齐（治理）

按 R4：D3 文档增「例外白名单」段（原文 `:28`/`:51` 之后），并把 `MotionKit.cpp:118-209` 的 9 个预设逐个
标注所属档位（宪章 `:32-35` 的四档）。**门禁不动**。附带交付一处真实缺口：人物墙 `PeoplePage` 与
`DedupDialog` 目前 0 Motion 引用 → 人物墙接 `stagger`（宪章 `:43` 未实现编排之一），成本 ≤ 1 个驱动器/卡。

### 3.7 诚实的最小窗宽（audit-32）

现状全仓 `setMinimumSize` **0 处**（只有 `nav_->setMinimumWidth(68)`，`MainWindow.cpp:263`），
QA 钉 `resize(1440, 900)`（`:1397`）。窄窗时 230px 卡宽 × 最少 3 列 = 690px + 68px 导航 + 边距，
低于此值网格必然塌。裁决：**声明 `setMinimumSize(960, 640)`**（3 列 + chrome 的实测下界），
并在门禁里用 `layout` 断言 960 宽下 `gridCols()==3`、1440 宽下 `==5`、2560 宽下仍 `==5`（封顶生效）。
「支持任意窄」是不成立的承诺，故不写。

---

## 4 · 门禁（名册 80 → 81）

**新门 1 道**：`LA_COLLECTION_QA`（kind=`exe`，**fast**，timeoutSec 取实测 + 余量）。腿组：

| 组 | 断言 |
|---|---|
| A API 对称 | `muteTemplate → listMutes` 可见；`unmuteTemplate` 删行后 `listMutes` 不含该 token；对未静音 token 调 unmute 返回**语义如实**（不许假 true） |
| B 唯一写者 | `unmuteExhibit` 落在 GUI 线程（复用 `lastWriteThreadId()` `Library.h:158` 取证形状）；`M_no_direct_window_writes` 扩含 `lib_->unmuteTemplate(` |
| C 导航完整性 | `kNav/kNavHall/kNavIcons` 三表**等长**且长度 = 8；`lampFor(Hall::Collection)==Jade`；`B7_oob_fallback`（int 99）仍绿 |
| D 播放同源 | 合集入口走 `openGaze(path, paths)`，源扫断言 `ui/` 内 `new GazeLightbox` 仍只有 1 处；`setScope` 必须先于 `enter`（切片到函数体比较，抄 `M_enter_before_setcuration` 的形状） |
| E 文案诚实 | 两条 ack 腿的新方向 + 变异证明 |
| F 死代码 | `makePlaceholder` 在 `ui/` 0 命中 |
| G 网格算术 | `gridCols(960)==3`、`(1440)==5`、`(2560)==5`（封顶）、`targetPx` 非正数回退默认（§3.3） |
| H 承诺清零 | 全仓 `src/`（去注释后）`下轮` 0 命中；`stash->setEnabled(false)` 段内 tooltip 不含「下轮」（§2.8 R7） |

**改线/加腿的既有门**：`LA_CURATE_QA`（R2 两条翻转 + B 组扩项 + 139 腿数变化需在名册/文档同步）、
`LA_LAMP_QA`（+`B8_collection_jade`；A 组扩 4 列）、`LA_CMDK_QA`（空态文案 3 样例可解析 + parity + EXPLAIN）、
`LA_MOTION_QA`（**只加不改**：stagger 接线腿；断言一律不动，见 R4）、`LA_LOBBY_QA`（pending 骨架 + 减动效下仍出现）、
`LA_GAZE_QA`（legend 存在 + legend 上每枚键实测有效）、`LA_PALETTE_QA`/ratchet（C6 + C3）、
`LA_I18N_QA`（entries 34 → 34+新增串；.ts 与 .qm 同提交）、`LA_LAYOUT`（新 `gridCols` 纯函数腿并入 `LA_LOBBY_QA`
还是独立门 → 裁决 R6：**并入 `LA_COLLECTION_QA` 的 G 组**，`Layout.h` 是纯算术，为它单开一道门会让名册虚胖）。

**名册纪律**（用户级 + 项目级记忆，硬性）：`gate_matrix.ps1` 新增行**必须纯 ASCII**（`:1198-1207` E11 会拦非 ASCII 字节，
`:1209-1222` E12 校验文本行数=解析数）；`-SelfTest` 必须 E1 gates=81 / fast=23 / full=58 / errs=0。
所有门跑在 **PowerShell**，不用 Git Bash。

---

## 5 · i18n

`native/i18n/LightAlbum_en.ts` 手工 append（**`lupdate` 禁用**——迭代指导书铁律 24），随后
`D:/qt/6.8.3/mingw_64/bin/lrelease.exe` 重编 `.qm`，`.ts` 与 `.qm` **同一提交**落盘（`i18n-qm-manual-lrelease` 判例）。
本波新增串估计 40–55 条（典藏页 + 空态 7 处 + legend + ack 2 条 + pending + ⌘K 空态 + 例外文案）。
`LA_I18N_QA` 的 finished 计数必须与 `.ts` 条目数一致、unfinished = 0。

**必须同批改的耦合点（否则会以为只是文案改动却撞红一道门）**：`LA_I18N_QA` 的 `ctxCheck` 把两条 ack 源串
**逐字写死在门禁里**（`src/main.cpp:10676` 与 `:10680`）。改 `keepAck`/`muteAck` 的中文时，
`.ts` 的 `<source>`、`main.cpp` 的 ctxCheck 字面量、`M_ack_*` 腿三处必须**同一提交**改线——
本项目有过「改了界面文字忘了改门禁字面量 → 门红 → 顺手把门改松」的判例，此处按最严口径执行。

---

## 6 · 验收口径（全部可测，不采信肉眼）

1. `-SelfTest`：E1 gates=81 fast=23 full=58 errs=0；E11 nonAsciiBytes=0；E12 textual=81 parsed=81。
2. 全量回归：81 门全绿，且 `LA_CURATE_QA` 腿数在名册/CHANGELOG/指导书三处**同一数字**（数字必署名测法）。
3. **兑现率 100%（B1）**：`grep -rn "下轮" src/` 在 ack 文案里 0 命中（除注释与门禁的反例字面量），
   并把这条 grep 固化为一门禁腿——**空头回执必须红，而不是靠人记**。
4. **blocker 归零（B2）**：合集可在典藏页看到并点进凝视；静音可在 tab3 逐条唤醒（实测前后 `listMutes` 计数差 1）。
5. **切灯 0 半套底色（B3）**：4 盏灯 × 5 个画布 widget 各截一帧，像素探针落在 token 表内；
   C6 基线实测数 ≤ 首跑值。
6. **时间走索引（B4）**：`EXPLAIN QUERY PLAN` 含 `USING INDEX` 且不含 `SCAN photos`；
   parity 腿在 5 组查询下逐字节一致。
7. **界面上的每句话实测**：legend 逐键、⌘K 样例逐条、空态里的入口名逐个可跳转。
8. 老库兼容：用 2.42.0 生成的库文件打开 → 迁移跑通 → `photos` 行数不变、回填计数与 SELECT 行数相等。

---

## 7 · 风险与对策

| 风险 | 对策 |
|---|---|
| 插入导航行导致 6 处裸行号错位（回归面最大） | §9 先归一为 `kRow*` 常量，再加一条源扫腿禁止 `setCurrentRow(<数字字面量>)` |
| 一次性回填在大库上慢（50k 行解析 + UPDATE） | 单事务 + `measuredMs` 回填；标记缺失时**只在 GUI 线程**跑；若实测 >3s，改「分批 + 进度门」并在本 spec 增补裁决（不允许偷偷挪到后台线程写库，破红线 2） |
| R2 翻转两条诚实腿 = 自我否决上一波的裁决 | 改线在**同一提交**内完成，且新腿方向不变（仍要求「不许假可用」，只是新增「不许假不可用」） |
| EXPLAIN 腿受 SQLite 统计影响 | 门禁内先 `ANALYZE`；若仍不稳，退化为「查询计划文本含 idx_ 名」并记裁决 |
| `LA_MOTION_QA` 与宪章冲突（R4）若被后人反过来修 | 例外白名单写进 D3 文档正文 + 在 `MotionKit.cpp:155` 上方一行注释指向该段 |

---

## 8 · 收口

版本 `2.42.0 → 2.43.0`（`CMakeLists.txt:2` 唯一真源）；`CHANGELOG.md` 顶部新节（门数与腿数按 §6.2 实测填，
不许沿用旧数）；`docs/迭代指导书.md` §3 门数 + §7 台账行；新建 `docs/LightAlbum-2.43.0-使用说明书.md`
（`release.ps1:985-991` 对缺手册硬抛错，本波不打包但手册照写）。
**不 push、不 tag、不打包**，等用户明确授权。

## 9 · 行号归一（A 轨第一步）

`MainWindow` 里 6 处裸行号（`:219/507/526/531/537/543/641/1129`）与 `:1265-1267` 的 `row == N` 谓词
统一改为具名常量：`kRowLobby/kRowMap/kRowGallery/kRowCollection/kRowPeople/kRowStudio/kRowLog/kRowAbout`，
值由 `kNav` 表下标单点导出。动效预设选择器（`:1265-1267`）改为**按空间语义**取预设
（`Hall::Lobby → kLightFirst`、`Hall::People → kPushDoor`、其余 `kNavFade`），把「行号 → 动效」的隐式耦合
换成「语义 → 动效」——这也是本波 Nielsen #4.1（同类操作同一位置/同一隐喻）在代码侧的镜像。
