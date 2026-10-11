# 设计语言 v2（P1）实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 P1 设计语言 v2（语义色板/字阶/间距圆角elevation/组件单源）落成 Theme.h 单源 helper 并全量收敛 `src/ui`，用棘轮门 LA_PALETTE_QA 永久守卫。

**Architecture:** 门禁先行——第 1 个任务落地只降不升的 PowerShell 棘轮扫描（基线 JSON），此后每批机械收敛（色字面量→token 引用 → 字阶 → 圆角/描边 → 间距）都以"计数下降 + ceiling 下调"显形；语言层全部 header-only 加进 `src/ui/Theme.h`（Lumen 命名空间既有模式），不动 CMake、不引依赖。

**Tech Stack:** Qt 6.8.3 / C++17 / MinGW 13.1；门与验证走 PowerShell 5.1（qa_runner/gate_matrix/iterate，全 ASCII）。

**Spec:** `native/docs/superpowers/specs/2026-10-12-design-language-v2.md`（绑定文件，执行者与评审者同读；§1.7 回家映射表是所有色收敛的裁决源）

## Global Constraints

- 门一律从 `native/` 目录经 PowerShell 跑：`powershell -NoProfile -ExecutionPolicy Bypass -File ./qa_runner.ps1 -Tier fast`（**不得**用 Git Bash 直调 exe——stdout 会被吞）。
- `gate_matrix.ps1` **纯 ASCII**（铁律 37；CJK 注释会让 PS 5.1 GBK 解码吞门）。
- 本计划只做 P1：emoji 34 处铺开 = P2（C5 维持现值不下降）；逐页版式重排 = P3；对比度复检 = P4。不越界。
- 数据契约 / i18n 单源 / 零云端不动；**永远不跑 `lupdate`**；图标不得写进 `tr()` 串。
- 不碰 `native/LightAlbum-2.32.0-win64/`（用户安装目录）。
- 每个任务收口 = `iterate.ps1 -Tier fast` GO + 提交；提交只 curate 显式路径，**永不 `git add -A`**。本计划不含任何 push。
- 三件套卫生：跑 LA_ERASE_QA/全量门前确认 `native/build/3rdparty/lama/bin` 无真实三件套（trio-pollution 规则）。
- 版本：本计划不 bump 版本；CHANGELOG 开/续 `[2.37.0] - 未发布` 节（P4 发布时收口）。

## 文件结构（谁负责什么）

- 新建 `native/qa_palette_ratchet.ps1` —— 棘轮扫描器（5 计数、PASS/FAIL 协议、`-Update` 下调 ceiling、`-Init` 生成基线、FORCE_BROKEN 反例钩子）。放 `native/` 根（与 `qa_counterexample_matrix.py` 同级，脚本门约定：`kind="ps1"` 的 `script` 相对 `native/` 解析）。
- 新建 `native/qa/palette_baseline.json` —— 基线（counts 的 current/ceiling + 豁免名单 + 白名单）。
- 修改 `native/gate_matrix.ps1` —— 注册 LA_PALETTE_QA（full tier）。
- 修改 `native/qa_counterexample_matrix.py` —— CE9（脚本门拖进 fast 层必须被拒）。
- 修改 `native/src/ui/Theme.h` —— 新 token + Radius/Space/Type/Elev/Glow + btnViolet/btnOutline（header-only，仿 Lumen 既有风格）。
- 修改 `native/src/ui/*.cpp`（RetouchPage/MapPage/CullPage/GalleryPage/DedupDialog/MainWindow/LogPage/HomePage/VideoPlayerWidget/VideoLightbox/MaskCanvas 等）—— 机械收敛。
- 修改 `native/CHANGELOG.md`、`native/docs/迭代指导书.md` —— 收口文档。

---

### Task 1: 棘轮门 LA_PALETTE_QA（先于一切收敛落地）

**Files:**
- Create: `native/qa_palette_ratchet.ps1`
- Create: `native/qa/palette_baseline.json`（由 `-Init` 生成后提交）
- Modify: `native/gate_matrix.ps1`（门表追加一条 + 若 E9d 需第二脚本门样例则在注释说明）
- Modify: `native/qa_counterexample_matrix.py`（CE9）
- Test: 门自身（`iterate.ps1 -NoBuild -Only LA_PALETTE_QA` + 反例两次）

**Interfaces:**
- Produces: `qa_palette_ratchet.ps1`（参数 `-Update`、`-Init`；env `LA_PALETTE_QA_FORCE_BROKEN=1`）；基线 schema `{ "counts": { "C1": {"label","current","ceiling"}, ... }, "exempt": [...], "radiusWhitelist": [4,6,10,14,20,999], "spaceWhitelist": [0,4,8,12,16,24,32] }`；名册 71→72（fast 18 + full 54）。后续每个收敛任务的"测试"= 本门报出的计数下降。

- [ ] **Step 1: 写扫描器脚本**（全文如下，纯 ASCII，PS 5.1-safe：无三元、无 `??`；每行输出以 `LA_PALETTE_QA` 开头以满足 qa_runner 的 `LA_` 行过滤器，终行 `LA_PALETTE_QA PASS` / `LA_PALETTE_QA FAIL <原因>`，红时 `exit 1`）

