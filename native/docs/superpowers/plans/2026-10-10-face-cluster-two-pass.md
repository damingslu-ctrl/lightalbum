# FaceCluster Two-Pass Refinement Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace FaceCluster's order-sensitive greedy single-link (head-representative) with a two-pass algorithm (greedy seed + up to 3 centroid-refinement iterations) that is order-invariant and uses a mean centroid as each cluster's representative.

**Architecture:** `FaceCluster::cluster()` (pure static, called from `main.cpp:3667 runFaceCluster` inside a QtConcurrent worker) is rewritten internally; the public signature is untouched so the caller and DB-write path don't change. New pure-function QA groups (O/C/S/D) + a second counterexample (`qaSetForceMergeForQa`) go into `runFaceClusterQa`; the 8 existing DB-integration groups (emb/conserve/group/write/sync/retrieve/rename/seed) stay untouched.

**Tech Stack:** C++17, Qt 6.8.3 (QString/QVector/QByteArray), MinGW 13.1, existing offscreen `qa_runner.ps1` harness.

**Spec:** `docs/superpowers/specs/2026-10-10-engine-upgrade-erase-facecluster-design.md` §3
**Branch order:** this is Route A — execute BEFORE `docs/superpowers/plans/2026-10-10-erase-bridge-onnx.md`. It adds no gate (count stays 69). The 2.35.0 bump, CHANGELOG, guide §3/§7 rows and memory writeback happen ONCE in Route B Task 10, which must read `PASS=<route A total>` as its baseline.

## Global Constraints

- 铁律 20：门禁断言测真算法，不另立桩。
- 铁律 22：期望值恒为规格值，绝不 `expect = broken ? X : Y`；两个反例开关都不得让期望式分支。
- 铁律 25/30/31：`measuredMs` 从 `qa_runner.ps1 -Timing` 实测回填，非估值；先测后定。
- 铁律 29：合成测试数据确定生成（硬编码向量，无随机、宿主无关）。
- 铁律 34：禁空心门 —— O 组在旧贪心下必红，是"证明真升级"的硬判别。
- 铁律 37：`gate_matrix.ps1` 改动区保持纯 ASCII（PS 5.1 GBK 解码会吞注释后的注册）。
- API 签名不变：`static ClusterResult cluster(const QVector<ClusterInput>&, double threshold=0.4, int startIndex=1)`。
- 守恒契约不变：`outputCount == inputCount`，每 path 恰出现一次。
- 版本随路线 B 合并到 2.35.0（本 plan 不改版本；版本 bump 属收口 task）。

---

### Task 1: Internal helpers — normalize + dot

**Files:**
- Modify: `native/src/core/FaceCluster.cpp` (add file-static helpers above `cosine`)

**Interfaces:**
- Consumes: nothing new.
- Produces (file-static, used by Task 2): `static QByteArray fcNormalize(const QByteArray& emb)` returns a unit-length copy of the float32 blob; `static double fcDotN(const float* a, const float* b, int dim)` returns the raw dot product (== cosine of two already-normalized vectors).

- [ ] **Step 1: Add the helpers**

Insert above the existing `FaceCluster::cosine` in `native/src/core/FaceCluster.cpp`:

```cpp
// 单位化一份 float32 embedding（长度非 4 倍数或零向量原样返回）。
static QByteArray fcNormalize(const QByteArray& emb)
{
    QByteArray out = emb;
    const int n = out.size() / 4;
    if (n <= 0) return out;
    float* f = reinterpret_cast<float*>(out.data());
    double s = 0.0;
    for (int i = 0; i < n; ++i) s += double(f[i]) * double(f[i]);
    if (s <= 0.0) return out;                     // 零向量：不除，保持原样
    const double inv = 1.0 / std::sqrt(s);
    for (int i = 0; i < n; ++i) f[i] = float(double(f[i]) * inv);
    return out;
}

// 两个已单位化向量的点积（== 余弦）。长度须一致。
static double fcDotN(const float* a, const float* b, int n)
{
    double d = 0.0;
    for (int i = 0; i < n; ++i) d += double(a[i]) * double(b[i]);
    return d;
}
```

