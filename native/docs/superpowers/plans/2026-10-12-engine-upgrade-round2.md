# 引擎升级第二轮实施计划（R1 Lumen 显式算子产品化 / R2 HEIC+WebP EXIF 补全 / R3 Erase residual 收尾）

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 13 个已金样对拍的 LumenEngine 显式算子接进 RetouchPage「一键增强」（R1）、给 ExifReader 补 HEIC/WebP 容器→TIFF blob 抽取（R2）、给 EraseBridge 补任务淘汰/取消与陈旧注释订正（R3），全部以新门/扩门守护，收口 2.38.0。

**Architecture:** 门禁先行——R1/R2/R3 各自先把断言写成对当前实现的红（LA_LUMEN_OPS_UI_QA 静态扫描、LA_RETOUCH_A_QA 诚实判定修复、LA_EXIF_CONTAINER_QA 合成夹具、LA_ERASE_QA G1/G2 扩组），再实现转绿；引擎数学零改动（LumenEngine.{h,cpp} 不动）。R1 纯 UI 增量接线；R2 只补「容器→blob」一层，blob 喂现有 TIFF 解析器；R3 局部改动 EraseBridge/EraseRunner。

**Tech Stack:** Qt 6.8.3 / C++17 / MinGW 13.1；门与验证走 Windows PowerShell 5.1（qa_runner/gate_matrix/iterate，gate_matrix.ps1 全 ASCII）。

**Spec:** `native/docs/superpowers/specs/2026-10-12-engine-upgrade-round2-lumenops-exif-erase-design.md`（绑定文件，执行者与评审者同读；§2.4/§3.3/§4.3 是各门断言的规格值来源）

## Global Constraints

- 门一律从 `native/` 目录经 **Windows PowerShell** 跑：`powershell -NoProfile -ExecutionPolicy Bypass -File ./qa_runner.ps1 -Only <GATE>`（**不得**用 Git Bash 直调 exe——stdout 会被吞）。
- `gate_matrix.ps1` **纯 ASCII**（铁律 37）；本计划新增的注释块全英文 ASCII。新增条目必须以 `@{n=` 形式进表（E12 文本计数 == 解析计数）。
- 构建：`cd native && cmake --build build --target LightAlbum -j 8`（MinGW）。pre-commit 钩子要求**先构建后提交**。
- 提交只 curate 显式路径，**永不 `git add -A`**；**本计划不含任何 push**（push 授权仅覆盖 2.36.0，已用尽；全部本地）。
- 铁律 20（门与实现同源）/ 22（期望值不随反例开关分支）/ 25·30·31（measuredMs 一律 `-Timing` 实测回填，不估）/ 29（宿主无关、无随机：合成夹具字节确定）/ 34（禁空心门：反例必须咬得动）/ 37。
- **跑 LA_ERASE_QA / 全量门前**确认 `native/build/3rdparty/lama/bin` 无真实三件套（trio-pollution 规则；stub 在 `native/build/inpaint_stub.exe`）。若发现 trio 存在：停下询问用户，不得自行删除。
- C5 棘轮：新增 UI **零新增 emoji**（LA_PALETTE_QA ceiling 只降不升）；**永远不跑 `lupdate`**；图标不得写进 `tr()` 串。
- 不碰 `native/LightAlbum-2.32.0-win64/`（用户安装目录）；不提交 `native/_engine_survey_2026-10-12.md`、`native/_ui_audit/` 等 scratch。
- 用户真样本路径（.heic 等）只作本地 env 值，**不入仓库/CHANGELOG/文档**。
- 版本收口：CMakeLists.txt `2.37.0 → 2.38.0`（Task 11）；CHANGELOG 开 `[2.38.0] - 2026-10-12`。

## 计划级裁决（两条，执行者必须遵守）

1. **LA_LUMEN_OPS_UI_QA 用 C++ exe 门落 fast 层**。spec §2.4 写「脚本门 fast」与 matrix 规则 E5/E9d（fast 层禁 ps1 脚本门）冲突 → 裁决：改为 exe 静态扫描门（`runLumenOpsUiQa`，main.cpp 内，环境变量 `LA_LUMEN_OPS_UI_QA` 分派）。fast 层意图（PR 必跑）保留。**应急**：若 T4 实测 measuredMs > 500（E6 fast 预算）→ 降 full 层（名册变为 fast 18 + full 56，总数仍 74），并在 matrix 注释里记录降级原因。
2. **LA_EXIF_CONTAINER_QA 不带 `need=`**。spec §3.3 的 `need=LA_HEIC_SAMPLE` 措辞与 §5.4「SKIP 仅既有 need= 门」冲突 → 裁决：合成夹具段恒跑（不带 need）；真样本臂改为**进程内 env 判定**（`LA_HEIC_SAMPLE` 有值→断言真样本；无值→打印 `realSample=skipped`）。既有 SKIP 名单零变化。

**顺带修复（R1 范围内，spec §2.4 烟测断言的诚实前提）**：`RetouchPage::qaShootA` 计算 `ok` 却恒 `QaExit::requestExit(0)`（RetouchPage.cpp:736）= 空心门；R1-T2 修成诚实退出码。

## 文件结构（谁负责什么）

- 修改 `native/src/ui/RetouchPage.{h,cpp}` —— R1 一键增强工具区 + 13 接线 + 异步 + 撤销集成 + 导出重放；R3 取消按钮。
- 修改 `native/src/core/RtHistory.h` —— `RtEditState` 增 `enh/ops` 两字段。
- 修改 `native/src/main.cpp` —— `runLumenOpsUiQa`（R1-T1）、`qaShootA` 诚实判定（R1-T2）、`runExifContainerQa`（R2-T1）、`runEraseQa` G1/G2 扩组（R3）。
- 修改 `native/src/core/ExifReader.{h,cpp}` —— `extractWebpExif`/`extractHeicExif` + 分派 + 头注释订正。
- 修改 `native/src/core/EraseBridge.{h,cpp}` —— 淘汰 + cancel + 注释订正；`EraseRunner.{h,cpp}` —— `requestCancel` + 分片等待。
- 修改 `native/src/core/FaceCluster.h` —— 注释订正（2 行）。
- 修改 `native/gate_matrix.ps1` —— 注册 LA_LUMEN_OPS_UI_QA（fast）+ LA_EXIF_CONTAINER_QA（full）+ measuredMs 回填。
- 修改 `native/CHANGELOG.md`、`native/docs/迭代指导书.md` —— 收口文档（Task 11）。

---

### Task 1 (R1-T1): LA_LUMEN_OPS_UI_QA 静态接线门（接线前必红）

**Files:**
- Modify: `native/src/main.cpp`（新增 `runLumenOpsUiQa` 函数 ≈1849 行后 + 分派 ≈6162 行后）
- Modify: `native/gate_matrix.ps1`（表尾 ≈557 行后追加一条 fast 门）
- Test: `qa_runner.ps1 -Only LA_LUMEN_OPS_UI_QA`（对未接线实现必红）+ `FORCE_BROKEN` 反例

**Interfaces:**
- Produces: 门名 `LA_LUMEN_OPS_UI_QA`；env `LA_LUMEN_OPS_UI_QA_FORCE_BROKEN`；输出行 `LA_LUMEN_OPS_UI_QA PASS|FAIL covered=N/13 files=M wiring=0|1 missing=[...]`；exit 0/1。
- Produces: `runLumenOpsUiQa()` 供后续 T4 回填 measuredMs。

- [ ] **Step 1: 写门函数**。在 runRetouchOpQa 的收尾（main.cpp 第 1842-1849 行，`const bool pass = (checks > 0) && ...` 到 `return pass ? 0 : 1;\n}` 之后）、`// 方向 C 第一步：相册内搜索过滤断言` 注释之前，插入：

```cpp
// 2.38.0 R1：一键增强接线门（fast 层 exe 门；E5/E9d 禁止 ps1 入 fast 层，spec §2.4 层级意图保留）。
// 断言 src/ui/ 对 13 个 LumenEngine 显式算子各 >=1 处真实调用（先剥注释再匹配），
// 且 RetouchPage.cpp 含事件接线（opBtn_ / onEnhOpClicked 同时出现）。
// F1 fail-open 守卫：扫描根找不到或 0 文件 -> 直接 FAIL（绝不静默放行，铁律34）。
// 反例：LA_LUMEN_OPS_UI_QA_FORCE_BROKEN=1 -> 期望表注入 phantom 算子名（覆盖 13/14）-> 必红。
int runLumenOpsUiQa()
{
    const QStringList kOps = {
        QStringLiteral("filmStyle"),     QStringLiteral("blackWhite"),   QStringLiteral("glowStyle"),
        QStringLiteral("redeyeRemove"),  QStringLiteral("vignetteCorrect"), QStringLiteral("toneMapHdr"),
        QStringLiteral("structure"),     QStringLiteral("dodge"),        QStringLiteral("dehaze"),
        QStringLiteral("chromaticCorrect"), QStringLiteral("portraitBeautify"),
        QStringLiteral("levelCorrect"),  QStringLiteral("lensCorrect"),
    };
    QStringList expected = kOps;
    const bool broken = !qEnvironmentVariableIsEmpty("LA_LUMEN_OPS_UI_QA_FORCE_BROKEN");
    if (broken) expected << QStringLiteral("qaPhantomOp");   // 反例：注入不存在的算子名

    QString root;
    for (const QString& cand : { QCoreApplication::applicationDirPath() + QStringLiteral("/../src/ui"),
                                 QCoreApplication::applicationDirPath() + QStringLiteral("/src/ui"),
                                 QStringLiteral("src/ui") }) {
        if (QFileInfo(cand).isDir()) { root = cand; break; }
    }
    int files = 0;
    QSet<QString> covered;
    bool wiring = false;
    if (!root.isEmpty()) {
        const QFileInfoList list = QDir(root).entryInfoList(
            { QStringLiteral("*.cpp"), QStringLiteral("*.h") }, QDir::Files, QDir::Name);
        for (const QFileInfo& fi : list) {
            QFile f(fi.absoluteFilePath());
            if (!f.open(QIODevice::ReadOnly)) continue;
            QString text = QString::fromUtf8(f.readAll());
            ++files;
            text.remove(QRegularExpression(QStringLiteral("/\\*.*?\\*/"),
                QRegularExpression::DotMatchesEverythingOption));      // 剥块注释
            text.remove(QRegularExpression(QStringLiteral("//[^\\n]*"))); // 剥行注释
            for (const QString& op : expected)
                if (text.contains(QStringLiteral("LumenEngine::") + op + QLatin1Char('(')))
                    covered.insert(op);
            if (fi.fileName() == QLatin1String("RetouchPage.cpp")
                && text.contains(QStringLiteral("opBtn_"))
                && text.contains(QStringLiteral("onEnhOpClicked")))
                wiring = true;
        }
    }
    QStringList missing;
    for (const QString& op : expected) if (!covered.contains(op)) missing << op;
    const bool ok = files > 0 && wiring && missing.isEmpty();
    std::fprintf(stderr, "LA_LUMEN_OPS_UI_QA %s covered=%d/%d files=%d wiring=%d missing=[%s]\n",
                 ok ? "PASS" : "FAIL", covered.size(), expected.size(), files, wiring ? 1 : 0,
                 qPrintable(missing.join(QStringLiteral(","))));
    std::fflush(stderr);
    return ok ? 0 : 1;
}
```

