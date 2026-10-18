# 波1+波2「可信+可达」实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 落地 spec `native/docs/superpowers/specs/2026-10-18-wave12-trust-reach-design.md`（本地 2828def）：前厅/四灯/凝视/⌘K v1/人物馆，HomePage+CullPage 退役，门禁 78→79，收口 2.41.0。

**Architecture:** Theme.h 加四灯表（Lamp/Space/LampTokens/lamp()/lampFor()/activeLamp()/setActiveLamp()/locked 语义 + QSettings lamp/active|lamp/mode），11 个核心色 token 从 constexpr 常量改 lamp-aware 内联函数（`Lumen::bg()/panel()/card()/cardHi()/inset()/ink()/muted()/faint()/line()/gold()/emerald()`，全仓 ~250 引用点一次 sed 清扫，QSS 仍在构造期烘焙、切灯=整帧重建时重新读到新值）；DarkPalette `apply(app, lamp)` 泛化（Dark:: 原常量保留作 LA_A11Y_CONTRAST_QA 审计夹具）；MotionKit +3 预设；新建 LobbyPage/PeoplePage/CmdKPanel/GazeLightbox（PhotoLightbox 内嵌类先零行为迁出）；MainWindow 重接线 + rethemeAll。门禁物形承 LA_CULL_EXEMPT_QA（分组断言 + `*_FORCE_BROKEN` 反例 + measuredMs 回填）。

**Tech Stack:** Qt 6.8.3 MinGW C++17（CMake 3.21）；PowerShell 门禁；lrelease = `D:/Qt/6.8.3/mingw_64/bin/lrelease.exe`。

**Spec:** `native/docs/superpowers/specs/2026-10-18-wave12-trust-reach-design.md`（唯一论证基座；下方「计划裁决」章节记录对 spec 的 4 处显式裁决，均为可验证工程裁定而非改需求）。

## Global Constraints

- **门禁必须经 PowerShell**：`powershell -ExecutionPolicy Bypass -File qa_runner.ps1 [-Exe <path>] [-Only <gate>] [-Tier fast]`；判红 = 独立大写 `\bFAIL\b` token；每门必须自退出（requestExit/被 dispatch return）。
- **gate_matrix.ps1 新增/修改行必须纯 ASCII**（E11/E12 在岗；CJK 注释在 PS5.1 GBK 解码下曾吞 12 门）；注释学 LA_CULL_EXEMPT_QA/LA_MAP_SMOKE 风格（断言组 + 试金石 hook + measuredMs 记账）。
- **R-06 红线**：用户真实照片永不进仓；新 QA 一律合成图/内存 Library/临时目录（承 LA_CULL_EXEMPT_QA B 组 QTemporaryDir 形）。
- **铁律 20 同源**：门禁直调产品纯函数（LobbyPage::onThisDay/recent、PeoplePage::buildRooms、CmdKPanel::parse/parseChips、GazeLightbox::dominantColor、Lumen::lamp/lampFor）；反例只经 `LA_*_FORCE_BROKEN` 环境变量在门禁入口注入。
- **铁律 21 注入点过滤**：四处新 UI 全部 `setHidePrivate(!vaultUnlocked_)`；合成 priv 照片不得进候选池/词典/展室。
- **Library GUI 线程单写者**：凝视写库 signal → MainWindow → `Library::setMark(path, mark)`（单事务原子；撤销=`setMark(path,"")`）；视图层不持写能力。
- **构建**：`cd native && cmake --build build -j`（MinGW Makefiles 已配置）；不新增依赖；不改 CMake target 结构。
- **棘轮**：新 UI 颜色一律 `Lumen::lamp()`/工厂（禁裸 hex）；font-size 一律 `Lumen::Type::css()`/`Type::*pt*`（7 档）；radius∈{4,6,10,14,20,999}；spacing∈{0,4,8,12,16,24,32}；禁 translate() 行内 emoji（C5）。
- **i18n**：`QCoreApplication::translate("LightAlbum","中文")`；.ts 手编（lupdate 禁用）；lrelease 后 .qm 与 .ts 同一提交（模板 f92ecb4）。
- **文案**：剔底片(skip)·收藏(like)·锁定(lock)；「最新到馆」（非「最近入藏」，spec §2.2 勘误）；图库标题「全部藏品」。
- **push 纪律**：全程本地提交，绝不推送（推送额度已花完）。
- **SDD ledger**：`<repo-root>/.superpowers/sdd/2026-10-18-wave12-trust-reach/progress.md` 首行 `# SDD ledger — plan: native/docs/superpowers/plans/2026-10-18-wave12-trust-reach.md`。

## 计划裁决（对 spec 的显式工程裁定）

1. **spec §2.2 的 CmdFilter 缺兜底字段**：金句「…在公园的…」中「公园」必须落到 contains 兜底才能「四 chip 全中+公园走兜底」。加 `QString text;`（空=未指定）。parse 签名补词典参数保持纯函数可测：`static CmdFilter parse(const QString& sentence, const QStringList& personDict = {}, const QStringList& placeDict = {});`。
2. **老页面切灯可见性**：spec「切灯=全窗重装帧」要求重建后的老页面换色 ⇒ 老 QSS 的 `Lumen::INK` 等常量必须改为 lamp-aware 函数（否则重装后颜色不变，切灯是假动作）。§2.1「新代码走 LampTokens」由此推广为「全部代码走 lamp-aware 访问器」；一次 sed 清扫，值域变化即 V4 L1 视觉微调（f4f7f4→E8E4D8、d9a94e→D8A657），是 V4 的本意。
3. **extended token 去留**：LampTokens 仅 8 列（spec 钉死）。`PANEL/CARD_HI/FAINT` 是表面角色 → 改为从 8 列派生的函数（PANEL=inset 列；CARD_HI=surface 向 ink 混 8%；FAINT=muted 向 bg 混 50%）；`EMERALD_HI/DK、GOLD_HI/DK、VIOLET、CORAL、CAT1..8、SKY/ROSE/TEAL/OLIVE、*_IMG、GOLD_WASH、DISABLED` 为品牌/数据编码色，**本轮冻结为常量**（不随灯），并在 Theme.h 注释一行说明。
4. **LA_HOME_SMOKE 出口**：HomePage 删除后该门体一并删（矩阵行同步删）；`animOk` 环境变量守卫列表里的 LA_CULL_SMOKE/LA_CULL_DUEL_QA/LA_CULL_VIDEO_QA 三项同步删。

## 四灯 8×4 全值（本计划钉死；LA_LAMP_QA 按此断言）

| 列 | L1 Nocturne | L2 Jade | L3 Gekkan | L4 Dusk |
|----|----|----|----|----|
| bg | #141B16 | #F2EFE6 | #FAFAFA | #241A10 |
| surface | #1D2A22 | #FFFFFF | #FFFFFF | #33241A |
| inset | #0E1410 | #E9E5D9 | #EFEFEC | #2A1E14 |
| ink | #E8E4D8 | #2B2B28 | #111111 | #F2E4CE |
| muted | #8DA396 | #6D675C | #8A8A85 | #A8896B |
| line | #26312A | #E2DED3 | #E4E4E0 | #4A3320 |
| gold | #D8A657 | #B98A2F | #B98A2F | #E2A95C |
| emerald | #4FC08A | #3F6B52 | #B98A2F | #B98A2F |