- [ ] **Step 2: Verify it compiles (helpers unused yet)**

Run: `cd native && CM="C:/Users/damin/.workbuddy/binaries/python/envs/default/Lib/site-packages/cmake/data/bin/cmake.exe"; export PATH="/d/qt/Tools/mingw1310_64/bin:$PATH"; "$CM" --build build -j 4 2>&1 | tail -5`
Expected: build succeeds with `-Wunused-function` warning at worst (helpers not called yet). No error.

- [ ] **Step 3: Commit**

```bash
git add native/src/core/FaceCluster.cpp
git commit -m "feat(facecluster): add normalize/dot helpers for two-pass refinement"
```

---

### Task 2: Two-pass `cluster()` algorithm

**Files:**
- Modify: `native/src/core/FaceCluster.cpp:28-68` (rewrite `cluster()`; keep conservation counting + existing `g_forceBroken` drop)

**Interfaces:**
- Consumes: `fcNormalize`, `fcDotN` (Task 1); `FaceCluster::cosine` (unchanged, still public for QA reuse).
- Produces: same `ClusterResult cluster(...)`; introduces file-scope `static int fcRefine(const QVector<QByteArray>& norm, int dim, double threshold, QVector<int>& assign, QVector<QByteArray>& cents)` returning the number of reassigned items (0 == converged). Behavior: order-invariant partition, centroid = mean-of-members (renormalized).

- [ ] **Step 1: Rewrite `cluster()`**

Replace the body of `cluster` (keep the leading `static bool g_forceBroken` and its setter untouched — that's Task 3's sibling):

```cpp
// 一遍质心精炼：重算每簇质心（成员单位向量均值再单位化）→ 每 item 重分配到最近
// 质心（>=threshold），否则新建 singleton 簇。返回本轮 reassignment 数。
static int fcRefine(const QVector<QByteArray>& norm, int dim, double threshold,
                    QVector<int>& assign, QVector<QByteArray>& cents)
{
    const int g0 = cents.size();
    QVector<QVector<double>> sum(g0, QVector<double>(dim, 0.0));
    QVector<int> cnt(g0, 0);
    for (int i = 0; i < norm.size(); ++i) {
        const int g = assign[i];
        if (g < 0 || g >= g0) continue;
        const float* f = reinterpret_cast<const float*>(norm[i].constData());
        for (int d = 0; d < dim; ++d) sum[g][d] += f[d];
        ++cnt[g];
    }
    QVector<QByteArray> newCent(g0);
    for (int g = 0; g < g0; ++g) {
        if (cnt[g] == 0) { newCent[g] = cents[g]; continue; }
        QByteArray b(dim * 4, '\0');
        float* o = reinterpret_cast<float*>(b.data());
        double s = 0.0;
        for (int d = 0; d < dim; ++d) { const double v = sum[g][d] / cnt[g]; o[d] = float(v); s += v * v; }
        const double inv = s > 0.0 ? 1.0 / std::sqrt(s) : 0.0;
        for (int d = 0; d < dim; ++d) o[d] = float(o[d] * inv);
        newCent[g] = b;
    }
    cents = newCent;
    int changed = 0;
    for (int i = 0; i < norm.size(); ++i) {
        if (assign[i] == -2) continue;              // 维度非法：singleton 固定，不参与
        const float* f = reinterpret_cast<const float*>(norm[i].constData());
        int best = -1; double bestc = -1.0;
        for (int g = 0; g < cents.size(); ++g) {
            const double c = fcDotN(f, reinterpret_cast<const float*>(cents[g].constData()), dim);
            if (c >= threshold && c > bestc) { bestc = c; best = g; }
        }
        if (best < 0) { best = cents.size(); cents.append(norm[i]); }
        if (assign[i] != best) { assign[i] = best; ++changed; }
    }
    return changed;
}

ClusterResult FaceCluster::cluster(const QVector<ClusterInput>& items,
                                   double threshold, int startIndex)
{
    ClusterResult r;
    r.inputCount = items.size();
    const int dim = items.isEmpty() ? 0 : items.at(0).emb.size() / 4;

    QVector<QByteArray> norm;
    norm.reserve(items.size());
    for (const ClusterInput& it : items) norm.append(fcNormalize(it.emb));

    // ---- 种子：贪心 leader（与旧行为一致，仅用于给精环一个好起点）----
    QVector<int> assign(items.size(), -2);          // -2 = 维度非法 singleton
    QVector<QByteArray> cents;
    for (int i = 0; i < items.size(); ++i) {
        if (dim <= 0 || norm[i].size() / 4 != dim) { assign[i] = -2; continue; }
        const float* f = reinterpret_cast<const float*>(norm[i].constData());
        int best = -1; double bestc = -1.0;
        for (int g = 0; g < cents.size(); ++g) {
            const double c = fcDotN(f, reinterpret_cast<const float*>(cents[g].constData()), dim);
            if (c >= threshold && c > bestc) { bestc = c; best = g; }
        }
        if (best < 0) { best = cents.size(); cents.append(norm[i]); }
        assign[i] = best;
    }

    // ---- 精炼：≤3 轮，收敛早停 ----
    for (int iter = 0; iter < 3; ++iter)
        if (fcRefine(norm, dim, threshold, assign, cents) == 0) break;

    // ---- 落簇 + 守恒 ----
    const int nGroups = cents.size();
    for (int g = 0; g < nGroups; ++g) {
        ClusterGroup grp; grp.id = g;
        grp.label = QStringLiteral("人物%1").arg(startIndex + g);
        r.groups.append(grp);
    }
    // 维度非法项：每项自成一簇（追加在末尾，label 顺延），保持旧「非法 emb→新簇」语义。
    int extra = 0;
    for (int i = 0; i < items.size(); ++i) {
        int g = assign[i];
        if (g == -2) g = nGroups + (extra++);
        while (r.groups.size() <= g) {
            ClusterGroup grp; grp.id = r.groups.size();
            grp.label = QStringLiteral("人物%1").arg(startIndex + grp.id);
            r.groups.append(grp);
        }
        r.groups[g].paths.append(items.at(i).path);   // 天然保证每 path 恰一次
    }

    int out = 0;
    for (const ClusterGroup& g : r.groups) out += g.paths.size();
    r.outputCount = out;

    // QA 反例 1（原有语义）：从首个非空簇丢一张 → 守恒转红。
    if (g_forceBroken) {
        for (ClusterGroup& g : r.groups) {
            if (!g.paths.isEmpty()) { g.paths.removeFirst(); --r.outputCount; break; }
        }
    }
    return r;
}
```

