# EraseBridge ONNX Executor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn EraseBridge from an honest stub (`modelReady()` hardcoded false, tasks park at `blocked_no_model`) into a real engine backed by a local ONNX (LaMa) inpainting subprocess, and add a hermetic `LA_ERASE_QA` contract gate plus an opt-in `LA_ERASE_MODEL_QA` quality gate.

**Architecture:** EraseBridge (GUI thread) delegates the actual inference to a persistent `QProcess` child speaking a fixed frame protocol (`inpaint.exe`, an independent MSVC build bundling onnxruntime + lama.onnx). A MinGW `inpaint_stub.exe` target built in the main tree implements the same protocol deterministically with no model, so `LA_ERASE_QA` (state machine + protocol round-trip + process persistence + crash recovery + mask-out byte conservation) runs green in every CI run — the 191MB model is NOT required for the contract gate. `LA_ERASE_MODEL_QA` exercises the real LaMa output and SKIPs when the model is absent (same skip semantics as `LA_SCAN_*`/`LA_RAW_SAMPLE`).

**Tech Stack:** C++17, Qt 6.8.3 (QProcess, QImage), MinGW 13.1 (stub), MSVC + ONNX Runtime C++ API (real inpaint.exe), PowerShell harness (`qa_runner.ps1`, `gate_matrix.ps1`, `fetch_lama.ps1`, `release.ps1`).

**Spec:** `docs/superpowers/specs/2026-10-10-engine-upgrade-erase-facecluster-design.md` §2

## Global Constraints

- 铁律 20：门禁测真执行器/真协议（stub 走的就是真 QProcess 通路）。
- 铁律 22：两个反例都不改期望式（`expect` 恒为规格值）。
- 铁律 25/30/31：`measuredMs` 从 `-Timing` 实测回填。
- 铁律 29：宿主无关 / 无随机（stub 输出确定，合成 base/mask 固定）。
- 铁律 34：禁空心门 —— mask-out 守恒断言"泄漏 1 像素即红"。
- 铁律 37 + 铁律 7：`gate_matrix.ps1` 改动区、`fetch_lama.ps1`、`release.ps1` 新增块全 ASCII（PS 5.1 无 BOM 按 GBK 解码，CJK 会吞注册/导致 unterminated string）。
- 零云端：运行时不发起任何网络请求；模型只在构建期由 `fetch_lama.ps1`（用户主动）落盘。
- 许可隔离：LaMa/LICENSE 缺失 → `fetch_lama.ps1` 失败 + `release.ps1` C9 硬 throw。
- 崩溃半径：`inpaint.exe`/stub 段错误只死子进程，主程序恢复并 `failed` 该任务。
- 版本：与路线 A 合并 bump 到 2.35.0（本 plan Task 10 单点执行一次）。

---

### Task 1: Frame protocol header (shared by runner + stub)

**Files:**
- Create: `native/src/core/EraseProto.h`

**Interfaces:**
- Produces: `namespace LaEraseProto` with exactly five inline functions: `packRgb`, `packMaskAlpha`, `writeRequest`, `readResponse`, plus nothing else — used by BOTH EraseRunner (Task 3) and inpaint_stub (Task 2).
- Protocol (exact):
  - Child on start writes line `READY 1\n`.
  - Request: ASCII line `TASK <id> <w> <h> <nb> <nm>\n` where `<nb>=w*h*3`, `<nm>=w*h`; then `<nb>` bytes RGB (base converted to RGB888, row-major, no padding); then `<nm>` bytes mask (alpha channel, 255=erase).
  - Response: `OK <id> <w> <h> <no>\n` + `<no>` RGB bytes, or `ERR <id> <code>\n`.

- [ ] **Step 1: Write the header**

Create `native/src/core/EraseProto.h`:

```cpp
#pragma once
// EraseBridge <-> inpaint.exe / inpaint_stub.exe 帧协议（唯一真源，两端共用）。
// 纯 ASCII 行 + 二进制体；无随机、宿主无关。详见 spec §2.2。
#include <QIODevice>
#include <QImage>
#include <QByteArray>
#include <QString>
#include <cstring>

namespace LaEraseProto {

// base 归一到 RGB888 后逐像素抽 R,G,B（跳 alpha，行无 padding）。
inline QByteArray packRgb(const QImage& img)
{
    const QImage rgb = img.convertToFormat(QImage::Format_RGB888);
    QByteArray out;
    out.resize(rgb.width() * rgb.height() * 3);
    uchar* d = reinterpret_cast<uchar*>(out.data());
    for (int y = 0; y < rgb.height(); ++y) {
        const uchar* line = rgb.constScanLine(y);
        memcpy(d, line, size_t(rgb.width()) * 3);
        d += size_t(rgb.width()) * 3;
    }
    return out;
}

// mask 取 alpha 通道（ARGB32），255=消除区。
inline QByteArray packMaskAlpha(const QImage& maskIn)
{
    const QImage mask = maskIn.convertToFormat(QImage::Format_ARGB32);
    QByteArray out;
    out.resize(mask.width() * mask.height());
    uchar* d = reinterpret_cast<uchar*>(out.data());
    for (int y = 0; y < mask.height(); ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(mask.constScanLine(y));
        for (int x = 0; x < mask.width(); ++x) *d++ = uchar(qAlpha(line[x]));
    }
    return out;
}

// 写请求到 io（子进程 stdin）。id 正整数。
inline bool writeRequest(QIODevice* io, quint32 id, const QImage& base, const QImage& mask, QString* err)
{
    const QImage rgbImg = base.convertToFormat(QImage::Format_RGB888);
    const QImage maskImg = mask.convertToFormat(QImage::Format_ARGB32);
    if (maskImg.width() != rgbImg.width() || maskImg.height() != rgbImg.height()) {
        if (err) *err = QStringLiteral("mask size != base");
        return false;
    }
    const QByteArray rgb = packRgb(rgbImg);
    const QByteArray m   = packMaskAlpha(maskImg);
    const int w = rgbImg.width(), h = rgbImg.height();
    QByteArray hdr = QStringLiteral("TASK %1 %2 %3 %4 %5\n")
        .arg(id).arg(w).arg(h).arg(rgb.size()).arg(m.size()).toUtf8();
    if (io->write(hdr) != hdr.size()) { if (err) *err = QStringLiteral("write hdr"); return false; }
    if (io->write(rgb) != rgb.size()) { if (err) *err = QStringLiteral("write rgb"); return false; }
    if (io->write(m)   != m.size())   { if (err) *err = QStringLiteral("write mask"); return false; }
    io->flush();
    return true;
}

// 读响应：成功填 outImage(QImage)。hdr/body 任何不一致 → false + err。
inline bool readResponse(QIODevice* io, quint32 expectId, QImage& outImage, QString* err)
{
    const QByteArray hdr = io->readLine();
    const QList<QByteArray> tk = hdr.trimmed().split(' ');
    if (tk.isEmpty()) { if (err) *err = QStringLiteral("empty resp"); return false; }
    if (tk[0] == "ERR") {
        if (err) *err = QStringLiteral("child ERR: ") + QString::fromUtf8(hdr.trimmed());
        return false;
    }
    // OK <id> <w> <h> <no>
    if (tk.size() != 5 || tk[0] != "OK") { if (err) *err = QStringLiteral("bad resp hdr"); return false; }
    const quint32 id = tk[1].toUInt();
    const int w = tk[2].toInt(), h = tk[3].toInt();
    const int no = tk[4].toInt();
    if (id != expectId) { if (err) *err = QStringLiteral("resp id mismatch"); return false; }
    QByteArray rgb = io->read(no);
    if (rgb.size() != no) { if (err) *err = QStringLiteral("short rgb"); return false; }
    QImage img(w, h, QImage::Format_RGB888);
    const uchar* s = reinterpret_cast<const uchar*>(rgb.constData());
    for (int y = 0; y < h; ++y) {
        uchar* line = img.scanLine(y);
        memcpy(line, s + size_t(y) * w * 3, size_t(w) * 3);
    }
    outImage = img;
    return true;
}

} // namespace LaEraseProto
```