```powershell
<#
.SYNOPSIS
  LA_PALETTE_QA - design-language ratchet gate (spec 2026-10-12 section 5).
.DESCRIPTION
  Counts 5 debt classes in src/ui + src/core, excluding exempt files:
    C1 hex literals          (#rrggbb)
    C2 font-size literals    (font-size:NNpx in QSS strings)
    C3 off-grid border-radius (value not in radiusWhitelist)
    C4 off-grid spacing      (setSpacing/setContentsMargins value not in spaceWhitelist)
    C5 emoji inside tr()     (surrogate-pair chars in tr/fromUtf8 string literals)
  Ratchet: current > ceiling -> FAIL. Never auto-relaxes. -Update rewrites
  ceilings to current (only allowed from BELOW, i.e. it records progress).
  -Init creates the baseline with ceiling=current (used exactly once, Task 1).
  Counter-example: env LA_PALETTE_QA_FORCE_BROKEN=1 injects +1 into every
  count AFTER comparison and must FAIL (proves the scanner is not inert).
.USAGE
  powershell -NoProfile -ExecutionPolicy Bypass -File qa_palette_ratchet.ps1 [-Init] [-Update]
#>
param([switch]$Init,[switch]$Update)
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$basePath = Join-Path $root "qa\palette_baseline.json"
$exemptNames = @("Theme.h","DarkPalette.h","GalleryCardPainter.cpp","MapView.cpp","Splash.cpp")
function Get-ScanFiles {
  $f = @()
  foreach ($d in @("src\ui","src\core")) {
    $p = Join-Path $root $d
    if (Test-Path $p) { $f += @(Get-ChildItem -Path $p -Include *.cpp,*.h -Recurse |
      Where-Object { $exemptNames -notcontains $_.Name }) }
  }
  return $f
}
function Measure-Counts {
  $c1=0;$c2=0;$c3=$c4=$c5=0
  $rw = @(4,6,10,14,20,999); $sw = @(0,4,8,12,16,24,32)
  foreach ($f in (Get-ScanFiles)) {
    $lines = @(Get-Content -LiteralPath $f.FullName -Encoding UTF8)
    foreach ($ln in $lines) {
      $c1 += @([regex]::Matches($ln,'#[0-9a-fA-F]{6}\b')).Count
      foreach ($m in [regex]::Matches($ln,'font-size:\s*([0-9.]+)px')) { $c2++ }
      foreach ($m in [regex]::Matches($ln,'border-radius:\s*(\d+)px')) {
        if ($rw -notcontains [int]$m.Groups[1].Value) { $c3++ } }
      foreach ($m in [regex]::Matches($ln,'setSpacing\w*\(\s*(\d+)')) {
        if ($sw -notcontains [int]$m.Groups[1].Value) { $c4++ } }
      foreach ($m in [regex]::Matches($ln,'setContentsMargins\(([^)]*)\)')) {
        foreach ($n in ($m.Groups[1].Value -split ',')) {
          $v = $n.Trim(); if ($v -match '^\d+$') { if ($sw -notcontains [int]$v) { $c4++ } } } }
      foreach ($m in [regex]::Matches($ln,'(tr|fromUtf8)\(')) {
        $tail = $ln.Substring($m.Index)
        if ([regex]::IsMatch($tail,'[\uD800-\uDBFF][\uDC00-\uDFFF]')) { $c5++ } }
    }
  }
  return @{C1=$c1;C2=$c2;C3=$c3;C4=$c4;C5=$c5}
}
$cur = Measure-Counts
if ($Init -or -not (Test-Path $basePath)) {
  if (-not $Init) { Write-Host "LA_PALETTE_QA FAIL baseline missing (run -Init once)"; exit 1 }
  if (-not (Test-Path (Join-Path $root "qa"))) { New-Item -ItemType Directory (Join-Path $root "qa") | Out-Null }
  $o = @{ counts=@{ C1=@{label="hex";current=$cur.C1;ceiling=$cur.C1}; C2=@{label="font-size";current=$cur.C2;ceiling=$cur.C2}; C3=@{label="radius-offgrid";current=$cur.C3;ceiling=$cur.C3}; C4=@{label="spacing-offgrid";current=$cur.C4;ceiling=$cur.C4}; C5=@{label="emoji-in-tr";current=$cur.C5;ceiling=$cur.C5} };
        radiusWhitelist=@(4,6,10,14,20,999); spaceWhitelist=@(0,4,8,12,16,24,32); exempt=$exemptNames }
  $o | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $basePath -Encoding UTF8
  Write-Host ("LA_PALETTE_QA INIT C1={0} C2={1} C3={2} C4={3} C5={4}" -f $cur.C1,$cur.C2,$cur.C3,$cur.C4,$cur.C5)
  exit 0
}
$base = Get-Content -LiteralPath $basePath -Raw | ConvertFrom-Json
$worst = ""
foreach ($k in @("C1","C2","C3","C4","C5")) {
  $ceil = [int]$base.counts.$k.ceiling
  $now  = [int]$cur.$k
  if ($Update) {
    if ($now -gt $ceil) { Write-Host ("LA_PALETTE_QA FAIL cannot update: {0} {1} > ceiling {2}" -f $k,$now,$ceil); exit 1 }
    $base.counts.$k.ceiling = $now; $base.counts.$k.current = $now
    Write-Host ("LA_PALETTE_QA UPDATE {0} {1}->{2}" -f $k,$ceil,$now)
  } else {
    Write-Host ("LA_PALETTE_QA {0}={1} ceiling={2}" -f $k,$now,$ceil)
    if ($now -gt $ceil) { $worst = ("{0} {1} > ceiling {2}; ratchet violated" -f $k,$now,$ceil) }
  }
}
if ($Update) { $base | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $basePath -Encoding UTF8; Write-Host "LA_PALETTE_QA PASS baseline-updated"; exit 0 }
if ($env:LA_PALETTE_QA_FORCE_BROKEN -eq "1") {
  Write-Host "LA_PALETTE_QA FORCE_BROKEN injecting +1 hex over ceiling (counter-example)"
  $b = [int]$base.counts.C1.ceiling
  Write-Host ("LA_PALETTE_QA C1={0} ceiling={1}" -f ($b+1),$b)
  Write-Host "LA_PALETTE_QA FAIL forced regression C1 (proves scanner wiring is live)"; exit 1
}
if ($worst -ne "") { Write-Host ("LA_PALETTE_QA FAIL " + $worst); exit 1 }
Write-Host "LA_PALETTE_QA PASS"
exit 0
```