若 main.cpp 缺下列 include 则补（多已存在）：`<QSet>`、`<QRegularExpression>`、`<QDir>`、`<QFileInfo>`（按现有 include 区风格追加）。

- [ ] **Step 2: 挂分派**。在 LA_RETOUCH_OP_QA 分派（main.cpp:6160-6162）之后插入：

```cpp
    // 2.38.0 R1：一键增强 13 算子接线门（静态扫描 src/ui；接线前必红）
    if (qEnvironmentVariableIsSet("LA_LUMEN_OPS_UI_QA"))
        return runLumenOpsUiQa();
```

- [ ] **Step 3: 注册名册**。`gate_matrix.ps1` 表尾 `@{n="LA_PALETTE_QA"; ...}`（557 行）之后、`)`（558 行）之前追加（全 ASCII 注释）：

```powershell
  # 2.38.0 R1: one-tap enhancement wiring gate (13 LumenEngine explicit ops wired into
  # src/ui + event hookup in RetouchPage.cpp). FAST tier exe gate: rules E5/E9d bar ps1
  # gates from the fast layer, so the spec section 2.4 "fast tier" intent is delivered as
  # an exe static-scan gate instead of a script gate (plan-level ruling 1). F1 fail-open
  # guard: 0 files scanned -> FAIL. Counter-example LA_LUMEN_OPS_UI_QA_FORCE_BROKEN=1
  # injects a phantom op name (13/14 coverage) -> RED. measuredMs=0 placeholder; E8
  # requires the key, rule 25/31 require the -Timing value (backfilled in R1-T4).
  @{n="LA_LUMEN_OPS_UI_QA"; png=$false; tier="fast"; measuredMs=0},
```

- [ ] **Step 4: 构建**。

Run: `cmake --build build --target LightAlbum -j 8`（cwd=native）
Expected: 构建成功。

- [ ] **Step 5: RED 实证（接线前必红）**。

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File ./qa_runner.ps1 -Only LA_LUMEN_OPS_UI_QA`（cwd=native）
Expected: `[FAIL]`，输出 `LA_LUMEN_OPS_UI_QA FAIL covered=0/13 ... wiring=0 missing=[filmStyle,...]` —— 13 个算子名在 src/ui 零命中（spec §2.1 已实证）。

- [ ] **Step 6: FORCE_BROKEN 反例实证**。

Run: `$env:LA_LUMEN_OPS_UI_QA_FORCE_BROKEN="1"; powershell ... -Only LA_LUMEN_OPS_UI_QA; Remove-Item Env:LA_LUMEN_OPS_UI_QA_FORCE_BROKEN`
Expected: `[FAIL]`，`covered=0/14`、missing 含 `qaPhantomOp`。注意：此臂在接线后（T4）复验须转红为 `13/14`（本步只证反例注入生效）。

- [ ] **Step 7: 提交**。

```bash
git add native/src/main.cpp native/gate_matrix.ps1
git commit -m "feat(gates): LA_LUMEN_OPS_UI_QA 静态接线门（R1 先行；对未接线实现必红）"
```

---

### Task 2 (R1-T2): qaShootA 诚实判定修复 + 13 入口存在性断言（对当前实现必红）

**Files:**
- Modify: `native/src/ui/RetouchPage.cpp`（`qaShootA`，677-737 行）
- Test: `qa_runner.ps1 -Only LA_RETOUCH_A_QA`（对未接线实现必红）

**Interfaces:**
- Consumes: T3 将创建的按钮命名契约 `opBtn_<key>`（objectName，13 个）。
- Produces: LA_RETOUCH_A_QA 诚实退出（ok→0 / !ok→1）+ `enhOps=` 计数进输出行；T3 之后转绿。

- [ ] **Step 1: 改 qaShootA 尾段**。把 723-736 行（`// ext>1180 证明...` 注释起到 `QaExit::requestExit(0);` 止）整体替换为：

```cpp
    // ext>1180 证明右栈整栏未被裁切（否则右栏内容会止于 ≈1120）；两页右栏均有内容。
    // 2.38.0 R1：一键增强 13 入口存在性（objectName opBtn_<key>；构造即建，无需点击）。
    int enhOps = 0;
    for (auto* b : findChildren<QPushButton*>())
        if (b->objectName().startsWith(QStringLiteral("opBtn_"))) ++enhOps;
    const bool ok = rightStack_->count() == 6 && sliders_.size() == 12 && modeBtns_.size() == 6
                    && pgPreset == 1 && pgAdj == 0 && gold > 0
                    && st1.second > 1180 && st0.second > 1180
                    && st1.first > 500 && st0.first > 500
                    && enhOps == 13;
    std::fprintf(stderr, "LA_RETOUCH_A_QA pages=%d sliders=%d modes=%d map=%d/%d "
                          "adjExt=%d presetExt=%d gold=%d enhOps=%d ok=%d png=%s\n",
                 rightStack_->count(), sliders_.size(), modeBtns_.size(), pgPreset, pgAdj,
                 st1.second, st0.second, gold, enhOps, ok ? 1 : 0, qPrintable(png));
    std::fprintf(stderr, "LA_RETOUCH_A_QA %s ok=%d enhOps=%d\n", ok ? "PASS" : "FAIL", ok ? 1 : 0, enhOps);
    std::fflush(stderr);
    img1.save(png);
    const QString presetPng = QString(png).section(QLatin1Char('.'), 0, 0) + QStringLiteral("_preset.png");
    img0.save(presetPng);
    QaExit::requestExit(ok ? 0 : 1);   // 2.38.0 R1：诚实判定（旧版恒 0 = 空心门，已实测证实）
```

- [ ] **Step 2: 构建**。

Run: `cmake --build build --target LightAlbum -j 8`（cwd=native）

- [ ] **Step 3: RED 实证（enhOps=0 必红）**。

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File ./qa_runner.ps1 -Only LA_RETOUCH_A_QA`（cwd=native）
Expected: `[FAIL]`，输出 `LA_RETOUCH_A_QA FAIL ok=0 enhOps=0`。这同时证明旧门是空心门、现被修复。

- [ ] **Step 4: 提交**。

```bash
git add native/src/ui/RetouchPage.cpp
git commit -m "fix(gates): qaShootA 诚实判定 + enhOps==13 断言（旧版恒 exit 0 的空心门修复；对未接线必红）"
```

---

### Task 3 (R1-T3): RetouchPage 一键增强工具区 + 13 接线 + 异步 + 撤销集成

**Files:**
- Modify: `native/src/ui/RetouchPage.h`（成员/槽声明）
- Modify: `native/src/ui/RetouchPage.cpp`（kEnhOps/EnhOutcome/runEnhOp + buildEnhOps + onEnhOpClicked + rebuildGeo 基图 + 状态捕获/恢复 + 换图清除 + 导出重放 + undo/redo 忙碌守卫 + 构造函数接线）
- Modify: `native/src/core/RtHistory.h`（RtEditState 增字段）
- Test: `qa_runner.ps1 -Only LA_LUMEN_OPS_UI_QA`（转绿）+ `-Only LA_RETOUCH_A_QA`（转绿）

**Interfaces:**
- Consumes: `LumenEngine::` 13 算子（签名见 LumenEngine.h:68-89，全部有默认参数——`levelCorrect(img)` / `redeyeRemove(img, &pixels)` / `portraitBeautify(img, 1.0, &frac)` / `vignetteCorrect(img, 1.0, &k)` 等可直接调用）。
- Produces: objectName 契约 `opBtn_<key>`（13 个）；`ops_` 序列（导出重放源）；`enhImg_`（几何管线基图）。

- [ ] **Step 1: RetouchPage.h 声明**。在「P4-2 蒙版」块之前插入：

```cpp
    // 2.38.0 R1：一键增强（Lumen 13 显式算子；异步 + 撤销集成 + 诚实反馈）
    void buildEnhOps(class QGridLayout* gl);
    void onEnhOpClicked(const QString& key);
    QVector<QPushButton*> opBtns_;
    QStringList ops_;                        // 已应用算子序列（导出全分辨率重放用）
    QImage    enhImg_;                       // 增强结果（null=未用；几何管线基图）
    bool      opBusy_ = false;
    int       opGen_ = 0;                    // 换图/重置代次（丢弃跨代飞行结果）
```

并确保头部 include `<QStringList>`、`<QImage>`（`RtHistory.h` 即将带 QImage，但显式包含更稳）。

- [ ] **Step 2: RtHistory.h 扩状态**。`#include <QVector>` 后加 `#include <QImage>` `#include <QStringList>`；struct 内 `QRectF cropNorm;` 后加两字段；operator== 扩展（保留 qFuzzyCompare 两行不动）：

```cpp
    QImage      enh;                  // 2.38.0 R1：一键增强结果（null=未用）
    QStringList ops;                  // 已应用算子序列（重放）

    bool operator==(const RtEditState& o) const {
        return cur == o.cur && agentPlan == o.agentPlan && agentMode == o.agentMode
            && rot == o.rot && flipH == o.flipH && flipV == o.flipV
            && qFuzzyCompare(angle, o.angle) && qFuzzyCompare(aspect, o.aspect)
            && cropNorm == o.cropNorm
            && enh == o.enh && ops == o.ops;
    }
```

（QImage::operator== 内容比较；快照 COW 共享未变数据，撤销栈内存增量为每次增强一张 ≤1024px 图，可接受。）

- [ ] **Step 3: .cpp 匿名命名空间加算子表 + 执行器**。在 `kSliders` 表（47-60 行）之后插入：