- [ ] **Step 2: Commit**

The header has no translation unit of its own (inline-only) — compile verification happens with Task 2's stub build.

```bash
git add native/src/core/EraseProto.h
git commit -m "feat(erase): add EraseProto frame protocol (shared runner+stub)"
```

---

### Task 2: `inpaint_stub.exe` MinGW tool target (deterministic, no model)

**Files:**
- Create: `native/tools/inpaint_stub.cpp`
- Modify: `native/CMakeLists.txt` (add `add_executable(inpaint_stub ...)` tool target)

**Interfaces:**
- Consumes: `EraseProto.h` (Task 1).
- Produces: `inpaint_stub.exe` in the build dir at `$<TARGET_FILE_DIR:LightAlbum>/../inpaint_stub/inpaint_stub.exe`. Behavior: print `READY 1\n`; loop reading one `TASK` request; output = base with every mask==255 pixel set to gray (128,128,128), mask!=255 pixels copied byte-exact; write `OK ...`. Exits on stdin EOF.

- [ ] **Step 1: Write the stub**

Create `native/tools/inpaint_stub.cpp`:

```cpp
// 确定性 stub：实现 EraseProto 协议，mask==255 → 灰(128)，其余逐字节拷贝。
// 无模型、无随机、无网络。用于 LA_ERASE_QA 恒绿契约门。非发布产物。
#include <QCoreApplication>
#include <QIODevice>
#include <cstdio>
#include "core/EraseProto.h"

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    FILE* in = stdin;
    FILE* out = stdout;
    fprintf(out, "READY 1\n"); fflush(out);
    char hdr[256];
    while (fgets(hdr, sizeof(hdr), in)) {
        unsigned id; int w, h, nb, nm;
        if (sscanf(hdr, "TASK %u %d %d %d %d", &id, &w, &h, &nb, &nm) != 5) break;
        QByteArray rgb(nb, '\0');         fread(rgb.data(), 1, nb, in);
        QByteArray msk(nm, '\0');         fread(msk.data(), 1, nm, in);
        QByteArray res = rgb;             // mask 外逐字节不变
        const int px = w * h;
        for (int i = 0; i < px && i < msk.size(); ++i) {
            if ((uchar)msk[i] == 255) { res[i*3+0]=128; res[i*3+1]=128; res[i*3+2]=128; }
        }
        fprintf(out, "OK %u %d %d %d\n", id, w, h, res.size());
        fwrite(res.constData(), 1, res.size(), out);
        fflush(out);
    }
    return 0;
}
```

- [ ] **Step 2: Add the CMake tool target**

In `native/CMakeLists.txt`, after the `LightAlbum` target definition (before the LibRaw staging block ~line 148), add:

```cmake
# 2.35.0 AI 消除契约门用的确定性 stub（无模型、非发布产物）。见 EraseProto.h 协议。
add_executable(inpaint_stub tools/inpaint_stub.cpp)
target_include_directories(inpaint_stub PRIVATE src)
target_link_libraries(inpaint_stub PRIVATE Qt6::Core)
```

- [ ] **Step 3: Build**

Run: `cd native && CM="C:/Users/damin/.workbuddy/binaries/python/envs/default/Lib/site-packages/cmake/data/bin/cmake.exe"; export PATH="/d/qt/Tools/mingw1310_64/bin:$PATH"; "$CM" -S . -B build >/dev/null; "$CM" --build build -j 4 --target inpaint_stub 2>&1 | tail -5`
Expected: `Built target inpaint_stub`.

- [ ] **Step 4: Smoke the protocol manually**

Run (from build dir, feed one TASK: 2x2 black base, one erase pixel):
```bash
cd native && printf 'TASK 1 2 2 12 4\n' | cat - <(printf '\000\000\000\000\000\000\000\000\000\000\000\000\377\000\000\000') | ./build/inpaint_stub/inpaint_stub.exe 2>/dev/null | head -c 40 | xxd | head
```
Expected: stdout begins `4f4b 2031 2032 2032...` ("OK 1 2 2 ...") and the first erased RGB triple is `808080` (128,128,128).

- [ ] **Step 5: Commit**

```bash
git add native/tools/inpaint_stub.cpp native/CMakeLists.txt
git commit -m "feat(erase): add deterministic inpaint_stub tool target"
```

---

### Task 3: `EraseRunner` — persistent QProcess wrapper

**Files:**
- Create: `native/src/core/EraseRunner.h`, `native/src/core/EraseRunner.cpp`

**Interfaces:**
- Consumes: `EraseProto.h` (Task 1); resolves `inpaint.exe` OR `inpaint_stub.exe` (Task 2) next to the app under `<exe dir>/3rdparty/lama/bin/inpaint.exe`, falling back to `<build>/inpaint_stub/inpaint_stub.exe`.
- Produces:
  - `class EraseRunner : public QObject` with:
    - `explicit EraseRunner(QObject* parent=nullptr); ~EraseRunner();`
    - `bool ready();` — lazily spawn + await `READY 1` line (returns false if neither exe found).
    - `bool processId(qint64& pid);` — current child PID (0 if not running) — used by QA group C.
    - `bool run(quint32 id, const QImage& base, const QImage& mask, QImage& out, QString* err);` — one synchronous round-trip; auto-restarts child if not running; on child death returns false, sets `err`.
    - `void qaKillChild();` — kill the process to test crash recovery (QA group D only).
    - `static void qaInjectBadSize(bool on);` — QA counterexample hook (Task 5 + Task 9): when on, `run()` corrupts `out` with an out-of-bounds `copy` (→ null QImage) so `applyResult` honestly rejects it. Mirrors `FaceCluster::setForceBrokenForQa` static-hook pattern.
    - `QString exePath() const;` — resolved binary path (for logs).

- [ ] **Step 1: Header**