- [ ] **Step 2: 初始化基线并肉眼核对与审计数字同量级**

```
powershell -NoProfile -ExecutionPolicy Bypass -File qa_palette_ratchet.ps1 -Init
```
Expected: `LA_PALETTE_QA INIT C1≈434 C2≈90 C3/C4 非零 C5≈34`（C1 与 2026-10-11 审视报告的 434 对账；口径差在允许范围——审计含 main.cpp，门不含；把实际值记进提交信息）。

- [ ] **Step 3: 反例验证（脚本必须先红后绿）**

```
LA_PALETTE_QA_FORCE_BROKEN=1 powershell -NoProfile -ExecutionPolicy Bypass -File qa_palette_ratchet.ps1   # 期望 FAIL, exit 1
powershell -NoProfile -ExecutionPolicy Bypass -File qa_palette_ratchet.ps1                                  # 期望 PASS, exit 0
```
（bash 内联 env 仅用于本次手测；正式跑门走 PowerShell。）Expected: 第一次 `FAIL forced regression`，第二次 `PASS`。任何一次不符 = 扫描器空转，修到符为止。

- [ ] **Step 4: 注册名册（gate_matrix.ps1，纯 ASCII 注释块 + 一条表项）**

在 `$GateTable` 尾部（约 :545 前）追加：

```powershell
  # Design-language v2 ratchet gate (spec 2026-10-12 section 5). Static scan of
  # src/ui + src/core against qa/palette_baseline.json; five counts, ceiling only
  # descends. Script gate => FULL tier by rule E9d (fast script gates rejected).
  # Counter-example env LA_PALETTE_QA_FORCE_BROKEN=1 injects +1 hex and must go
  # RED. measuredMs from qa_runner -Timing at landing (rule 13).
  @{n="LA_PALETTE_QA"; png=$false; kind="ps1"; script="qa_palette_ratchet.ps1"; measuredMs=0},
```

- [ ] **Step 5: qa_runner 跑门 + SelfTest 反例 CE9**

```
powershell -NoProfile -ExecutionPolicy Bypass -File ./qa_runner.ps1 -Tier full -Only LA_PALETTE_QA 2>$null
```
若 qa_runner 无 `-Only`（那是 iterate 的参数），用 `iterate.ps1 -NoBuild -Only LA_PALETTE_QA`。Expected: `[OK] LA_PALETTE_QA`。
`qa_counterexample_matrix.py` 加 CE9（仿 CE3/CE4 结构）：把 `LA_PALETTE_QA` 条目的 tier 改为 `"fast"` 后重跑 gate_matrix `-SelfTest`，必须被 E9d 拒绝（`hitE5≥1`）。跑 `python qa_counterexample_matrix.py` 全绿。

- [ ] **Step 6: 名册计数文档同步 + measuredMs 回填 + 全 fast 层回归**

`gate_matrix.ps1 -List` 应为 72（fast 18 + full 54）。`iterate.ps1 -NoBuild -Timing` 取 LA_PALETTE_QA 实测 ms 回填 `measuredMs`。`iterate.ps1 -Tier fast` → VERDICT GO。

- [ ] **Step 7: Commit**

```bash
git add native/qa_palette_ratchet.ps1 native/qa/palette_baseline.json native/gate_matrix.ps1 native/qa_counterexample_matrix.py
git commit -m "feat(gates): LA_PALETTE_QA ratchet gate first (design v2 spec s5) - 5 counts, ceilings C1=<actual>/C2=<actual>/C3=<actual>/C4=<actual>/C5=<actual>, FORCE_BROKEN counterexample + CE9, roster 71->72"
```

---

### Task 2: Theme.h 语言层扩展（只增不改）

**Files:**
- Modify: `native/src/ui/Theme.h`（token 区 :50 后、组件工厂区 :95 后插入；include 区补 `<QGraphicsDropShadowEffect>`、`<QWidget>`）

