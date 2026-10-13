# 引擎升级第三轮（LumenEngine 算子内并行 + 量化安全 float）实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 `LumenEngine.cpp` 中 spec §3.1 清单内的纯映射像素循环改为 QtConcurrent 行块并行、把 §4.1 清单内的量化后终末颜色算术 double→float，并用 `LA_LUMEN_SMOKE` 新增的「串行 vs 并行 逐字节一致」决定性子断言守护零回归。

**Architecture:** 单一文件局部执行器 `lumenParallelRows(h, body)`（4× 超订阅行块 + `LA_LUMEN_THREADS=1` 串行退化）承接所有逐行并行；归约/测量/排序/Hough/addGrain/管线链一律保持串行；float 只切「量化后即定且不参与阈值判定」的算术。零新依赖（Qt6::Concurrent 已链接）、零新门（roster 维持 74）。

**Tech Stack:** C++17 / Qt 6.8.3 MinGW / QtConcurrent::blockingMap / std::atomic（仅 Task 5）；QA 门经 PowerShell `native/qa_runner.ps1`；金样 fixture 在 `D:/Test/workbuddy/2026-09-18-09-00-13/_gold_lumen/`（121 文件，已在盘）。

**Spec:** `native/docs/superpowers/specs/2026-10-13-lumenengine-parallel-float-design.md`（commit 17bb0f2）

## Global Constraints

- 所有命令的 cwd = 仓库根 `D:/Test/workbuddy/2026-09-18-09-00-13`（Bash 工具默认 cwd 是其子目录 `native/LightAlbum-2.32.0-win64`，必须显式回到仓库根或用 `git -C`）。
- 构建（PowerShell，禁 Git Bash 跑门）：
  `powershell -ExecutionPolicy Bypass -Command "$env:PATH='C:\Users\damin\.workbuddy\binaries\python\envs\default\Lib\site-packages\cmake\data\bin;D:\qt\Tools\mingw1310_64\bin;'+$env:PATH; cmake --build native/build --target LightAlbum -j 8"`
- 跑单门：`powershell -ExecutionPolicy Bypass -File native/qa_runner.ps1 -Exe native/build/LightAlbum.exe -Only LA_LUMEN_SMOKE`（`-Only` 一次只能一个门名，逗号列表跑零个）。
- **每改 `native/src/**` 必在同一次提交里保持门与实现同源（铁律 #20）；提交前必须先重建 exe（pre-commit 陈旧 exe 守卫会拦），且永不 `--no-verify`。**
- 门判据**绝不**因反例开关而变（铁律 #22）：`LA_LUMEN_SMOKE_FORCE_DIFF` 只注入损坏（翻转基准图一字节），断言永远是「逐字节相等」。
- `gate_matrix.ps1` 及任何 roster 脚本改动保持**纯 ASCII**（铁律 #37）；`measuredMs` 只准从 `qa_runner -Timing` 实测回填（铁律 #25/#31）。
- **并行红线**：spec §3.2 所列循环（`measureStats/measureExtra/hist8/boxDownscale/areaResample/partial_sort(dehaze)/Hough acc/whiteBalance 幂均值归约/enhanceContrastPil 均值归约/portraitBeautify 归约/vignette 环带归约/addGrain/autoPipeline/retouchWithPlan 链`）**一律不得并行化**——审阅发现即打回。
- **float 红线**：`toL`（19595/38470/7471 整型定点）、`ycbcrSplit`/`denoise` 内联 YCbCr 拆分（`long long` 截断）、`rgb2hsv` 六扇区整型、`chopMul/screen/composite` 整型 `/255` **不得切 float**。
- 并行体纪律：body 内逐行代码**逐字不变**（只允许缩进变化与声明挪进 body）；每个并行区之前必须在单线程点强制 detach（对将被写的 QImage 先调一次 `scanLine(0)` 或 `data()`）；并行体内**只触碰自己行区间**。
- 零新依赖；`native/CMakeLists.txt` 直到收口任务都不碰（Qt6::Concurrent 已在 `:19` find_package、`:139` 链接）。
- 本地提交，按文件名 `git add`，**永不 push**（push auth 仅覆盖过 2.36.0）。收口版本 2.39.0；roster 门数保持 74 不变。
- `LA_LUMEN_THREADS` 语义：`"1"`→强制串行；未设/非数/≤0→`QThread::idealThreadCount()`（再 ≤0 兜底 1）。