```cpp
// 2.38.0 R1：一键增强 13 算子（组内平铺；key 与 LumenEngine 方法严格同名）
struct EnhOp { const char* key; const char* label; int group; };   // group: 0 影调 / 1 人像 / 2 校正
const EnhOp kEnhOps[] = {
    {"filmStyle",       "胶片", 0}, {"blackWhite",  "黑白", 0}, {"toneMapHdr",      "HDR",  0},
    {"structure",       "结构", 0}, {"dodge",       "减淡", 0}, {"glowStyle",       "柔光", 0},
    {"redeyeRemove",    "红眼", 1}, {"portraitBeautify", "美颜", 1},
    {"levelCorrect",    "水平", 2}, {"lensCorrect",  "镜头", 2}, {"vignetteCorrect", "暗角", 2},
    {"dehaze",          "去雾", 2}, {"chromaticCorrect", "色差", 2},
};

// 算子执行结果：img=null 表示未改动（诚实 no-op）；note 为诚实反馈（空=用默认文案）
struct EnhOutcome { QImage img; QString note; };

EnhOutcome runEnhOp(const QImage& src, const QString& key)
{
    EnhOutcome o;
    if (src.isNull()) return o;
    if (key == QStringLiteral("filmStyle"))            o.img = LumenEngine::filmStyle(src);
    else if (key == QStringLiteral("blackWhite"))      o.img = LumenEngine::blackWhite(src);
    else if (key == QStringLiteral("glowStyle"))       o.img = LumenEngine::glowStyle(src);
    else if (key == QStringLiteral("toneMapHdr"))      o.img = LumenEngine::toneMapHdr(src);
    else if (key == QStringLiteral("structure"))       o.img = LumenEngine::structure(src);
    else if (key == QStringLiteral("dodge"))           o.img = LumenEngine::dodge(src);
    else if (key == QStringLiteral("dehaze"))          o.img = LumenEngine::dehaze(src);
    else if (key == QStringLiteral("chromaticCorrect"))o.img = LumenEngine::chromaticCorrect(src);
    else if (key == QStringLiteral("lensCorrect"))     o.img = LumenEngine::lensCorrect(src);
    else if (key == QStringLiteral("redeyeRemove")) {
        int pixels = 0;
        o.img = LumenEngine::redeyeRemove(src, &pixels);
        o.note = pixels > 0
            ? QCoreApplication::translate("LightAlbum", "红眼已修 %1 点").arg(pixels)
            : QCoreApplication::translate("LightAlbum", "未检测到红眼 · 未改动");
    } else if (key == QStringLiteral("portraitBeautify")) {
        double frac = -1.0;
        o.img = LumenEngine::portraitBeautify(src, 1.0, &frac);
        if (frac >= 0.0)
            o.note = QCoreApplication::translate("LightAlbum", "美颜 · 肤色占比 %1%")
                         .arg(frac * 100.0, 0, 'f', 1);
    } else if (key == QStringLiteral("vignetteCorrect")) {
        double k = 0.0;
        o.img = LumenEngine::vignetteCorrect(src, 1.0, &k);
    } else if (key == QStringLiteral("levelCorrect")) {
        const double ang = LumenEngine::levelDetectAngle(src);
        if (ang == 0.0) {
            o.note = QCoreApplication::translate("LightAlbum", "未检测到可校正的倾斜 · 未改动");
            return o;                                   // 诚实 no-op：不追加 ops_、不进历史
        }
        o.img = LumenEngine::levelCorrect(src);
        o.note = QCoreApplication::translate("LightAlbum", "水平校正 %1°").arg(ang, 0, 'f', 2);
    }
    return o;
}
```

- [ ] **Step 4: buildEnhOps + 构造函数接线**。构造函数 page 0 调整块（382-384 行）改为：

```cpp
        auto* gl = new QGridLayout(panel);
        gl->setColumnStretch(1, 1);
        buildEnhOps(gl);                                     // 2.38.0 R1：一键增强 13 入口（三组）
        for (const RtSlider& def : kSliders) buildSlider(def, gl);
```

在 `buildSlider` 实现（770 行）之后新增：

```cpp
// 2.38.0 R1：一键增强工具区（标题 + 三组九宫格；插在滑杆行之前，buildSlider 依 rowCount 自动顺延）
void RetouchPage::buildEnhOps(QGridLayout* gl)
{
    if (!gl) return;
    int row = 0;
    auto* ht = new QLabel(QCoreApplication::translate("LightAlbum", "一键增强"), this);
    ht->setStyleSheet(QString::fromUtf8("color:%1;font-weight:bold;").arg(Lumen::hex(Lumen::INK))
                      + Lumen::Type::css(Lumen::Type::Tier::Body));
    gl->addWidget(ht, row++, 0, 1, 3);
    const QStringList kGroups = { QCoreApplication::translate("LightAlbum", "影调"),
                                  QCoreApplication::translate("LightAlbum", "人像"),
                                  QCoreApplication::translate("LightAlbum", "校正") };
    for (int g = 0; g < kGroups.size(); ++g) {
        auto* gt = new QLabel(kGroups[g], this);
        gt->setStyleSheet(QString::fromUtf8("color:%1;font-weight:bold;").arg(Lumen::hex(Lumen::MUTED))
                          + Lumen::Type::css(Lumen::Type::Tier::Small));
        gl->addWidget(gt, row++, 0, 1, 3);
        int col = 0;
        for (const EnhOp& op : kEnhOps) {
            if (op.group != g) continue;
            auto* b = new QPushButton(QString::fromUtf8(op.label), this);
            b->setObjectName(QStringLiteral("opBtn_") + QString::fromLatin1(op.key));
            b->setStyleSheet(Lumen::btnGhost(Lumen::INK));
            b->setCursor(Qt::PointingHandCursor);
            gl->addWidget(b, row, col % 3);
            if (++col % 3 == 0) ++row;
            opBtns_.append(b);
            const QString key = QString::fromLatin1(op.key);
            connect(b, &QPushButton::clicked, this, [this, key]() { onEnhOpClicked(key); });
        }
        if (col % 3 != 0) ++row;
    }
}
```

- [ ] **Step 5: onEnhOpClicked**（放 onAgentRun 之后）。加：

```cpp
// 2.38.0 R1：点击一键增强入口——异步执行（QtConcurrent）+ 写回编辑态 + 走撤销通道。
// 诚实反馈：no-op（如 levelCorrect 无倾斜）不追加 ops_、不产生历史点。
void RetouchPage::onEnhOpClicked(const QString& key)
{
    if (opBusy_ || geoSrc_.isNull()) return;
    exitAgentMode();                     // 手动动作即退出智能体模式
    clearCardSel();
    opBusy_ = true;
    const int gen = opGen_;
    for (auto* b : opBtns_) b->setEnabled(false);
    prog_->setRange(0, 0);               // 忙碌态（不确定进度；完成回落）
    badge_->setText(QCoreApplication::translate("LightAlbum", "一键增强处理中…（本地计算，零云端）"));
    const QImage src = geoImg_.isNull() ? geoSrc_ : geoImg_;
    auto* watcher = new QFutureWatcher<EnhOutcome>(this);
    connect(watcher, &QFutureWatcher<EnhOutcome>::finished, this, [this, watcher, key, gen]() {
        watcher->deleteLater();
        opBusy_ = false;
        prog_->setRange(0, 100);
        prog_->setValue(0);
        for (auto* b : opBtns_) b->setEnabled(true);
        if (gen != opGen_) return;       // 换图/重置后抵达的飞行结果：丢弃（不跨图套用）
        const EnhOutcome oc = watcher->result();
        if (oc.img.isNull()) {
            badge_->setText(oc.note.isEmpty()
                ? QCoreApplication::translate("LightAlbum", "该操作未产生改动")
                : oc.note);
            return;
        }
        enhImg_ = oc.img;
        ops_.append(key);
        rebuildGeo();                    // 几何管线基图 = enhImg_（doApply 末尾提交撤销快照）
        badge_->setText(oc.note.isEmpty()
            ? QCoreApplication::translate("LightAlbum", "一键增强 · %1 · 零云端").arg(key)
            : oc.note);                  // 诚实反馈优先于默认文案（rebuildGeo 后设置，防被 updateBadge 覆盖）
    });
    watcher->setFuture(QtConcurrent::run([src, key]() { return runEnhOp(src, key); }));
}
```

- [ ] **Step 6: rebuildGeo 基图切换**（885-887 行）：

```cpp
void RetouchPage::rebuildGeo()
{
    const QImage& base = enhImg_.isNull() ? geoSrc_ : enhImg_;   // 2.38.0 R1：几何在增强结果之上
    geoImg_ = applyGeometry(base, rot_, flipH_, flipV_, angle_, aspect_, cropNorm_);
```

- [ ] **Step 7: 状态捕获/恢复**。captureState（1233-1242）追加 `s.enh = enhImg_; s.ops = ops_;`；applyState（1244-1263）在 `angle_ = s.angle; ...cropNorm_ = s.cropNorm_;` 行后追加 `enhImg_ = s.enh; ops_ = s.ops;`（都在 `rebuildGeo()` 之前）。

- [ ] **Step 8: 换图/换帧/重置清除**。
  - loadPhoto 空图早退支（1202-1208）：在 `geoSrc_ = QImage(); geoImg_ = QImage();` 后加 `enhImg_ = QImage(); ops_.clear(); ++opGen_;`
  - loadPhoto 正常支（1211-1212）：`geoSrc_ = img;` 后加 `enhImg_ = QImage(); ops_.clear(); ++opGen_;`（在 `restoring_ = true` 窗口内）
  - onVideoFrameChanged（1180-1182）：同正常支在 `geoSrc_ = img;` 后加同三句。
  - onReset（1072-1079）：`clearCardSel();` 后加 `enhImg_ = QImage(); ops_.clear(); ++opGen_;`（重置 = 全量回基线，含增强层；doApply 末尾自然提交撤销点）。

- [ ] **Step 9: undo/redo 忙碌守卫**。onUndo（1265）与 onRedo（1273）首行加 `if (opBusy_) return;`。

- [ ] **Step 10: 导出全分辨率重放**。onExport（1091-1094）：在 `work` 缩放之后、`work = applyGeometry(...)` **之前**插入：

```cpp
    // 2.38.0 R1：一键增强序列在全分辨率上如实重放（预览所见 = 导出所得；顺序与预览一致：先增强后几何）
    for (const QString& k : ops_) {
        const EnhOutcome oc = runEnhOp(work, k);
        if (!oc.img.isNull()) work = oc.img;
    }
```

- [ ] **Step 11: 构建**。

Run: `cmake --build build --target LightAlbum -j 8`（cwd=native）

- [ ] **Step 12: 双门转绿**。

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File ./qa_runner.ps1 -Only LA_LUMEN_OPS_UI_QA`（cwd=native）
Expected: `[OK]` `LA_LUMEN_OPS_UI_QA PASS covered=13/13 files=N wiring=1 missing=[]`。

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File ./qa_runner.ps1 -Only LA_RETOUCH_A_QA`（cwd=native）
Expected: `[OK]` `LA_RETOUCH_A_QA PASS ok=1 enhOps=13`。

- [ ] **Step 13: 提交**。

```bash
git add native/src/ui/RetouchPage.h native/src/ui/RetouchPage.cpp native/src/core/RtHistory.h
git commit -m "feat(retouch): 一键增强 13 算子接线（异步+撤销集成+导出重放+诚实反馈）"
```

---

### Task 4 (R1-T4): R1 验证电池——FORCE_BROKEN 反例 + measuredMs 回填 + 名册自检

**Files:**
- Modify: `native/gate_matrix.ps1`（LA_LUMEN_OPS_UI_QA、LA_RETOUCH_A_QA 的 measuredMs 回填）
- Test: 反例臂 + `-Timing` + `gate_matrix.ps1 -SelfTest`

**Interfaces:**
- Consumes: T1-T3 全部产物。

- [ ] **Step 1: FORCE_BROKEN 接线后反例（13/14 必红）**。

Run: `$env:LA_LUMEN_OPS_UI_QA_FORCE_BROKEN="1"; powershell -NoProfile -ExecutionPolicy Bypass -File ./qa_runner.ps1 -Only LA_LUMEN_OPS_UI_QA; Remove-Item Env:LA_LUMEN_OPS_UI_QA_FORCE_BROKEN`（cwd=native）
Expected: `[FAIL]`，`covered=13/14`、missing 含 `qaPhantomOp`。