注：V4 卡片给了 L1 底/面/文/缀、L2 底/面/文/主、L3 底/灰阶/彩点、L4 底/霞/线；未指名列由本表按语义连续性钉死（L2 line/muted 取 V4 html 自身正文用色 #E2DED3/#6D675C；L3/L4 同理）。L3「唯一彩点 #B98A2F」= gold==emerald==#B98A2F（LA_LAMP_QA 断言 L3 两列相等即是「唯一彩点」的机器表达）。

---

### Task 1: Theme.h 四灯 + 7 档字阶 + DarkPalette 泛化 + 全仓 token 函数化

**Files:**
- Modify: `src/ui/Theme.h`（token 块重写 + Type 7 档 + 新增 Lamp 命名空间）
- Modify: `src/ui/DarkPalette.h`（apply 泛化）
- Modify: 全仓 `Lumen::<TOKEN>` 引用点（sed 清扫，仅 cpp/h，勿动 build/）
- Create: `src/main.cpp` 内 `runLampQa()`（门禁注册在 Task 10，本任务先落体）
- Modify: `src/main.cpp:8500` 调用点 `Dark::apply(app)` → `Dark::apply(app, Lumen::activeLamp())`

**Interfaces（终版，签名钉死）:**

```cpp
namespace Lumen {
enum class Lamp : int { Nocturne = 1, Jade = 2, Gekkan = 3, Dusk = 4 };
enum class Space : int { Lobby, Gallery, People, Studio, Vault, Common };

struct LampTokens { QColor bg, surface, inset, ink, muted, line, gold, emerald; };
const LampTokens& lamp(Lamp l);              // 表不可变；非法值回退 Nocturne
Lamp  lampFor(Space s);                      // Lobby=Nocturne, 其余=Jade（Vault 留 r 映射波4）
bool  lampFollowSpace();  void setLampFollowSpace(bool);   // QSettings lamp/mode("follow"/"manual")
Lamp  activeLamp();       void setActiveLamp(Lamp);       // QSettings lamp/active(int 1..4)
QString lampCssVars(Lamp l);                 // "--la-bg:...;--la-surface:..." 段（前厅/⌘K 用）

// 11 个表面角色访问器（读活跃灯表；新代码与全部迁移点统一走这里）
inline QColor bg()        { return lamp(activeLamp()).bg; }
inline QColor panel()     { return lamp(activeLamp()).inset; }           // 裁决 3
inline QColor card()      { return lamp(activeLamp()).surface; }
inline QColor cardHi()    { QColor c = lamp(activeLamp()).surface; /*向 ink 混 8%*/ ...; return c; }
inline QColor inset()     { return lamp(activeLamp()).inset; }
inline QColor ink()       { return lamp(activeLamp()).ink; }
inline QColor muted()     { return lamp(activeLamp()).muted; }
inline QColor faint()     { /*向 bg 混 50%*/ ... }
inline QColor line()      { return lamp(activeLamp()).line; }
inline QColor gold()      { return lamp(activeLamp()).gold; }
inline QColor emerald()   { return lamp(activeLamp()).emerald; }
}
```

- Type 7 档（Theme.h `Type` 现块替换；既有符号名全部保值，调用点零破坏）：

```cpp
enum class Tier { Display, Title, Label, Head, Body, Small, MonoMeta };   // 7 档
inline QFont label(bool serifAllowed = true)   // 展签体 17px/500；衬线仅 L1/L4 允许
{
    const bool serif = serifAllowed && (activeLamp()==Lamp::Nocturne || activeLamp()==Lamp::Dusk);
    return serif ? serifFont(12.75, QFont::DemiBold, 1.0)
                 : yahei(12.75, QFont::DemiBold);
}
inline QFont monoMeta() { return mono(8.25); }   // 11px 馆务 Mono-Meta
// css() 内 pts[] = {20, 15, 12.75, 12, 9.5, 9, 8.25}（px(12.75)==17、px(8.25)==11）
```

- DarkPalette.h 泛化：

```cpp
inline void apply(QApplication& app, Lumen::Lamp l = Lumen::activeLamp())
{
    const Lumen::LampTokens& t = Lumen::lamp(l);
    QPalette pal;
    pal.setColor(QPalette::Window, t.bg);   ...   // 角色映射沿用现 apply，值换 t.* 列
    ...
}
// Dark::Bg..Emerald 8 常量原样保留（LA_A11Y_CONTRAST_QA 15 对审计夹具，头部注释写明）
```

- sed 清扫（Git Bash，词边界；先跑计数记 ledger，后执行； committed 顺序：先改 Theme.h/DarkPalette.h 完整落定新文件，再清扫其余文件，再全量编译）：

```bash
# 计数（ledger 记录）
grep -rEo 'Lumen::(BG|PANEL|CARD|CARD_HI|INSET|LINE|INK|MUTED|FAINT|GOLD|EMERALD)\b' src | sort | uniq -c
# 清扫（11 条，仅 src/ 下 .cpp/.h；-i 原地；\b 防误伤 *_IMG/_HI/_DK/CARD_HI 已被显式列出）
grep -rlE 'Lumen::(BG|PANEL|CARD|CARD_HI|INSET|LINE|INK|MUTED|FAINT|GOLD|EMERALD)\b' src \
 | xargs sed -i -E \
   -e 's/Lumen::BG\b/Lumen::bg()/g' \
   -e 's/Lumen::PANEL\b/Lumen::panel()/g' \
   -e 's/Lumen::CARD_HI\b/Lumen::cardHi()/g' \
   -e 's/Lumen::CARD\b/Lumen::card()/g' \
   -e 's/Lumen::INSET\b/Lumen::inset()/g' \
   -e 's/Lumen::LINE\b/Lumen::line()/g' \
   -e 's/Lumen::INK\b/Lumen::ink()/g' \
   -e 's/Lumen::MUTED\b/Lumen::muted()/g' \
   -e 's/Lumen::FAINT\b/Lumen::faint()/g' \
   -e 's/Lumen::GOLD\b/Lumen::gold()/g' \
   -e 's/Lumen::EMERALD\b/Lumen::emerald()/g'
```

  sed 后 `grep -rE 'Lumen::(BG|PANEL|CARD|CARD_HI|INSET|LINE|INK|MUTED|FAINT|GOLD|EMERALD)\b' src` 必须 0 命中；Theme.h/DarkPalette.h 定义块例外由「先重写定义」自然覆盖（定义块里旧的 `inline constexpr QColor BG =...` 行整体删除）。
- runLampQa 主体（main.cpp，靠近 runCullExemptQa 附近；铁律 20 直调产品 API）：

```cpp
// 2.41.0 四灯系统门禁（LA_LAMP_QA）。断言：
//   A 4×8 token 完备：每组 8 列有效色；四组两两任列相异（pairwise distinct）
//   B lampFor 映射：Lobby=Nocturne，Gallery/People/Studio/Vault=Jade（Space 枚举全覆盖）
//   C 锁语义：follow 档位下 lampFor(space) 优先级 > activeLamp（用 setLampFollowSpace 注入验证）
//   D QSettings 往返：setActiveLamp(l) → activeLamp()==l；QCoreApplication 作用域内读写
// 反例 LA_LAMP_QA_FORCE_BROKEN=1：lamp() 返回 L1/L2 两张表互换后的映射 → A 组 pairwise 必红。
```

  实现模板与 runCullExemptQa 同形（chk lambda + checks/fails 计数 + 尾部 `LA_LAMP_QA %s checks=%d failed=%d broken=%d` 单行 + fflush + `return pass?0:1`）；broken 臂在 A 组前做表互换（swap(lamp(Nocturne), lamp(Jade)) 的故障形态请用**访问器旁路**实现：broken 时 A 组读到的表通过强制 activeLamp 反转映射注入，不污染产品表存储）。measuredMs 首跑后回填 matrix。
