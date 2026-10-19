# 波3「主动」· 智能策展系统「常设展」设计规格 · 2026-10-19

> 来源：愿景包 `native/docs/design_studio/2026-10-14-vision/` 的 **V5 五波次排期**（波1「可信」/波2「可达」
> 已随 2.41.0 交付）与 **V6 策展系统全案**详设；本文是该波的 writing-plans 论证基座，冲突处本 spec 优先。
> 前序：`specs/2026-10-18-wave12-trust-reach-design.md`（波1+波2）。

## 0 · 范围、红线与已裁决的文档冲突

**本波 = 波3「主动」：策展管家 v1（周轮换、无偏好学习）+ 废片巡检。** 一句话：把第四轮落地的全本机 AI
（clipinfer/faceinfer/EXIF/去重/模糊度）从「被动检索」翻转为「主动开讲」——每周由本机 AI 产出一版
**常设展提案**（5–9 帧 + 展题 + 一句话小记），用户只用 **换一批 / 收藏此展 / 别再这类** 三种手势回应。

**三条红线（V6 §一，不可破）**
1. 选题与打分 **100% 离线**（clipinfer / faceinfer / EXIF 三源），**零网络调用**。
2. 建议**永不弹窗**——只出现在前厅「今日一展」卡与 ⌘K 结果里；一键永久静音，**默认不推送**。
3. 淘汰类建议（废片巡检）**永不自动删除**，只打包成待点头的批次，且**可整批反悔**。

### 0.1 文档冲突裁决（本 spec 必须裁，否则 writing-plans 无法落笔）

| # | 冲突 | 两方原文 | 裁决 |
|---|---|---|---|
| **R1** | 「逐帧换展」属波3 还是波4？ | V6 §四：「点开即凝视模式序列：**逐帧换展**」；V3 #2 把「翻页=换展…旧画淡出 120ms、新画携光入位 360ms + 呼吸停 2400ms」列为**凝视模式自身**的动效；而 wave12 spec §6 备注把「凝视换展」标注为**波4** | **波3 交付策展序列的自动逐帧**（含 2400ms 呼吸停）。理由：V5 波次表把「凝视模式」整件列入**波1 且已交付**基础态（`Mode::Gaze` 已存在），波4 那条指的是**长廊/时间滑轨语境**下的换展打磨；且 2400ms 呼吸是 D3 **既有档位**，不新造时长。若用户否认，唯一备选是退化为「手动翻页 + 展名小记字幕」（本 spec 不收，见 §6 风险 1）。 |
| **R2** | 偏好学习做不做？ | V6 §二打分器有 `w2·用户偏好`，§四「收藏此展→模板权重+1」；V5 波3 原文写「策展管家 v1（周轮换，**无偏好学习**）」 | **v1 不做偏好学习**：打分器保留 `w2` 字段但**恒为 0**；「收藏此展」只落**永久合集**（用户可见的正反馈），**不回灌**排序权重。门禁断言 `w2==0` 且打分与收藏历史**逐字节独立**（防未来偷偷接上）。 |
| **R3** | 「每周日 03:00 空闲时生成」怎么实现？ | V6 §四：定时生成，复用索引线程避开工作时段 | **v1 用「懒生成 + 7 天节流」**：应用启动/前厅 `refresh` 时，若上次成功生成距今 >7 天，则在**后台线程**生成一次。不做真实 03:00 定时器。如实记录「非定时、为节流式惰性生成」。 |
| **R4** | 高度层 E4 是否存在？ | V4 §四：E4 开馆 = 金描边+内发光，仅 ⌘K 面板与今日一展卡；实测 `Theme.h:291` **只有 `Elev::E1/E2`**，E3/E4 不存在 | **新增 `Elev::E3`(聚光) + `Elev::E4`(开馆)**；E4 首批两点应用 = **今日一展卡（新）+ ⌘K 面板（retro）**，与 V5「E4 两点应用」逐字对齐。 |
| **R5** | 「已展出 / 冷却 / 静音 / 收藏合集」的持久化从哪来？ | V6 §六称「数据面现成」；实测**无**任何展出历史、冷却、静音、合集存储（`CmdKPanel.cpp:386-390`「暂存为合集」是 `setEnabled(false)` 占位；`Scanner.cpp` 只有单表 `photos` + `meta`） | **本波新增 SQLite 持久化**（见 §4）：`exhibits`（展历史 + 冷却 + 静音）与 `albums`/`album_items`（收藏此展→永久合集），沿用 `Scanner.cpp:34-61` 建表 + `Library.cpp:29-40` 幂等迁移的既有模式。**「数据面现成」对 AI 三源成立、对状态面不成立**，如实记为缺口。 |
| **R6** | 「入库时间 / 近 4 周已展出 / 最近一次批量导入」的数据从哪来？ | V6 用「入库>1 年」「近 2 周新入藏」「最近一次批量导入」；实测 `seen_at REAL` 列**存在但 `Library::open` 不 SELECT**（`Library.cpp:60-62`），`Photo` 无该字段；`mtime` 同样未载入 | **补读 `seen_at` 进 `Photo`**（新增 `Photo::seenAt`；`Library::open` SELECT 增加该列）。这是 R2/R3 之外唯一的数据前置，属**最小必要新增**。 |
| **R7** | 12 类模板是否全量交付？缺数据源的怎么办？ | V6 §三给了 12 类首发集合；但部分模板依赖本波才有的状态（「从未展出」）或需新机制（主色聚类 / 轨迹连续段） | **12 类模板表全量入册**（id/展名模板/数据源/方向 全 12 条，纯函数表）；**某模板在其数据源缺失时返回空集并计入 `skipped[reason]`**——**绝不伪造数据**（沿用波1「绝不画假数据」的既有裁决）。门禁断言 skipped 的原因分类正确、且空返回值不产生任何卡片。 |