- [ ] **Step 2: measuredMs 实测回填**。

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File ./qa_runner.ps1 -Only LA_LUMEN_OPS_UI_QA,LA_RETOUCH_A_QA -Timing`（cwd=native）
Expected: 两门 `[OK]` + 各自毫秒数。把输出的毫秒值写进 `gate_matrix.ps1`：LA_LUMEN_OPS_UI_QA（当前 0）、LA_RETOUCH_A_QA（当前 2707，qaShootA 改动后重测）。**判据**：LA_LUMEN_OPS_UI_QA 若 > 500（E6 fast 预算）→ 按计划级裁决 1 执行降 full 层并更新注释（名册变 fast 18 + full 56，总数不变）。

- [ ] **Step 3: 名册自检**。

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File ./gate_matrix.ps1 -SelfTest`（cwd=native）
Expected: `LA_GATE_MATRIX_QA PASS gates=74 fast=19 full=55 ... fails=0`；E11 nonAscii=0；E12 textual==parsed==74。

- [ ] **Step 4: 提交**。

```bash
git add native/gate_matrix.ps1
git commit -m "chore(gates): R1 门实测回填（LA_LUMEN_OPS_UI_QA / LA_RETOUCH_A_QA measuredMs）"
```

---

### Task 5 (R2-T1): LA_EXIF_CONTAINER_QA 合成夹具门（实现前必红）

**Files:**
- Modify: `native/src/main.cpp`（新增 `runExifContainerQa` + fixture 构造 helpers + 分派）
- Modify: `native/gate_matrix.ps1`（表尾追加一条 full 门）
- Test: `qa_runner.ps1 -Only LA_EXIF_CONTAINER_QA`（对未实现抽取必红）

**Interfaces:**
- Consumes: `ExifReader::readExif(path)`（ExifReader.h:25；返回 `ExifData{d,tm,hasGps,la,lo,...}`；fallback 时 d=mtime 日期 "yyyy-MM-dd"）。
- Produces: 门名 `LA_EXIF_CONTAINER_QA`；env `LA_HEIC_SAMPLE`（真样本臂）；输出行 `LA_EXIF_CONTAINER_QA PASS|FAIL webp=2 heic=1 counter=2 realSample=ok|fail|skipped`；exit 0/1。

- [ ] **Step 1: 写门函数**。在 `runEraseQa` 收尾（main.cpp:4791 `}`）之后、`// 2.35.0 真模型质量门` 注释（4793）之前插入。全部字节确定（铁律 29），不依赖宿主时区/locale：

```cpp
// ---------------- 2.38.0 R2：HEIC/WebP EXIF 容器提取门（full 层） ----------------
// 合成夹具字节确定（铁律29）：(a) WebP RIFF+EXIF chunk（直 TIFF / 带 "Exif\0\0" 前缀 两变体）
// (b) 最小 HEIC ftyp+meta(iinf/infe "Exif"+iloc v0)+mdat（4 字节 BE 偏移 + 同款 TIFF blob）。
// 反例恒开：越界盒尺寸 / 自指循环子盒 -> 不崩 + 走 mtime 回退（== 文件自身 mtime 日期）。
// 真样本臂为进程内 env 判定（LA_HEIC_SAMPLE）：无值打印 skipped（本门无 need=，合成段恒跑）。
static QByteArray exifBe16(quint16 v)
{
    QByteArray b; b.append(char(v >> 8)); b.append(char(v & 0xFF)); return b;
}
static QByteArray exifBe32(quint32 v)
{
    QByteArray b;
    for (int i = 3; i >= 0; --i) b.append(char((v >> (8 * i)) & 0xFF));
    return b;
}
static QByteArray exifBox(const char* ty, const QByteArray& payload)
{
    return exifBe32(quint32(8 + payload.size())) + QByteArray(ty, 4) + payload;
}
// 最小 TIFF（LE）：IFD0{0x8769->ExifIFD, 0x8825->GPS} / ExifIFD{0x9003="2019:07:04 12:34:56"} /
// GPS{1="N", 2=(30/1,15/1,0/1), 3="E", 4=(121/1,30/1,0/1)}  —— 固定布局常量偏移。
static QByteArray exifTiffFixture()
{
    QByteArray b;
    auto u16 = [&](quint16 v){ b.append(char(v & 0xFF)); b.append(char(v >> 8)); };
    auto u32 = [&](quint32 v){ for (int i = 0; i < 4; ++i) b.append(char((v >> (8 * i)) & 0xFF)); };
    auto entry = [&](quint16 tag, quint16 ty, quint32 cnt, quint32 val){
        u16(tag); u16(ty); u32(cnt); u32(val);
    };
    b.append("II", 2); u16(42); u32(8);                       // 头：IFD0 @ 8
    u16(2);                                                   // IFD0：2 条目
    entry(0x8769, 4, 1, 38);                                  // ExifIFD @ 38
    entry(0x8825, 4, 1, 76);                                  // GPS IFD @ 76
    u32(0);                                                   // next IFD
    u16(1);                                                   // ExifIFD：1 条目
    entry(0x9003, 2, 20, 56);                                 // DateTimeOriginal @ 56
    u32(0);
    b.append("2019:07:04 12:34:56\0", 20);                    // 56..75
    u16(4);                                                   // GPS IFD：4 条目
    entry(1, 2, 2, quint32('N'));                             // 内联 "N\0"
    entry(2, 5, 3, 130);                                      // lat RATIONAL x3 @ 130
    entry(3, 2, 2, quint32('E'));                             // 内联 "E\0"
    entry(4, 5, 3, 154);                                      // lon RATIONAL x3 @ 154
    u32(0);
    const quint32 lats[6] = { 30, 1, 15, 1, 0, 1 };
    for (quint32 v : lats) u32(v);                            // 130..153
    const quint32 lons[6] = { 121, 1, 30, 1, 0, 1 };
    for (quint32 v : lons) u32(v);                            // 154..177
    return b;                                                 // 全长 178
}
static QByteArray exifWebpFixture(bool withPrefix)
{
    QByteArray payload = withPrefix ? QByteArray("Exif\0\0", 6) : QByteArray();
    payload += exifTiffFixture();
    QByteArray chunk = QByteArray("EXIF", 4);
    const quint32 len = quint32(payload.size());
    for (int i = 0; i < 4; ++i) chunk.append(char((len >> (8 * i)) & 0xFF));   // RIFF 小端
    chunk += payload;
    if (len & 1u) chunk.append('\0');                         // chunk 2 字节对齐
    QByteArray body = QByteArray("WEBP", 4) + chunk;
    QByteArray f = QByteArray("RIFF", 4);
    const quint32 sz = quint32(body.size());
    for (int i = 0; i < 4; ++i) f.append(char((sz >> (8 * i)) & 0xFF));
    f += body;
    return f;
}
static QByteArray exifHeicFixture()
{
    const QByteArray tiff = exifTiffFixture();
    QByteArray mdatPayload(4, '\0');                          // 前 4 字节 BE：到 TIFF 头偏移 = 0
    mdatPayload += tiff;
    QByteArray infeBody;                                      // infe v2: item_ID=1, type "Exif"
    infeBody.append(char(2)).append(3, '\0');
    infeBody += exifBe16(1); infeBody += exifBe16(0);
    infeBody.append("Exif", 4); infeBody.append('\0');
    QByteArray iinfBody; iinfBody.append(4, '\0'); iinfBody += exifBe16(1);
    iinfBody += exifBox("infe", infeBody);
    QByteArray ilocBody; ilocBody.append(4, '\0');            // iloc v0
    ilocBody.append(char(0x44)).append(char(0x00));           // 4/4 尺寸；base 0 / index 0
    ilocBody += exifBe16(1);                                  // 1 item
    ilocBody += exifBe16(1); ilocBody += exifBe16(0); ilocBody += exifBe16(1);  // id/dref/extCnt
    const int offFieldAt = ilocBody.size();                   // extent offset 占位位置（体内）
    ilocBody += exifBe32(0);                                  // 占位：绝对偏移后补
    ilocBody += exifBe32(quint32(mdatPayload.size()));        // extent length
    QByteArray metaBody; metaBody.append(4, '\0');
    metaBody += exifBox("iinf", iinfBody);
    metaBody += exifBox("iloc", ilocBody);
    QByteArray ftypBody; ftypBody.append("heic", 4);
    ftypBody.append(4, '\0'); ftypBody.append("mif1", 4);
    const QByteArray ftyp = exifBox("ftyp", ftypBody);
    const QByteArray meta = exifBox("meta", metaBody);
    const QByteArray mdat = exifBox("mdat", mdatPayload);
    QByteArray file = ftyp + meta + mdat;
    const quint32 abs = quint32(ftyp.size() + meta.size() + 8);   // mdat payload 绝对偏移
    const int absInFile = ftyp.size() + 8 + 4 + exifBox("iinf", iinfBody).size() + 8 + offFieldAt;
    for (int i = 0; i < 4; ++i) file[absInFile + i] = char((abs >> (8 * (3 - i))) & 0xFF);
    return file;
}
static bool exifWriteFile(const QString& path, const QByteArray& data)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    return f.write(data) == data.size();
}

int runExifContainerQa()
{
    QStringList fails;
    QTemporaryDir tmp;
    if (!tmp.isValid()) { std::fprintf(stderr, "LA_EXIF_CONTAINER_QA FAIL tmpdir\n"); return 1; }
    const auto check = [&](bool ok, const QString& what) {
        if (!ok) fails << what;
    };
    const QString url = [&](const QString& name){ return tmp.filePath(name); };

    // ---- 正例：WebP 两变体 + HEIC（期望值 = 夹具写死值，铁律22：不随任何开关变）----
    const QString f1 = url(QStringLiteral("a.webp"));
    const QString f2 = url(QStringLiteral("b.webp"));
    const QString f3 = url(QStringLiteral("c.heic"));
    if (!exifWriteFile(f1, exifWebpFixture(false))
        || !exifWriteFile(f2, exifWebpFixture(true))
        || !exifWriteFile(f3, exifHeicFixture())) {
        std::fprintf(stderr, "LA_EXIF_CONTAINER_QA FAIL fixture write\n");
        return 1;
    }
    for (const QString& p : { f1, f2, f3 }) {
        const ExifData e = ExifReader::readExif(p);
        check(e.d == QStringLiteral("2019-07-04"), QStringLiteral("date %1 got=%2").arg(p, e.d));
        check(e.tm == QStringLiteral("12:34"), QStringLiteral("time %1 got=%2").arg(p, e.tm));
        check(e.hasGps, QStringLiteral("gps %1").arg(p));
        check(qAbs(e.la - 30.25) < 1e-4, QStringLiteral("lat %1 got=%2").arg(p).arg(e.la));
        check(qAbs(e.lo - 121.5) < 1e-4, QStringLiteral("lon %1 got=%2").arg(p).arg(e.lo));
    }

    // ---- 反例（恒开）：越界盒尺寸 / 自指循环子盒 -> 不崩 + mtime 回退 ----
    {
        QByteArray broken = exifHeicFixture();
        for (int i = 0; i < 4; ++i) broken[i] = char(0x7F);          // 顶层盒尺寸 0x7FFFFFFF 越界
        const QString bp = url(QStringLiteral("bad1.heic"));
        exifWriteFile(bp, broken);
        const ExifData e = ExifReader::readExif(bp);
        const QString mdate = QFileInfo(bp).lastModified().date().toString(QStringLiteral("yyyy-MM-dd"));
        check(e.d == mdate, QStringLiteral("counter out-of-range: fallback got=%1 expect=%2").arg(e.d, mdate));
    }
    {
        QByteArray loop = exifHeicFixture();
        // iinf 盒位于 meta 内：ftyp + meta 头 8 + FullBox 4 之后；把它的 size 改为覆盖 meta 外
        const int iinfSizeAt = exifBox("ftyp", QByteArray("heic",4) + QByteArray(4,'\0') + QByteArray("mif1",4)).size() + 8 + 4;
        for (int i = 0; i < 4; ++i) loop[iinfSizeAt + i] = char(0x7F);  // 子盒越界/自指 -> 放弃
        const QString bp = url(QStringLiteral("bad2.heic"));
        exifWriteFile(bp, loop);
        const ExifData e = ExifReader::readExif(bp);
        const QString mdate = QFileInfo(bp).lastModified().date().toString(QStringLiteral("yyyy-MM-dd"));
        check(e.d == mdate, QStringLiteral("counter self-loop: fallback got=%1 expect=%2").arg(e.d, mdate));
    }

    // ---- 真样本臂：进程内 env 判定（无 need=；本地路径值不入仓）----
    QString realLine = QStringLiteral("skipped");
    const QByteArray sample = qgetenv("LA_HEIC_SAMPLE");
    if (!sample.isEmpty()) {
        const ExifData sd = ExifReader::readExif(QString::fromLocal8Bit(sample));
        if (sd.d.isEmpty()) { fails << QStringLiteral("realSample no date parsed"); realLine = QStringLiteral("fail"); }
        else realLine = QStringLiteral("ok");
    }

    if (fails.isEmpty())
        std::fprintf(stderr, "LA_EXIF_CONTAINER_QA PASS webp=2 heic=1 counter=2 realSample=%s\n",
                     qPrintable(realLine));
    else
        std::fprintf(stderr, "LA_EXIF_CONTAINER_QA FAIL reason=%s\n", qPrintable(fails.join(QChar(';'))));
    std::fflush(stderr);
    return fails.isEmpty() ? 0 : 1;
}
```