- LA_MOTION_QA 不在此任务；LA_PALETTE_QA baseline 重生成在 Task 10。

**Steps:**
- [ ] 重写 Theme.h token 块（11 函数 + Lamp 命名空间全量 + 注释狡决 2/3）、Type 7 档块
- [ ] 重写 DarkPalette.h apply(app, lamp)
- [ ] main.cpp:8500 换参 + runLampQa body + LA_LAMP_QA 反例臂
- [ ] sed 计数先落 ledger → sed 清扫 → build 零错（`cmake --build build -j`）
- [ ] 用 PowerShell 跑 LA_LAMP_QA 正例 + `LA_LAMP_QA_FORCE_BROKEN=1` 反例（红）→ 修正反例臂使反例恰红、正例绿
- [ ] 全 fast 层回归（qa_runner -Tier fast）确认 sed 无行为回归
- [ ] Commit: `feat(theme): 四灯系统 L1-L4 + 7 档字阶 + 表面 token lamp-aware 化（spec §2.1）`

---

### Task 2: MotionKit +3 预设（数据与门禁断言）

**Files:**
- Modify: `src/core/MotionKit.h:46` 增 3 个 kXxx 内联名；`src/core/MotionKit.cpp` makeTable() 增 3 组
- Modify: `src/main.cpp` runMotionQa C 组（:4608 附近）

**Interfaces:**

```cpp
inline const QByteArray kLightFirst { "LightFirst" };  // slow 640 进前厅/凝视入场（光先行）
inline const QByteArray kPushDoor   { "PushDoor" };    // standard 360 进人物馆
inline const QByteArray kLightsDown { "LightsDown" };  // standard 360 预留（凝视退场键，本轮不接 UI）
```

```cpp
// LightFirst slow 640：光先行（glow 先到峰再退），画后进。
{ Preset p; p.name = kLightFirst; p.durationMs = 640;
  p.keys = {
    { kPropGlow,    0.0, 1.0, OutCubic, 0.0, 0.35, false, true, 0.0 },  // 光先行包络
    { kPropOpacity, 0.0, 1.0, OutCubic, 0.15, 0.85, true, false, 0.0 },
    { kPropOffsetY, 24.0, 0.0, Spring,  0.15, 1.0, false, false, 0.0 },
  }; t.append(p); }
// PushDoor standard 360：scale 1.02→1.00（推门）+ 快淡入。
{ Preset p; p.name = kPushDoor; p.durationMs = 360;
  p.keys = {
    { kPropScale,   1.02, 1.0, OutCubic, 0.0, 1.0, false, false, 0.0 },
    { kPropOpacity, 0.0, 1.0, OutQuad,  0.0, 0.4, true, false, 0.0 },
  }; t.append(p); }
// LightsDown standard 360：淡出+微上移（预留键）。
{ Preset p; p.name = kLightsDown; p.durationMs = 360;
  p.keys = {
    { kPropOpacity, 1.0, 0.0, InQuad, 0.0, 0.5, true, false, 0.0 },
    { kPropOffsetY, 0.0, -10.0, OutCubic, 0.2, 1.0, false, false, 0.0 },
  }; t.append(p); }
```

- runMotionQa C 组改两组数（:4608 附近）：`names.size() >= 5` → `>= 8`；追加三条注册断言：

```cpp
note(Motion::presetNames().contains(Motion::kLightFirst), QStringLiteral("C LightFirst registered"));
note(Motion::presetNames().contains(Motion::kPushDoor),   QStringLiteral("C PushDoor registered"));
note(Motion::presetNames().contains(Motion::kLightsDown), QStringLiteral("C LightsDown registered"));
note(Motion::preset(Motion::kLightFirst).durationMs == 640, QStringLiteral("C LightFirst slow 640"));
note(Motion::preset(Motion::kPushDoor).durationMs == 360,   QStringLiteral("C PushDoor standard 360"));
```

- evaluate(p,0/1) 维数断言既有循环自动覆盖新键（keys 非空 + dim==keys）。减动效语义不需新代码（Driver animate=false 全曲线 instant 既有逻辑）。

**Steps:**
- [ ] 改 MotionKit.h/.cpp（按上块逐行）
- [ ] 改 runMotionQa C 组断言
- [ ] build → PowerShell `-Only LA_MOTION_QA` 绿
- [ ] Commit: `feat(motion): LightFirst/PushDoor/LightsDown 三预设 + LA_MOTION_QA 注册断言（spec §3）`

---

### Task 3: LobbyPage（前厅）+ LA_LOBBY_QA

**Files:**
- Create: `src/ui/LobbyPage.h` / `src/ui/LobbyPage.cpp`
- Modify: `src/main.cpp` 新 `runLobbyQa()`（放 runLampQa 邻位）
- Modify: `native/CMakeLists.txt` LA_SOURCES 增 `src/ui/LobbyPage.h`/`.cpp`（HomePage 条目退役在 Task 9，不新建 task）

**Interfaces（签名 spec §2.2 钉死）:**

```cpp
class LobbyPage : public QWidget {
    Q_OBJECT
public:
    LobbyPage(const QVector<Photo>* photos, const QString& thumbsDir, QWidget* parent = nullptr);
    void setHidePrivate(bool hide);   void refresh();
signals:
    void openGaze(const QString& startPath);
    void navigateTo(int row);                       // 复用 HomePage::navigateTo(int) 同义
    void importRequested();   void icloudImportRequested();
public:
    static QVector<const Photo*> onThisDay(const QVector<Photo>& in, const QDate& today,
                                           bool hidePrivate, int maxN = 9);
    static QVector<const Photo*> recent(const QVector<Photo>& in,
                                        bool hidePrivate, int maxN = 12);
private:
    const QVector<Photo>* photos_ = nullptr;
    QString thumbsDir_;  bool hidePrivate_ = true;
    QWidget* heroCard_ = nullptr;  QWidget* recentStrip_ = nullptr;
    ...（构型：厅心 onThisDay 大卡 + 玻璃指令栏（点开 emit importRequested 的占位胶囊，⌘K v1 不在厅内放输入）+ 最新到馆横条 12 张 + 空库引导导入卡）
};
```

**纯函数实现规格（TDD 先红）：**