## File Structure

| 文件 | 职责 | 任务 |
|---|---|---|
| `native/src/core/LumenEngine.cpp` | 执行器 + 全部热点循环并行化 + float 切换 | T1-T7 |
| `native/src/main.cpp` | `runLumenSmoke`（:5582-5736）增决定性子断言块 | T1，T8 验证 |
| `native/gate_matrix.ps1` | `:103` measuredMs/timeoutSec 实测回填 | T9 |
| `native/CMakeLists.txt`、`native/CHANGELOG.md`、`native/docs/迭代指导书.md` | 收口 | T10 |

**统一改写模板**（Task 2-6 全部套用；`H` 为该循环原边界表达式）：

```cpp
// 之前
for (int y = 0; y < H; ++y) { <body> }
// 之后
lumenParallelRows(H, [&](int y0, int y1) {
    for (int y = y0; y < y1; ++y) { <body 逐字不变> }
});
```

---

### Task 1: `lumenParallelRows` 执行器 + `gaussianBlur` 首杠杆 + 门决定性子断言（铁律 #20 同提交）

**Files:**
- Modify: `native/src/core/LumenEngine.cpp`（includes 区 `:1-6`；匿名命名空间 `:19-23` 之后插 helper；`gaussianBlur` `:270-308`）
- Modify: `native/src/main.cpp`（`runLumenSmoke` 末尾 `:5732` 之后、`LA_LUMEN_SMOKE %s` 打印之前插 DET 块）

**Interfaces:**
- Produces: `lumenParallelRows(int h, const std::function<void(int,int)>&)` —— LumenEngine.cpp 匿名命名空间文件局部，Task 2-6 直接调用。
- Produces: 门 stderr 令牌 `LA_LUMEN_DET <name> equal=0|1`；反例 env `LA_LUMEN_SMOKE_FORCE_DIFF`（置位即损坏串行基准 -> 必红）。

- [ ] **Step 1: LumenEngine.cpp 加 include 与 helper**

在 `#include <random>`（`:6`）之后加：

```cpp
#include <QtConcurrent/QtConcurrentRun>
#include <QThread>
```

在第一个匿名命名空间（`namespace {` `:19`）内、`constexpr double P_SHADES = 6.0;` 之后插入：

```cpp
// 逐行块并行执行器：把 [0,h) 切成行块用 QtConcurrent::blockingMap 分派。
// body(y0,y1) 负责行区间 [y0,y1)，仅用于"每输出像素只读自身/邻域（只读源）、只写自身像素"的纯映射循环。
// LA_LUMEN_THREADS=1 -> 强制串行（金样门串行/并行逐字节比对基准）；未设/坏值 -> idealThreadCount。
static void lumenParallelRows(int h, const std::function<void(int, int)>& body)
{
    int nt = 0;
    const QByteArray tv = qgetenv("LA_LUMEN_THREADS");
    if (!tv.isEmpty()) nt = tv.toInt();
    if (nt <= 0) nt = QThread::idealThreadCount();
    if (nt <= 1 || h <= 1) { body(0, h); return; }
    const int nchunk = std::max(1, std::min(h, nt * 4));
    const int rows = (h + nchunk - 1) / nchunk;
    QVector<QPair<int, int>> blocks;
    for (int y = 0; y < h; y += rows) blocks.append(qMakePair(y, qMin(y + rows, h)));
    QtConcurrent::blockingMap(blocks, [&](const QPair<int, int>& b) { body(b.first, b.second); });
}
```

- [ ] **Step 2: `gaussianBlur` 双 pass 改并行（本计划最高杠杆：约 11 个算子经它）**

`:270` `QImage tmp(img.size(), img.format());` 之后加两行强制 detach（单线程段完成），随后把两个 `for (int y = 0; y < h; ++y)`（横向 `:271-289`、纵向 `:290-308`）按模板包进 `lumenParallelRows(h, ...)`，body 逐字不变：

```cpp
    QImage tmp(img.size(), img.format());
    tmp.scanLine(0);   // 单线程段完成 detach；并行体只写各自行
    img.scanLine(0);
    lumenParallelRows(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            // …原横向 pass 的循环体逐字搬入，缩进不变语义…
        }
    });
    lumenParallelRows(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            // …原纵向 pass 的循环体逐字搬入…
        }
    });
```