### 0.2 明确不做（波4/波5 边界，写清以免越界）
- **波4「沉浸」**：时间滑轨长廊 + 地理互切 + 人物生涯时间条/周年展（「周年展」在本波只作为**人物周年模板**的一个选题逻辑，不做轮换机制）。
- **波5「出口」**：画布布展 + 海报导出 + **⌘K v2 整句句法解析**。
- **本波不做**：偏好学习（V5 明确）、真实定时器（R3）、云端/联网（红线1）、自动删除（红线3）、
  「别再这类」的设置页反悔 UI（v1 只回写冷却，反悔入口**如实标注「下轮上线」**，不假装可用）。

---

## 1 · 总体架构

### 1.1 分层

```
┌ UI 层（GUI 线程） ──────────────────────────────────────────────┐
│ LobbyPage「今日一展」卡（新 makeCurateCard）                     │
│   ├ 展名(Display/展签体) + 主图 + 小记 + 三手势按钮             │
│   └ 点开 → GazeLightbox 凝视序列（自动逐帧 + 呼吸停 2400ms）     │
│ CmdKPanel：策展展品出现在结果里（复用，E4 描边）                 │
└────────────────────────────────────────────────────────────────┘
        ▲ 只读：Exhibit / BadShotBatch        │ 三手势 → 意图
        │                                     ▼
┌ 编排层（后台线程，纯计算）──────────────────────────────────────┐
│ CurateScheduler：7 天节流 → 触发一次生成                        │
│   candidatePool → bucket → propose(12 模板) → score(w1/w3/w4)   │
│   → compose(主图+陪展+展名+小记) → frequencyGuard(冷却/静音/周) │
│ 全为**纯函数**（输入 photos+state+now，输出 Exhibit），门禁可直测 │
└────────────────────────────────────────────────────────────────┘
        │ 结果回写（Qt::QueuedConnection，GUI 单写者，铁律）
        ▼
┌ 持久层（SQLite，GUI 线程唯一写者）──────────────────────────────┐
│ Library：+exhibits（展历史/冷却/静音） +albums/album_items（合集）│
│ Photo：+seenAt（R6）                                            │
└────────────────────────────────────────────────────────────────┘
```