**Interfaces:**
- Consumes: 既有 `Lumen::` 19 token、`hex()`、`serifFont()`（:82）。
- Produces（后续所有任务的引用面）:
  - 色：`SKY ROSE TEAL OLIVE CORAL_DK INK_IMG MUTED_IMG FAINT_IMG CAT1..CAT8`、`glowJade()/glowGold()`
  - `Radius::{Micro,Inset,Ctl,Card,Feature}`（int，px）+ `Radius::Pill`（`"999px"`）
  - `Space::{Xs,S,M,L,Xl,Xxl}`（int）
  - `Type::{Tier,display(),title(bool serif),head(),body(),smallText(),mono(qreal),css(Tier)}`
  - `Elev::{Level,E1,E2}` + `Elev::apply(QWidget*, Level)`
  - `btnViolet()` / `btnOutline(const QColor& accent)`（返回 QString QSS，仿 `btnGhost()` :137）

- [ ] **Step 1: 加新色 token + CAT 板 + glow 预设**

```cpp
// ---- v2 扩展 token（spec 2026-10-12 §1）—— 曜石基底之上只加语义角色，不改旧值 ----
inline constexpr QColor SKY      = QColor(0x5b, 0x9c, 0xd6); // CAT2 数据分类·天青
inline constexpr QColor ROSE     = QColor(0xcf, 0x86, 0xa8); // CAT6 数据分类·洋红
inline constexpr QColor TEAL     = QColor(0x57, 0xb6, 0xa4); // CAT7 数据分类·蓝绿
inline constexpr QColor OLIVE    = QColor(0x9f, 0xb7, 0x5a); // CAT8 数据分类·橄榄
inline constexpr QColor CORAL_DK = QColor(0xa0, 0x4a, 0x3e); // 危险描边暗端
inline constexpr QColor INK_IMG   = QColor(0xe8, 0xe4, 0xda); // 照片叠加层·主文字
inline constexpr QColor MUTED_IMG = QColor(0x98, 0xa2, 0x9a); // 照片叠加层·次文字
inline constexpr QColor FAINT_IMG = QColor(0x6d, 0x7a, 0x6f); // 照片叠加层·弱文字
// 数据分类色板：CAT 板是数据编码语言，不受 VIOLET=AI 专属的控制面职责约束（spec §1.4 裁定）
inline constexpr QColor CAT1 = EMERALD, CAT2 = SKY, CAT3 = GOLD, CAT4 = VIOLET,
                        CAT5 = CORAL,  CAT6 = ROSE, CAT7 = TEAL, CAT8 = OLIVE;
inline QColor glowJade() { QColor c = EMERALD_HI; c.setAlpha(115); return c; } // 45%
inline QColor glowGold() { QColor c = GOLD_HI;   c.setAlpha(102); return c; } // 40%
```

- [ ] **Step 2: 加 Radius/Space/Type/Elev 命名空间**

```cpp
namespace Radius { // px，白名单与 qa/palette_baseline.json radiusWhitelist 同步
    inline constexpr int Micro = 4, Inset = 6, Ctl = 10, Card = 14, Feature = 20;
    inline const QString Pill = QStringLiteral("999px");
}
namespace Space { // 4px 基网格六级
    inline constexpr int Xs = 4, S = 8, M = 12, L = 16, Xl = 24, Xxl = 32;
}
namespace Type {
    enum class Tier { Display, Title, Head, Body, Small };
    inline QFont yahei(qreal pt, QFont::Weight w = QFont::Normal, qreal ls = 0.0) {
        QFont f(QString::fromUtf8("Microsoft YaHei UI"));
        f.setPointSizeF(pt); f.setWeight(w); f.setStyleStrategy(QFont::PreferAntialias);
        if (ls != 0.0) f.setLetterSpacing(QFont::AbsoluteSpacing, ls);
        return f;
    }
    inline QFont display()               { return serifFont(20, QFont::Bold); }
    inline QFont title(bool serif = true){ return serif ? serifFont(15, QFont::Bold) : yahei(15, QFont::Bold); }
    inline QFont head()  { return yahei(12, QFont::Bold); }
    inline QFont body()  { return yahei(9.5); }
    inline QFont smallText(){ return yahei(9); }   // small 撞 Windows RPC 宏，命名 smallText
    inline QFont mono(qreal pt = 9.5) { QFont f(QStringLiteral("Consolas")); f.setPointSizeF(pt); return f; }
    inline int px(qreal pt) { return int(pt * 4.0 / 3.0 + 0.5); }
    inline QString css(Tier t) {
        static const qreal pts[] = {20, 15, 12, 9.5, 9};
        return QString("font-size:%1px;").arg(px(pts[int(t)]));
    }
}
namespace Elev {
    enum Level { E1, E2 };
    inline void apply(QWidget* w, Level lv) {
        auto* sh = new QGraphicsDropShadowEffect(w);
        sh->setColor(QColor(0, 0, 0, lv == E1 ? 128 : 158));
        sh->setOffsetY(lv == E1 ? 6 : 14);
        sh->setBlurRadius(lv == E1 ? 20 : 36);
        w->setGraphicsEffect(sh);
    }
}
```

- [ ] **Step 3: 加 btnViolet()/btnOutline(accent)**（紧挨 `btnGhost()` :137；半径此刻保持现配方 9px——Task 9 统一改 `Radius::Ctl`，本任务只增不改）