- [ ] **Step 3: `runLumenSmoke` 加 DET 决定性子断言块**

在 Block D 水平校正的收尾 `}`（`main.cpp:5732`）之后、`std::fprintf(stderr, "LA_LUMEN_SMOKE %s\n", ...)` 之前插入：

```cpp
    // ---- R3 决定性子断言：串行（LA_LUMEN_THREADS=1）vs 默认并行，逐字节 memcmp 全等。
    // 强于 MAD：任何跨块写竞态/误并行归约即使 MAD 仍在容差内也必红（铁律34）。
    // 反例：LA_LUMEN_SMOKE_FORCE_DIFF=1 只损坏串行基准一字节；期望表冻结"必须相等"，绝不随开关变（铁律22）。
    {
        const QImage dIn(QDir(dir).filePath(QStringLiteral("in_0.png")));
        if (!dIn.isNull()) {
            qputenv("LA_LUMEN_THREADS", "1");
            const QImage sLens = LumenEngine::lensCorrect(dIn);
            const QImage sVig  = LumenEngine::vignetteCorrect(dIn);
            const QImage sGlow = LumenEngine::glowStyle(dIn);
            const QImage sHdr  = LumenEngine::toneMapHdr(dIn);
            const QImage sDeh  = LumenEngine::dehaze(dIn);
            const QImage sPor  = LumenEngine::portraitBeautify(dIn);
            qunsetenv("LA_LUMEN_THREADS");
            const QImage pLens = LumenEngine::lensCorrect(dIn);
            const QImage pVig  = LumenEngine::vignetteCorrect(dIn);
            const QImage pGlow = LumenEngine::glowStyle(dIn);
            const QImage pHdr  = LumenEngine::toneMapHdr(dIn);
            const QImage pDeh  = LumenEngine::dehaze(dIn);
            const QImage pPor  = LumenEngine::portraitBeautify(dIn);
            auto cmp = [&](const char* nm, QImage a, const QImage& b) {
                if (qEnvironmentVariableIsSet("LA_LUMEN_SMOKE_FORCE_DIFF") && !a.isNull())
                    a.scanLine(0)[0] ^= uchar(0x01);   // a 为按值句柄，scanLine 触发私有 detach，不污染 s* 原图
                const bool eq = a.size() == b.size() && a.bytesPerLine() == b.bytesPerLine()
                    && a.sizeInBytes() == b.sizeInBytes()
                    && std::memcmp(a.constBits(), b.constBits(), size_t(a.sizeInBytes())) == 0;
                std::fprintf(stderr, "LA_LUMEN_DET %s equal=%d\n", nm, eq ? 1 : 0);
                if (!eq) pass = false;
            };
            cmp("lens", sLens, pLens);
            cmp("vignette", sVig, pVig);
            cmp("glow", sGlow, pGlow);
            cmp("hdr", sHdr, pHdr);
            cmp("dehaze", sDeh, pDeh);
            cmp("portrait", sPor, pPor);
        }
    }
```

（`main.cpp` 顶部若无 `#include <cstring>` 则补——memcmp 所在 TU；先 grep 确认。）

- [ ] **Step 4: 构建**

仓库根跑 Global Constraints 的构建命令。Expected: 链接成功（既有 hex-escape 警告可忽略）。

- [ ] **Step 5: RED —— FORCE_DIFF 反例必须咬**

qa_runner 经 `System.Diagnostics.Process` 启动 exe 且不清空父环境（`qa_runner.ps1:91-95` 只增不删），所以在外层 PowerShell 设置的 `LA_LUMEN_SMOKE_FORCE_DIFF` 会透传进门进程。RED 命令（仓库根）：

```powershell
powershell -ExecutionPolicy Bypass -Command "$env:LA_LUMEN_SMOKE_FORCE_DIFF='1'; & native\qa_runner.ps1 -Exe native/build/LightAlbum.exe -Only LA_LUMEN_SMOKE"
```

Expected: 至少若干行 `LA_LUMEN_DET ... equal=0` + 末行 `LA_LUMEN_SMOKE FAIL`，进程退出码 1。

- [ ] **Step 6: GREEN —— 正常门**