注意：本门无 FORCE_BROKEN 钩子（反例是恒开的损坏夹具，不是注入开关；铁律 34 由反例臂本身满足）。若 main.cpp 缺 include 则补 `<QTemporaryDir>`、`<QFile>`、`<QFileInfo>`、`<ExifReader.h>`（`#include "core/ExifReader.h"` 按现有写法）。

- [ ] **Step 2: 挂分派**。在 LA_RETOUCH_OP_QA / LA_LUMEN_OPS_UI_QA 分派块之后插入：

```cpp
    // 2.38.0 R2：HEIC/WebP EXIF 容器提取门（合成夹具 + 恒开反例；实现前必红）
    if (qEnvironmentVariableIsSet("LA_EXIF_CONTAINER_QA"))
        return runExifContainerQa();
```

- [ ] **Step 3: 注册名册**。`gate_matrix.ps1` 表尾、LA_LUMEN_OPS_UI_QA 条目之后追加：

```powershell
  # 2.38.0 R2: HEIC/WebP EXIF container extraction gate (full tier). Byte-exact synthetic
  # fixtures (WebP RIFF/EXIF x2 variants, minimal HEIC ftyp/meta/iinf/iloc/mdat) with fixed
  # expectations, plus always-on counter-examples: out-of-range box size and a self-
  # referential child box must not crash and must fall back to the file's own mtime date.
  # Real-sample arm is env-gated INSIDE the process (LA_HEIC_SAMPLE); the gate carries no
  # need= by design (plan-level ruling 2), so the synthetic part always runs. measuredMs=0
  # placeholder; E8 requires the key, rules 25/31 the -Timing value (backfilled in R2-T2).
  @{n="LA_EXIF_CONTAINER_QA"; png=$false; measuredMs=0},
```

- [ ] **Step 4: 构建**。

Run: `cmake --build build --target LightAlbum -j 8`（cwd=native）

- [ ] **Step 5: RED 实证（实现前必红）**。

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File ./qa_runner.ps1 -Only LA_EXIF_CONTAINER_QA`（cwd=native）
Expected: `[FAIL]`，`date a.webp got=<mtime 日期>`（WebP/HEIC 均回落 mtime，≠ 2019-07-04；反例两臂反而通过——它们期望的就是回退）。

- [ ] **Step 6: 提交**。

```bash
git add native/src/main.cpp native/gate_matrix.ps1
git commit -m "feat(gates): LA_EXIF_CONTAINER_QA 合成夹具门（WebP/HEIC/反例；实现前必红）"
```

---

### Task 6 (R2-T2): extractWebpExif / extractHeicExif 实现 + 分派（转绿）

**Files:**
- Modify: `native/src/core/ExifReader.cpp`（两抽取函数 + 分派分支）
- Modify: `native/src/core/ExifReader.h`（头注释订正：删「WebP/HEIC/RAW 不解析」过时措辞）
- Test: `qa_runner.ps1 -Only LA_EXIF_CONTAINER_QA`（转绿）+ measuredMs 回填

**Interfaces:**
- Produces: `extractWebpExif(QByteArray)->QByteArray`、`extractHeicExif(QByteArray)->QByteArray`（匿名命名空间内，与 extractJpegExif 同级）；失败一律返回空 → 上层 `fillMtimeFallback` 诚实降级。

- [ ] **Step 1: 两个抽取函数**。加在 `extractPngExif`（ExifReader.cpp:169 `}` 之后、`// ---------------- 语义字段` 注释之前）：

```cpp
// ---------------- WebP（RIFF）EXIF chunk 提取（2.38.0 R2） ----------------
QByteArray extractWebpExif(const QByteArray& f)
{
    if (f.size() < 12 || std::memcmp(f.constData(), "RIFF", 4) != 0
        || std::memcmp(f.constData() + 8, "WEBP", 4) != 0) return {};
    qsizetype off = 12;
    while (off + 8 <= f.size()) {
        const quint32 len = quint32(uchar(f[off + 4])) | (quint32(uchar(f[off + 5])) << 8)
                          | (quint32(uchar(f[off + 6])) << 16) | (quint32(uchar(f[off + 7])) << 24);
        if (len > quint32(f.size())) return {};               // 防呆：尺寸越界即放弃
        if (std::memcmp(f.constData() + off, "EXIF", 4) == 0) {
            QByteArray payload = f.mid(off + 8, qsizetype(len));
            if (payload.size() >= 6 && std::memcmp(payload.constData(), "Exif\0\0", 6) == 0)
                payload.remove(0, 6);                          // 兼容带 "Exif\0\0" 前缀的写入器
            return payload;
        }
        off += 8 + qsizetype(len) + (len & 1u);                // chunk 2 字节对齐
    }
    return {};
}

// ---------------- HEIC（ISOBMFF）EXIF 提取：meta/iinf/iloc 最小 walker（2.38.0 R2） ----------------
// 防呆：盒尺寸越界 / 子盒超出父盒 / extent 越界 -> 返回空 -> 上层走 mtime 回退（诚实降级）。
quint64 exifBe(const uchar* p, int n)
{
    quint64 v = 0;
    for (int i = 0; i < n; ++i) v = (v << 8) | p[i];
    return v;
}

QByteArray extractHeicExif(const QByteArray& f)
{
    const uchar* d = reinterpret_cast<const uchar*>(f.constData());
    const qint64 n = f.size();
    qint64 metaOff = -1, metaSize = 0, off = 0;
    while (off + 8 <= n) {                                     // 顶层盒扫描
        qint64 size = qint64(exifBe(d + off, 4));
        const char* ty = reinterpret_cast<const char*>(d + off + 4);
        qint64 hdr = 8;
        if (size == 1) {                                       // 64 位 largesize
            if (off + 16 > n) return {};
            size = qint64(exifBe(d + off + 8, 8));
            hdr = 16;
        } else if (size == 0) {
            size = n - off;                                    // 到文件尾
        }
        if (size < hdr || off + size > n) return {};           // 防呆：越界即放弃
        if (std::memcmp(ty, "meta", 4) == 0) { metaOff = off; metaSize = size; break; }
        off += size;
    }
    if (metaOff < 0) return {};
    qint64 p = metaOff + 8 + 4;                                // meta FullBox：version/flags 4 字节
    const qint64 metaEnd = metaOff + metaSize;
    qint64 exifItemId = -1, ilocOff = -1, ilocSize = 0;
    while (p + 8 <= metaEnd) {                                 // 子盒 walker（限于 meta 内）
        const qint64 size = qint64(exifBe(d + p, 4));
        const char* ty = reinterpret_cast<const char*>(d + p + 4);
        if (size < 8 || p + size > metaEnd) break;             // 防呆：越界/自指 -> 放弃
        if (std::memcmp(ty, "iinf", 4) == 0) {
            const qint64 iinfEnd = p + size;
            const int ver = d[p + 8];                          // iinf FullBox version
            qint64 q = p + 8 + 4;
            if (q > iinfEnd) return {};
            q += (ver == 0) ? 2 : 4;                           // entry_count（v0: u16 / v1+: u32）
            while (q + 8 <= iinfEnd) {                         // infe 条目扫描
                const qint64 isz = qint64(exifBe(d + q, 4));
                const char* ity = reinterpret_cast<const char*>(d + q + 4);
                if (isz < 8 || q + isz > iinfEnd) break;
                if (std::memcmp(ity, "infe", 4) == 0) {
                    const int iver = d[q + 8];                 // infe FullBox version
                    qint64 r = q + 8 + 4;
                    qint64 itemId = 0;
                    if (iver == 2 && r + 8 <= q + isz) { itemId = qint64(exifBe(d + r, 2)); r += 4; }
                    else if (iver >= 3 && r + 10 <= q + isz) { itemId = qint64(exifBe(d + r, 4)); r += 6; }
                    else { q += isz; continue; }
                    if (r + 4 <= q + isz
                        && std::memcmp(reinterpret_cast<const char*>(d + r), "Exif", 4) == 0)
                        exifItemId = itemId;
                }
                q += isz;
            }
        } else if (std::memcmp(ty, "iloc", 4) == 0) {
            ilocOff = p; ilocSize = size;
        }
        p += size;
    }
    if (exifItemId < 0 || ilocOff < 0) return {};
    const qint64 ilocEnd = ilocOff + ilocSize;
    const int iver = d[ilocOff + 8];
    qint64 q = ilocOff + 8 + 4;
    const int b1 = d[q], b2 = d[q + 1]; q += 2;
    const int offSize = b1 >> 4, lenSize = b1 & 0xF, baseOffSize = b2 >> 4, indexSize = b2 & 0xF;
    if ((offSize != 4 && offSize != 8) || (lenSize != 4 && lenSize != 8)) return {};
    qint64 itemCount = qint64(exifBe(d + q, iver == 2 ? 4 : 2));
    q += (iver == 2) ? 4 : 2;
    for (qint64 i = 0; i < itemCount; ++i) {
        if (q + 2 > ilocEnd) return {};
        const qint64 itemId = (iver < 2) ? qint64(exifBe(d + q, 2)) : qint64(exifBe(d + q, 4));
        q += (iver < 2) ? 2 : 4;
        int cm = 0;
        if (iver >= 1) { if (q + 2 > ilocEnd) return {}; cm = int(exifBe(d + q, 2)) & 0xF; q += 2; }
        q += 2;                                                // data_reference_index
        qint64 base = 0;
        if (baseOffSize > 0) { if (q + baseOffSize > ilocEnd) return {}; base = qint64(exifBe(d + q, baseOffSize)); }
        q += baseOffSize;
        if (q + 2 > ilocEnd) return {};
        const qint64 extCount = qint64(exifBe(d + q, 2)); q += 2;
        for (qint64 e = 0; e < extCount; ++e) {
            q += indexSize;                                    // extent_index（v1/2 且 index_size>0；v0 恒 0）
            if (q + offSize + lenSize > ilocEnd) return {};
            const qint64 extOff = qint64(exifBe(d + q, offSize)); q += offSize;
            const qint64 extLen = qint64(exifBe(d + q, lenSize)); q += lenSize;
            if (itemId == exifItemId && cm == 0 && e == 0) {   // construction_method 0 = 文件内偏移
                if (extOff <= 0 || extLen <= 0 || base + extOff + extLen > n) return {};
                const QByteArray payload = f.mid(qsizetype(base + extOff), qsizetype(extLen));
                if (payload.size() < 4) return {};
                const qint64 ti = qint64(exifBe(reinterpret_cast<const uchar*>(payload.constData()), 4));
                const qint64 start = 4 + ti;                   // payload 前 4 字节 = 到 TIFF 头偏移
                if (start < 0 || start > payload.size()) return {};
                return payload.mid(qsizetype(start));
            }
        }
    }
    return {};
}
```