**纪律继承**：后台线程**绝不**直接触库（记忆 `ai-single-writer-gui-thread`）；生成结果经 `Qt::QueuedConnection`
回写，沿用既有 `AiWriter` 同款语义。三手势是**意图**，由 GUI 线程落库并 `refreshExhibits()`。

### 1.2 组件拓扑

| 动作 | 文件 | 说明 |
|---|---|---|
| 新增 | `core/CurateEngine.{h,cpp}` | 纯函数：候选池/分桶/12 模板表/选题/打分/成展/频控/废片巡检 |
| 新增 | `core/CurateState.h` | 纯数据结构：`CurateWeights/Exhibit/Theme/BadShotBatch/SkipReason` |
| 改造 | `core/Library.{h,cpp}` | +`seenAt` 载入；+`exhibits`/`albums`/`album_items` 建表与迁移；+读写 API |
| 改造 | `ui/LobbyPage.{h,cpp}` | +`makeCurateCard()`（E4 开馆层 + 三手势 + 呼吸微光） |
| 改造 | `ui/GazeLightbox.{h,cpp}` | +`setCuration(const Exhibit&, thumbsDir)`（序列播放 + 展名/小记字幕） |
| 改造 | `ui/Theme.h` | +`Elev::E3/E4`（E4＝金描边+内发光） |
| 改造 | `ui/Icons.h` | +馆长图标组 `curate/star/reject/swap`（1.5px 描边体系内，金色变体） |
| 改造 | `core/MotionKit.{h,cpp}` | +`kExhibitBreath`（2400ms 呼吸，复用既有 `Pulse` 曲线，不新造曲线） |
| 改造 | `ui/MainWindow.cpp` | 策展编排接线：节流触发 / 三手势接线 / `refreshExhibits` 汇合 |
| 新增 | `main.cpp` | `runCurateQa()` + env 分发 + FORCE_BROKEN |
| 改造 | `gate_matrix.ps1` | 注册 `LA_CURATE_QA`（fast，纯 ASCII） |

---

## 2 · 组件契约

### 2.1 `core/CurateState.h`（纯数据，无 Qt widget 依赖）

```cpp
namespace Curate {
enum class TemplateId { OnThisDay, PersonAnniv, HiddenReunion, CitySlice, HuePilgrimage,
                        MonoHour, BeyondBurst, TravelReel, FestivalEcho, NewIntake,
                        LightArchive, BadShotSweep };          // 12 类，序即 V6 §三表序
enum class Direction { Up, Down };                          // Down 仅 BadShotSweep

struct Weights { double fresh = 0.55, pref = 0.0, quality = 0.30, repeat = 0.15; };  // pref 恒 0（R2）

struct Theme {                 // 一个候选主题
  TemplateId id; QString title; QString blurb;              // 展名 + 一句话小记（模板文案，非 LLM）
  QVector<QString> paths;      // 主图在最前，随后 4–8 陪展
  QString primaryPath;         // 主图（展内画质分最高）
  double score = 0.0; QVector<QString> reasons;             // 打分可解释（门禁断言用）
};

struct Exhibit { bool valid=false; Theme theme; qint64 generatedAt=0;
                 QString skipReason; };                     // 空返回必带 skipReason（R7）

struct BadShotRecord { QString path; QString why; };         // why ∈ {burst, blurry, stale-shot}
struct BadShotBatch  { QVector<BadShotRecord> items; qint64 builtAt=0; };

struct Now { qint64 epochSec; int y,m,d; };                  // 注入式时间（门禁可复现，禁读系统钟）
}
```