`powershell -ExecutionPolicy Bypass -File native/qa_runner.ps1 -Exe native/build/LightAlbum.exe -Only LA_LUMEN_SMOKE`
Expected: `LA_LUMEN_DET` ×6 全 `equal=1`，`LA_LUMEN_SMOKE PASS`，exit 0，`[PASS]`。

- [ ] **Step 7: Commit（helper + 引擎 + 门同提交，铁律 #20）**

```bash
git -C "/d/Test/workbuddy/2026-09-18-09-00-13" add native/src/core/LumenEngine.cpp native/src/main.cpp
git -C "/d/Test/workbuddy/2026-09-18-09-00-13" commit -m "feat(lumen): lumenParallelRows 行块执行器 + gaussianBlur 算子内并行 + LA_LUMEN_SMOKE 串行/并行逐字节决定性子断言（R3-T1，FORCE_DIFF 反例）"
```

（pre-commit fast tier 会跑；已重建 exe 故陈旧守卫过。）

---

### Task 2: PIL 基础映射族并行（pointRgb/pointGray/chopGray/invertGray/toL8/rgb2hsv/hsv2rgb）

**Files:**
- Modify: `native/src/core/LumenEngine.cpp` 行段（当前 HEAD 5240cb4 基线，Task 1 后 helper 已存在）：`chopGray :32-43`、`invertGray :50-54`、`pointGray :61-65`、`pointRgb :117-121`、`toL8 :175-180`、`rgb2hsv :190-212`、`hsv2rgb :218-250`

**Interfaces:**
- Consumes: `lumenParallelRows`（Task 1）。

- [ ] **Step 1: 逐点套用统一模板**。完整展开一个（`pointGray`），其余六个同法：

```cpp
QImage pointGray(const QImage& a, const int lut[256])
{
    QImage out = grayLike(a);
    out.scanLine(0);                       // 单线程段强制 detach
    lumenParallelRows(a.height(), [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const uchar* pa = a.constScanLine(y);
            uchar* po = out.scanLine(y);
            for (int x = 0; x < a.width(); ++x) po[x] = uchar(lut[pa[x]]);
        }
    });
    return out;
}
```

各点边界表达式与 detach 位（写完必须自检）：

| 点 | H 表达式 | 被写对象（并行区前强制） | 备注 |
|---|---|---|---|
| `chopGray` | `a.height()` | `out.scanLine(0)` | a、b 只读 |
| `invertGray` | `a.height()` | `out.scanLine(0)` | |
| `pointRgb` | `img.height()` | `img.scanLine(0)`（就地写） | img 是 convertToFormat 局部副本 |
| `toL8` | `src.height()` | `out.scanLine(0)` | |
| `rgb2hsv` | `src.height()` | `H->scanLine(0); S->scanLine(0); V->scanLine(0);` | **整型截断保持不动**（§4.2），只包行 |
| `hsv2rgb` | `h.height()` | `out.scanLine(0)` | 数学本轮不动（Task 7 才切 float） |

- [ ] **Step 2: 构建**（Global Constraints 命令）。
- [ ] **Step 3: 门**：`-Only LA_LUMEN_SMOKE` Expected PASS + DET ×6 equal=1。行为保持重构；判别力已由 Task 1 FORCE_DIFF 与 Task 8 真实竞态证明。
- [ ] **Step 4: Commit** `perf(lumen): PIL 基础映射族逐行并行 (pointRgb/pointGray/chopGray/invertGray/toL8/rgb2hsv/hsv2rgb)`

---

### Task 3: 中层/合成映射族并行（enhanceBrightness/enhanceColor/channelGain/blend/screen/composite/unsharpMask 应用段/blackWhite mix）

**Files:**
- Modify: `native/src/core/LumenEngine.cpp`：`enhanceBrightness :474-480`、`enhanceColor :488-496`、`blend :852-860`、`screen :869-877`、`composite :887-898`、`channelGain :905-911`、`unsharpMask` 应用 `:317-330`、`blackWhite` mix `:1008-1015`

**Interfaces:** Consumes `lumenParallelRows`。