Create `native/src/core/EraseRunner.h`:

```cpp
#pragma once
#include <QObject>
#include <QProcess>
#include <QImage>
#include <QString>

class EraseRunner : public QObject {
    Q_OBJECT
public:
    explicit EraseRunner(QObject* parent = nullptr);
    ~EraseRunner() override;

    bool ready();                                        // spawn + await READY
    bool run(quint32 id, const QImage& base, const QImage& mask, QImage& out, QString* err);
    bool processId(qint64& pid) const;
    void qaKillChild();
    static void qaInjectBadSize(bool on);                // QA 反例钩子（Task 5/9 用）
    QString exePath() const { return exePath_; }

private:
    bool ensureStarted();
    QProcess* proc_ = nullptr;
    QString exePath_;
    bool ready_ = false;
};
```

- [ ] **Step 2: Implementation**

Create `native/src/core/EraseRunner.cpp` — resolve exe (release `3rdparty/lama/bin/inpaint.exe` → dev `inpaint_stub`), spawn `QProcess` with workingDirectory = the exe's own directory (real `inpaint.exe` loads `lama.onnx` relative to cwd; stub ignores it), `waitForReadyRead` for `READY`, then frame round-trips. `run()` is synchronous with a 20s hard timeout (matches watchdog). `qaKillChild()` calls `proc_->kill()` and clears `ready_`. Full source below:

```cpp
#include "EraseRunner.h"
#include "EraseProto.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

// ---- QA 反例状态（打开→run() 返回越界 copy 得到的 null QImage）----
static bool g_bad = false;
void EraseRunner::qaInjectBadSize(bool on) { g_bad = on; }

EraseRunner::EraseRunner(QObject* parent) : QObject(parent) {}
EraseRunner::~EraseRunner()
{
    if (proc_) { proc_->terminate(); if (!proc_->waitForFinished(2000)) proc_->kill(); }
}

static QString findExe()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    QStringList candidates = {
        appDir + "/3rdparty/lama/bin/inpaint.exe",
        appDir + "/inpaint_stub/inpaint_stub.exe",
        appDir + "/../inpaint_stub/inpaint_stub.exe",   // dev build tree
    };
    for (const QString& p : candidates) if (QFileInfo::exists(p)) return p;
    return QString();
}

bool EraseRunner::ensureStarted()
{
    if (ready_ && proc_ && proc_->state() == QProcess::Running) return true;
    if (exePath_.isEmpty()) exePath_ = findExe();
    if (exePath_.isEmpty()) return false;
    if (!proc_) proc_ = new QProcess(this);
    if (proc_->state() != QProcess::NotRunning) { proc_->kill(); proc_->waitForFinished(1000); }
    proc_->setProcessChannelMode(QProcess::SeparateChannels);
    proc_->setWorkingDirectory(QFileInfo(exePath_).absolutePath());   // lama.onnx 同目录解析
    proc_->start(exePath_, {});
    if (!proc_->waitForStarted(5000)) return false;
    if (!proc_->waitForReadyRead(5000)) return false;
    const QByteArray line = proc_->readLine();
    if (!line.startsWith("READY")) return false;
    ready_ = true;
    return true;
}

bool EraseRunner::ready() { return ensureStarted(); }

bool EraseRunner::processId(qint64& pid) const
{
    if (proc_ && proc_->state() == QProcess::Running) { pid = proc_->processId(); return true; }
    pid = 0; return false;
}

void EraseRunner::qaKillChild()
{
    if (proc_) proc_->kill();
    ready_ = false;
}

bool EraseRunner::run(quint32 id, const QImage& base, const QImage& mask, QImage& out, QString* err)
{
    if (!ensureStarted()) { if (err) *err = QStringLiteral("executor unavailable"); return false; }
    if (!LaEraseProto::writeRequest(proc_, id, base, mask, err)) return false;
    if (!proc_->waitForReadyRead(20000)) { if (err) *err = QStringLiteral("timeout"); qaKillChild(); return false; }
    if (!LaEraseProto::readResponse(proc_, id, out, err)) { qaKillChild(); return false; }
    // QA 反例：越界 copy → null QImage → applyResult 诚实拒收（期望值不动，铁律22）。
    if (g_bad && !out.isNull()) out = out.copy(0, 0, out.width() + 1, out.height());
    return true;
}
```

- [ ] **Step 3: Add to LightAlbum sources + build**

Modify `native/CMakeLists.txt` — add `src/core/EraseRunner.cpp` and `src/core/EraseRunner.h` to the LightAlbum target sources list (next to the existing `core/EraseBridge.*` entries). Then run the same build command as Task 2 Step 3 (full build, not just the stub target). Expected: `Built target LightAlbum` + `Built target inpaint_stub`, no errors.

- [ ] **Step 4: Commit**

```bash
git add native/src/core/EraseRunner.h native/src/core/EraseRunner.cpp
git commit -m "feat(erase): EraseRunner persistent QProcess + frame protocol client"
```

---

### Task 4: Wire EraseBridge to EraseRunner (real `modelReady` + `running` state)

**Files:**
- Modify: `native/src/core/EraseBridge.h` (add `EraseRunner* runner_`, inline QA forwarders `qaKillExecutorForTest()` / `qaPeekRunnerPid()`, status-comment update)
- Modify: `native/src/core/EraseBridge.cpp` (ctor + `modelReady()` + `submit()` async drive)

**Interfaces:**
- Consumes: `EraseRunner` (Task 3).
- Produces: `EraseBridge::modelReady()` returns `runner_->ready()`; `submit()` when ready launches the run and, while it's in flight, status is `running`; final `done`/`failed` via `applyResult`. State set for `EraseTask::status`: `queued | running | blocked_no_model | done | failed` (adds `running`).
- Note: this is a synchronous demo path (the UI is already wired); the run is driven by a 0-delay `QTimer::singleShot` so `submit` returns first, matching async UX.

- [ ] **Step 1: Header changes**

In `EraseBridge.h` private section add (with `class EraseRunner;` forward declaration above `class EraseBridge`):
```cpp
    class EraseRunner* runner_ = nullptr;
```
In the `public:` section add the two QA-only forwarders (used by Task 5 groups C/D):
```cpp
    // QA 钩子（LA_ERASE_QA 专用，不在正式 UI 路径上调用）：
    void qaKillExecutorForTest() { if (runner_) runner_->qaKillChild(); }
    void qaPeekRunnerPid(qint64& pid) const { if (runner_) runner_->processId(pid); }
```
Also update the `EraseTask::status` comment on line 16 to list the new state: `// queued / running / blocked_no_model / done / failed`.

- [ ] **Step 2: Implementation changes**

In `EraseBridge.cpp` include EraseRunner, build the runner in ctor, replace stub `modelReady()` with `return runner_ && runner_->ready();`. Rewrite `submit()` to enqueue + async run + write real status transitions (the two QA forwarders stay inline in the header from Step 1 — no .cpp definitions):