- [ ] **Step 2: Build**

Run: `cd native && CM="C:/Users/damin/.workbuddy/binaries/python/envs/default/Lib/site-packages/cmake/data/bin/cmake.exe"; export PATH="/d/qt/Tools/mingw1310_64/bin:$PATH"; "$CM" --build build -j 4 2>&1 | tail -8`
Expected: `[100%] Built target LightAlbum` (no errors).

- [ ] **Step 3: Existing suite still green (algorithm is behavior-compatible on easy inputs)**

Run: `cd native && powershell -NoProfile -ExecutionPolicy Bypass -File ./qa_runner.ps1 -Only LA_FACE_CLUSTER_QA 2>&1 | tail -6`
Expected: `LA_FACE_CLUSTER_QA PASS ...` — the 8 existing groups (emb/conserve/group/...) still pass; new O/C/S/D not added yet.

- [ ] **Step 4: Commit**

```bash
git add native/src/core/FaceCluster.cpp
git commit -m "feat(facecluster): two-pass seed+centroid-refinement, order-invariant"
```

---

### Task 3: Second counterexample hook — `qaSetForceMergeForQa`

**Files:**
- Modify: `native/src/core/FaceCluster.h:44-45` (declare setter)
- Modify: `native/src/core/FaceCluster.cpp` (add `g_forceMerge` + injection at end of `cluster`)

**Interfaces:**
- Consumes: `cluster()` result (Task 2).
- Produces: `static void FaceCluster::qaSetForceMergeForQa(bool on)` — when on, after normal clustering, collapse ALL members into `groups[0]` (single giant cluster). This breaks D (不同人必分簇) and C (质心优于头) without touching any expected value.