- [ ] **Step 1: 逐点套模板**。detach 位：写 `img` 的（enhanceBrightness/enhanceColor/channelGain/unsharpMask/blackWhite mix/blend/screen 就地写 `ia`）在并行区前 `img.scanLine(0)`（blend/screen 为 `ia.scanLine(0)`）；写新 `out` 的（composite）`out.scanLine(0)`。H 表达式：各自 `img.height()`/`ia.height()`/`ia.height()`。body 逐字不变；`enhanceColor` 对 `gray`、`unsharpMask` 对 `blur` 的 `constScanLine` 只读邻接不动。
- [ ] **Step 2: 构建**。
- [ ] **Step 3: 门** `-Only LA_LUMEN_SMOKE` Expected PASS + DET ×6 equal=1。
- [ ] **Step 4: Commit** `perf(lumen): 中层映射族逐行并行 (enhance*/channelGain/blend/screen/composite/unsharp 应用/blackWhite mix)`

---

### Task 4: halo 滤波族并行（median3/median3G/smoothG/smoothFilter/minFilter/medianFilter）

**Files:**
- Modify: `native/src/core/LumenEngine.cpp`：`median3G :74-88`、`smoothG :98-111`、`median3 :339-354`、`smoothFilter :364-379`、`minFilter :1227-1247`、`medianFilter(size>3) :1260-1277`

**Interfaces:** Consumes `lumenParallelRows`。

- [ ] **Step 1: 套模板**。这些循环读**完整只读源**（halo ±1 或 ±r 越界已 `std::clamp`），写独立新 `out`（或 `minFilter` 的 `out`）→ 行块间无竞态。每个函数并行区前 `out.scanLine(0)`。
- [ ] **Step 2: `medianFilter` 专属修正**：`:1259` 的 `QVector<int> vr(nn), vg(nn), vb(nn);` 是跨行复用的暂存——**必须整行挪进并行 lambda 体首**（每块独享），其余不动：

```cpp
    out.scanLine(0);
    lumenParallelRows(h, [&](int y0, int y1) {
        QVector<int> vr(nn), vg(nn), vb(nn);      // 暂存改为块局部（原 :1259 行删除）
        for (int y = y0; y < y1; ++y) { /* 原 body 逐字 */ }
    });
```

- [ ] **Step 3: 构建**。
- [ ] **Step 4: 门** Expected PASS + DET ×6 equal=1。
- [ ] **Step 5: Commit** `perf(lumen): halo 滤波族逐行并行 (median3/median3G/smoothG/smoothFilter/minFilter/medianFilter，暂存降为块局部)`

---

### Task 5: redeye 双 pass（cnt 线程局部求和）+ vignette 径向全分辨率循环

**Files:**
- Modify: `native/src/core/LumenEngine.cpp`：includes 加 `#include <atomic>`；`redeyeRemove :1034-1055`、`vignetteCorrect` 径向 `:1105-1117`

**Interfaces:** Consumes `lumenParallelRows`。

- [ ] **Step 1: redeye pass1 整段替换**（这是全计划唯一被触碰的归约，整数加法交换律保证与串行逐位一致）：

```cpp
    QVector<uchar> hit(w * h, 0);
    uchar* hitBase = hit.data();   // 单线程段 detach；并行体按行偏移直写
    std::atomic<int> cntAcc{0};
    lumenParallelRows(h, [&](int y0, int y1) {
        int local = 0;
        for (int y = y0; y < y1; ++y) {
            const QRgb* s = reinterpret_cast<const QRgb*>(img.constScanLine(y));
            for (int x = 0; x < w; ++x) {
                const int r = qRed(s[x]), g = qGreen(s[x]), b = qBlue(s[x]);
                if (r < 120 || g > 110 || b > 110) continue;
                if (r < 2.2 * g || r < 2.2 * b) continue;
                hitBase[y * w + x] = 1; ++local;
            }
        }
        cntAcc.fetch_add(local, std::memory_order_relaxed);
    });
    int cnt = cntAcc.load();
```

（原 `int cnt = 0;`（`:1035`）随替换消失；面积门 `:1045-1046` 与 `*pixels = cnt` 原样。）