```cpp
#include "EraseRunner.h"
#include <QTimer>

EraseBridge::EraseBridge(QObject* parent) : QObject(parent), runner_(new EraseRunner(this)) {}

bool EraseBridge::modelReady() const { return runner_ && runner_->ready(); }

int EraseBridge::submit(const QImage& base, const QImage& mask)
{
    EraseTask t;
    t.id = nextId_++;
    t.base = base;
    t.mask = mask.convertToFormat(QImage::Format_ARGB32);
    if (modelReady()) { t.status = QStringLiteral("queued"); t.note = QStringLiteral("任务已入列，等待本地模型执行"); }
    else { t.status = QStringLiteral("blocked_no_model"); t.note = QStringLiteral("模型槽位空 · 任务挂起（零云端）"); }
    tasks_.append(t);
    emit taskUpdated(t.id);

    if (t.status == QLatin1String("queued")) {
        QTimer::singleShot(0, this, [this, id = t.id]() {
            EraseTask* q = nullptr;
            for (EraseTask& x : tasks_) if (x.id == id) { q = &x; break; }
            if (!q) return;
            q->status = QStringLiteral("running");
            emit taskUpdated(id);
            QImage out; QString err;
            const bool ok = runner_->run(quint32(id), q->base, q->mask, out, &err);
            applyResult(id, ok ? out : QImage());
            if (!ok) { for (EraseTask& y : tasks_) if (y.id == id) y.note = err; emit taskUpdated(id); }
        });
    }
    return t.id;
}
```

- [ ] **Step 3: Build (stub present → real submit now works)**

Run: same MinGW build command.
Expected: `[100%] Built target LightAlbum`.

- [ ] **Step 4: Commit**

```bash
git add native/src/core/EraseBridge.h native/src/core/EraseBridge.cpp native/CMakeLists.txt
git commit -m "feat(erase): EraseBridge drives EraseRunner; real modelReady + running state"
```

---

### Task 5: `LA_ERASE_QA` — hermetic contract gate (groups A/B/C/D + counterexample)

**Files:**
- Modify: `native/src/main.cpp` (add `int runEraseQa()` + dispatch)
- Modify: `native/gate_matrix.ps1` (register `LA_ERASE_QA`)

**Interfaces:**
- Consumes: `EraseBridge` + `EraseRunner` (Task 4), `inpaint_stub` (Task 2), `EraseRunner::qaInjectBadSize` (Task 3).
- Produces: `runEraseQa()` — offscreen, uses the stub (no model required). Assertions A/B/C/D. Counterexample `LA_ERASE_QA_FORCE_BROKEN=1` sets `qaInjectBadSize(true)` BEFORE the first submit: every `run()` result arrives null-sized, `applyResult` honestly rejects it, and the A/B/D assertions go RED together (expectations never branch — rule 22).
- Exit-code convention (qa_runner.ps1:56-65): ANY non-zero exit ⇒ RED; SKIP is NOT an exit code, it is roster `need=` gating. A missing stub means the dev build is broken → print FAIL and return 1 (never silently skip a contract gate).

- [ ] **Step 1: Add includes + the gate function**

In `native/src/main.cpp` add next to the other core QA includes (after `#include "core/MotionDriver.h"` at :46):

```cpp
#include "core/EraseBridge.h"   // 2.35.0 AI 消除契约门（LA_ERASE_QA 直调）
#include "core/EraseRunner.h"   // 2.35.0 执行器 QA 钩子（qaInjectBadSize）
```

Place `runEraseQa()` next to `runMotionQa()` (~`main.cpp:4311`):

```cpp
// 2.35.0 AI 消除契约门（离屏，走真 QProcess+帧协议，执行器=确定性 inpaint_stub）。
// A 状态机 queued→running→done；B mask 外逐字节守恒 + mask 内确有改动；
// C 子进程常驻（两次任务同 PID）；D kill 后下一任务自动重启恢复。
// 反例 LA_ERASE_QA_FORCE_BROKEN=1 → run() 返回越界 copy 的 null 结果 →
// applyResult 诚实拒收 → A/B/D 断言同红（期望值恒为规格值，铁律22；非空心，铁律34）。
int runEraseQa()
{
    const bool broken = !qEnvironmentVariableIsEmpty("LA_ERASE_QA_FORCE_BROKEN");
    QStringList fails;
    const auto note = [&fails](bool ok, const QString& w){ if (!ok) fails << w; };

    EraseBridge bridge;
    // 契约门不允许"静默放行"：stub 缺席 = 开发构建坏了 → 诚实 FAIL（return 非 0 即 RED）。
    if (!bridge.modelReady()) {
        std::fprintf(stderr, "LA_ERASE_QA FAIL executor unavailable -- build target inpaint_stub first\n");
        std::fflush(stderr);
        return 1;
    }
    if (broken) EraseRunner::qaInjectBadSize(true);   // 必须在首次 submit 之前打开

    const int w = 200, h = 200;
    QImage base(w, h, QImage::Format_RGB32);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
        base.setPixel(x, y, qRgb((x * 2) & 0xff, (y * 2) & 0xff, 0x40));
    QImage mask(w, h, QImage::Format_ARGB32);
    mask.fill(qRgba(0, 0, 0, 0));
    for (int y = 60; y <= 140; ++y) for (int x = 60; x <= 140; ++x) {
        const double dx = x - 100, dy = y - 100;
        if (dx * dx + dy * dy <= 40 * 40) mask.setPixel(x, y, qRgba(0, 0, 0, 255));
    }

    // ---- A 状态机 + B 像素守恒 ----
    const int id = bridge.submit(base, mask);
    note(bridge.task(id) != nullptr, QStringLiteral("A: task missing after submit"));
    QCoreApplication::processEvents();               // flush singleShot(0) 的 running→run→回填链
    const EraseTask* ct = bridge.task(id);
    note(ct && ct->status == QLatin1String("done"),
         QStringLiteral("A state machine reaches done, got=%1").arg(ct ? ct->status : QStringLiteral("?")));
    if (ct && ct->status == QLatin1String("done") && !ct->result.isNull() && ct->result.size() == base.size()) {
        int outDiff = 0, inDiff = 0, inTot = 0;
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
            const bool inside = qAlpha(mask.pixel(x, y)) == 255;
            if (base.pixel(x, y) != ct->result.pixel(x, y)) { inside ? ++inDiff : ++outDiff; }
            if (inside) ++inTot;
        }
        note(outDiff == 0, QStringLiteral("B mask-out byte conservation, leak=%1").arg(outDiff));
        note(inTot > 0 && inDiff * 10 >= inTot,
             QStringLiteral("B mask-in changed ratio=%1").arg(inTot ? double(inDiff) / inTot : -1.0));
    } else {
        note(false, QStringLiteral("B: done result present for pixel check"));
    }

    // ---- C 子进程常驻（同 PID 跨两次 submit）----
    qint64 pidA = 0, pidB = 0;
    bridge.qaPeekRunnerPid(pidA);
    const int id2 = bridge.submit(base, mask);
    QCoreApplication::processEvents();
    bridge.qaPeekRunnerPid(pidB);
    note(pidA != 0 && pidA == pidB,
         QStringLiteral("C child process persists, pidA=%1 pidB=%2").arg(pidA).arg(pidB));

    // ---- D 崩溃恢复（kill 后下一任务自动重启并走完状态机）----
    bridge.qaKillExecutorForTest();
    const int id3 = bridge.submit(base, mask);
    QCoreApplication::processEvents();
    const EraseTask* t3 = bridge.task(id3);
    note(t3 && t3->status == QLatin1String("done"),
         QStringLiteral("D after child kill next task recovers, got=%1").arg(t3 ? t3->status : QStringLiteral("?")));

    EraseRunner::qaInjectBadSize(false);             // 静态钩子复位（不得泄漏给后续门）

    if (fails.isEmpty())
        std::fprintf(stderr, "LA_ERASE_QA PASS broken=%d state=1 conserve=1 persist=1 recover=1\n", int(broken));
    else
        std::fprintf(stderr, "LA_ERASE_QA FAIL broken=%d reason=%s\n",
                     int(broken), qPrintable(fails.join(QChar(';'))));
    std::fflush(stderr);
    return fails.isEmpty() ? 0 : 1;
}
```