- [ ] **Step 2: 分派分支**。readExif 的签名识别块（ExifReader.cpp:278-296）：在 `tiffSig` 定义之后加两签名：

```cpp
        const bool webpSig = head.size() >= 12
            && std::memcmp(head.constData(), "RIFF", 4) == 0
            && std::memcmp(head.constData() + 8, "WEBP", 4) == 0;
        const bool heicSig = head.size() >= 8
            && std::memcmp(head.constData() + 4, "ftyp", 4) == 0;
```

并在 `} else if (pngSig) { ... }` 之后、`tiffSig` 分支之前插入：

```cpp
        } else if (webpSig) {
            f.seek(0);
            QByteArray all = (f.size() < qint64(8) * 1024 * 1024)
                ? f.readAll() : f.read(qint64(8) * 1024 * 1024);
            blob = extractWebpExif(all);
        } else if (heicSig && f.size() < qint64(64) * 1024 * 1024) {
            f.seek(0);
            blob = extractHeicExif(f.readAll());               // 盒链读取总量上限 64MB
```

- [ ] **Step 3: 头注释订正**。ExifReader.h 第 3-5 行「其余格式（WebP/HEIC/RAW）不解析」改为：

```cpp
// 零第三方依赖：JPEG 走 APP1("Exif\0\0")，TIFF/DNG 整文件即 TIFF 头，
// PNG 走 eXIf chunk；WebP 走 RIFF EXIF chunk、HEIC 走 ISOBMFF meta/iinf/iloc
// （2.38.0 新增，容器→TIFF blob 一层）；RAW 仍不解析。任何一步失败返回兜底值，绝不抛异常。
```

- [ ] **Step 4: 构建**。

Run: `cmake --build build --target LightAlbum -j 8`（cwd=native）

- [ ] **Step 5: 转绿 + 零回归**。

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File ./qa_runner.ps1 -Only LA_EXIF_CONTAINER_QA`（cwd=native）
Expected: `[OK]` `LA_EXIF_CONTAINER_QA PASS webp=2 heic=1 counter=2 realSample=skipped`。

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File ./qa_runner.ps1 -Only LA_SCAN_SMOKE,LA_RAW_QA,LA_EDIT_RAW_QA`（cwd=native；PNG/JPEG/TIFF 既有解析回归）
Expected: 全 `[OK]`（分派只增分支，三格式行为不变）。

- [ ] **Step 6: measuredMs 回填**。

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File ./qa_runner.ps1 -Only LA_EXIF_CONTAINER_QA -Timing`（cwd=native）
把毫秒写回 gate_matrix.ps1 的 LA_EXIF_CONTAINER_QA 条目。

- [ ] **Step 7: 提交**。

```bash
git add native/src/core/ExifReader.cpp native/src/core/ExifReader.h native/gate_matrix.ps1
git commit -m "feat(exif): HEIC/WebP 容器 EXIF 抽取（ISOBMFF box walker + RIFF chunk；失败走 mtime 回退）"
```

---

### Task 7 (R3-T1): LA_ERASE_QA G1 淘汰断言（对当前实现必红）

**Files:**
- Modify: `native/src/core/EraseBridge.h`（QA 访问器 `qaTaskCount()`）
- Modify: `native/src/main.cpp`（runEraseQa 增 G1 组，插在 A 补 blocked 块之后、复位区之前）
- Test: `qa_runner.ps1 -Only LA_ERASE_QA`（G1 必红）

**Interfaces:**
- Produces: `EraseBridge::qaTaskCount() const -> int`（QA 断言用；先例 qaKillExecutorForTest/qaPeekRunnerPid）。
- Produces: runEraseQa 输出行末尾将追加 `evict=0|1 cancel=0|1`（T8/T9 转绿后为 1）。

- [ ] **Step 1: 访问器**。EraseBridge.h 公有区（`void qaPeekRunnerPid(qint64& pid) const;` 附近）加：

```cpp
    int qaTaskCount() const { return tasks_.size(); }   // 2.38.0 R3：G1 淘汰断言用
```

- [ ] **Step 2: G1 组**。runEraseQa 内、`EraseRunner::qaSetForceNoExecutor(false);`（4777 行）之后、`EraseRunner::qaInjectBadSize(false);` 复位区（4779）之前插入：

```cpp
    // ---- G1 淘汰（2.38.0 R3）：连续 9 个任务后任务表保留最近 kTasksKeep=8，
    //      最旧已不可见、最新仍在。共享 bridge 的执行器 DELAY=220 已在顶部设定，
    //      串行等待终态即可（无随机、无时序依赖，铁律29）。
    {
        int lastId = -1;
        bool allTerminal = true;
        for (int i = 0; i < 9; ++i) {
            lastId = bridge.submit(base, mask);
            allTerminal = eraseWaitUntil([&]{ const EraseTask* t = bridge.task(lastId);
                return t && (t->status == QLatin1String("done") || t->status == QLatin1String("failed")); }, 8000);
            if (!allTerminal) break;
        }
        note(allTerminal, QStringLiteral("G1 all 9 tasks reached terminal"));
        note(bridge.qaTaskCount() <= 8,
             QStringLiteral("G1 task table capped at 8, got=%1").arg(bridge.qaTaskCount()));
        note(bridge.task(1) == nullptr,
             QStringLiteral("G1 oldest task evicted (id=1)"));
        note(bridge.task(lastId) != nullptr,
             QStringLiteral("G1 newest task still visible (id=%1)").arg(lastId));
    }
```

注意：此处假定 runEraseQa 顶部第一个提交的 id 恒为 1（bridge 是 runEraseQa 内首个 EraseBridge、`nextId_` 从 1 起）；A 组在 G1 之前已提交 id 1/2/3/4 等——**实现者须先核对**该函数里 G1 之前 bridge 的提交序列（A/B/C/D/E 各组），把「最旧」断言改为对 G1 组自身第一个 id 的引用（局部列表 `QVector<int> g1ids` 记录 9 个 id，断言 `bridge.task(g1ids[0]) == nullptr && bridge.task(g1ids[8]) != nullptr`），而非硬编码 id=1——这是本条的关键正确性细节。

- [ ] **Step 3: 构建 + RED 实证**。

Run: `cmake --build build --target LightAlbum -j 8`（cwd=native）
Run: `powershell -NoProfile -ExecutionPolicy Bypass -File ./qa_runner.ps1 -Only LA_ERASE_QA`（cwd=native；先确认 trio 不存在）
Expected: `[FAIL]`，G1 行 `task table capped at 8, got=>8`（当前 tasks_ 无移除）。

- [ ] **Step 4: 提交**。

```bash
git add native/src/core/EraseBridge.h native/src/main.cpp
git commit -m "test(erase): LA_ERASE_QA G1 淘汰断言（对无淘汰实现必红）"
```

---

### Task 8 (R3-T2): 淘汰实现 + 陈旧注释订正（G1 转绿）

**Files:**
- Modify: `native/src/core/EraseBridge.h`（kTasksKeep 声明区 + 注释订正）
- Modify: `native/src/core/EraseBridge.cpp`（pruneTasks + submit 尾调用 + status 注释补 cancelled）
- Modify: `native/src/core/FaceCluster.h`（6-7 行注释订正）
- Test: `LA_ERASE_QA` G1 转绿 + `LA_ERASE_QA_FORCE_NO_EVICT=1` 反例必红

**Interfaces:**
- Produces: 淘汰语义——`submit()` 尾部修剪「最旧且已终态」前缀直到 `size <= kTasksKeep`；`running` 永不淘汰（spec §4.2）；env `LA_ERASE_QA_FORCE_NO_EVICT=1` 关停（反例钩子）。

- [ ] **Step 1: 淘汰实现**。EraseBridge.cpp：

```cpp
// 2.38.0 R3：任务表淘汰——保留最近 kTasksKeep 个；仅修剪「前缀中已终态」的最旧项，
// running/queued 永不淘汰（spec §4.2）。终态 = done/failed/cancelled/blocked_no_model。
static constexpr int kTasksKeep = 8;

static bool isTerminalStatus(const QString& s)
{
    return s == QLatin1String("done") || s == QLatin1String("failed")
        || s == QLatin1String("cancelled") || s == QLatin1String("blocked_no_model");
}

void EraseBridge::pruneTasks()
{
    if (!qEnvironmentVariableIsEmpty("LA_ERASE_QA_FORCE_NO_EVICT")) return;   // QA 反例钩子
    while (tasks_.size() > kTasksKeep && isTerminalStatus(tasks_.first().status))
        tasks_.removeFirst();                                                   // 淘汰即释放 QImage
}
```

submit() 尾部（`return t.id;` 前）加 `pruneTasks();`。EraseBridge.h 声明：`void pruneTasks();`（私有区）。

- [ ] **Step 2: status 注释补全**。EraseBridge.h 第 19 行 `// queued / running / blocked_no_model / done / failed` 改为 `// queued / running / blocked_no_model / done / failed / cancelled`。

- [ ] **Step 3: EraseBridge.h 顶部陈旧注释订正**。第 2-9 行整体替换为（与实现一致的现状描述）：

```cpp
// P4-4 AI 消除桥：蒙版 → 任务表（FIFO）→ 结果回填。2.35.0 起接入真引擎
// （inpaint.exe 子进程帧协议，stub 为 MinGW 开发替身）；2.36.0 起任务经专属
// QThread 上的 EraseRunner 异步执行、GUI 永不阻塞；2.38.0 增任务淘汰（保留最近
// kTasksKeep=8，running 永不淘汰）与取消（kill 子进程 + 复位，下次提交冷启动）。
// 零云端铁律：本桥仅接受**本地**模型执行器接入；无执行器时任务诚实挂起
// （status=blocked_no_model），绝不伪造「消除成功」的结果。
// 线程契约：tasks_ 表由 GUI 线程独占（AiWriter 单写者先例）；对 GUI 侧调用方
// （RetouchPage 等）API 保持稳定；终态经 queued 信号回 GUI 线程回填。
```