- [ ] **Step 1: Declare the setter**

In `native/src/core/FaceCluster.h`, after `static void setForceBrokenForQa(bool on);`:

```cpp
    // QA 反例 2：打开后把所有成员塌进 group 0（破坏分簇语义，测 O/C/D）。
    static void qaSetForceMergeForQa(bool on);
```

- [ ] **Step 2: Add the flag + injection**

In `native/src/core/FaceCluster.cpp`, next to `static bool g_forceBroken = false;`:

```cpp
static bool g_forceMerge = false;
void FaceCluster::qaSetForceMergeForQa(bool on) { g_forceMerge = on; }
```

At the end of `cluster()`, right before `return r;` (after the `g_forceBroken` block):

```cpp
    // QA 反例 2：全塌进 group 0（守恒不变，但分簇/质心判别必红）。
    if (g_forceMerge && r.groups.size() > 1) {
        for (int g = 1; g < r.groups.size(); ++g)
            for (const QString& p : r.groups[g].paths)
                r.groups[0].paths.append(p);
        // 保留 groups[0]，清空其余成员（成员总数不变→守恒仍成立）
        for (int g = 1; g < r.groups.size(); ++g) r.groups[g].paths.clear();
    }
    return r;
```

- [ ] **Step 3: Build**

Run (same as Task 2 Step 2). Expected: builds.

- [ ] **Step 4: Commit**

```bash
git add native/src/core/FaceCluster.h native/src/core/FaceCluster.cpp
git commit -m "feat(facecluster): add qaSetForceMergeForQa counterexample hook"
```

---

### Task 4: Pure-function QA groups O / C / S / D

**Files:**
- Modify: `native/src/main.cpp` inside `runFaceClusterQa` (insert a pure block AFTER the existing `fails`/`note` declarations at `main.cpp:3783-3786`, before the `// ① 索引自检` comment at `:3788`)
- Modify: `native/src/main.cpp` includes (add `<QDataStream>` and `<algorithm>` next to `<cmath>` at `main.cpp:29` — neither is currently included)

**Interfaces:**
- Consumes: `FaceCluster::cluster`, `FaceCluster::cosine` (Task 2); existing `QStringList fails` + `note` lambda (main.cpp:3783-3786 — reuse them, do NOT re-declare).
- Produces: the four pure assertions fold into the SAME `fails` list, so the existing `okAll = fails.isEmpty()` (main.cpp:3925) already turns the gate RED when any of O/C/S/D fails. Local `const bool pureOk = fails.isEmpty();` captured right after the pure block feeds the four PASS-line flags.

- [ ] **Step 1: Add the include**

Next to `#include <cmath>` (main.cpp:29):

```cpp
#include <algorithm>          // 2.35.0 std::reverse / std::rotate（O 组顺序不变性排列）
```

- [ ] **Step 2: Insert the pure block**

Immediately after the existing `note` lambda definition (`main.cpp:3786`, i.e. after `};` of `const auto note = ...`) and BEFORE the `// ① 索引自检 + 守恒基线` comment, insert (no re-declaration of `fails`/`note` — they already exist above):