### 2.2 `core/CurateEngine.{h,cpp}`（**全纯函数**，门禁核心）

```cpp
namespace Curate {
// 12 模板表（静态，只读）：id → {i18n 展名 key, 数据源枚举, 方向}
const QVector<TemplateSpec>& templateTable();      // 恒 12 条；门禁断言 count==12 且 id 无重

// 候选池 = 全库 − 私密 − 近 4 周已展出
QVector<const Photo*> candidatePool(const QVector<Photo>& all, bool hidePrivate,
                                    const QStringList& recentExhibitPaths, const Now& now);

// 分桶器：clipinfer 8 类 × 人物名 × 年/月 × 城市
Buckets bucket(const QVector<const Photo*>& pool);

// 选题器：每个模板各产 0..1 个候选主题（缺数据源 → 空 + SkipReason）
QVector<Theme> propose(TemplateId id, const Buckets&, const QVector<Photo>& all, const Now&);

// 打分器（全局选题分）：score = w1·新鲜感 + w2·偏好(恒0) − w4·重复惩罚
//   ★ 画质(w3) 不在此式内——它只作用于「展内排序/主图选择」（见 compose 与 R7 注）
double score(const Theme&, const Weights&, const ExhibitHistory&, const Now&);

// 成展器：展内排序 = w3·画质（唯一消费 quality 的地方）；
//   主图 = 展内画质分最高；陪展 = 类内多样（去重邻域去冗余）
Theme compose(const Theme&, const QVector<Photo>& all, const QString& thumbsDir);

// 频控器：每周 ≤1 · 同人同题 60 天冷却 · 静音 → 返回是否放行 + 理由
bool frequencyAllows(const Theme&, const ExhibitHistory&, const MuteState&, const Now&);

// 废片巡检：连拍冗余 ∩ 模糊度低分 ∩ 截图过期；只打包，永不删除
BadShotBatch inspectBadShots(const QVector<Photo>& all, const QString& thumbsDir, const Now&);

// 画质分（V6「凝视分」在本波的定义）：只用现成引擎，只对候选帧算（见 R7 注）
double qualityScore(const Photo&, const QString& thumbsDir);   // CullEngine::blurScore+exposureStats
}
```

**R7 注（关键的诚实裁剪）**：全库逐张算模糊/pHash 太贵。v1 规定 **`qualityScore` 只在「已选出的展内帧」上调用**
（O(5–9)），**不进入全局打分**——全局打分只用 `新鲜感 / 重复惩罚`（`quality` 权重作用于**展内排序与主图选择**）。
门禁 H 组断言：把一个库的 quality 全同 vs 全异，**全局选题结果逐字节相同**（证明 quality 未越界）。

### 2.3 12 类模板表（V6 §三，逐条列数据源，缺源即 skip）

| id | 展名模板 | 数据源（本机，已核实） | 缺源时 |
|---|---|---|---|
| OnThisDay | 那年今天 / 「N 年前的 M 月」 | `Photo::year` + `Photo::date`（`Library.h:55-74`） | skip |
| PersonAnniv | 「{人物} · 第 N 年」 | `Photo::person`（逗号名串）× `year`（`PeoplePage.cpp:100`） | skip |
| HiddenReunion | 「一直没被看见的 N 张」 | 从未展出（`exhibits` 历史）∩ `seenAt` >1 年 ∩ 画质 > 阈值 | skip |
| CitySlice | 「此间 · M 月」 | `Photo::city`（`GeoIndex::resolve`）∩ `seenAt` 近 2 周 | skip |
| HuePilgrimage | 「满墙皆青」 | `category`(8 类) + 主色（波1 `dominantColor`） | **v1 降级**：仅按 `category` 出题，主色聚类留后；如实记 |
| MonoHour | 「无彩之日」 | `category == 黑白`（`Classifier.cpp:47-51`） | skip |
| BeyondBurst | 「同一秒的第二选择」 | 去重邻域（`CullEngine::computePhash/hamming`）∩ 画质陡峭 | skip |
| TravelReel | 「沿着海岸线走的那天」 | GPS 连续段 ∩ 跨日（`Photo::hasGps/la/lo/date`） | **v1 降级**：仅「同城跨日 ≥2 天」；轨迹连续段留后；如实记 |
| FestivalEcho | 「去年除夕的餐桌」 | 静态节历表（**本波新增常量表**）∩ 历史同名节日照 | skip |
| NewIntake | 「入藏 N 张 · 先看 8 张」 | `seenAt` 最近一次批次（R6） | skip |
| LightArchive | 「傍晚六点以后」 | `category ∈ {夜景·星空, 风光}` + `exposureStats` | skip |
| BadShotSweep | 「可以退藏的 N 张」 | 连拍冗余 ∩ 模糊低分 ∩ 截图过期（**唯一 `Direction::Down`**） | skip |