- [ ] **Step 4: FaceCluster.h 注释订正**。第 6-7 行（`// 算法：贪心单连锁——按序遍历，对每个 embedding 找「代表脸（群首 emb）余弦相似度 / >= threshold」的已有簇，命中则并入，否则新建簇。代表脸取群首。`）替换为：

```cpp
// 算法：两趟精炼（2.35.0）——贪心种子连锁建初簇后，按簇质心做 <=3 轮重分配
//       （逐 embedding 就近并入，质心每轮更新），直至稳定或轮数上限；代表脸=最终质心。
```

- [ ] **Step 5: 构建 + 转绿 + 反例**。

Run: `cmake --build build --target LightAlbum -j 8`（cwd=native）
Run: `powershell -NoProfile -ExecutionPolicy Bypass -File ./qa_runner.ps1 -Only LA_ERASE_QA`（cwd=native）
Expected: `[OK]`，G1 四行全过。
Run: `$env:LA_ERASE_QA_FORCE_NO_EVICT="1"; powershell ... -Only LA_ERASE_QA; Remove-Item Env:LA_ERASE_QA_FORCE_NO_EVICT`（cwd=native）
Expected: `[FAIL]` G1 capped 断言转红（got=13+，即 G1 前既有任务+9 全留）。

- [ ] **Step 6: 提交**。

```bash
git add native/src/core/EraseBridge.h native/src/core/EraseBridge.cpp native/src/core/FaceCluster.h
git commit -m "feat(erase): 任务表淘汰（kTasksKeep=8, running 永不淘汰）+ 陈旧注释订正"
```

---

### Task 9 (R3-T3): cancel 内核（EraseRunner 分片等待 + EraseBridge.cancel + G2 断言）

**Files:**
- Modify: `native/src/core/EraseRunner.{h,cpp}`（requestCancel + cancelTarget_ + run() 分片循环）
- Modify: `native/src/core/EraseBridge.{h,cpp}`（cancel + onTaskFinished 守卫）
- Modify: `native/src/main.cpp`（runEraseQa 增 G2 组）
- Test: `LA_ERASE_QA`（先 RED 后 GREEN）+ `LA_ERASE_QA_FORCE_IGNORE_CANCEL=1` 必红

**Interfaces:**
- Produces: `EraseRunner::requestCancel(quint32 id)`（任意线程可调——只写 atomic，先例 `processId()` GUI 侧读）；`EraseBridge::cancel(int taskId) -> bool`；env `LA_ERASE_QA_FORCE_IGNORE_CANCEL=1`；终态 `cancelled`（note=「已取消 · 执行器正在复位（下次提交冷启动）」）。

- [ ] **Step 1: RED——先写 no-op cancel + G2 断言**。
  - EraseBridge.h 公有区加 `bool cancel(int taskId);`；.cpp 加最小体：

```cpp
bool EraseBridge::cancel(int taskId)
{
    Q_UNUSED(taskId);
    return false;   // RED 骨架：先证断言咬得动，下一步换真实现
}
```
  - main.cpp runEraseQa 的 G1 组之后插入 G2 组：

```cpp
    // ---- G2 取消（2.38.0 R3）：飞行中 cancel -> 态 cancelled + 子进程终止 +
    //      复位后新任务仍能成功。专属桥 + 专属执行器：DELAY=1500 保证取消必然
    //      落在飞行中（确定性；stub 冷启动为毫秒级，真模型 ~8s 是已文档化代价）。
    qputenv("LA_ERASE_STUB_DELAY_MS", "1500");
    {
        EraseBridge cancelBridge;
        const int cid = cancelBridge.submit(base, mask);
        bool runningSeen = eraseWaitUntil([&]{ const EraseTask* t = cancelBridge.task(cid);
            return t && t->status == QLatin1String("running"); }, 3000);
        note(runningSeen, QStringLiteral("G2 task observed running before cancel"));
        qint64 pidBefore = 0;
        eraseWaitUntil([&]{ cancelBridge.qaPeekRunnerPid(pidBefore); return pidBefore != 0; }, 5000);
        const bool accepted = cancelBridge.cancel(cid);
        const EraseTask* ct = cancelBridge.task(cid);
        note(accepted && ct && ct->status == QLatin1String("cancelled"),
             QStringLiteral("G2 cancel accepted -> cancelled, got=%1").arg(ct ? ct->status : QStringLiteral("?")));
        qint64 pidAfter = pidBefore;
        eraseWaitUntil([&]{ cancelBridge.qaPeekRunnerPid(pidAfter); return pidAfter == 0; }, 5000);
        note(pidAfter == 0, QStringLiteral("G2 runner child terminated pidBefore=%1 pidAfter=%2")
                                .arg(pidBefore).arg(pidAfter));
        // 状态定格：worker 的 taskFinished 回调抵达后不得把 cancelled 覆写成 failed/done
        const bool overwritten = eraseWaitUntil([&]{
            const EraseTask* t = cancelBridge.task(cid);
            return !t || t->status != QLatin1String("cancelled");
        }, 500);
        note(!overwritten, QStringLiteral("G2 cancelled status survives worker callback"));
        const int nid = cancelBridge.submit(base, mask);
        const bool nf = eraseWaitUntil([&]{ const EraseTask* t = cancelBridge.task(nid);
            return t && (t->status == QLatin1String("done") || t->status == QLatin1String("failed")); }, 8000);
        const EraseTask* nt = cancelBridge.task(nid);
        note(nf && nt && nt->status == QLatin1String("done"),
             QStringLiteral("G2 post-cancel recovery: next task done, got=%1").arg(nt ? nt->status : QStringLiteral("?")));
    }
    qputenv("LA_ERASE_STUB_DELAY_MS", QByteArray());   // 桩延迟复位（既有纪律）
```

  注意：G2 专属执行器与共享 bridge 的执行器并存（独立进程，无冲突）；G2 结束后共享 bridge 的执行器仍按 220ms 老配置存活，不受影响。DELAY 复位保持既有纪律区（4780-4782）不变。
  - 构建 + 跑门 → **RED**：`G2 cancel accepted -> cancelled, got=running`（no-op cancel 拒绝受理）。

- [ ] **Step 2: EraseRunner 实现**。EraseRunner.h：公有区 `void requestCancel(quint32 id);`（注释：「任意线程安全：仅写 atomic；owner 线程 run() 分片观测」）；私有 `std::atomic<quint32> cancelTarget_{0};`。.cpp 加 `#include <QElapsedTimer>` 与：

```cpp
void EraseRunner::requestCancel(quint32 id) { cancelTarget_.store(id); }
```

run()（115-125 行）整体替换为：

```cpp
bool EraseRunner::run(quint32 id, const QImage& base, const QImage& mask, QImage& out, QString* err)
{
    // 2.38.0 R3：取消预检（排到队首前 / 冷启动握手后各一次），命中即中止。
    if (cancelTarget_.load() == id) {
        cancelTarget_.store(0);
        if (err) *err = QStringLiteral("已取消");
        return false;
    }
    if (!ensureStarted()) { if (err) *err = QStringLiteral("executor unavailable"); return false; }
    if (cancelTarget_.load() == id) {
        cancelTarget_.store(0);
        if (err) *err = QStringLiteral("已取消");
        qaKillChild();
        return false;
    }
    if (!LaEraseProto::writeRequest(proc_, id, base, mask, err)) return false;
    // 分片等待（100ms 粒度观测取消；总预算 20000 不变）。子进程死亡 -> 交回失败路径。
    QElapsedTimer timer; timer.start();
    bool ready = false;
    while (timer.elapsed() < 20000) {
        if (cancelTarget_.load() == id) {
            cancelTarget_.store(0);
            if (err) *err = QStringLiteral("已取消");
            qaKillChild();                                 // 杀子进程 + 复位（下次 submit 冷启动）
            return false;
        }
        if (proc_->state() != QProcess::Running) break;
        if (proc_->waitForReadyRead(100)) { ready = true; break; }
    }
    if (!ready) { if (err) *err = QStringLiteral("timeout"); qaKillChild(); return false; }
    if (!LaEraseProto::readResponse(proc_, id, out, err)) { qaKillChild(); return false; }
    // QA 反例：越界 copy → null QImage → applyResult 诚实拒收（期望值不动，铁律22）。
    if (g_bad.load() && !out.isNull()) out = out.copy(0, 0, out.width() + 1, out.height());
    return true;
}
```

- [ ] **Step 3: EraseBridge.cancel 真实现 + 回调守卫**。.cpp 替换 Step 1 的骨架：

```cpp
bool EraseBridge::cancel(int taskId)
{
    if (!qEnvironmentVariableIsEmpty("LA_ERASE_QA_FORCE_IGNORE_CANCEL")) return true;   // QA 反例钩子：忽略取消
    for (EraseTask& t : tasks_) {
        if (t.id != taskId) continue;
        if (t.status != QLatin1String("queued") && t.status != QLatin1String("running"))
            return false;                                  // 终态任务拒绝受理
        t.status = QStringLiteral("cancelled");
        t.note = QStringLiteral("已取消 · 执行器正在复位（下次提交冷启动）");
        emit taskUpdated(t.id);
        if (runner_) runner_->requestCancel(quint32(taskId));   // 直调安全：仅 atomic 写
        return true;
    }
    return false;
}
```

onTaskFinished（37-47 行）加守卫（`if (!q) return;` 之后）：

```cpp
    if (q->status == QLatin1String("cancelled")) return;   // 2.38.0 R3：取消终态由 cancel() 定格，回调不得覆写
```

- [ ] **Step 4: 构建 + GREEN + 反例**。

Run: `cmake --build build --target LightAlbum -j 8`（cwd=native）
Run: `powershell -NoProfile -ExecutionPolicy Bypass -File ./qa_runner.ps1 -Only LA_ERASE_QA`（cwd=native）
Expected: `[OK]`（A/B/C/D/E/A补/G1/G2 全过；G2 四断言 + 恢复断言全绿）。
Run: `$env:LA_ERASE_QA_FORCE_IGNORE_CANCEL="1"; powershell ... -Only LA_ERASE_QA; Remove-Item Env:LA_ERASE_QA_FORCE_IGNORE_CANCEL`（cwd=native）
Expected: `[FAIL]`（cancel 被忽略 → 态仍 running → G2 断言转红）。**注意**：`LA_ERASE_QA_FORCE_BROKEN=1` 臂（D 组越界反例）仍须全行为不变——cancel 不参与该臂。

- [ ] **Step 5: 提交**。

```bash
git add native/src/core/EraseRunner.h native/src/core/EraseRunner.cpp native/src/core/EraseBridge.h native/src/core/EraseBridge.cpp native/src/main.cpp
git commit -m "feat(erase): 任务取消内核（分片等待观测 atomic cancelTarget + kill 复位 + cancelled 终态定格）"
```

---

### Task 10 (R3-T4): RetouchPage 取消按钮 + qaShootA 断言 + LA_ERASE_QA 重测