- `usable(p)` 谓词 = `!p.priv && !p.missing && !p.thumbFile.isEmpty()`。
- `onThisDay`：①主选 = usable 且 `date.left(10)`（`QDate::fromString` "yyyy-MM-dd"，失败再试 "yyyy:MM:dd"——StoryGen.cpp:27 已有此形）月/日恰等 today 月/日；②不足 3 张 → 同月全库（usable，month 相等）；③仍不足 → 同季（3-5 春/6-8 夏/9-11 秋/12-2 冬）跨年；④仍不足 → 返回稀薄结果（调用 UI 显示「今日无展 · 明日再会」空态，**不**回填假数据）；排序全部 = `|year − today.year|` 升序、同级 rating 降序、同级 date 串倒序；闰日：today 月/日无次级命中且 `today.month()==2 && today.day()==28` 时额外并入 2-29 照片（历法友好规则，QA 断言之）。
- `recent`：usable 之上按**当前** photos 向量序**头取**（Library 载入后已 date ISO 倒序排序，Library.cpp:130-132——文案「最新到馆」）；空→空表。
- 反例臂 `LA_LOBBY_QA_FORCE_BROKEN=1`：onThisDay 跳过 priv 过滤（模拟铁律 21 失效）→ priv 反例组必红。
- 构型约束：QWidget 无 title bar；颜色一律 `Lumen::bg()/card()/ink()/muted()/gold()`；字体 `Lumen::Type::css(Tier::Label/Tier::Title)`；spacing 白名单；入场由 Task 8 接 kLightFirst（此任务不动转场）。
- runLobbyQa（模板承 LA_CULL_EXEMPT_QA）：A onThisDay 正例（合成库：today=2026-10-18 注入 3+ 张 2023-10-18 / 2025-10-18 → 预期排序 |Δy| 升序；rating 打破同级）；B 闰日组（today=2025-02-28 与 2020-02-29）；C 回退链（0 张当日 → 同月；同月 0 → 季；全空 → 空 vector）；D recent 组（date 倒序头取 + priv/missing/thumb 过滤 + maxN）；E priv 反例（FORCE_BROKEN 臂）。
- **编排**：先写 runLobbyQa 引用 `LobbyPage::onThisDay/recent` → stub 实现永远返回空表 → build → ` powershell -Only LA_LOBBY_QA` 必红 → 写真实实现 → 绿 → 反例臂验证红。stub 不提交，一步到位 commit（提交里含门禁）。

**Steps:**
- [ ] 写 LobbyPage.h/.cpp（先 stub 版 onThisDay/recent = 空 vector + 空 UI）
- [ ] 写 runLobbyQa + CMake 加行
- [ ] PowerShell `-Only LA_LOBBY_QA` 红证（stub 下 A-D 组红）→ 记 ledger
- [ ] 实现 onThisDay/recent 真语义 + 主 UI 构型（含空态）→ `-Only LA_LOBBY_QA` 绿
- [ ] `LA_LOBBY_QA_FORCE_BROKEN=1` 红证 E 组
- [ ] measuredMs 回填占位（本任务记运行值，注册改于 Task 10）
- [ ] Commit: `feat(lobby): 前厅页 onThisDay/recent 纯函数 + LA_LOBBY_QA（spec §2.2）`

---

### Task 4: PeoplePage（人物馆）+ LA_PEOPLE_QA

**Files:**
- Create: `src/ui/PeoplePage.h` / `src/ui/PeoplePage.cpp`
- Modify: `src/main.cpp` `runPeopleQa()`；CMakeLists +2 行

**Interfaces（spec §2.3 钉死）:**

```cpp
struct Room {
    QString name; int count = 0; int yearMin = 0, yearMax = 0;
    QColor ring;                          // 首字母色环哈希——实现沿用 GalleryPage 现成哈希（执行时 grep buildRooms/ring 做法，无现成则用 qHash(name)%8 → CAT1..8）
    QVector<const Photo*> photos;         // 日期升序（date 串升序 stable_sort）
};
class PeoplePage : public QWidget {
    Q_OBJECT
public:
    PeoplePage(const QVector<Photo>* photos, const QString& thumbsDir, QWidget* parent = nullptr);
    void setHidePrivate(bool hide);   void refresh();
signals:
    void openGaze(const QString& person, const QString& startPath);
    void filterGallery(const QString& person);
    void clusteringRequested();           //→ MainWindow 复用 personDetect/faceCluster 链
public:
    static QVector<Room> buildRooms(const QVector<Photo>& in, bool hidePrivate);
};
```

- buildRooms：单次扫库；每照 `person.split(',')` 入对应房（同人多照片/一照多人）；usable 谓词同 Task 3 + `!person.isEmpty()`；hidePrivate=true 时 priv 全剔除；房内 photos 日期**升序**（输入向量是倒序 → `std::stable_sort(room.photos, asc)` 或逆序追加后反转）；count==photos.size()；yearMin/Max 由房内 photo.year 聚合；按 count 降序、同级 name 升序输出；空 name/photo 完全防御（trim 后入）。
- 反例 `LA_PEOPLE_QA_FORCE_BROKEN=1`：buildRooms 忽略 hidePrivate（priv 涌入）→ C 组必红。
- runPeopleQa 组：A 聚合正例（合成库三人/一照双名 CSV）；B 升序+跨度；C priv 剔除（+FORCE_BROKEN 红证）；D 空库/空 person 防御；E count==photos.size()。
- UI 构型：房卡横向网格（--mat 12px 装裱卡：`card()` 白面 + 2px 投影用 `Lumen::Elev` E2 既有接线），展签 `Type::label(true)`，年跨度 `Type::monoMeta()`；展室展开态 = 该房 photos 横串缩略图行（点击 → openGaze(person, path)）；空态卡「馆内布展中」+「识别人物」按钮 → clusteringRequested。

**Steps:**
- [ ] PeoplePage.h/.cpp（stub buildRooms 空）+ runPeopleQa + CMake
- [ ] PowerShell `-Only LA_PEOPLE_QA` 红证 stub
- [ ] 实现 buildRooms/房间卡 UI → 绿
- [ ] `LA_PEOPLE_QA_FORCE_BROKEN=1` 红证 C 组
- [ ] Commit: `feat(people): 人物馆 buildRooms 聚合 + LA_PEOPLE_QA（spec §2.3）`

---

### task 5: CmdKPanel（⌘K v1 词典层）+ LA_CMDK_QA

**Files:**
- Create: `src/ui/CmdKPanel.h` / `src/ui/CmdKPanel.cpp`
- Modify: `src/main.cpp` `runCmdkQa()`；CMakeLists +2 行

**Interfaces（spec §2.4 + 计划裁决 1）:**

```cpp
struct CmdFilter {
    int yearMin = 0, yearMax = 0;         // 0=未指定（含相等：2019 年查询 ⇒ min=max=2019）
    int month = 0, day = 0;               // 1..12 / 1..31；0=未指定
    QString season, person, tokenPlace, category;     // 空=未指定
    QString text;                          // 计划裁决 1：兜底 contains 词
    bool empty() const;
};
class CmdKPanel : public QWidget {
    Q_OBJECT
public:
    explicit CmdKPanel(const QVector<Photo>* photos, QWidget* parent = nullptr);
    void openFor(Lumen::Space s);          // 开馆层（E4 金描边）；记录空间语义键
signals:
    void applyFilter(const CmdFilter& f);
    void openRoom(const QString& person);
    void openGazeFiltered(const CmdFilter& f);
    void lookBackToday(const QDate& d);
public:
    static CmdFilter parse(const QString& sentence, const QStringList& personDict = {},
                           const QStringList& placeDict = {});
    static QStringList parseChips(const QString& sentence, const QStringList& personDict = {},
                                  const QStringList& placeDict = {});
};
```