- [ ] **Step 2: redeye pass2 套模板**（`img.scanLine(0)` 先行；`hit` 读 `hitBase[y * w + x]` 替换 `hit[y * w + x]`，其余逐字）。
- [ ] **Step 3: vignette 径向循环套模板**：H=`h`，并行区前 `img.scanLine(0)`（就地写；`k/gmax/cx2/cy2/rmax2` 全是只读捕获）。`:1073-1081` 环带归约**保持串行**（§3.2）。
- [ ] **Step 4: 构建**。
- [ ] **Step 5: 门** Expected PASS + DET ×6 equal=1（DET 覆盖 vignette 与 portrait/dehaze 等）。redeye 不在 DET 名单——补验：临时 `$env:LA_LUMEN_DEBUG` 不需要；直接对拍逻辑已由 Block B `op_redeye_*` MAD 覆盖，串行一致性由 Task 8 竞态反例类证明。
- [ ] **Step 6: Commit** `perf(lumen): redeye 双 pass + vignette 径向逐行并行（cnt 线程局部求和原子合并，与串行逐位等值）`

---

### Task 6: lensCorrect 按瓦片并行

**Files:**
- Modify: `native/src/core/LumenEngine.cpp:1742-1766`

- [ ] **Step 1: 整段替换外层双循环**（tile 完全独立：读只读 `pad`，写 `out` 各自 tile 矩形；`solve3/lensSrc/bilinearSample` 纯函数线程安全）：

```cpp
    QImage out(pw, ph, QImage::Format_RGB32);
    out.fill(0);
    out.scanLine(0);
    QList<QPair<int, int>> tiles;
    for (int iy = 0; iy < N; ++iy)
        for (int ix = 0; ix < N; ++ix) tiles.append(qMakePair(ix, iy));
    QtConcurrent::blockingMap(tiles, [&](const QPair<int, int>& tl) {
        const int ix = tl.first, iy = tl.second;
        int x0 = int(ix * pw / N), x1 = int((ix + 1) * pw / N);
        int y0 = int(iy * ph / N), y1 = int((iy + 1) * ph / N);
        // …原 :1748-1765 body 逐字搬入（含 if (x1 <= x0) 修正、solve3 两次、内层 ly/lx 双循环）…
    });
```

（LumenEngine.cpp 顶部补 `#include <QList>`。）

- [ ] **Step 2: 构建**。
- [ ] **Step 3: 门** Expected PASS + `LA_LUMEN_DET lens equal=1` + Block B `LUMEN_OP <i> lens` MAD 全部 <= 8.0。
- [ ] **Step 4: Commit** `perf(lumen): lensCorrect 按瓦片并行（blockingMap over N×N tiles）`

---

### Task 7: double→float（仅 spec §4.1 清单，量化后终末算术）

**Files:**
- Modify: `native/src/core/LumenEngine.cpp`：`gaussianBlur` 累加器、`hsv2rgb`、`enhanceBrightness`、`channelGain`、`blend`、`vignetteCorrect` 径向增益

- [ ] **Step 1: 逐点切换（精确表达式）**

1. `gaussianBlur`（Task 1 后的并行体内）：`double acc[3] = {0, 0, 0};` → `float acc[3] = {0.f, 0.f, 0.f};`；`const double kw = kern[i + kr];` → `const float kw = float(kern[i + kr]);`（kern 本体 `QVector<double>` 不动，kernel 归一化不参与逐像素）。
2. `hsv2rgb` 内层：`const double H/S/V`、`h6/f/p/q/t/rf/gf/bf` → `float`；`int(std::floor(h6))` → `int(std::floor(h6))`（floor(float) 保留）；末量化 `clamp255(int(rf * 255.0 + 0.5))` → `clamp255(int(rf * 255.0f + 0.5f))`。
3. `enhanceBrightness`：函数体首行加 `const float ff = float(f);`，三处 `* f + 0.5` → `* ff + 0.5f`。
4. `channelGain`：`const float fr = float(gr), fg = float(gg), fb = float(gb);`，三处对应替换 `+ 0.5` → `+ 0.5f`。
5. `blend`：`const float ff = float(f), fi = float(1.0 - f);`，`qRed(pa[x]) * (1 - f) + qRed(pb[x]) * f + 0.5` → `qRed(pa[x]) * fi + qRed(pb[x]) * ff + 0.5f`（三通道同式）。
6. `vignetteCorrect` 径向体内：`const double dy/dx/r2/inv/gain` → `float`（`k` 捕获为 `const float kf = float(k); const float gmaxf = float(gmax);` 参与乘算；`cx2/cy2/rmax2` 转 float）；`+ 0.5` → `+ 0.5f`。