### 2.4 视觉与动效（全部引用既有 token/档位，不新造）
- **E4 开馆层**（新）：`Elev::E4` = 1px 金描边（`Lumen::gold`）+ 内发光 alpha ≤10%（V4 §五「光晕」规范）；仅用于今日一展卡与 ⌘K 面板。
- **呼吸微光**：`MotionKit::kExhibitBreath` 2400ms，复用既有 `Pulse` 曲线（`MotionKit.cpp:45-48`），**不新造曲线**。
- **序列播放**：换帧 = 既有 `LightboxSlide` 380ms；每帧驻足 = 2400ms 呼吸停（D3 既有档位）。
- **图标**：`Icons::make` 新增 `curate/star/reject/swap`，1.5px 描边 24 格圆角端点；**仅**馆长图标组允许金色变体。
- **展签体**：展名用既有 `Type::Display`；人物名/年份牌用 `Type::Label`（衬线混排仅 L1/L4，亮灯一律无衬线——沿用 V4 纪律）。

---

## 3 · 门禁 `LA_CURATE_QA`（fast 层，纯函数，含 FORCE_BROKEN）

比照 `runLobbyQa`（`main.cpp:1349`）形状：`int runCurateQa()`，env 分发（`main.cpp:9278` 同款），
`LA_CURATE_QA_FORCE_BROKEN` 反例臂，打印 `LA_CURATE_QA PASS checks=.. failed=.. broken=..`，注册
`gate_matrix.ps1` 一行（纯 ASCII）。

| 组 | 断言（每条必须有能咬红它的变异） |
|---|---|
| **A 模板表** | 12 条且 id 无重；方向 Up×11 + Down×1；每条声明数据源非空 |
| **B 候选池** | 私密照不入池；近 4 周已展出不入池；其余全在 |
| **C 新鲜感单调** | 距上次展出越久分越高；从未展出加权最高；同输入两次生成**逐字节相同**（确定性） |
| **D 重复惩罚** | 同人同题 60 天冷却内不放行 |
| **E 无偏好学习** | `Weights::pref == 0`；**两套不同收藏历史 → 打分逐字节相同**（防偷偷接上） |
| **F 频控** | 每周 ≤1；静音态 → 全不放行 |
| **G 冷启动** | 入库 <200 → 仅 `NewIntake`/`HiddenReunion`；<50 → `Exhibit::valid==false` |
| **H 画质不越界** | quality 全同 vs 全异的库 → **全局选题结果逐字节相同**（quality 只在展内） |
| **I 废片巡检** | 只打包不删除；整批退藏（`setMarks`）后可整批反悔（清 mark 回原态） |
| **J 文案非 LLM** | 展名/小记由模板拼接产生，输出可复现、无外部调用；文案串在 `.ts` 内 |
| **K 缺源诚实** | 缺数据源的模板返回**空 + skipReason**，且不产生任何卡片 |
| **L 反例矩阵** | 每个 FORCE_BROKEN 变体恰使上表对应的组红、其余组绿 |