**parse 词典规则（v1 全部确定性、零网络；QA 金句按此断言）：**
- 时间词法：`前年`→today.y−2；`去年`→today.y−1；`今年`→today.y；`/(\d{4})年/`→固定年份（1970..2100）；`春/夏/秋/冬`→season（且「去年秋天」这类**年份词+季节共现**要同时成立）；`N月`/`三月`式月份表（一月..十二月）→month；`N日/N号`→day。today 由 `QDate::currentDate()` 取（QA 用句子相对年份断言而非绝对年份）。
- 人物：personDict 前缀模糊命中（句子含人名即捕）； loc：placeDict 命中单词 → tokenPlace。
- 风格：固定 8 类融合 `Classifier.cpp:46 fixed` 表字面（「人像/风光/夜景·星空/城市·建筑/微距·花卉/动物/美食/黑白」）——QA 金句「黑白」命中 category。
- 兜底：其余去掉已匹配词后的**剩余内容 trim** → text（含「公园」「照片」之外的裸词；金句剩余「在公园的照片」→ tokenPlace=公园(若词典含)否者 text=「公园」——**规则钉死：placeDict 未命中时，「在X」结构的 X 一并进 text 兜底，不进 tokenPlace**）。
- parseChips：对句子产出 chip 字串列表（chip 文案=捕获原文，如「去年秋天」「小雨」「黑白」[「公园」皮肤为兜底 chip 仅当 text 非空且 tokenPlace 为空]）。
- 反例 `LA_CMDK_QA_FORCE_BROKEN=1`：词典错位注入（month↔season 表互换）→ B 组必红。
- runCmdkQa 组：A 金句集（「去年秋天和小雨在公园的黑白照片」+ 补 2 句：`2021年冬的美味`→ text=美食? NO——「美食」不是「美味」……金句集钉死 3 句：
  1. `去年秋天和小雨在公园的黑白照片` → year=today.y−1, season=秋, person=小雨, category=黑白, text=「在公园的照片」（placeDict 不含公园时），tokenPlace=""
  2. `今年六月的夜景` → year=today.y, month=6, category=夜景·星空
  3. `2019年 阿岚` → yearMin=2019=yearMax, person=阿岚
  街头+ placeDict 冒烟（placeDict 含「呼伦贝尔」的句子 → tokenPlace）。
  B 兜底 contains（text 提取正确性）；C 月份/季节正则杂测（更杂句不崩、无关句 empty()==true）；D FORCE_BROKEN 红证；E empty()/chip 对仗（chip 数==捕获词数）。
- UI 构型（V2 屏D）：E4 层（金描边+内发光 `Lumen::glowGold()` 既有工厂）/ --mat 容器（`card()` 面 + E2 投影）；输入 QLineEdit + 提示词条（时间/人物/地点/风格四组胶囊提示，凭 photos 指针派生 personDict——锁定态=hidePrivate 词典，迷入 openFor 时机由 Task 8 传入）；chips 行（点击循环候选 × 移除，纯 UI 态）与动作行（Enter → applyFilter / 开 room；「暂存为合集」按钮 disabled+tooltip「下轮上线」）。

**Steps:**
- [ ] CmdKPanel.h/.cpp（stub parse 全空）+ runCmdkQa + CMake
- [ ] PowerShell `-Only LA_CMDK_QA` red 证 stub
- [ ] 实现 parse/parseChips + 面板 UI → 绿
- [ ] `LA_CMDK_QA_FORCE_BROKEN=1` 红证
- [ ] Commit: `feat(cmdk): ⌘K v1 词典解析 + LA_CMDK_QA（spec §2.4）`

---

### Task 6: PhotoLightbox 零行为迁出（GazeLightbox 立骨）

**Files:**
- Create: `src/ui/GazeLightbox.h` / `src/ui/GazeLightbox.cpp`（现 PhotoLightbox 内嵌类整体搬运 + 类名想成 GazeLightbox 旧兼容名不受影响）
- Modify: `src/ui/MainWindow.cpp`（include + 前置类型 + 构造点 `new GazeLightbox(...)`）；`native/CMakeLists.txt` +2 行
- **零行为迁移**：Mode 只含 Browse；全部键盘/滚动/编辑跳转/视频/入场转场原样保留（:723-743 构造与 openLightbox :1106 的旧接口语义不动——签名 `PhotoLightbox( Library*, parent)` 内有 2.22.0 注释与 FullCache 外注入纪律**注释原文迁移**）。

**Steps:**
- [ ] 剪切 MainWindow.cpp:153 起 PhotoLightbox 全类 → GazeLightbox.{h,cpp}（类名、Q_OBJECT、成员全保留）
- [ ] MainWindow 各点改 include/前向声明 + 构造类型名
- [ ] build + fast 层（LA_LIGHTBOX 相关门若在名册，跑之；LA_GALLERY_FILL_QA 等全绿）
- [ ] Commit: `refactor(ui): PhotoLightbox 独立为 GazeLightbox（零行为，spec §1.2 骨架）`

---

### Task 7: 凝视模式 + 主色纯函数 + LA_GAZE_QA

**Files:**
- Modify: `src/ui/GazeLightbox.h/.cpp`（Mode::Gaze 叠加）
- Modify: `src/ui/MainWindow.cpp`（markRequested/dismissed 接线 → read 型刷新，Task 8 汇入）
- Modify: `src/main.cpp` `runGazeQa()`

**Interfaces（spec §2.5）:**

```cpp
enum class Mode { Browse, Gaze };
void enter(Mode m, const QString& startPath);        // Gaze 模式入馆
void setHidePrivate(bool hide);                       // BrowseScope 步进纪律继续生效
signals:
    void markRequested(const QString& path, const QString& mark);   // 单张（len=1 语义）
    void ratingChanged(const QString& path, int rating);
    void dismissed(bool dirty);
public:
    static QColor dominantColor(const QImage& thumb);  // 纯函数（门禁直调）
```

- dominantColor 规格（QA 合成图精断）：缩略图缩到 64px（KeepAspectRatio）后中心加权采样：以中心 1/2 半径内像素全采 + 外圈每 2px 采样；RGB 转 HSL 后取**饱和度字段 >0.25** 的样本中位数（qFuzzyCompare 排序）；无达标样本 → 回退 `Lumen::emerald()` 亮度 -10%（低饱和回退，QA 以灰图断言）；全程不落库、不写设置。
- Gaze 键位（V3 metaphors）：`S`→markRequested(path,"like")·`A`/`Delete`→markRequested(path,"skip")·`L`→markRequested(path,"lock")；**每次按键即发 signals**（MainWindow 端 `lib_->setMark(path, mark)`，写库即回——不需要批量缓冲）；toast QLabel + QTimer 3s 撤销（撤销==同键再按 → 主窗再次 setMark(path,"")，UI toast 提示「已撤销」）；浏览中仍保留星级 1-5、←/→、Esc 退出（BrowseScope nextVisible 主干不变）。
- environment：Gaze 叠加 = 无边框形态（当前灯箱已无边框）；hover 雾条（底 部 Dense）；环境光晕 = 窗口四缘 `dominantColor` alpha ≤10% 径向渐变（`Lumen::glowCss(color, strength)` 若无现成工厂则新建 Theme.h `glowCss`；三处共用由 Task 8 串联）；换展动画 = 旧画淡出 120ms（const 时长）+ 新画 Motion::Driver(kLightbox) enter（≤2 并发： GazeLightbox 计数 active Drivers ≥2 时立即起新动效并停最旧）。
- runGazeQa 组：A dominantColor 暖/冷/灰三合成图（纯橙/纯蓝图→目标色相中位数±容忍）；B 低饱和灰图→回退色非黑非白（=emerald 列暗化）；C FORCE_BROKEN 旁路主色钳制（无回退分支）→ B 组红证（回退区破）；D 私密步进纪律：**断言直接落到 BrowseScope::nextVisible 上**（复用现 LA_PRIVACY_QA 的 seq 构造形），Gaze 也走 nextVisible（代码 grep 断言 enter/Gaze 翻页共用 navigate() 调用点，grep 形承 :723 注释例）。