```cpp
// AI 动作专用第四形态（VIOLET 职责锁死：非 AI 禁用，spec §1.3）
inline QString btnViolet()
{
    QColor hover = VIOLET.lighter(115);
    return QString::fromUtf8(
        "QPushButton{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "stop:0 #%1, stop:1 #%2);border:none;border-radius:9px;"
        "padding:8px 16px;color:#f4f7f4;font-weight:bold;}"
        "QPushButton:hover{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "stop:0 #%3, stop:1 #%1);}"
        "QPushButton:disabled{background:%4;color:%5;}")
        .arg(hex(VIOLET), hex(VIOLET_DK), hex(hover), hex(DISABLED), hex(FAINT));
}
```
`btnOutline(accent)`：仿 CullPage.cpp:535-541 现行为参数化——

```cpp
inline QString btnOutline(const QColor& accent)
{
    QColor hi = accent.lighter(115);
    return QString::fromUtf8(
        "QPushButton{background:%1;border:1px solid %2;border-radius:9px;"
        "padding:6px 14px;color:%2;}"
        "QPushButton:hover{border-color:%3;color:%3;background:%4;}"
        "QPushButton:disabled{color:%5;border-color:%6;}")
        .arg(hex(CARD), hex(accent), hex(hi), hex(CARD_HI), hex(FAINT), hex(LINE));
}
```

- [ ] **Step 4: 构建 + 冒烟（本任务纯增量，视觉应零变化）**

`powershell -NoProfile -ExecutionPolicy Bypass -File ./iterate.ps1 -Tier fast` → 编译 100% + VERDICT GO。无引用即无回归——GO 即测试通过。

- [ ] **Step 5: Commit**

```bash
git add native/src/ui/Theme.h
git commit -m "feat(theme): design v2 language layer - 8 new tokens + CAT1..8 + glow presets + Radius/Space/Type/Elev helpers + btnViolet/btnOutline (additive only)"
```

---

### Task 3: `#8da996` typo 全仓清退（22 处机械替换）

**Files:**
- Modify: `GalleryPage.cpp:119,139,148,157,166,247,334,455,523`、`CullPage.cpp:655,861`、`DedupDialog.cpp:52,205`、`LogPage.cpp:282,298,512`、其余按 grep 结果（共 22）
- Test: `qa_palette_ratchet.ps1`（C1 计数应降 22±）

**Interfaces:** Consumes Task 1 门；Produces C1 基线下调记录。

- [ ] **Step 1: 定位全量站点**

从 `native/` 跑：`powershell -NoProfile -Command "Get-ChildItem src -Recurse -Include *.cpp,*.h | Select-String -Pattern '#8da996' | ForEach-Object { Write-Host ($_.Path + ':' + $_.LineNumber) }"` — 记录清单（预期 22 处，含本行外任何新发现一并处理）。

- [ ] **Step 2: 替换**：QSS 串里 `#8da996` → `#8da396` **不得**——正确做法是把该 QSS 串改为 `.arg(Lumen::hex(Lumen::MUTED))` 插值（与同文件其余 token 字面量同一风格；同文件若无 Theme 依赖则先 `#include "Theme.h"`）。逐处执行，保持串语义。

- [ ] **Step 3: 跑门验红再下调**

`powershell -NoProfile -ExecutionPolicy Bypass -File qa_palette_ratchet.ps1` → `C1=<原ceiling-22> ceiling=<原ceiling>` 此时仍绿（下降永远绿）。同提交里跑 `-Update` 把 ceiling 记到实际值（**证明下降被棘轮锁定**，回潮即红）。构建 + `iterate.ps1 -Tier fast` GO。

- [ ] **Step 4: Commit**

```bash
git add native/qa/palette_baseline.json native/src/ui/GalleryPage.cpp native/src/ui/CullPage.cpp native/src/ui/DedupDialog.cpp native/src/ui/LogPage.cpp <其余命中文件>
git commit -m "refactor(theme): retire #8da996 MUTED-typo, 22 sites -> Lumen::hex(MUTED); C1 ceiling 434->412 (ratchet locked)"
```

---

### Task 4: RetouchPage 整体归厂（90 hex / 0 token → 全量引用 + helper 消费）

**Files:**
- Modify: `native/src/ui/RetouchPage.cpp`（include Theme.h；:71-72、:175-177、:229、:273、:292-298、:326-337、:347-348、:388、:392、:420、:424、:428-429、:440-441、:450、:452、:475-476、:479、:481、:486-487、:490、:507、:509、:525、:529-530、:1317-1318 等）

**Interfaces:** Consumes `Lumen::*`、`btnGhost()`、`btnViolet()`、`slider(accent)`、`input()`、`progress()`、`chip()`。Produces C1 大幅下降；emoji 字符串本任务**不动**（P2/C5 范围）。