Note on the pixel compare: `base` is RGB32 and `ct->result` comes back as RGB888 via the protocol; `QImage::pixel()` normalizes both to `QRgb` with alpha=255, so `operator!=` on `QRgb` is a safe same-format comparison here (both opaque). The stub sets erased pixels to (128,128,128), so `inDiff/inTot == 1.0` and the ≥10% ratio always holds in the positive run; a leaky executor (touching a mask-out pixel) trips `outDiff != 0` immediately (rule 34).

- [ ] **Step 2: Dispatch in main.cpp**

Add next to `LA_MOTION_QA` dispatch (~`main.cpp:5826`):

```cpp
    if (qEnvironmentVariableIsSet("LA_ERASE_QA"))
        return runEraseQa();
```

- [ ] **Step 3: Build**

Run MinGW build. Expected: compiles.

- [ ] **Step 4: Run positive**

Run: `cd native && powershell -NoProfile -ExecutionPolicy Bypass -File ./qa_runner.ps1 -Only LA_ERASE_QA 2>&1 | tail -6`
Expected: `LA_ERASE_QA PASS broken=0 state=1 conserve=1 persist=1 recover=1` `[OK]`.

- [ ] **Step 5: Run counterexample**

Run: `LA_ERASE_QA_FORCE_BROKEN=1 powershell -NoProfile -ExecutionPolicy Bypass -File ./qa_runner.ps1 -Only LA_ERASE_QA 2>&1 | tail -3`
Expected: `LA_ERASE_QA FAIL broken=1 reason=A state machine reaches done ...` — non-hollow.

- [ ] **Step 6: measuredMs + register in gate_matrix.ps1 (pure ASCII)**

Run `-Timing` to get measuredMs. Then append the block below at the END of the `$gateTable` array in `native/gate_matrix.ps1` -- that is, directly after the current last entry `@{n="LA_TRANSITION_QA";  png=$false; measuredMs=400}` (line 522), before the closing `)` on line 523. Add a trailing comma to `LA_TRANSITION_QA`'s line; the NEW entry becomes the last one and carries NO trailing comma (the table's final entry never does). Keep every added line pure ASCII (铁律37):

```
  # 2.35.0 EraseBridge: hermetic contract gate (uses deterministic inpaint_stub.exe).
  # Asserts (A) state machine queued->running->done; (B) mask-out byte conservation
  # + mask-in change ratio >= 10%; (C) child process PID persists across submissions;
  # (D) after qaKillChild crash, next submit auto-restarts and completes. Counterexample
  # LA_ERASE_QA_FORCE_BROKEN=1 arms EraseRunner::qaInjectBadSize BEFORE the first submit;
  # run() then returns an out-of-bounds-copy (null) QImage, applyResult rejects it, and the
  # done/pixel/recovery assertions turn RED together (rule 22: expectations never branch).
  # Tier: FULL (QProcess lifecycle, no external env). measuredMs=<value from -Timing run above>.
  @{n="LA_ERASE_QA"; png=$false; measuredMs=<value from -Timing run above>}
```

- [ ] **Step 7: Self-test**

Run: `cd native && powershell -NoProfile -ExecutionPolicy Bypass -File ./gate_matrix.ps1 -SelfTest 2>&1 | tail -3`
Expected: `E1 gates=70 fast=18 full=52`, `E11 nonAsciiBytes=0`, `E12 textual=70 parsed=70`, `fails=0`.

- [ ] **Step 8: Commit**

```bash
git add native/src/core/EraseRunner.h native/src/core/EraseRunner.cpp native/src/core/EraseBridge.h native/src/main.cpp native/gate_matrix.ps1
git commit -m "test(erase): LA_ERASE_QA hermetic contract gate (A/B/C/D) + counterexample + roster #70"
```

---

### Task 6: Real `inpaint.exe` — MSVC project + ONNX Runtime

**Files:**
- Create: `inpaint/inpaint.cpp` (repo-root, outside `native/`)
- Create: `inpaint/CMakeLists.txt` (MSVC standalone)
- Create: `inpaint/BUILD.md`
- Create: `inpaint/THIRD_PARTY-NOTICE.md` (onnxruntime MIT)

**Interfaces:**
- Consumes: the same frame protocol (Task 1). Since `inpaint.exe` is MSVC, it CANNOT `#include` Qt headers (MinGW-only ABI), so it re-implements `packRgb`/`packMaskAlpha` inline (small, no Qt dependency, only stdio + `<vector>` + ONNX Runtime C++ headers). The protocol bytes are the contract.

- [ ] **Step 1: inpaint.cpp (skeleton, ~150 lines)**