**Steps:**
- [ ] dominantColor stub（恒返回黑）+ runGazeQa → 红证
- [ ] 实dominantColor + Gaze Mode（键位/toast/换展/glow）→ 绿
- [ ] `LA_GAZE_QA_FORCE_BROKEN=1` 红证 + D 组 grep 断言
- [ ] Commit: `feat(gaze): 凝视模式 S/A/L 键位即写库 + dominantColor + LA_GAZE_QA（spec §2.5）`

---

### task 8: MainWindow 重接线（导航/栈/灯钮/⌘K/凝视入口/图库通道/rethemeAll）

**Files:**
- Modify: `src/ui/MainWindow.h`（成员 home_→lobby_, cull_→people_, 增 cmdk_/gaze_/rethemeAll/buildPages 声明；search_ 成员删）
- Modify: `src/ui/MainWindow.cpp`（kNav/kNavIcons、栈构建抽 buildPages、顶栏、快捷键、凝视入口、refresh 汇合、GalleryPage cmd 通道、GalleryModel 扩容引用）
- Modify: `src/ui/GalleryPage.h/.cpp`（标题文案「全部藏品」+ applyCmdFilter 通道）
- Modify: `src/ui/GalleryModel.h/.cpp`（过滤键扩容）
- Modify: `src/ui/Icons.h`（+3 kind：lobby/people/lamp）

**契约细节：**

1. **Icons +3 kind**（Icons.h else-if 链，1.8px pen 风格保持）：
```cpp
} else if (kind == QLatin1String("lobby")) {   // 拱门（前厅）
    p.drawArc(QRectF(r.left(), r.top() + r.height() * 0.3,
                     r.width(), r.height() * 0.9), 0, 180 * 16);
    p.drawLine(QPointF(r.center().x(), r.center().y()), QPointF(r.center().x(), r.bottom()));
} else if (kind == QLatin1String("people")) {  // 双人像（人物馆）
    p.drawEllipse(QPointF(r.left() + r.width() * 0.32, r.top() + r.height() * 0.3),
                  r.width() * 0.16, r.width() * 0.16);
    p.drawArc(QRectF(r.left(), r.top() + r.height() * 0.45, r.width() * 0.62, r.height() * 0.7),
              0, 180 * 16);
    p.drawEllipse(QPointF(r.right() - r.width() * 0.28, r.top() + r.height() * 0.34),
                  r.width() * 0.13, r.width() * 0.13);
    p.drawArc(QRectF(r.right() - r.width() * 0.56, r.top() + r.height() * 0.52,
                     r.width() * 0.56, r.height() * 0.6), 0, 180 * 16);
} else if (kind == QLatin1String("lamp")) {   // 台灯（切灯钮）
    p.drawArc(QRectF(r.center().x() - r.width() * 0.32, r.top(),
                     r.width() * 0.64, r.height() * 0.6), 0, 180 * 16);
    p.drawLine(QPointF(r.left() + r.width() * 0.12, r.top() + r.height() * 0.3),
               QPointF(r.right() - r.width() * 0.12, r.top() + r.height() * 0.3));
    p.drawLine(QPointF(r.center().x(), r.top() + r.height() * 0.3),
               QPointF(r.center().x(), r.bottom() - r.height() * 0.18));
    p.drawLine(QPointF(r.center().x() - r.width() * 0.18, r.bottom() - r.height() * 0.18),
               QPointF(r.center().x() + r.width() * 0.18, r.bottom() - r.height() * 0.18));
}
```
2. **kNav/kNavIcons 换名**（MainWindow.cpp:103-112）：
```cpp
const NavItem kNav[] = {
    {"前厅", "P1"}, {"足迹", "P2"}, {"图库", "P1"},
    {"人物馆", "P3"}, {"修图", "P3"},
    {"日志", "P2"}, {"关于", "P3"},
};
const char* kNavIcons[] = { "lobby", "map", "gallery", "people", "retouch", "enhance", "log" };
```
  注释同步：row 3 分割线语义=人物馆（delegate 画线逻辑不动）；「与 web 版导航一致」注释加一行 2.41.0 档案注记（旧名沿革）。
3. **栈构建抽 buildPages()**：把 :723-743 的 addWidget 块与 connect 全部移入 `void buildPages()`；栈容器/页成员在 ctor 里判空重建；return;? no——**rethemeAll 重建时先 `stack_->blockSignals(true)` → 逐 widget deleteLater + stack_->removeWidget → 重建 → setCurrentIndex(记行)**。connect 的 home_/gallery_/cull_/lightbox 相关槽在 buildPages 内重连；模块级连接（顶栏搜索塌缩到 cmdk 通道）只连一次。
4. **P19 顶栏让位/渐显、浮岛重绘**：retheme 后 navItems_ 重画（restyleNavIcons 现有闭包重跑）。
5. **顶栏**：search_ 删（:634-656 + connect :779）；`searchToggle_` 点击 → `cmdk_->openFor(currentSpace())`；新增 `QShortcut(QKeySequence(QStringLiteral("Ctrl+K")), this)` 同一 openFor；`purpose btn 灯钮`：`QToolButton lampBtn_` + `Icons::make("lamp")` + InstantPopup menu：L1 夜展 / L2 玉照 / L3 素印 / L4 暮金（各自 QPalette 预览毛 序 via lampCssVars 前垫色块：menu 用 QWidgetAction? **No——用 QMenu + QAction，icon=Icons::make("lamp", lamp(l).gold)**）+ separator + 「跟随空间」checkable QAction（初始 lampFollowSpace()==follow）；点击灯项 → `setLampFollowSpace(false); setActiveLamp(l); rethemeAll();`；follow → 只设 follow。灯钮贻顶栏在 vaultBtn_ 与 searchToggle_ 之间。
6. **凝视入口**：`gallery photoActivated` (:831) / 人物馆 openGaze / 前厅 openGaze → `gaze_->enter(Mode::Browse or Mode::Gaze, path)`（前厅厅心卡、人物馆展室=Gaze；图库双击=Browse 保旧 — spec「图库双击 openLightbox 改 gaze_->enter(...)」）；scope 传递=现 openLightbox(:1106) 的 scope 参数逻辑搬用（gallery_.visiblePaths() 等）。
7. **markRequested 接线**（GazeLightbox）→：
```cpp
connect(gaze_, &GazeLightbox::markRequested, this, [this](const QString& path, const QString& mark) {
    if (!lib_ || path.isEmpty()) return;
    lib_->setMark(path, mark);                      // 单事务原子（len=1 语义）
    lib_->refresh? -> read 型 refresh 契约= Library::reload/marks 已同步内存（查看 setMark 实现实际是否同步 photos_；若否，刷新 photos_ 由 lib 内部处理——铁律：视图拿同向量的 const 引用，写后必须保证 photos_ 内 mark 更新）
    gaze_ 保持打开（不闪退）；toast 由 GazeLightbox 自绘
});
connect(gaze_, &GazeLightbox::dismissed, this, [this](bool dirty) {
    if (dirty) { lobby_->refresh(); people_->refresh(); gallery_->refresh(); }
});
```
  setMark 的内存同步性执行时 grep Library.cpp::setMark 确认（若仅写 SQL 不同步 photos_，需在 MainWindow 处调用 lib_->reloadMarks() 之类现成只读刷新——以实际代码为准，plan 不发明 API）。