---

## 4 · 数据与持久化（R5/R6）

**新增 SQLite 表**（沿用 `Scanner.cpp:34-61` 建表 + `Library.cpp:29-40` 幂等迁移）：
```sql
CREATE TABLE IF NOT EXISTS exhibits (
  id INTEGER PRIMARY KEY, template TEXT, title TEXT, blurb TEXT,
  primary_path TEXT, paths TEXT,       -- JSON/逗号串
  generated_at REAL, shown_at REAL,    -- 生成/展示时间（新鲜感与「已展出」依据）
  feedback TEXT                        -- ''|'kept'|'swapped'|'muted'
);
CREATE TABLE IF NOT EXISTS albums (
  id INTEGER PRIMARY KEY, name TEXT, created_at REAL, source TEXT   -- source='curate'
);
CREATE TABLE IF NOT EXISTS album_items (album_id INTEGER, path TEXT, ord INTEGER);
```
**新增 Library API**：`addExhibit/updateExhibitFeedback/loadExhibitHistory/muteTemplate/loadMuteState/
createAlbum/addAlbumItems/listAlbums`。**`Photo` + `seenAt`（qint64）**，`Library::open` SELECT 增列。
**写者纪律**：所有写 API 只由 GUI 线程调用（铁律）；生成是后台纯计算，结果经 `Qt::QueuedConnection` 回写。

---

## 5 · 风险与已勘明事实（writing-plans 输入）

1. **R1 的备选已被否**：若坚持「逐帧换展=波4」，波3 只剩一张静态卡，价值不足以称之为「巨大升级」；故本 spec 收「波3 交付序列」，风险是**可能越界波4 的换展过渡**——缓解：过渡只用既有 `LightboxSlide`，不新造曲线/时长。
2. **`qualityScore` 成本**：每帧一次 `blurScore`（Laplacian 方差，`CullEngine.h:105`）在缩略图上跑，O(展内张数)；**不得**对全库调用（门禁 H 组守此界）。
3. **后台生成与 GUI 刷新竞态**：生成结果回写必须 `QueuedConnection`；`refreshExhibits()` 是唯一刷新汇合点（`MainWindow.cpp:791`）。
4. **`seen_at` 语义**：它是**扫描/入库时间**而非拍摄时间；文案不得把它说成「拍摄于」（沿用波1「措辞对齐数据真相」）。
5. **`person` 无 id**：是逗号名串（`PeoplePage.cpp:100`），改名会波及历史展的「同人同题」判定——v1 以**名字**为键并接受该脆弱性，如实记。
6. **节历表**（FestivalEcho）是**静态常量表**（春节/除夕等日期需按年表或农历近似），v1 只收**固定公历节日**（元旦/劳动节/国庆/圣诞…），农历节日**不做**，如实记。
7. **i18n**：12 展名模板 + 三手势 + 卡片/空态文案 → `.ts` 手编 + `lrelease`，`.ts`/`.qm` 同批提交（沿用记忆 `i18n-qm-manual-lrelease`）。
8. **门禁名册**：80 门（79+1），`-SelfTest` 为唯一真源；新门必须带 `measuredMs`（E8）。

---

## 6 · 波次边界（本波收口即 2.42.0）
- 交付：策展管家 v1（12 模板选题 + 打分(无偏好) + 频控 + 前厅卡 + 凝视序列 + 三手势 + 收藏合集）+ 废片巡检批次 + `LA_CURATE_QA`。
- 留给波4：时间滑轨长廊 + 地理互切 + 生涯时间条/周年展轮换 + 凝视换展在长廊语境的过渡打磨。
- 留给波5：画布布展 + 海报导出 + ⌘K v2 整句解析 + 偏好学习 + 「别再这类」设置页反悔 UI。