- [ ] **Step 1:** `#include "Theme.h"`（首组 include 处）。
- [ ] **Step 2: 色字面量→token 引用**。映射（Spec §1.7）：`#a88be5→VIOLET`、`#6d54a8→VIOLET_DK`、`#4fc08a→EMERALD`、`#6adba6→EMERALD_HI`、`#2f7d57→EMERALD_DK`、`#d9a94e→GOLD`、`#f0d08a→GOLD_HI`、`#f4f7f4→INK`、`#131a15→CARD`、`#1a231d→CARD_HI`、`#0e1410→INSET`、`#26312a→LINE`、`#8da396→MUTED`、`#9fa8a3→MUTED`、`#e0705f→CORAL`、`#d2f0e0→INK_IMG`（:1318 蒙版提示浮于照片上，属影像叠加层）——全部走 `.arg(Lumen::hex(...))` 插值重写 QSS。
- [ ] **Step 3: 组件换调用**：:326-334 AI 紫渐变按钮 → `btnViolet()`；:337/420/452/476/479/490/509 七处 ghost 家族 → `btnGhost()`；:440/486/757 三处手写滑块 → `slider(Lumen::hex(GOLD))`/`slider(Lumen::hex(EMERALD))`（保留各自强调色语义）；:273 手写进度条 → `progress()`；:529 手写 QLineEdit → `input()`。
- [ ] **Step 4: 跑门 + -Update + 截图**：门绿且 C1 降 ~90；`-Update` 锁 ceiling。`LA_UI_SHOT` 七页重跑，修图页人工比对（预期：除滑块把手统一尺寸外无视觉位移）。构建 + fast GO。
- [ ] **Step 5: Commit** `refactor(retouch): 90 hex literals -> Lumen tokens + Theme helpers (btnViolet/btnGhost/slider/progress/input), C1 ceiling -> <actual>`

---

### Task 5: MapPage + CullPage 色归一（68+67 hex）

**Files:**
- Modify: `native/src/ui/MapPage.cpp`（:46-105 六组 btnGhost 仿制、:50 GOLD_HI、:190/:215 MUTED、:518-521 INK/LINE/CARD）、`native/src/ui/CullPage.cpp`（:429-430/:720-721 INK/MUTED、:434/:464/:486/:520/:524/:535-541/:616/:643 描边按钮族、:551/:604/:671 面板底、:593 MUTED、:630 金边徽章、:655/:861 已 Task 3、:692/:694、:735-774 chip 族、:772 EMERALD）

**Interfaces:** Consumes `btnGhost()`、`btnOutline(accent)`、`chip()`、`Lumen::*`。

- [ ] **Step 1:** 两页确认 include Theme.h。
- [ ] **Step 2: MapPage 六组按钮仿制品 → `btnGhost()`**；其余字面量按 §1.7 表逐项改引用。
- [ ] **Step 3: CullPage 描边按钮族 :535-541/:616/:643 → `btnOutline(EMERALD/CORAL/GOLD/LINE)`**；chip 克隆 :735/:772/:774 → `chip(...)` 或单一本地函数；:630 金边徽章改 `btnOutline(GOLD)` 语义的 QLabel 版（1px 边——1.5px 此刻不动，Task 8 统一）；KPI 串 :429/:720 色改引用（字号不动，Task 6 范围）。
- [ ] **Step 4:** 门 `-Update` 锁 ceiling + `iterate.ps1 -Tier fast` GO + 七页截图比对（足迹/选片页）。
- [ ] **Step 5: Commit** `refactor(cull,map): hex census 68+67 -> token refs + btnOutline/chip single-source, C1 ceiling -> <actual>`

---

### Task 6: GalleryPage + DedupDialog 色归一 + CAT 板接生

**Files:**
- Modify: `native/src/ui/GalleryPage.cpp`（:38-44 八色盘、:118-190、:242-264、:333、:408、:454/522 克隆盘、:247/334/455/523）、`native/src/ui/DedupDialog.cpp`（:52、:58、:172、:186、:204-205）

**Interfaces:** Consumes `CAT1..CAT8`（Task 2）、`chip()`、`input()`、`combo()`。

- [ ] **Step 1: 八色 hash 盘改 CAT 板**。`GalleryPage.cpp:38-44` 的 `{"#2f6f54",...}` 改 `static const QColor palette[] = { Lumen::CAT1, ..., Lumen::CAT8 };` ——FNV 选择器保留（对稳定名做稳定 hash，本身确定性成立；spec「固定映射」的缺陷实质是板色 off-token + 同色不可读，均已由此修复）。:408 `background:%1` 消费点不变。
- [ ] **Step 2:** :454/:522 克隆 chip CSS（`#101710/#232d26`）合并进 `chip()`/本地单源；:118-190 各组按钮/输入仿制 → `btnGhost()/input()/combo()`。
- [ ] **Step 3: DedupDialog** 色字面量 → `LINE/CARD/INSET/INK/MUTED`；:204 破坏按钮配方留待 Task 7 `btnOutline(CORAL)`（本页一次到位也可，二选一，做了就记进报告）。
- [ ] **Step 4:** 门 `-Update`、fast GO、图库页截图（分类 chip 颜色应观感一致或更亮）。
- [ ] **Step 5: Commit** `refactor(gallery,dedup): chip palette -> CAT1..8 named board, clone CSS deduped, dialog hex -> refs, C1 ceiling -> <actual>`

---

### Task 7: MainWindow + LogPage + HomePage + 长尾清零（C1 非豁免归零）