```cpp
// ONNX LaMa 独立进程：读 TASK 帧 → 推理 → 写 OK 帧。无 Qt，纯 stdio + ORT C++ API。
// 编译：MSVC + onnxruntime 静态或动态库。见 BUILD.md。
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <onnxruntime_cxx_api.h>

// ---- LaMa 模型推理（session 只建一次，Env 必须先于 Session 存活且常驻）----
// 输入：base RGB (H,W,3) uint8 + mask (H,W) uint8 (255=erase)
// 输出：H,W,3 uint8 (mask 内为模型填充，mask 外逐字节等于 base)
static std::vector<uint8_t> runInpaint(const std::vector<uint8_t>& rgb,
                                        const std::vector<uint8_t>& msk,
                                        int w, int h)
{
    static Ort::Env env(ORT_LOGGING_LEVEL_ERROR, "inpaint");
    static Ort::Session* sess = [&env]() {
        Ort::SessionOptions opt;
        opt.SetIntraOpNumThreads(2);
        opt.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        // cwd 已被 EraseRunner 设为 exe 同目录（3rdparty/lama/bin），lama.onnx 同盘。
        return new Ort::Session(env, L"lama.onnx", opt);
    }();
    // 1) 组装 ONNX 输入 tensor (1,5,H,W): rgb[0..2]/255 + mask/255 + 全 1 通道。
    //    （channel 布局/节点名以下载到的 lama.onnx 为准，BUILD.md 有 netron 核对表。）
    // 2) sess->Run(...) → (1,3,H,W) float，*255 clamp [0,255]。
    // 3) 关键守恒：只把 mask==255 的像素写入模型输出，其余逐字节保留 rgb。
    std::vector<uint8_t> out = rgb;
    // ...（按 BUILD.md 的 input/output 节点名填充 2)3) 的实现）
    return out;
}

int main()
{
    fprintf(stdout, "READY 1\n"); fflush(stdout);
    char hdr[256];
    while (fgets(hdr, sizeof(hdr), stdin)) {
        unsigned id; int w, h, nb, nm;
        if (sscanf(hdr, "TASK %u %d %d %d %d", &id, &w, &h, &nb, &nm) != 5) break;
        std::vector<uint8_t> rgb(nb); fread(rgb.data(), 1, nb, stdin);
        std::vector<uint8_t> msk(nm); fread(msk.data(), 1, nm, stdin);
        auto res = runInpaint(rgb, msk, w, h);
        if (res.size() != size_t(nb)) { fprintf(stdout, "ERR %u SIZE\n", id); fflush(stdout); continue; }
        fprintf(stdout, "OK %u %d %d %d\n", id, w, h, int(res.size()));
        fwrite(res.data(), 1, res.size(), stdout); fflush(stdout);
    }
    return 0;
}
```

- [ ] **Step 2: inpaint/CMakeLists.txt (MSVC)**

```cmake
cmake_minimum_required(VERSION 3.21)
project(inpaint CXX)
set(CMAKE_CXX_STANDARD 17)
find_package(onnxruntime REQUIRED) # 用户提供 ONRUNTIME_ROOT 或已装到系统
add_executable(inpaint inpaint.cpp)
target_link_libraries(inpaint PRIVATE onnxruntime::onnxruntime)
```

- [ ] **Step 3: inpaint/BUILD.md**

Document: (a) ONNX Runtime download URL/version pin (v1.19.x Windows x64 C/C++ API build, MIT); (b) exact lama.onnx download + SHA-256 (verifies against `fetch_lama.ps1`); (c) how to enumerate lama.onnx input/output node names with `python -m onnxruntime.tools.symbolic_load` or `Netron`; (d) step-by-step MSVC build + where the outputs go.

- [ ] **Step 4: Commit**

```bash
git add inpaint/
git commit -m "feat(erase): inpaint.exe MSVC project + ONNX skeleton + BUILD.md"
```

---

### Task 7: `fetch_lama.ps1` — download model + LICENSE hard-check

**Files:**
- Create: `native/fetch_lama.ps1` (pure ASCII, mirrors `fetch_ffmpeg.ps1`)

**Interfaces:**
- Produces: downloads `inpaint.exe`, `onnxruntime.dll`, `lama.onnx`, `LICENSE` into `3rdparty/lama/bin/` (exe + dll + onnx) and `3rdparty/lama/LICENSE` (top). Fails (throws) if LICENSE missing/empty or SHA-256 mismatch. `-SelfTest` mode exercises the checks without network.

- [ ] **Step 1: Write fetch_lama.ps1 (ASCII only)**