**Files:**
- Modify: `native/src/ui/RetouchPage.h`（`onEraseCancel` + `eraseCancelBtn_` + `eraseLastTaskId_`）
- Modify: `native/src/ui/RetouchPage.cpp`（按钮创建 + 接线 + onEraseClicked 记录 id + onEraseTaskUpdated 显隐 + qaShootA 增 hasEraseCancel）
- Modify: `native/gate_matrix.ps1`（LA_ERASE_QA、LA_RETOUCH_A_QA measuredMs 重测回填）
- Test: `LA_ERASE_QA` + `LA_RETOUCH_A_QA` 双绿 + 全量快验

**Interfaces:**
- Consumes: `EraseBridge::cancel(int)`（T9）。
- Produces: objectName 契约 `eraseCancelBtn`；进行中任务（queued/running）才出现的取消按钮（零 emoji）。

- [ ] **Step 1: UI 接线**。RetouchPage.h 私有槽区加 `void onEraseCancel();`（注释 `// 2.38.0 R3`），成员区（P4-4 块）加：

```cpp
    QPushButton*   eraseCancelBtn_ = nullptr;   // 2.38.0 R3：进行中任务取消（queued/running 才显示）
    int            eraseLastTaskId_ = -1;       // 最近一次 submit 的任务 id
```

- [ ] **Step 2: 构造 + 槽实现**（.cpp）：
  - 页 3 块 `ml->addWidget(eraseBtn_); ml->addWidget(eraseSlotLbl_);`（514 行）之后加：

```cpp
        eraseCancelBtn_ = new QPushButton(QCoreApplication::translate("LightAlbum", "取消任务"), mp);
        eraseCancelBtn_->setObjectName(QStringLiteral("eraseCancelBtn"));
        eraseCancelBtn_->setStyleSheet(Lumen::btnGhost(Lumen::INK));
        eraseCancelBtn_->hide();                       // 有 queued/running 任务才出现
        ml->addWidget(eraseCancelBtn_);
```

  - connect 区（584 行附近）加 `connect(eraseCancelBtn_, &QPushButton::clicked, this, &RetouchPage::onEraseCancel);`
  - onEraseClicked（1018 行）：`eraseBridge_->submit(...)` 改为 `eraseLastTaskId_ = eraseBridge_->submit(geoImg_, canvas_->mask());`
  - onEraseTaskUpdated（1022-1038）在 `if (!t) return;` 之后加：

```cpp
    if (eraseCancelBtn_) {   // 2.38.0 R3：进行中任务才显示取消（按钮零 emoji，C5 棘轮不动）
        const bool active = t->status == QStringLiteral("queued") || t->status == QStringLiteral("running");
        eraseCancelBtn_->setVisible(active);
        eraseCancelBtn_->setEnabled(active);
    }
```

  - 新增槽（放 onEraseTaskUpdated 之后）：

```cpp
void RetouchPage::onEraseCancel()
{
    if (eraseLastTaskId_ < 0) return;
    if (eraseBridge_->cancel(eraseLastTaskId_))
        badge_->setText(QCoreApplication::translate("LightAlbum", "取消已受理 · 本地执行器正在复位"));
}
```

- [ ] **Step 3: qaShootA 增断言**。T2 改过的判定链再加一项：`const bool hasEraseCancel = findChild<QPushButton*>(QStringLiteral("eraseCancelBtn")) != nullptr;`，`ok` 链尾加 `&& hasEraseCancel`；两行输出各加 `eraseCancel=%d`。

- [ ] **Step 4: 构建 + 双绿**。

Run: `cmake --build build --target LightAlbum -j 8`（cwd=native）
Run: `powershell -NoProfile -ExecutionPolicy Bypass -File ./qa_runner.ps1 -Only LA_ERASE_QA,LA_RETOUCH_A_QA`（cwd=native）
Expected: 双 `[OK]`；LA_RETOUCH_A_QA 行含 `eraseCancel=1`。

- [ ] **Step 5: measuredMs 重测回填**。

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File ./qa_runner.ps1 -Only LA_ERASE_QA,LA_RETOUCH_A_QA -Timing`（cwd=native）
把两值写回 gate_matrix.ps1（LA_ERASE_QA 原 3667，G1/G2 约增 5-7s；LA_RETOUCH_A_QA 原 2707，本轮已两改）。**判据**：LA_ERASE_QA 新值若逼近其 timeoutSec 默认 25s，则在条目上加 `timeoutSec=60` 并注记原因。

- [ ] **Step 6: 提交**。

```bash
git add native/src/ui/RetouchPage.h native/src/ui/RetouchPage.cpp native/gate_matrix.ps1
git commit -m "feat(retouch): 消除任务取消按钮（进行中显隐）+ 门实测回填（LA_ERASE_QA/LA_RETOUCH_A_QA）"
```

---

### Task 11: 收口 2.38.0（版本 + 文档 + 全量 + 记忆）

**Files:**
- Modify: `native/CMakeLists.txt:2`（VERSION 2.37.0 → 2.38.0）
- Modify: `native/CHANGELOG.md`（新增 `[2.38.0] - 2026-10-12`）
- Modify: `native/docs/迭代指导书.md`（§3 门数 72→74；§7 台账一行）
- Modify: 记忆文件（见 Step 5）
- Test: 全量套件 + SelfTest

- [ ] **Step 1: 版本 bump + CHANGELOG**。CMakeLists.txt VERSION 2.38.0；CHANGELOG 顶注新节（三小节 + 回归行）：

```markdown
## [2.38.0] - 2026-10-12

### R1 Lumen 13 显式算子产品化
- RetouchPage 新增「一键增强」工具区：13 算子三组平铺（影调/人像/校正），QtConcurrent
  异步执行、忙碌态、走现有撤销/重做通道（RtEditState 增 enh/ops），导出按序列全分辨率重放。
- 诚实反馈：红眼修复点数 / 肤色占比 / 水平校正角度；无倾斜 no-op 明确提示、不改动。
- 新门 LA_LUMEN_OPS_UI_QA（fast 层 exe 静态扫描，接线前必红）；修复 LA_RETOUCH_A_QA
  恒 exit 0 空心门 + 增 enhOps/eraseCancel 存在性断言。
- 引擎数学零改动（LumenEngine.{h,cpp} 未触；LA_LUMEN_SMOKE 金样对拍为回归护栏）。

### R2 HEIC/WebP EXIF 容器补全
- ExifReader 增 WebP（RIFF EXIF chunk，兼容 "Exif\0\0" 前缀）与 HEIC（ISOBMFF
  meta/iinf/iloc 最小 walker）抽取，容器→TIFF blob 一层，复喂现有 TIFF 解析器；
  失败一律走 mtime 回退（诚实降级，绝不伪造）。
- 新门 LA_EXIF_CONTAINER_QA（full）：合成夹具字节确定 + 恒开反例（越界盒/自指子盒
  不崩且回退）；真样本臂 LA_HEIC_SAMPLE env 内判定（无值 skipped）。
- JPEG/PNG/TIFF 三格式行为不变（既有解析断言全绿）。

### R3 Erase residual 收尾
- EraseBridge 任务表淘汰（保留最近 8；running 永不淘汰；submit 尾修剪）；ES
  取消链路：EraseRunner.requestCancel（atomic）+ run() 100ms 分片等待观测 +
  kill 子进程复位；cancelled 终态定格（worker 回调不得覆写）；RetouchPage 取消按钮。
- EraseBridge.h / FaceCluster.h 陈旧注释订正；LA_ERASE_QA 扩 G1（淘汰）G2（取消）
  两组 + FORCE_NO_EVICT / FORCE_IGNORE_CANCEL 反例钩子。

### 回归
- 名册 72 → 74 门（fast 19 + full 55）；gate_matrix SelfTest fails=0；
  全量套件 FAIL=0 CHECK=0 KILL=0，SKIP 仅既有 need= 门。
```

- [ ] **Step 2: 迭代指导书**。§3 门数行 72→74（fast 18→19 + full 54→55）；§7 台账加一行（2.38.0：R1 一键增强 13 接线 / R2 HEIC+WebP EXIF / R3 淘汰+取消；门 74）。

- [ ] **Step 3: 全量套件**（trio 检查后）。

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File ./qa_runner.ps1 -Tier full`（cwd=native）
Expected: `FAIL=0 CHECK=0 KILL=0`；SKIP 仅既有 need= 门（LA_SCAN_SMOKE/LA_RAW_QA/LA_ERASE_MODEL_QA 等）。

- [ ] **Step 4: 名册自检复跑**。

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File ./gate_matrix.ps1 -SelfTest`（cwd=native）
Expected: `gates=74 fast=19 full=55 ... fails=0`。

- [ ] **Step 5: 记忆更新**（项目记忆目录）：
  - 更新 `engine-upgrade-roadmap.md`：round 2 已落地（R1/R2/R3），路线 4-7 与巡查 #3-#8 留档（引 round-2 spec §9）。
  - 新增 `exif-container-parse.md`：HEIC/WebP 容器→TIFF blob 抽取范式（iloc construction_method 0、payload 前 4 字节偏移、fail-open mtime 回退）。
  - 更新 `release-state.md`：2.38.0 编码完成（未打包；打包/tag 留待用户指令，本地）。

- [ ] **Step 6: 局部行为抽验**（真实样本，env 传入不落盘）：

Run: `$env:LA_HEIC_SAMPLE="<用户真 .heic 路径>"; powershell ... -Only LA_EXIF_CONTAINER_QA; Remove-Item Env:LA_HEIC_SAMPLE`（cwd=native）
Expected: `realSample=ok`。（若用户样本解析失败 → 诚实 FAIL，按实际形态修 walker，不降期望。）

- [ ] **Step 7: 收口提交**。

```bash
git add native/CMakeLists.txt native/CHANGELOG.md native/docs/迭代指导书.md
git commit -m "release-prep(2.38.0): 版本 bump + CHANGELOG 三路线收口 + 指导书门数 74"
```

（记忆文件位于用户级目录，不属于仓库提交。）

---

## 自审记录（写计划者已执行）

- **Spec 覆盖**：R1（§2.1-2.5 → T1/T2/T3/T4；§2.4 两门全落）、R2（§3.1-3.4 → T5/T6）、R3（§4.1-4.4 → T7/T8/T9/T10；注释订正 T8）、§5 验证（T4/T6/T10/T11）、§7 台账（T11）逐项有任务。
- **占位符扫描**：所有代码步骤含完整代码；两处 measuredMs 明确「实测回填」而非 TBD 数值（先例 LA_ERASE_MODEL_QA measuredMs=0 + 注释）。
- **类型一致**：`opBtn_<key>`（T1/T2/T3 同源）；`qaTaskCount/int`、`cancel(int)->bool`、`requestCancel(quint32)`、`cancelTarget_ std::atomic<quint32>`（T7-T10 一致）；`RtEditState.enh/ops`（T3 定义、T3 消费）。
- **两处计划级裁决**已写入 Global Constraints 后的裁决节（E5/E9d 兼容 + need= 语义）。
- **已知代价**（诚实记录）：取消后冷启动（真模型 ~8s）写进 note 文案；撤销栈新增 QImage 快照（COW 共享，单次增强 ≈≤1024px 图）。