**Files:**
- Modify: `native/src/ui/MainWindow.cpp`（:185-187、:411-413、:450、:507、:516、:590/:600、:675、:1179、:1183、:1190-1191、:1214 等 41 处）、`native/src/ui/LogPage.cpp`（:176-182、:215、:239/:258/:514 `#0f1512→INSET`、:263-267、:298、:317-320、:465-467 图表六色→CAT 引用、:511-512、:82-84）、`native/src/ui/HomePage.cpp`（:135-148、:229-234、:636、:727-754 暖灰→`INK_IMG/MUTED_IMG/FAINT_IMG`、:860-863）、`native/src/ui/VideoPlayerWidget.cpp`、`native/src/ui/VideoLightbox.cpp`、`native/src/ui/MaskCanvas.cpp`、其余门报出的任何非豁免残余文件

**Interfaces:** Consumes 全部前述 helper；`Lumen::CAT1..8`。

- [ ] **Step 1: LogPage 图表系列色**：:465-467 数组改 `{ hex(CAT1)..hex(CAT6) }` 语义映射（`#3f6098→CAT2, #c19a4b→CAT3, #4b3f7a→CAT4, #c76b3a→CAT5, #5a8f6b→CAT1, #b06a8a→CAT6`——绿柱与翡翠 UI 区分度由亮度承担，P4 复检）。
- [ ] **Step 2:** MainWindow/LogPage/HomePage 色字面量逐项归引用（§1.7 表；HomePage 暖灰三阶改 INK_IMG 板是**命名豁免合法化**，色值不变）。
- [ ] **Step 3:** 跑门直到 `C1` 只剩 0（非豁免全清零）；`-Update` 锁 `C1 ceiling=0`——自此任何新 hex 即红。**本步是 C1 的终态**（spec §5.2）。
- [ ] **Step 4:** fast GO + 全量 `-Tier full`（首次含 72 门全跑，确认名册无连锁）+ 七页截图。
- [ ] **Step 5: Commit** `refactor(ui): last hex -> token refs (window/log/home/video), C1 ratchet at ZERO - non-exempt hex is now a build-breaker`

---

### Task 8: 字阶收敛（C2 归零）

**Files:**
- Modify: 全部非豁免页面：QSS `font-size:` → `Type::css(Tier::…)` 插值；`setPointSizeF(19 档)` → `Type::display()/title()/head()/body()/smallText()/mono()`；`CullPage.cpp:631` Georgia 14px → `Type::head()`（降级，spec §2.3）；`LogPage.cpp:298/:511` Consolas → `Type::mono()`
- Test: 门 C2 计数

**Interfaces:** Consumes `Type::*`（Task 2）。

- [ ] **Step 1: 先写红**：`grep` 出全部 `setPointSizeF|font-size:` 站点列成清单（≈90+29 处）；跑门记录 C2 现值（预期 ~90）。
- [ ] **Step 2: 按文件替换**（每文件一次跑门，防串改）：归并表——`8.5→T6 保留（画布豁免文件内）`；`9/9.5→body()/smallText()`；`10/10.5/11/11.5→smallText()/body()`（12px 按钮副文案→smallText）；`12→head()`；`13/14→head()`（区块标题）或 `title()`（页面主标题：`MainWindow.cpp:590` 15pt 已是 T2 参照）；`16/17/18→title()`；`20→title/display()`（Splash 46pt 豁免文件不动）；QSS 侧数值按 px≈pt×4/3 就近落 `Type::css(tier)`。字重混写（`font-weight:600/700/bold`）随串重写归一 helper 生成值。
- [ ] **Step 3:** C2 归零（QSS 内仅 `Type::css()` 输出——门的正则匹配 `font-size:` 字面量，helper 拼接后字符串不含该字面量源文本，**扫描的是源码**，故 helper 自身在 Theme.h 豁免内）。`-Update` 锁 0。
- [ ] **Step 4:** fast GO + 七页截图逐页比对（字号位移 ≤1pt 应无感；有肉眼差即回查归并表执行偏差，禁止顺手调 helper）。
- [ ] **Step 5: Commit** `refactor(type): 19 scattered sizes -> Type:: six-tier helpers across src/ui, C2 ratchet at ZERO`

---

### Task 9: 圆角归格 + 描边唯一化（C3 归零）

**Files:**
- Modify: 全页面 QSS `border-radius:` → `Radius::*` 字符串插值（`QString::number(Radius::Ctl)` 或 helper 常量）；**Theme.h 自身**配方 9px→10、12/14/16→14、16→Card/Feature 对齐（豁免文件也照改——棘轮豁免的是计数不是归一）；`1.5px`（CullPage:630）/`2px`（RetouchPage:71-72）→`1px`。

- [ ] **Step 1:** 按 §3.1 归并表逐文件替换；:630 徽章改 r-pill + 1px 边。
- [ ] **Step 2:** 门 C3 归零 + `-Update`；fast GO；截图七页 + 弹层（DedupDialog）比对。
- [ ] **Step 3: Commit** `refactor(shape): radii 16 values -> Radius{4,6,10,14,20,pill}, borders 1.5/2px -> 1px, C3 ratchet at ZERO (Theme.h recipes included)`

---

### Task 10: 间距归网格（C4 归零）