Follow the exact structure of `fetch_ffmpeg.ps1` (read it as template) with these key blocks:
1. `$expectedSha` for `lama.onnx` (paste actual SHA from spec's chosen release).
2. `Invoke-WebRequest` for each artifact; if download fails → throw with clear "run again or build inpaint.exe per inpaint/BUILD.md" message.
3. After all downloads, **hard check**: `Test-Path $licensePath -and (Get-Item).Length -gt 0` else throw `"LaMa LICENSE missing -- cannot ship a model with unknown provenance"`.
4. `-SelfTest` mode: create a temp dir, drop fake `lama.onnx`/`LICENSE`/`inpaint.exe` with matching sha (fake), assert happy path OK; then remove LICENSE and assert throw; then corrupt SHA and assert throw.

Full skeleton with actual URLs is a **placeholder violation** — the implementer must fill URLs/SHA from spec §7 D4 model URL + `fetch_ffmpeg.ps1` template. Task 7 Step 2 documents: "Copy `fetch_ffmpeg.ps1` verbatim; replace `ffmpeg/ffprobe` names with `inpaint.exe/onnxruntime.dll/lama.onnx`; replace download URLs with the values from spec D4; add a mandatory `LICENSE` check mirroring lines 736-742 of `release.ps1`."

- [ ] **Step 2: SelfTest**

Run: `cd native && powershell -NoProfile -ExecutionPolicy Bypass -File ./fetch_lama.ps1 -SelfTest 2>&1 | tail -6`
Expected: `fetch_lama OK contracts=2 caught=2` (LICENSE-missing injection + SHA-mismatch injection both throw, happy path succeeds).

- [ ] **Step 3: Commit**

```bash
git add native/fetch_lama.ps1
git commit -m "feat(erase): fetch_lama.ps1 downloads model + LICENSE hard-check (ASCII)"
```

---

### Task 8: `release.ps1` — C9 license contract + lama staging

**Files:**
- Modify: `native/release.ps1` (~720 area + contract roster + header docs)

**Interfaces:**
- Produces:
  - New `Assert-LamaLicense($staging)` throws if `3rdparty/lama/bin/inpaint.exe` staged but `3rdparty/lama/LICENSE` missing/empty. Mirrors the LibRaw pattern (line 740).
  - New staging block 3.67: copy `inpaint.exe + onnxruntime.dll + lama.onnx + LICENSE` from `3rdparty/lama/{bin,.}` to staging; WARN (not throw) when the pre-build fetch was skipped (RAW/ffmpeg precedent — graceful).
  - Add `"C9"` to the `$contractIds` roster.
  - Add `-SelfTest` injection: remove `LICENSE` → assert C9 throws.

- [ ] **Step 1: Add the C9 doc line + $contractIds entry**

In `release.ps1`, add near the C1-C8 header (line 9-16):
```
#   C9 lama model license (inpaint.exe present in staging without LICENSE is a hard error)
```
And append `"C9"` to `$contractIds` (find via `grep $contractIds release.ps1`).

- [ ] **Step 2: Add the Assert-LamaLicense function**

Mirror `Assert-LicenseCompliance` (line 201) but for the lama directory. Full body:
```powershell
function Assert-LamaLicense($staging) {
  $binP = Join-Path $staging "3rdparty/lama/bin/inpaint.exe"
  if (-not (Test-Path $binP)) { return }   # not staged (graceful skip)
  $lic = Join-Path $staging "3rdparty/lama/LICENSE"
  if (-not (Test-Path $lic)) { throw "C9 inpaint.exe staged but 3rdparty/lama/LICENSE missing" }
  if ((Get-Item $lic).Length -le 0) { throw "C9 lama LICENSE is empty" }
  # onnxruntime MIT notice must travel too
  $mit = Join-Path $staging "3rdparty/lama/THIRD_PARTY-NOTICE.md"
  if (-not (Test-Path $mit)) { throw "C9 onnxruntime MIT notice missing from 3rdparty/lama" }
}
```

- [ ] **Step 3: Add the staging block after LibRaw (mirror 3.65)**

Follow the LibRaw block structure exactly. If `3rdparty/lama/bin/inpaint.exe` present, copy `inpaint.exe`, `onnxruntime.dll`, `lama.onnx`, plus `LICENSE` and `THIRD_PARTY-NOTICE.md` to staging; then call `Assert-LamaLicense $staging`; else `Write-Host "WARN: lama not fetched -- AI erase degrades to blocked_no_model in this build"`.

- [ ] **Step 4: Wire C9 into the main contract loop + add -SelfTest injection**

In `-SelfTest`, after existing C8 injections, add a case: stage `inpaint.exe` (touch file) + delete `LICENSE` → assert `Assert-LamaLicense` throws.

- [ ] **Step 5: Self-test release.ps1**

Run: `cd native && powershell -NoProfile -ExecutionPolicy Bypass -File ./release.ps1 -SelfTest 2>&1 | tail -3`
Expected: `LA_RELEASE_QA PASS contracts=9 injected=<N+1> caught=<N+1> baseline=<M+1>` — C9 caught by injection.

- [ ] **Step 6: Commit**

```bash
git add native/release.ps1
git commit -m "feat(release): C9 lama LICENSE contract + 3rdparty/lama staging + injection"
```

---

### Task 9: `LA_ERASE_MODEL_QA` — opt-in real-quality gate (pixel conservation with LaMa)

**Files:**
- Modify: `native/src/main.cpp` (add `runEraseModelQa()`)
- Modify: `native/gate_matrix.ps1` (register `LA_ERASE_MODEL_QA`)

**Interfaces:**
- Consumes: `EraseRunner` — resolves to real `inpaint.exe` if present, else the gate refuses to assert quality.
- Produces: real pixel conservation (mask-out byte-preserved, mask-in actually changed) using LaMa, plus a soft "no visible gray blob" sanity (mean-abs-diff inside mask < threshold, proving the fill isn't a constant stub-like fill). SKIP is done the runner way (qa_runner.ps1:74-82): the roster entry declares `need=@("LA_LAMA_MODEL")` (path to the fetched `lama.onnx`); when the env var is absent the gate never launches — that's the opt-in. When it IS present but `inpaint.exe` is missing or the resolved executor is the stub, the gate prints FAIL and returns non-zero (the env var promised a real model; honest red beats silent green).

- [ ] **Step 1: Add runEraseModelQa**

Insert in `native/src/main.cpp` right after `runEraseQa()` (added by Task 5). The roster `need=@("LA_LAMA_MODEL")` guarantees the runner only launches this gate when the user opted in; inside the gate, an absent/stub executor is therefore a RED (broken promise), never a silent skip.

```cpp
// 2.35.0 真模型质量门（opt-in）。前提由名册 need=@("LA_LAMA_MODEL") 把关：变量缺失时
// qa_runner 直接 [SKIP]，不启动本门。启动即意味着用户声称模型已 fetch：
// 此时若执行器仍不存在、或解析到的是 stub，都必须诚实 FAIL（绝不空心放行，铁律34）。
// M-A mask 外（1px 膨胀容差外）逐字节守恒；M-B mask 内确有合理改动；
// M-C 同输入两次提交逐通道 ±1 幂等。反例 LA_ERASE_MODEL_QA_FORCE_BROKEN=1 → 全部转红。
int runEraseModelQa()
{
    const bool broken = !qEnvironmentVariableIsEmpty("LA_ERASE_MODEL_QA_FORCE_BROKEN");
    QStringList fails;
    const auto note = [&fails](bool ok, const QString& w){ if (!ok) fails << w; };

    EraseRunner runner;
    if (!runner.ready()) {
        std::fprintf(stderr, "LA_ERASE_MODEL_QA FAIL executor unavailable though LA_LAMA_MODEL set\n");
        std::fflush(stderr); return 1;
    }
    if (!runner.exePath().endsWith(QStringLiteral("inpaint.exe"))) {
        std::fprintf(stderr, "LA_ERASE_MODEL_QA FAIL resolved executor is stub, not real inpaint.exe: %s\n",
                     qPrintable(runner.exePath()));
        std::fflush(stderr); return 1;
    }
    if (broken) EraseRunner::qaInjectBadSize(true);   // 首次提交前打开

    const int w = 256, h = 256;
    QImage base(w, h, QImage::Format_RGB32);          // 确定性渐变（宿主无关，铁律29）
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
        base.setPixel(x, y, qRgb((x + y) & 0xff, (x * 2) & 0xff, (y * 3) & 0xff));
    QImage mask(w, h, QImage::Format_ARGB32);
    mask.fill(qRgba(0, 0, 0, 0));
    for (int y = 96; y <= 160; ++y) for (int x = 96; x <= 160; ++x)
        mask.setPixel(x, y, qRgba(0, 0, 0, 255));     // 65x65 实心方块

    QImage out1; QString err;
    if (!runner.run(1, base, mask, out1, &err) || out1.isNull() || out1.size() != base.size()) {
        std::fprintf(stderr, "LA_ERASE_MODEL_QA FAIL first run: %s\n", qPrintable(err));
        std::fflush(stderr);
        EraseRunner::qaInjectBadSize(false);
        return 1;
    }
    QImage out2;
    const bool ok2 = runner.run(2, base, mask, out2, &err);

    // mask 3x3 膨胀（真模型羽化可能轻推紧邻像素；膨胀环带之外必须逐字节相同）
    auto dilated = [](const QImage& m, int x, int y) {
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
            const int xx = x + dx, yy = y + dy;
            if (xx < 0 || yy < 0 || xx >= m.width() || yy >= m.height()) continue;
            if (qAlpha(m.pixel(xx, yy)) == 255) return true;
        }
        return false;
    };
    int farDiff = 0, inDiff = 0, inTot = 0; double inAbs = 0.0;
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const QRgb a = base.pixel(x, y), b = out1.pixel(x, y);
        const bool inside = qAlpha(mask.pixel(x, y)) == 255;
        if (inside) {
            ++inTot;
            inAbs += (qAbs(qRed(a) - qRed(b)) + qAbs(qGreen(a) - qGreen(b)) + qAbs(qBlue(a) - qBlue(b))) / 3.0;
            if (a != b) ++inDiff;
        } else if (!dilated(mask, x, y) && a != b) {
            ++farDiff;
        }
    }
    note(farDiff == 0, QStringLiteral("M-A conservation outside dilated mask, diffs=%1").arg(farDiff));
    note(inTot > 0 && double(inDiff) / inTot >= 0.6,
         QStringLiteral("M-B mask-in change ratio=%1").arg(inTot ? double(inDiff) / inTot : -1.0));
    note(inTot > 0 && inAbs / inTot < 40.0,
         QStringLiteral("M-B mask-in mean-abs-diff=%1 (wild fill?)").arg(inAbs / (inTot ? inTot : 1)));

    // M-C 幂等：同输入两次，逐通道 ±1
    int maxCh = 0;
    if (ok2 && !out2.isNull() && out2.size() == out1.size()) {
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
            const QRgb p = out1.pixel(x, y), q = out2.pixel(x, y);
            maxCh = qMax(maxCh, qAbs(qRed(p) - qRed(q)));
            maxCh = qMax(maxCh, qAbs(qGreen(p) - qGreen(q)));
            maxCh = qMax(maxCh, qAbs(qBlue(p) - qBlue(q)));
        }
        note(maxCh <= 1, QStringLiteral("M-C idempotence max channel delta=%1").arg(maxCh));
    } else {
        note(false, QStringLiteral("M-C second run unavailable: %1").arg(err));
    }

    EraseRunner::qaInjectBadSize(false);
    if (fails.isEmpty())
        std::fprintf(stderr, "LA_ERASE_MODEL_QA PASS broken=%d conserve=1 fill=1 idempotent=1\n", int(broken));
    else
        std::fprintf(stderr, "LA_ERASE_MODEL_QA FAIL broken=%d reason=%s\n",
                     int(broken), qPrintable(fails.join(QChar(';'))));
    std::fflush(stderr);
    return fails.isEmpty() ? 0 : 1;
}
```

- [ ] **Step 2: Dispatch in main.cpp**

`if (qEnvironmentVariableIsSet("LA_ERASE_MODEL_QA")) return runEraseModelQa();` after `LA_ERASE_QA`.

- [ ] **Step 3: Register in gate_matrix.ps1 (ASCII)**

Append at the END of `$gateTable`, after the `LA_ERASE_QA` entry added by Task 5 (that entry now takes its trailing comma), so `LA_ERASE_MODEL_QA` becomes the new last entry with NO trailing comma before the closing `)`:

```
  # 2.35.0 opt-in real LaMa quality gate. need=LA_LAMA_MODEL (path to fetched lama.onnx):
  # absent -> qa_runner [SKIP] (non-regression, same as LA_SCAN_SMOKE/LA_RAW_QA). When the
  # env IS set but inpaint.exe is missing/resolves to the stub -> honest FAIL (rule 34).
  # Asserts M-A mask-out conservation (1px dilate tolerance), M-B plausible mask-in fill,
  # M-C idempotence. measuredMs=0 = never yet run opted-in (precedent: LA_SCAN_SMOKE
  # carries 0); E8 demands the KEY, not a guessed number. Backfill from -Timing the
  # first time the gate is actually run with a fetched model (rule 25/31: data, not a guess).
  @{n="LA_ERASE_MODEL_QA"; png=$false; need=@("LA_LAMA_MODEL"); measuredMs=0}
```

Do NOT invent a timing number (rule 25/31: measuredMs is data). When the user later fetches the model and runs the gate once, backfill the measured value in a follow-up chore commit.

- [ ] **Step 4: Verify roster self-test**

Run `gate_matrix.ps1 -SelfTest`. Expected: `E1 gates=71 fast=18 full=53`, `nonAsciiBytes=0`, `textual==parsed`, `fails=0`.

- [ ] **Step 5: Commit**

```bash
git add native/src/main.cpp native/gate_matrix.ps1
git commit -m "test(erase): LA_ERASE_MODEL_QA opt-in real LaMa quality gate (#71)"
```

---

### Task 10: Combined closure (bump 2.35.0 + CHANGELOG + guide §3/§7 + memory)

**Files:**
- Modify: `native/CMakeLists.txt` (VERSION 2.34.0 → 2.35.0)
- Modify: `native/CHANGELOG.md` (add `[2.35.0]` section covering both routes A + B)
- Modify: `native/docs/迭代指导书.md` (§3 gate count 69→71 fast=18/full=53; §7 ledger one combined row `N-erase-facecluster`)
- Create: `C:\Users\damin\.qoder-cn\projects\...\memory\erase-inpaint-external-process.md`
- Create: `C:\Users\damin\.qoder-cn\projects\...\memory\face-cluster-two-pass.md`
- Modify: `C:\Users\damin\.qoder-cn\projects\...\memory\MEMORY.md` (index pointers)

**Interfaces:**
- Consumes: routes A + B complete + green.

- [ ] **Step 1: Version bump**

Edit `native/CMakeLists.txt:2` `project(LightAlbumNative VERSION 2.34.0 ...)` → `2.35.0`.

- [ ] **Step 2: CHANGELOG `[2.35.0]`**

Copy the spec §1 (Decisions), §2 (EraseBridge), §3 (FaceCluster) content into a user-facing entry; add a regression line filled from Step 5 measurement.

- [ ] **Step 3: Full 71-gate regression (PowerShell)**

Run: `cd native && powershell -NoProfile -ExecutionPolicy Bypass -File ./qa_runner.ps1 2>&1 | tail -6`
Expected: `PASS=59 FAIL=0 CHECK=0 SKIP=12 KILL=0` — 69 baseline gates (58 PASS + 11 SKIP) plus `LA_ERASE_QA` (passes on the stub) plus `LA_ERASE_MODEL_QA` (`[SKIP]` when `LA_LAMA_MODEL` is unset). Wall-clock logged.

- [ ] **Step 4: Guide §3 count + §7 ledger row**

`docs/迭代指导书.md`:
- §3 line "**69 门 = fast 18 + full 51**" → "**71 门 = fast 18 + full 53**".
- Append §7 table row `| N-erase-facecluster | 2026-10-10 | 引擎升级：EraseBridge 从桩到真引擎 (ONNX) + FaceCluster 贪心→两遍精炼 | <根因/要点> | 全量 71 门并集 SUMMARY: PASS=<N> FAIL=0 CHECK=0 KILL=0 SKIP=<M>; LA_ERASE_QA/LA_FACE_CLUSTER_QA 均正/反例双测; 版本 2.34.0→**2.35.0** | 见提交记录 / 2.35.0 |`.

- [ ] **Step 5: Memory writeback (2 files + index)**

Write both memory files following the auto-memory template (frontmatter + type `project` for each). Add pointers to `MEMORY.md` index.

- [ ] **Step 6: Final commit (do NOT push without user confirmation)**

```bash
git add native/CMakeLists.txt native/CHANGELOG.md docs/迭代指导书.md docs/superpowers/...
git commit -m "release(2.35.0): EraseBridge ONNX executor + FaceCluster two-pass refinement"
```

**Await explicit user confirmation before `git push` and before running `release.ps1` end-to-end (both affect shared/external state).**