```cpp
    // ---- 2.35.0 两遍聚类纯函数判别组（O/C/S/D，铁律34：O 在旧贪心下必红）----
    // ---- 合成 embedding（确定性，无随机，铁律29）----
    // ⚠ 打包必须走本机 float 字节序（与 FaceCluster::cosine 的 reinterpret_cast 读法
    //   同源）。禁用 QDataStream（默认大端会把小端读法打成乱序）。见 unitVec 注释同款坑。
    auto blob = [](std::initializer_list<double> v) {
        QByteArray b;
        for (double x : v) { const float f = float(x);
                             b.append(reinterpret_cast<const char*>(&f), 4); }
        return b;
    };
    // 两个"不同人"方向（正交）+ 各自近重复（同向量+固定微扰），dim=8 足够证算法
    const QByteArray A  = blob({1,0,0,0,0,0,0,0});
    const QByteArray Ap = blob({0.999,0.02,0,0,0,0,0,0});     // 同人噪声代表
    const QByteArray App= blob({0.998,0.03,-0.01,0,0,0,0,0});
    const QByteArray B  = blob({0,0,0,0,0,0,0,1});
    const QByteArray Bp = blob({0,0,0,0,0,0,0.02,0.999});
    const double kThr = 0.4;

    struct CI { QString p; QByteArray e; };
    auto build = [](QVector<CI> in){
        QVector<ClusterInput> o; for (auto& x : in){ ClusterInput c; c.path=x.p; c.emb=x.e; o<<c; } return o; };

    // 集合意义分区（不依赖簇序）：path→成员集合，规范化后比较
    auto partitionKey = [](const ClusterResult& r){
        QStringList parts;
        for (const ClusterGroup& g : r.groups) {
            QStringList m = g.paths; m.sort();
            if (!m.isEmpty()) parts << m.join(QChar('|'));
        }
        parts.sort(); return parts.join(QChar(';'));
    };

    // ---- O 顺序不变性 ----
    {
        const QVector<ClusterInput> base = build({{"A1",A},{"A2",Ap},{"A3",App},{"B1",B},{"B2",Bp},{"A4",A},{"C1",blob({0.5,0.5,0,0,0,0,0,0})},{"C2",blob({0.5,0.5,0.02,0,0,0,0,0})},{"B3",B},{"A5",Ap},{"B4",Bp},{"A6",App}});
        const QVector<ClusterInput> p1 = base;
        QVector<ClusterInput> p2 = base; std::reverse(p2.begin(), p2.end());
        QVector<ClusterInput> p3 = base; for (int i=1;i<p3.size();++i) std::swap(p3[i], p3[i-1]); // 定步乱排
        const QString k1 = partitionKey(FaceCluster::cluster(p1, kThr, 1));
        const QString k2 = partitionKey(FaceCluster::cluster(p2, kThr, 1));
        const QString k3 = partitionKey(FaceCluster::cluster(p3, kThr, 1));
        note(k1 == k2 && k2 == k3, QStringLiteral("O order-invariance: partition differs across permutations"));
    }

    // ---- C 质心优于头 ----
    {
        const QVector<ClusterInput> in = build({{"A1",A},{"A2",Ap},{"A3",App},{"B1",B},{"B2",Bp}});
        const ClusterResult r = FaceCluster::cluster(in, kThr, 1);
        bool better = true;
        for (const ClusterGroup& g : r.groups) {
            if (g.paths.size() < 2) continue;
            // 成员向量集合
            QVector<QByteArray> memb; for (const QString& p : g.paths) for (const ClusterInput& c : in) if (c.path==p) memb<<c.emb;
            const QByteArray& head = memb.first();
            // 质心 = 成员均值再单位化
            const int n = memb.first().size()/4;
            QVector<double> acc(n, 0.0);
            for (const QByteArray& e : memb){ const float* f=(const float*)e.constData(); for(int d=0;d<n;++d) acc[d]+=f[d]; }
            double s=0; QByteArray cb(n*4,'\0'); float* o=(float*)cb.data();
            for(int d=0;d<n;++d){ double v=acc[d]/memb.size(); o[d]=(float)v; s+=v*v; }
            double inv = s>0?1.0/std::sqrt(s):0.0; for(int d=0;d<n;++d) o[d]=(float)(o[d]*inv);
            double sumCent=0, sumHead=0;
            for (const QByteArray& e : memb){ sumCent += FaceCluster::cosine(e, cb); sumHead += FaceCluster::cosine(e, head); }
            if (!(sumCent/memb.size() >= sumHead/memb.size() - 1e-9)) better = false;
        }
        note(better, QStringLiteral("C centroid must not be worse than head-rep per cluster"));
    }

    // ---- S 同人必同簇（3 排列）----
    {
        const QVector<ClusterInput> base = build({{"A1",A},{"A2",Ap},{"A3",App},{"B1",B},{"B2",Bp}});
        bool ok = true;
        auto sameCluster = [](const ClusterResult& r, const QString& x, const QString& y){
            for (const ClusterGroup& g : r.groups) if (g.paths.contains(x) && g.paths.contains(y)) return true; return false; };
        for (int rot = 0; rot < 3; ++rot) {
            QVector<ClusterInput> in = base; std::rotate(in.begin(), in.begin()+rot, in.end());
            const ClusterResult r = FaceCluster::cluster(in, kThr, 1);
            if (!sameCluster(r,"A1","A2") || !sameCluster(r,"A2","A3") || !sameCluster(r,"B1","B2")) ok = false;
        }
        note(ok, QStringLiteral("S same-person must cluster together across permutations"));
    }

    // ---- D 异人必分簇 ----
    {
        const QVector<ClusterInput> in = build({{"A1",A},{"A2",Ap},{"B1",B},{"B2",Bp}});
        const ClusterResult r = FaceCluster::cluster(in, kThr, 1);
        bool seps = true;
        for (const ClusterGroup& g : r.groups)
            if (g.paths.contains("A1") && (g.paths.contains("B1")||g.paths.contains("B2"))) seps = false;
        note(seps, QStringLiteral("D different-person must not share a cluster"));
    }

    const bool pureOk = fails.isEmpty();   // 纯组判别快照（供 PASS 行四个标志）
```