8. **refresh 汇合**：导入完成/凝视 dismissed/vault 解锁回 调 →+`lobby_->refresh(); people_->refresh();`（锁定切换时 setHidePrivate 链同步四处）；导入 toast 文案「N 幅入藏」沿用 startImport 现有 toast 改词（grep「导入完成」桥至 toast 文案）。
9. **GalleryModel/Procedure 通道**（spec §4 ⌘K 流）：
```cpp
// GalleryModel.h/.cpp：过滤键扩容（机制承 hasCat/categoryFilter_）
void setCmdFilter(const CmdFilter& f);   void clearCmdFilter();
// 谓词追加（在现 category/person/text 过滤同处）:
//   (f.yearMin==0 || (p.year>=f.yearMin && p.year<=f.yearMax))
//   (f.month==0 || month(date)==f.month)  （date 字符串切月）
//   (f.season.isEmpty() || seasonOf(month)==f.season)
//   (f.person.isEmpty() || p.person.contains(f.person))
//   (f.category.isEmpty() || p.category==f.category)
//   (f.text.isEmpty() || title.contains(text)||rel.contains(text)||cam.contains(text))
//   (f.tokenPlace.isEmpty() || p.city.contains(tokenPlace)||p.prov.contains(tokenPlace)||p.region.contains(tokenPlace))
// GalleryPage：
void applyCmdFilter(const CmdFilter& f);      // model_.setCmdFilter(f) + searchText_ 同步 + updateCount 注记「检索：chips」
signals? 不新增（去重 CmdK applyFilter 由 MainWindow 直接调 gallery_->applyCmdFilter(f)）
```
   注：runGallerySearchQa 等既有门继续绿；CmdFilter 值是值类型（include ui/CmdKPanel.h 的 CmdFilter 或前移到 core/CmdFilter.h? 简单落定：CmdFilter struct 放 `src/ui/CmdKPanel.h` 内同文件——GalleryModel includes CmdKPanel.h 会拖 Q_OBJECT 头……**改定为独立小头 `src/core/CmdFilter.h`（纯 struct，无 Qt UI 依赖）**，CmdKPanel/GalleryModel/门禁同含 include（计划裁决收官）。
10. **rethemeAll**（spec §2.1/§4）：
```cpp
void MainWindow::rethemeAll()
{
    const int row = nav_->currentRow();
    Dark::apply(*qApp, Lumen::activeLamp());
    qApp->setPalette(qApp->palette());  // no-op 触发 propagate? 不需要
    qApp->setStyleSheet(QString());      // 全局卸（app 级无从），unpolish/polish 既有控件树：
    for (QWidget* w : QApplication::allWidgets()) { w->style()->unpolish(w); w->style()->polish(w); }
    buildPages();                        // 栈页/顶栏重建（新 QSS 烘 new lamp 值）
    nav_->setCurrentRow(row);            // 回放当前行（滚动/筛选重置=spec 接受代价）
    kpis_->hide();
    updateTopSolid(); syncTopbarFloat(); restyleNavIcons(row);
    if (animEnabled_) { /* slow 640 光先行包络：Motion::Driver(kLightFirst) 驱 全窗 QGraphicsOpacityEffect 0.0→1.0 + 边缘发光（沿用 onNav enter 段同样制法） */ }
}
```
   （「QSettings 持久」在 setActiveLamp 内完成，rethemeAll 不管存储。）灯钮 menu open 动作不疑义。
   **只重堆页/顶栏，左栏 nav_ 菜单与窗口本体不重建**（geometry 保持）——重绘即可。
11. **空库走位**：图库空态时人物馆 openGaze 不可达（无房），前厅 recent 空态引导导入卡——均结构性自然产物。
12. **红绿**：本任务是接线/构建性任务，测试=全 fast 回归 + 三新页手动 shot（`LA_UI_SHOT` 探针）offsavourite 快截验证非黑屏；LA_LOBBY/PEOPLE/CMDK/GAZE_QA 门（已在 main.cpp dispatch 后头? Task 10）先在快层显示绿。Task 8 完成时 buildPages 空指针/期序 souci 由编译+运行护卫。⚠ 灯钮/Ctrl+K 无用户输入时上屏零差异——LA_UI_SHOT 快拍三新页留证。

**Steps:**
- [ ] buildPages() 抽取 + rethemeAll 落地（fast 回归）
- [ ] kNav/Icons/成员换名 + 前厅/人物馆/⌘K/凝视四接线（fast 回归含新门既绿）
- [ ] GalleryModel setCmdFilter + GalleryPage applyCmdFilter + LA_GALLERY_SEARCH_QA 继续绿
- [ ] 顶栏改（search_ 删/toggle/lampBtn）+ Ctrl+K + 灯钮
- [ ] LA_UI_SHOT **快照三新页**（快拍记 ledger，非黑屏人工判读）
- [ ] Commit: `feat(ui): MainWindow 重接线——人物馆/前厅接骨 + rethemeAll 整帧切灯 + ⌘K/凝视入口（spec §2.6）`

---

### Task 9: 退役波（HomePage/CullPage + 4 门体）

**Files:**
- Delete: `src/ui/HomePage.h/.cpp`、`src/ui/CullPage.h/.cpp`
- Delete: `src/main.cpp` 4 门体（LA_CULL_SMOKE 冒烟函数/LA_CULL_DUEL_QA 体/LA_CULL_VIDEO_QA 体/runHomeSmokeQa 四组 dispatch 与函数体，即 CULL_SMOKE/CULL_DUEL/CULL_VIDEO/HOME_SMOKE）+ animOk 列表 3 项（LA_CULL_SMOKE/DUEL/VIDEO，:8895/8921/8929 附近）；HOME_SMOKE 无 animOk 项（纯函数门，不需要）
- 保留：LA_CULL_EXEMPT_QA 全套（纯谓词）+ PhotoDedup + CullEngine
- Modify: `native/CMakeLists.txt` 删 4 行 LA_SOURCES（:76-77/:88-89）
- Modify: `src/ui/MainWindow.cpp` 收尾引用（若 Task 8 后仍有 home_/cull_ 残址 grep 清零）

**Steps:**
- [ ] grep `HomePage|CullPage` 全仓（ src/ + CMakeLists）→ 清每一处
- [ ] 删 4 门体 + dispatch + animOk 项 + matrix 行（matrix 在 Task 10 一起动以保纯 ASCII 单点编辑）
- [ ] build（零错）+ fast 回归（79−4 门全绿 + LA_CULL_EXEMPT_QA 独跑绿）
- [ ] Commit: `chore(retire): HomePage/CullPage 退役 + 四门体摘除（EXEMPT/PhotoDedup/CullEngine 存活，spec §1.2/§5）`

---

### Task 10: 门禁名册动刀（78→79 纯 ASCII 单点）+ dispatch + measuredMs