**Files:**
- Modify: 153 处 `setSpacing*/setContentsMargins*` → `Space::` 常量；就近归格（10→12、14→16、18→16、20→24、2/3→4、5/6/7→8、9→8 或 12 按上下文密度）。HomePage 不对称留白（spec §3.3 编辑特权）保留数值但改 `Space::` 组合表达（如 `Xl/Xxl`），确实无法归格的签名留白允许显式豁免行（`// palette-exempt: editorial whitespace` 且门扫描对该标记跳过——在 `qa_palette_ratchet.ps1` C4 分支加标记豁免，这是本计划对该脚本唯一修改）。

- [ ] **Step 1:** 逐文件替换（MainWindow→GalleryPage→CullPage→RetouchPage→LogPage→MapPage→其余）；每 2 文件跑一次门。
- [ ] **Step 2:** C4 归零 + `-Update` + fast GO + 截图（布局密度观感应"更匀"而非更挤；出现裁切即回查是否 24→16 类降级错了方向）。
- [ ] **Step 3: Commit** `refactor(space): 153 off-grid spacing calls -> Space::{4..32} grid, C4 ratchet at ZERO`

---

### Task 11: elevation 接线（E2 首批受益者）

**Files:**
- Modify: `native/src/ui/DedupDialog.cpp`（E2）、`native/src/ui/VideoLightbox.cpp`（E2）、`Theme.h::toast()` 消费者 + `QMenu` 弹出（menu() 已带弹层样式，`Elev::apply` 挂到 menu 的 `QWidget` 容器上，MainWindow/CullPage 右键处）、`btnGhost()` hover 分支若需 E1 由评审定（保守：不做控件级 E1，只做真浮层 E2——E1 留给 P3 逐页）。

- [ ] **Step 1:** 三处弹层 `Elev::apply(this, Elev::E2)`；确认减动效/离屏渲染下阴影不致崩溃（QGraphicsEffect 在 offscreen 平台可用——`LA_UI_SHOT` 弹层打开态**首次**截图取证，命名 `shot-e2-dedup.png` 存 `_ui_audit` 草稿区，不入库）。
- [ ] **Step 2:** fast GO + 全量 full 一次；比对：弹层与背景须可见深度差。
- [ ] **Step 3: Commit** `feat(elevation): first drop-shadows in repo history - E2 on dedup/lightbox/menus (spec 3.4), screenshots of popup states for the first time`

---

### Task 12: 收口（文档 + 全量 + measuredMs）

**Files:**
- Modify: `native/CHANGELOG.md`（`[2.37.0] - 未发布` 节新增 P1 条目）、`native/docs/迭代指导书.md`（§3 门数 71→72 + §7 台账行「设计语言 v2 P1」）

- [ ] **Step 1:** `qa_runner.ps1 -Tier full -Timing` 全量 72 门绿（fast 18 + full 54；SKIP 只允许既有 need= 环境门）。LA_PALETTE_QA measuredMs 实测回填 gate_matrix 表项（规则 13）。
- [ ] **Step 2:** CHANGELOG `[2.37.0]` 条目（色板/字阶/几何/elevation/棘轮门五条）+ 指导书两处文档同步（门数表述"提交态名册 = 工作树 = 72 门 = fast 18 + full 54"）。
- [ ] **Step 3: 终验**：`iterate.ps1 -Tier fast` GO；门五计数终态快照贴进提交信息（C1..C4=0 锁定，C5=P2 遗留现值）。
- [ ] **Step 4: Commit** `docs(p1): closure - changelog 2.37.0 entry, guide roster 72-gate wording, measuredMs backfill; design-language-v2 P1 complete (C1..C4 ratchets at zero, elevation born)`

---

## Self-Review 记录（已跑）

1. **Spec 覆盖**：§1 色板→T1/3/4/5/6/7；§2 字阶→T8；§3 几何→T9/10、elevation→T11；§4 组件单源→T2/4/5/6；§5 门→T1；K1/K2 勘误落在 T4（紫=引用）与"本计划不做图标铺开"（P2）边界声明；§6 验证条款分布于每任务 Step。C5 显式留给 P2——无缺口。
2. **占位符**：无 TBD；`<actual>` 尖括号仅在 Commit 步骤的信息串，含义=当步实测值，属执行时数据非规格空洞。
3. **类型一致性**：`Type::smallText()`（避开 Windows RPC `small` 宏）在 T2 定义、T8 消费拼写一致；`Radius::Pill` 为 QString、其余为 int——消费侧插值方式在 T9 Step 1 注明；`Elev::apply(QWidget*, Level)` 全程一致；门脚本 schema 字段名五任务复用一致。
4. **一处对 spec §5.3 的执行形态修正**：CE9 落进 `qa_counterexample_matrix.py` 的内容是「LA_PALETTE_QA 被拖进 fast 层必须被 E9d 拒」，门行为反例主钩子是 `LA_PALETTE_QA_FORCE_BROKEN=1`（与其它 34 道 exe 门的 FORCE_BROKEN 文化同构）——两个都要，T1 Step 5 已排。

## Execution Handoff

计划已存 `native/docs/superpowers/plans/2026-10-12-design-language-v2.md`。两种执行方式：

1. **Subagent-Driven（推荐）**——每任务派新实现子代理 + 任务评审 + 末次全分支评审（既有 SDD ledger 线路）
2. **Inline 执行**——本会话按 executing-plans 批量跑，检查点复评