- [ ] **Step 2: float 红线自检**——grep 确认未动：`toL`（`19595`）、`ycbcrSplit`/`denoise` 的 `long long` 拆分、`rgb2hsv` 的 `43 *` 整型六扇区、`chopMul`、`screen/composite` 的 `/255` 整型、`measureExtra`/`whiteBalance`/`hpStats` 等归约内任何 double 字面量。
- [ ] **Step 3: 构建**。
- [ ] **Step 4: 门** Expected PASS——重点盯 Block C `LUMEN_PLAN <i> mad=` ≤ 4.0、Block D `mad=` ≤ 6.0 与 `angErr` ≤ 1.0、`steps_ok=1` 全列、DET ×6 equal=1（float 同时作用于串并两跑，DET 理应不受精度切换影响——若 DET 变红说明有并行/串行分叉，即真缺陷）。
- [ ] **Step 5: Commit** `perf(lumen): §4.1 量化后终末颜色算术 double→float（gaussianBlur 累加器/hsv2rgb/enhanceBrightness/channelGain/blend/vignette 增益）`

---

### Task 8: 反例咬合实证（临时突变，验红后回退；不落提交）

**Files:**
- 临时 Modify 后回退: `native/src/core/LumenEngine.cpp`

- [ ] **Step 1: 竞态反例（证明 DET 咬真实并行缺陷）**：把 `medianFilter` 的 `QVector<int> vr(nn), vg(nn), vb(nn);`（Task 4 已挪进 lambda 体）**再挪回 lambda 外**（恢复共享暂存 = 跨线程覆写竞态）。构建 → `-Only LA_LUMEN_SMOKE`。Expected: 至少一个 `LA_LUMEN_DET ... equal=0` + `LA_LUMEN_SMOKE FAIL`（竞态可能偶发同值；若首轮 equal=1 复跑至多 3 次，须出现 ≥1 次 FAIL；3 轮全 PASS 记 finding 上报，不得默默放行）。
- [ ] **Step 2: 回退 Step 1**：`git -C "/d/Test/workbuddy/2026-09-18-09-00-13" diff --stat` 确认仅 `native/src/core/LumenEngine.cpp` 有变 → `git -C ... checkout -- native/src/core/LumenEngine.cpp` → 重建。
- [ ] **Step 3: 精度反例（证明 §4.2 红线受门保护）**：把 `ycbcrSplit` 的三条 `long long` 截断公式改为 double 公式 + `std::lround`（如 `int(std::lround(0.299*r + 0.587*g + 0.114*b))`）。构建 → 门。Expected: `LUMEN_SMOKE FAIL`（portrait/plan 块 MAD 越界或 steps 翻转）。**兜底裁定（确定性规则）**：若首轮仍 PASS，撤销该突变改用**归约反例**——把 `whiteBalance :594-601` 幂均值 `acc` 由 double 改 float 累加（并行与否无关，纯精度翻转 `|gv-1|<0.005` 门 -> step key `wb` 出现/消失 -> `keys == goldKeys` 精确断言必红）；两突变均记录进报告。
- [ ] **Step 4: 回退 + 终验**：checkout + 重建 + `-Only LA_LUMEN_SMOKE` 回全绿（DET ×6 equal=1）。
- [ ] **Step 5: 无提交**——本任务产物是两次红/绿证据（写进任务报告文件），工作树必须回到 Task 7 提交后的干净态。

---

### Task 9: `LA_LUMEN_SMOKE` measuredMs 实测回填 + timeoutSec 裁定（纯 ASCII）

**Files:**
- Modify: `native/gate_matrix.ps1:103`（`@{n="LA_LUMEN_SMOKE"; png=$false; timeoutSec=90; measuredMs=66585}` 及其上方注释行）

- [ ] **Step 1: 实测**（铁律 #25/#31）：

```powershell
powershell -ExecutionPolicy Bypass -File native/qa_runner.ps1 -Exe native/build/LightAlbum.exe -Only LA_LUMEN_SMOKE -Timing
```

记录 JSON/输出中的 measuredMs 实际值 `M`。