**Files:**
- Modify: `gate_matrix.ps1`（纯 ASCII；只动 ASCII 内容）
  - 删 `LA_HOME_SMOKE`（:64）、`LA_CULL_SMOKE`（:145）、`LA_CULL_DUEL_QA`（:154 带注释块）、`LA_CULL_VIDEO_QA`（:356 带注释块）
  - 加 5 行（快层注册形=LA_CULL_EXEMPT_QA 同列；注释全 ASCII）：
```powershell
  # 2.41.0 wave1+2 trust+reach gates (LA_LOBBY_QA / LA_PEOPLE_QA / LA_CMDK_QA /
  # LA_GAZE_QA / LA_LAMP_QA). Pure synthetic-fixture gates (R-06: no user photos in
  # repo); assert-group shape inherited from LA_CULL_EXEMPT_QA. Each proven
  # non-vacuous via its own FORCE_BROKEN hook: LA_LOBBY_QA drops the privacy filter
  # (private photos enter the pool -> group E goes RED); LA_PEOPLE_QA ignores
  # hidePrivate; LA_CMDK_QA swaps the month/season lexicon; LA_GAZE_QA bypasses the
  # low-saturation fallback; LA_LAMP_QA swaps the L1/L2 tables (pairwise check RED).
  @{n="LA_LOBBY_QA";  png=$false; tier="fast"; measuredMs=@M1@},
  @{n="LA_PEOPLE_QA"; png=$false; tier="fast"; measuredMs=@M2@},
  @{n="LA_CMDK_QA";   png=$false; tier="fast"; measuredMs=@M3@},
  @{n="LA_GAZE_QA";   png=$false; tier="fast"; measuredMs=@M4@},
  @{n="LA_LAMP_QA";   png=$false; tier="fast"; measuredMs=@M5@},
```
   （@M1@..@M5@ = 首跑实测回填整型毫秒值。）
- Modify: `src/main.cpp` dispatch 段（runCullExemptQa 分支块序位置 ：8616 前）增：
```cpp
if (qEnvironmentVariableIsSet("LA_LAMP_QA"))   return runLampQa();
if (qEnvironmentVariableIsSet("LA_LOBBY_QA"))  return runLobbyQa();
if (qEnvironmentVariableIsSet("LA_PEOPLE_QA")) return runPeopleQa();
if (qEnvironmentVariableIsSet("LA_CMDK_QA"))   return runCmdkQa();
if (qEnvironmentVariableIsSet("LA_GAZE_QA"))   return runGazeQa();
```
- Modify: `src/main.cpp` runI18nQa 增断言：新增核心词「前厅」「人物馆」「检索」「最新到馆」「全部藏品」 translate() 后 ≠ 源中文（读 :/i18n/LightAlbum_en.qm）。
- Modify: `qa_palette_ratchet.ps1`？不测（新 UI 全走工厂，C1-C5 无 delta）→ 仍需 **baseline 重生成**（`powershell -File qa_palette_ratchet.ps1 -Update`，在回填门禁那步联动，把新文件的字面量计数纳入棘轮基底）。
- LA_UI_SHOT baseline 重定：执行时 grep 'SHOT' gate_matrix.ps1 找到快照基线比对门（若为 pixel-diff 类门，重拍 4 新页 + 旧页 lamp-视觉微差两张组合）→ 遵其门自身流程。
- SelfTest：`powershell -File gate_matrix.ps1 -SelfTest` + roster 计数断言= **79**（SelfTest 输出 gates 行数与 79 相等，红线是 fast 层禁 ps1 的 E5/E9d 不破）。
- **Steps:**
- [ ] matrix 删 4 行 + 加 5 行（运行占位值）→ SelfTest 绿 + gates=79
- [ ] main.cpp dispatch 5 门 → 快层五门全跑绿（若无绿查 Task 3-7 遗留）
- [ ] runI18nQa 扩（暂红 → Task 11 补 .ts 后转绿——**顺序：本任务先加断言+.ts 同步加（调整：直接把 i18n 编辑放本任务执行**，见下）
- [ ] i18n .ts 手编：新条目（枚举—执行时以 grep translate("LightAlbum" 全新增点为真值）至 ~40-60 条；`lrelease`（完整路径）；.qm 与 .ts **同一 commit**
- [ ] qa_palette_ratchet.ps1 -Update 基底落盘（C1-C5 零净增核验）
- [ ] LA_UI_SHOT 4 新页快拍+比对基底重定
- [ ] measuredMs 五值回填 → fast 层全绿终验
- [ ] Commit: `test(qa): 门禁名册 78→79（5 新第 + 4 退役）+ i18n 覆盖 + 棘轮基底/快照基底重定（spec §5）`

---

### Task 11: 收口（2.41.0 + 全量 + 文档）

**Files:**
- Modify: `CMakeLists.txt` `project(LightAlbumNative VERSION 2.40.0)` → `2.41.0`
- Modify: `docs/CHANGELOG.md` + `docs/client_iter_guide.md` §7 + `docs/LightAlbum-2.36.0-使用说明书.md`（改名或新增 2.41.0 章节——沿用现手册路径风格；不新建无 index 文件）
- Modify: `.superpowers/sdd/.../progress.md` 收尾账 + 项目记忆更新

**Steps:**
- [ ] 版本 bump + 全量重建
- [ ] 全量出处：`powershell -ExecutionPolicy Bypass -File qa_runner.ps1`（全 tier）79/79 绿 + `-Tier fast` 复验 + Timing 档留档 ledger
- [ ] 文档三件（CHANGELOG[2.41.0] / 指导书 §7 行 / 手册）
- [ ] ledger 收尾 + 项目记忆更新（release-state/engine-upgrade-roadmap 不动——这不是引擎轮；新建 wave12-trust-reach 状态条目）
- [ ] Commit: `chore(release): 2.41.0 收口——版本/CHANGELOG/指导书/手册 + 台账`

**全程不 push**。规格/spec/plan 提交序列照旧全本地。

---

## Self-Review（已跑）

1. **Spec 覆盖**：§1.1 导航表→Task 8.2；§1.2 拓扑→T3-T7/T9；§2.1→T1；§2.2→T3；§2.3→T4；§2.4→T5；§2.5→T6/T7；§2.6→T8；§3 动效/字阶/装裱/glowCss/文案/i18n→T1/T2/T3/T5/T7/T8/T10；§4 数据流→T7/T8/T10（markRequested/setMark/refresh 汇合/⌘K 通道）；§5 门禁→T10 + spec §5 物形保障在门禁体；§6 风险对策→计划裁决 1-4。
2. **占位扫描**：无 TBD/TODO；「执行时 grep」只用于 discovery（GalleryRing 哈希、setMark 内存同步性、SHOT 基线门名、translate 增点全量枚举），每种都给了裁决式后备规则（ring=qHash%8→CAT、prompt 不发明 API、基线门自身流程）。
3. **类型一致**：CmdFilter 放 core/CmdFilter.h（终版）；11 token 函数名 bg/panel/card/cardHi/inset/ink/muted/faint/line/gold/emerald 与 sed 表一一对应；预设名 kLightFirst/kPushDoor/kLightsDown 与矩阵注释、MotionQa 断言字串一致；信号名 openGaze/filterGallery/clusteringRequested/markRequested(path,mark) 逐处一致。