- [ ] **Step 3: Wire the four flags into the PASS line**

The existing PASS `fprintf` is at `main.cpp:3926-3929` (marker `emb=1 conserve=1 ... rename=1`). Replace ONLY the success branch (the `else` FAIL branch stays byte-for-byte as today):

```cpp
    const bool okAll = fails.isEmpty();
    if (okAll) {
        std::fprintf(stderr,
            "LA_FACE_CLUSTER_QA PASS rows=%d broken=%d emb=1 conserve=1 group=1 write=1 sync=1 retrieve=1 rename=1 order=%d centroid=%d sameper=%d diffper=%d\n",
            kRows, int(broken), int(pureOk), int(pureOk), int(pureOk), int(pureOk));
    } else {
        std::fprintf(stderr, "LA_FACE_CLUSTER_QA FAIL broken=%d reason=%s\n",
                     int(broken), qPrintable(fails.join(QChar(';'))));
    }
```

Any O/C/S/D failure already pushed a `fails` entry in Step 2, so `okAll` is false and the gate prints FAIL with the reason — the flags can never show `1` on a hollow pass.

- [ ] **Step 4: Build + run positive**

Run: `cd native && CM="C:/Users/damin/.workbuddy/binaries/python/envs/default/Lib/site-packages/cmake/data/bin/cmake.exe"; export PATH="/d/qt/Tools/mingw1310_64/bin:$PATH"; "$CM" --build build -j 4 >/dev/null 2>&1; powershell -NoProfile -ExecutionPolicy Bypass -File ./qa_runner.ps1 -Only LA_FACE_CLUSTER_QA 2>&1 | tail -4`
Expected: `... order=1 centroid=1 sameper=1 diffper=1` and `[OK]`.

- [ ] **Step 5: Verify O is a REAL discriminator — temporarily prove it fails on old algo**

This is a self-check, not a committed change. Temporarily set the refinement loop bound `iter < 3` to `iter < 0` (seed-only == old greedy), rebuild, run:
Expected: `LA_FACE_CLUSTER_QA FAIL ... reason=O order-invariance...` (proves O catches the pre-upgrade behavior). **Then revert to `iter < 3`** and rebuild. Record that you verified this.

- [ ] **Step 6: Commit**

```bash
git add native/src/main.cpp
git commit -m "test(facecluster): add O/C/S/D pure-function QA groups to LA_FACE_CLUSTER_QA"
```

---

### Task 5: Counterexample runs + measuredMs backfill + roster

**Files:**
- Modify: `native/gate_matrix.ps1` (`LA_FACE_CLUSTER_QA` comment + `measuredMs`)

**Interfaces:**
- Consumes: `FaceCluster::qaSetForceMergeForQa` (Task 3).
- Produces: nothing downstream.

- [ ] **Step 1: Add the FORCE_MERGE dispatch in main.cpp**

`runFaceClusterQa` already toggles the first counterexample at `main.cpp:3781` (`FaceCluster::setForceBrokenForQa(broken);`). Extend that spot with the second hook — replace that single line with:

```cpp
    FaceCluster::setForceBrokenForQa(broken);
    FaceCluster::qaSetForceMergeForQa(!qEnvironmentVariableIsEmpty("LA_FACE_CLUSTER_QA_FORCE_MERGE"));
```

In the same function's reset block (`main.cpp:3919-3921`, next to the existing `FaceCluster::setForceBrokenForQa(false);`), add the matching reset so the static hook cannot leak into later gates:

```cpp
    FaceCluster::qaSetForceMergeForQa(false);
```

(If Task 4 shifted these line numbers, locate by the unique strings `setForceBrokenForQa(broken)` and `// 复位静态钩子` rather than absolute lines.)

- [ ] **Step 2: Run both counterexamples → RED (non-hollow)**

Run: `cd native && powershell -NoProfile -ExecutionPolicy Bypass -File ./qa_runner.ps1 -Only LA_FACE_CLUSTER_QA 2>&1 | grep -E "PASS|FAIL|OK"`
Then the two injections (env forwarded to the exe):
```
LA_FACE_CLUSTER_QA_FORCE_BROKEN=1 powershell -NoProfile -ExecutionPolicy Bypass -File ./qa_runner.ps1 -Only LA_FACE_CLUSTER_QA 2>&1 | tail -3
LA_FACE_CLUSTER_QA_FORCE_MERGE=1  powershell -NoProfile -ExecutionPolicy Bypass -File ./qa_runner.ps1 -Only LA_FACE_CLUSTER_QA 2>&1 | tail -3
```
Expected: positive `[OK]`; BROKEN → `FAIL ... conserve` (drop breaks conservation); MERGE → `FAIL ... D .../C` (collapse breaks separation). Both non-hollow.

- [ ] **Step 3: measuredMs from -Timing**

Run: `cd native && powershell -NoProfile -ExecutionPolicy Bypass -File ./qa_runner.ps1 -Only LA_FACE_CLUSTER_QA -Timing 2>&1 | grep -E "\(.*ms\)"`
Expected: e.g. `LA_FACE_CLUSTER_QA  [OK] (NNNms)`.

- [ ] **Step 4: Backfill roster `measuredMs` (ASCII-only)**

In `native/gate_matrix.ps1`, update the existing `@{n="LA_FACE_CLUSTER_QA"; ... measuredMs=<old>}` to the measured value. If the comment block above it references algorithm, add an ASCII line:
```
  # 2.35.0: two-pass (greedy seed + centroid refine); groups O(order)/C(centroid)/
  # S(same-person)/D(different-person) added; FORCE_MERGE collapses all -> C/D RED.
```
Confirm the whole file stays pure ASCII (no CJK added).

- [ ] **Step 5: Self-test roster**

Run: `cd native && powershell -NoProfile -ExecutionPolicy Bypass -File ./gate_matrix.ps1 -SelfTest 2>&1 | tail -3`
Expected: `E11 nonAsciiBytes=0`, `E12 textual==parsed`, `fails=0` (still 69 gates — no new gate added in this route).

- [ ] **Step 6: Commit**

```bash
git add native/gate_matrix.ps1 native/src/main.cpp
git commit -m "chore(facecluster): backfill measuredMs + FORCE_MERGE dispatch + roster note"
```

---

### Task 6: Full-suite regression for FaceCluster

**Files:** none (verification only; no version bump in this task — 2.35.0 bump is done once in the combined closure).

- [ ] **Step 1: Full suite (PowerShell)**

Run: `cd native && powershell -NoProfile -ExecutionPolicy Bypass -File ./qa_runner.ps1 2>&1 | tail -6`
Expected: `PASS=58 FAIL=0 CHECK=0 SKIP=11 KILL=0` (same counts as 2.34.0 baseline — FaceCluster gate stays green, no new gate yet), `LA_FACE_CLUSTER_QA` shows the 4 new flags =1.

- [ ] **Step 2: If FAIL=0, this route is done**

Record wall clock. If a regression appears, diagnose against the algorithm change before touching other routes.

- [ ] **Step 3: No commit** (verification only; closure CHANGELOG/ledger happens in the combined route-B closure or a shared closeout task).