- [ ] **Step 2: 定 timeoutSec**：规则 = 若 `M > 30000` 保持 90；否则取「≥ 3×M 的最小 15 的倍数秒，下限 45s，上限 90s」。示例：M=12000ms → 3×=36s → 45s。
- [ ] **Step 3: 改 `:103`**：`measuredMs=<M 实测>`、`timeoutSec=<裁定值>`；上方注释加一行英文说明（**纯 ASCII**，铁律 #37）如 `# R3 in-op parallel + float: re-measured via -Timing (was 66585 serial CPU).`。
- [ ] **Step 4: SelfTest**：`powershell -ExecutionPolicy Bypass -File native/gate_matrix.ps1 -SelfTest` Expected: 74==74、E11 nonAsciiBytes=0、PASS。
- [ ] **Step 5: Commit** `chore(gate): LA_LUMEN_SMOKE measuredMs 实测回填 <66585->M>（R3 算子内并行提速）+ timeoutSec <裁定>`

---

### Task 10: 全量 74 门回归 + 2.39.0 收口

**Files:**
- Modify: `native/CMakeLists.txt`（`VERSION 2.38.0` → `2.39.0` 行）
- Modify: `native/CHANGELOG.md`（顶部新增 `[2.39.0] - 2026-10-13` 节）
- Modify: `native/docs/迭代指导书.md`（§3 门数维持 74 的口径句 + §7 路线 6 性能行：LumenEngine 算子内并行 + float，measuredMs 66585→`<M>`）

- [ ] **Step 1: 全量回归**（full tier）：

```powershell
powershell -ExecutionPolicy Bypass -File native/qa_runner.ps1 -Exe native/build/LightAlbum.exe
```

Expected: FAIL=0、CHECK=0、KILL=0、SKIP=12（既有 opt-in 基线，含 LA_ERASE_MODEL_QA 等 need= 门）；`LA_LUMEN_SMOKE` 与 `LA_LUMEN_OPS_UI_QA`、`LA_RETOUCH_A_QA` 必须 PASS。若 `native/build/3rdparty/lama/bin/` 存在真实 ONNX 三件套，**不得删除**，照套件跑；三件套缺失致 LA_ERASE_QA 语义问题按既载 skip/need 机制处理并记录。
- [ ] **Step 2: 收口三文件**：CMakeLists `VERSION 2.39.0`；CHANGELOG 新节（要点：lumenParallelRows 行块并行覆盖 spec §3.1 全清单、§4.1 float 切换、LA_LUMEN_SMOKE DET 逐字节决定性子断言 + FORCE_DIFF 反例、竞态/精度双反例实证、roster 74 不变、实测 66585→`<M>`ms）；指导书两处口径。
- [ ] **Step 3: 重建**（CMakeLists 触碰版本号会触发重配置）。
- [ ] **Step 4: Commit** `release-prep(2.39.0): VERSION bump + CHANGELOG 引擎三轮节 + 指导书 §3/§7 口径（roster 74 不变，LA_LUMEN_SMOKE <M>ms）`（pre-commit fast tier 把关）。
- [ ] **Step 5: 收尾汇报**：执行区间 commit 列表、门数 74、实测提速比、DET 断言证据、Task 8 两次反例红证。

---

## Self-Review（计划完成后控制器自查记录）

1. **Spec 覆盖**：§2 helper=T1；§3.1 全表=T1(blur)/T2(基础映射)/T3(中层)/T4(halo)/T5(redeye+vignette)/T6(lens)；§3.2 红线=Global Constraints+T2 备注（rgb2hsv 仅并行不切精度）；§4.1=T7；§4.2=T7 Step2 自检；§5.2=T1 Step3；§5.3=T8+T1 Step5；§5.4=T9；§5.5=T10；§1 版本 2.39.0=T10；§8 触及文件=File Structure 表。无缺口。
2. **占位符扫描**：T3-T6 的「body 逐字搬入」是刻意设计（统一模板 + 逐点 H/detach 参数表 = 精确指令，非 TBD）；所有新写代码均全文给出。
3. **类型一致性**：`lumenParallelRows(int, const std::function<void(int,int)>&)` 自 T1 定义后 T2-T6 调用签名一致；DET 令牌名 `LA_LUMEN_DET`/env 名 `LA_LUMEN_SMOKE_FORCE_DIFF`/`LA_LUMEN_THREADS` 全文一致；`hitBase` 在 T5 两步间一致。
