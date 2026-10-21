# 波4「兑现」实施计划 — 典藏补齐 + 时间地基 + 色债治理

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development
> (recommended) or superpowers:executing-plans to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 2.42.0 界面上已经说出口的三句越线承诺兑现成真（合集有去处、静音可反悔、⌘K 暂存键可用），
并把已测到的真债（时间查询全表载入、画布层硬写色、列数两套、最小窗宽缺失）在同一波里还掉；
版本收口 2.42.0 → 2.43.0。

**Architecture:** A 轨（用户可见）先行：`Hall::Collection` + `CollectionPage` 三 tab、凝视键位带、
前厅生成期骨架、⌘K 暂存激活、四页空态；B 轨（地基）随后：`photos` 三列 + 一年月索引 + 一次性回填、
`GalleryModel` 索引预过滤（唯一消费方）、`Layout.h` 三网格共用、画布层 4 个 lamp token、
棘轮 C6 / C3 收紧、最小窗宽。两轨共用一道新门 `LA_COLLECTION_QA`（A–I 组）。

**Tech Stack:** C++17 / Qt 6.8.3 / MinGW 13.1 / SQLite（WAL）/ CMake / PowerShell 5.1 门禁。

**Spec（权威，冲突时以此为准）:** `native/docs/superpowers/specs/2026-10-21-wave4-promise-kept-design.md`
（commit `9528fed` + 本计划的 R11 修订）

**BASE:** 计划落盘时的 `HEAD`（执行 T1 前用 `git rev-parse HEAD` 记录到台账）。

---

## 0 · 全局约束（每个任务都适用，违反即回滚）

1. **门禁只许通过 PowerShell 跑**：`powershell -NoProfile -ExecutionPolicy Bypass -File iterate.ps1 -Only <GATE>`。
   绝不用 Git Bash/MSYS 直跑 exe（Qt stdout 会被吞，结论不可信）。
2. **`native/gate_matrix.ps1` 必须保持纯 ASCII 字节**（CJK 注释会被 PS 5.1 按 GBK 解码吞掉整行门）。
   每次编辑后跑 `-SelfTest`，E11/E12 必须绿。
3. **GUI 线程唯一写者**：`Library` 无锁。任何写库（`createAlbum` / `muteTemplate` / `unmuteTemplate` /
   回填）只允许在 GUI 线程；后台只算不写。
4. **迁移 fail-open**：`Library::open` 的 `la_mig` 块任何 ALTER/回填失败都必须被吞掉，后续只读 SELECT
   照旧；老库不可因本波打不开。
5. **界面文字即承诺**：legend 里每一枚键、空态里每一个入口名、⌘K 提示里每一个样例，落盘前必须实测有效。
6. **i18n**：手工 append `native/i18n/LightAlbum_en.ts`，然后
   `D:/qt/6.8.3/mingw_64/bin/lrelease.exe native/i18n/LightAlbum_en.ts -qm native/i18n/LightAlbum_en.qm`；
   **`.ts` 与 `.qm` 同一提交**。**`lupdate` 禁用**。
7. **棘轮只降不升**：`qa_palette_ratchet.ps1` 的上限只能向下 `-Update`；C3 收紧必须先清零再改定义。
8. **门禁数字必须实测**：`measuredMs`、腿数、E1 的 gates/fast/full 一律填本次实测值，不许沿用旧数。
9. **提交纪律**：只 `git add` 精确路径；**绝不 `git add -A`**；提交前看 `git diff --numstat --cached`。
   `native/_scale_fixture.db` 与约 199 个未跟踪草稿不是本波的，永不加入提交。
10. **不做**：不 push、不 merge、不打 tag、不打包、不新增图标/模型/云端调用。

**构建命令（每任务收尾必跑）：**
`powershell -NoProfile -ExecutionPolicy Bypass -File iterate.ps1 -Only <GATE>`（会先构建再跑门）。
仅构建：`powershell -NoProfile -ExecutionPolicy Bypass -File iterate.ps1 -NoGates`（若脚本无此开关，
用 `iterate.ps1 -Only LA_SMOKE` 代替）。

---

## 1 · 文件结构（本波动作）

| 动作 | 文件 | 责任 |
|---|---|---|
| 新建 | `src/ui/CollectionPage.{h,cpp}` | 典藏三 tab：合集 / 展览史 / 安静下来的选题；只读 `Library` |
| 新建 | `src/ui/Layout.h` | `gridCols()` 纯算术（三网格唯一真源） |
| 改 | `src/ui/Theme.h` | `Hall::Collection` + `follow[7]` + 画布层 4 访问器 |
| 改 | `src/ui/MainWindow.{h,cpp}` | `kRow*` 归一、导航第 8 行、栈第 8 页、pending 接线、`setMinimumSize` |
| 改 | `src/core/Library.{h,cpp}` | `albumPaths` / `listMutes` / `unmuteTemplate` / 三列迁移 + 回填 / `pathsForShot*` |
| 改 | `src/core/Scanner.cpp` | `kExtraColumns` 三列 + UPSERT 绑定（唯一写点） |
| 改 | `src/core/CurateScheduler.{h,cpp}` | `keepSelection` / `unmuteExhibit` / 两条 ack 文案 |
| 改 | `src/ui/GalleryModel.{h,cpp}` | `setShotSource` + 年/月/季索引预过滤 |
| 改 | `src/ui/GalleryPage.{h,cpp}` | 转发 `setShotSource` + 公开 `selectedPaths()` + 空态 |
| 改 | `src/ui/CmdKPanel.{h,cpp}` | 暂存键激活、`setStashAvailable`、`stashRequested`、空解析说明 |
| 改 | `src/ui/GazeLightbox.{h,cpp}` | 键位带 + Space/Enter 实现 + `mode()` 访问器 |
| 改 | `src/ui/LobbyPage.{h,cpp}` | `setPending(bool)` 骨架 + 呼吸字 |
| 改 | `src/ui/{RetouchPage,LogPage,MapPage}.cpp` | 空态补齐 |
| 改 | 5 个自绘 widget | 画布层 token 替换（`CropCanvas` / `MaskCanvas` / `EditorialView` / `CompareWidget` / `VideoPlayerWidget`） |
| 改 | `src/main.cpp` | 新门 `runCollectionQa()` + dispatch + 既有门改线 |
| 改 | `native/gate_matrix.ps1` | 名册 80 → 81（纯 ASCII） |
| 改 | `native/qa_palette_ratchet.ps1` + `native/qa/palette_baseline.json` | C6 + C3 |
| 改 | `native/i18n/LightAlbum_en.ts` + `.qm` | 本波新增串 |
| 改 | `native/CMakeLists.txt` / `CHANGELOG.md` / `docs/迭代指导书.md` / 新手册 | 收口 |

---

## 2 · 任务总览与顺序

顺序不可换的点用 **→** 标出：

- **T1** 行号归一（§9）**→ 必须先于 T4 的导航插行**
- **T2** Library A 轨 API + Scheduler 两个方法 + `LA_COLLECTION_QA` 骨架（A/B 组）
- **T3** `Layout.h` + `Hall::Collection` + `follow[]` 扩格（G 组先行红→绿）
- **T4** `CollectionPage` + 导航/栈/凝视接线（C/D 组）
- **T5** 两条 ack 文案翻转 **→ 文案 + `M_ack_*` + `ctxCheck` + `.ts/.qm` 必须同一提交**（E 组）
- **T6** ⌘K 暂存键激活 + 空解析说明（H 组）
- **T7** 凝视键位带 + Space/Enter（`LA_GAZE_QA` 加腿）
- **T8** 前厅生成期骨架（`LA_LOBBY_QA` 加腿）
- **T9** 四页空态
- **T10** B 轨三列 + 一次性回填（Scanner 唯一写点）
- **T11** B 轨查询 + GalleryModel 预过滤（I 组：parity/EXPLAIN/隐私）
- **T12** 画布层 token + C3/C6 + D3 宪章 + 最小窗宽
- **T13** 名册收口 + i18n 全量 + 2.43.0 + 81 门回归

---

## T1 · 行号归一（§9，先于一切导航改动）

**Files:**
- Modify: `src/ui/MainWindow.cpp`（`kNav` 之后 ~`:130`；8 处 `setCurrentRow`；`onNav` 预设谓词 ~`:1265`）

**为什么先做：** 现在 `kNav` 是 7 行，`MainWindow` 里有 8 处裸行号
（`:219/507/526/531/537/543/641/1129`）。T4 在中间插一行会把它们全部错位；先归一，插入才是机械操作。

- [ ] **Step 1: 加行常量和编译期护栏**

在 `kNavIcons[]` 定义之后、匿名命名空间内加：

```cpp
// 波4 §9：导航行序单点真源。kNav 增删行时只改这里；static_assert 拦住「改了表忘了改常量」。
enum NavRow {
    kRowLobby = 0, kRowMap, kRowGallery, kRowPeople,
    kRowStudio, kRowLog, kRowAbout, kRowCount
};
static_assert(int(kRowCount) == int(sizeof(kNav) / sizeof(kNav[0])),
              "kNav rows and NavRow constants drifted");
```

- [ ] **Step 2: 替换 8 处裸行号**

逐处改为（行号是改前锚点，按内容匹配）：

| 锚点（改前） | 现文 | 改为 |
|---|---|---|
| ctor 尾部 | `nav_->setCurrentRow(0);` | `nav_->setCurrentRow(kRowLobby);` |
| buildUi 栈落位 | `nav_->setCurrentRow(2);` | `nav_->setCurrentRow(kRowGallery);` |
| cmdk `applyFilter` | `nav_->setCurrentRow(2);` | `nav_->setCurrentRow(kRowGallery);` |
| cmdk `openRoom` | `nav_->setCurrentRow(3);` | `nav_->setCurrentRow(kRowPeople);` |
| cmdk 另一处 | `nav_->setCurrentRow(2);` | `nav_->setCurrentRow(kRowGallery);` |
| cmdk 回前厅 | `nav_->setCurrentRow(0);` | `nav_->setCurrentRow(kRowLobby);` |
| 另一处 | `setCurrentRow(6)`（核实后按语义映射） | 对应 `kRow*` |
| `ensureLightbox` 后 | `nav_->setCurrentRow(4);` | `nav_->setCurrentRow(kRowStudio);` |

> 执行时 `grep -n "setCurrentRow(" src/ui/MainWindow.cpp` 逐条核对语义，不要按上表的旧行号盲改。

- [ ] **Step 3: 动效预设改为按空间语义**

`onNav` 里：

```cpp
// 波4 §9：行号 → 动效的隐式耦合换成「空间语义 → 动效」。
const Lumen::Hall hall = kNavHall[row];
const QByteArray presetName = hall == Lumen::Hall::Lobby  ? Motion::kLightFirst
                            : hall == Lumen::Hall::People ? Motion::kPushDoor
                                                          : Motion::kNavFade;
```

- [ ] **Step 4: 验证（此任务无新门，靠既有门 + grep）**

Run:
```
powershell -NoProfile -ExecutionPolicy Bypass -File iterate.ps1 -Only LA_LOBBY_QA
powershell -NoProfile -ExecutionPolicy Bypass -File iterate.ps1 -Only LA_GAZE_QA
powershell -NoProfile -ExecutionPolicy Bypass -File iterate.ps1 -Only LA_LAMP_QA
```
Expected: 三绿；`grep -n "setCurrentRow(\s*[0-9]" src/ui/MainWindow.cpp` 0 命中。

- [ ] **Step 5: 提交**

```bash
git add native/src/ui/MainWindow.cpp
git commit -m "refactor(nav): 行号归一为 kRow* 常量 + 按空间语义选动效（wave4 §9）"
```

---

## T2 · Library A 轨 API + Scheduler + 新门骨架

**Files:**
- Modify: `src/core/Library.h`（`AlbumRow` 之后 / curate API 区）
- Modify: `src/core/Library.cpp`（`listAlbums` 之后）
- Modify: `src/core/CurateScheduler.h/.cpp`（`keepExhibit` / `muteExhibit` 旁）
- Modify: `src/main.cpp`（新 `runCollectionQa()`；dispatch；`M_no_direct_window_writes` 扩项）
- Modify: `native/gate_matrix.ps1`（注册新门，纯 ASCII）

- [ ] **Step 1 (RED): 先写门 + 空实现，看它红**

1. 在 `Library.h` 加声明（先不实现）：

```cpp
    // ---- 波4 A 轨：合集取路径 + 静音清单/唤醒（GUI 线程；读走 la_lib，写走 la_rw）----
    QStringList albumPaths(qint64 albumId) const;      // ORDER BY ord（收藏顺序 = 回放顺序）
    struct MuteRow { QString tmpl; qint64 until = 0; }; // until = 绝对秒
    QVector<MuteRow> listMutes(qint64 now) const;       // WHERE until > ? ORDER BY until DESC
    bool unmuteTemplate(const QString& tmpl);           // 删行；未静音 token 返回 false（不假 true）
```

2. 在 `Library.cpp` 先用空实现顶住编译（`return {};` / `return false;`）。
3. 在 `src/main.cpp` 的策展门禁匿名命名空间之后（`runCurateQa` 收尾 `}` 之后）新增：

```cpp
namespace {

// 波4 兑现门（LA_COLLECTION_QA）：合集读写对称 / 唯一写者 / 导航完整性 / 播放同源 /
// 文案诚实 / 死代码 / 网格算术 / 承诺清零 / 时间地基。
int runCollectionQa()
{
    int checks = 0, fails = 0;
    QStringList failed;
    const auto chk = [&](const char* name, bool ok) {
        ++checks;
        if (!ok) { ++fails; failed << QString::fromLatin1(name); }
        std::fprintf(stderr, "  %-44s %s\n", name, ok ? "ok" : "FAIL");
    };
    const auto readSrc = [](const char* rel) {
        QFile f(QString::fromLatin1(rel));
        if (!f.open(QIODevice::ReadOnly)) return QString();
        return QString::fromUtf8(f.readAll());
    };

    QTemporaryDir dir;
    if (!dir.isValid()) {
        std::fprintf(stderr, "LA_COLLECTION_QA FAIL tempdir\n");
        return 1;
    }
    // ---- A 组：API 对称 ----
    {
        const QString db = dir.filePath(QStringLiteral("collection_a.db"));
        QVector<Library::SynthRow> rows;
        for (int i = 1; i <= 4; ++i) {
            Library::SynthRow r;
            r.path = QStringLiteral("p%1.jpg").arg(i);
            r.date = QStringLiteral("2019-0%1-10").arg(i);
            r.year = 2019;
            rows << r;
        }
        QString err;
        chk("A0_synth_db", Library::makeSyntheticDb(db, rows, &err));
        Library lib;
        chk("A1_open", lib.open(db, &err));

        const qint64 album = lib.createAlbum(QStringLiteral("西湖"),
                                             {QStringLiteral("p1.jpg"), QStringLiteral("p3.jpg")},
                                             QStringLiteral("curate"));
        chk("A2_album_paths_ord",
            lib.albumPaths(album) == QStringList({QStringLiteral("p1.jpg"), QStringLiteral("p3.jpg")}));
        chk("A3_album_paths_empty", lib.albumPaths(album + 999).isEmpty());

        chk("A4_unmute_never_muted_is_false",
            !lib.unmuteTemplate(QStringLiteral("never-muted")));
        chk("A5_mute_visible", lib.muteTemplate(QStringLiteral("tmpl-1"), 9999999999999LL)
            && lib.listMutes(1000).size() == 1
            && lib.listMutes(1000).first().tmpl == QStringLiteral("tmpl-1"));
        chk("A6_unmute_removes", lib.unmuteTemplate(QStringLiteral("tmpl-1"))
            && lib.listMutes(1000).isEmpty());
        chk("A7_unmute_twice_false", !lib.unmuteTemplate(QStringLiteral("tmpl-1")));
    }

    // ---- B 组：唯一写者 ----
    {
        const QString db = dir.filePath(QStringLiteral("collection_b.db"));
        Library::SynthRow r; r.path = QStringLiteral("q1.jpg");
        QString err;
        Library::makeSyntheticDb(db, {r}, &err);
        Library lib;
        lib.open(db, &err);
        lib.unmuteTemplate(QStringLiteral("x"));   // 即便删 0 行，也要 noteWriteThread
        chk("B1_unmute_on_gui_thread",
            lib.lastWriteThreadId() == quintptr(QThread::currentThreadId()));
        const QString mw = readSrc("src/ui/MainWindow.cpp");
        chk("B2_mainwindow_no_direct_unmute", !mw.contains(QStringLiteral("lib_->unmuteTemplate(")));
    }

    const bool pass = (checks > 0) && (fails == 0);
    std::fprintf(stderr, "LA_COLLECTION_QA %s checks=%d failed=%d\n",
                 pass ? "PASS" : "FAIL", checks, fails);
    if (!pass)
        std::fprintf(stderr, "LA_COLLECTION_QA failed: %s\n", qPrintable(failed.join(',')));
    std::fflush(stderr);
    return pass ? 0 : 1;
}

} // namespace（波4 兑现门禁）
```

4. dispatch 插入到 `LA_CURATE_QA` 之后、`LA_ERASE_QA` 之前：

```cpp
    // 2.43.0 波4 兑现：合集/静音/导航/播放同源/文案诚实/时间地基
    if (qEnvironmentVariableIsSet("LA_COLLECTION_QA"))
        return runCollectionQa();
```

5. `gate_matrix.ps1` 注册（**纯 ASCII**；先不带 tier，fail-safe 落 full）：

```powershell
  # 2.43.0 wave4 promise-kept gate. A..I groups: album/mute symmetry, single writer,
  # nav completeness, gaze same-source, ack honesty, dead code, grid arithmetic,
  # zero "next round" promises, time-column parity/EXPLAIN/privacy.
  @{n="LA_COLLECTION_QA"; png=$false; measuredMs=1},
```

6. 构建并运行 → **期望 A2–A7、B1 红**（空实现）。把红名单贴进台账。

- [ ] **Step 2 (GREEN): 实现三个 API + Scheduler 两方法**

`Library.cpp`（`listAlbums` 之后）：

```cpp
QStringList Library::albumPaths(qint64 albumId) const
{
    QStringList out;
    if (albumId <= 0 || !QSqlDatabase::contains(QStringLiteral("la_lib"))) return out;
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("la_lib")));
    q.prepare(QStringLiteral("SELECT path FROM album_items WHERE album_id=? ORDER BY ord"));
    q.addBindValue(albumId);
    if (!q.exec()) return out;
    while (q.next()) out << q.value(0).toString();
    return out;
}

QVector<Library::MuteRow> Library::listMutes(qint64 now) const
{
    QVector<MuteRow> out;
    if (!QSqlDatabase::contains(QStringLiteral("la_lib"))) return out;
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("la_lib")));
    q.prepare(QStringLiteral(
        "SELECT template, until FROM template_mutes WHERE until > ? ORDER BY until DESC"));
    q.addBindValue(double(now));
    if (!q.exec()) return out;
    while (q.next()) out.append({q.value(0).toString(), qint64(q.value(1).toDouble())});
    return out;
}

bool Library::unmuteTemplate(const QString& tmpl)
{
    if (tmpl.isEmpty()) return false;
    noteWriteThread();
    QSqlQuery q(rw());
    q.prepare(QStringLiteral("DELETE FROM template_mutes WHERE template = ?"));
    q.addBindValue(tmpl);
    if (!q.exec()) return false;
    return q.numRowsAffected() > 0;   // 0 行 = 本来就没静音：如实 false
}
```

`CurateScheduler.h`（`keepExhibit` / `muteExhibit` 声明旁）：

```cpp
    // 波4 R7：⌘K 暂存为合集 —— 与 keepExhibit 同一写入路径，来源记 'cmdk'。
    qint64 keepSelection(const QString& name, const QStringList& paths);
    // 波4 B2：唤醒被静音的模板（典藏 tab3）。GUI 线程写；未静音 → false。
    bool unmuteExhibit(const QString& token);
```

`CurateScheduler.cpp`：

```cpp
qint64 CurateScheduler::keepSelection(const QString& name, const QStringList& paths)
{
    if (!lib_ || paths.isEmpty() || name.trimmed().isEmpty()) return 0;
    return lib_->createAlbum(name, paths, QStringLiteral("cmdk"));
}

bool CurateScheduler::unmuteExhibit(const QString& token)
{
    if (!lib_ || token.isEmpty()) return false;
    return lib_->unmuteTemplate(token);
}
```

`src/main.cpp` 的 `M_no_direct_window_writes` 片段加一项（同一提交）：

```cpp
             && !mw.contains(QStringLiteral("lib_->unmuteTemplate("))
```

- [ ] **Step 3: 跑门 + 量成本**

```
powershell -NoProfile -ExecutionPolicy Bypass -File iterate.ps1 -Only LA_COLLECTION_QA
powershell -NoProfile -ExecutionPolicy Bypass -File qa_runner.ps1 -Only LA_COLLECTION_QA -Timing
```
Expected: `LA_COLLECTION_QA PASS checks=9 failed=0`（A0–A7 + B1/B2）。
把 `-Timing` 的 warm 中位/最大值填回 `measuredMs`。若 >500ms → **删掉 `tier="fast"`**（fail-safe full），
并在台账写下裁决（spec 写 fast 是成本猜测，E6 以实测为准）。

- [ ] **Step 4: 提交**

```bash
git add native/src/core/Library.h native/src/core/Library.cpp \
        native/src/core/CurateScheduler.h native/src/core/CurateScheduler.cpp \
        native/src/main.cpp native/gate_matrix.ps1
git commit -m "feat(curate): 合集路径/静音清单/唤醒 API + LA_COLLECTION_QA 骨架（wave4 T2）"
```

---

## T3 · `Layout.h` + `Hall::Collection` + 灯表扩格

**Files:**
- Create: `src/ui/Layout.h`
- Modify: `src/ui/Theme.h`
- Modify: `native/CMakeLists.txt`
- Modify: `src/main.cpp`（G 组腿）

- [ ] **Step 1 (RED): 先写 G 组腿，配一个桩实现**

`Layout.h` 先落桩：

```cpp
#pragma once
inline int gridCols(int widthPx, int targetPx = 230, int lo = 3, int hi = 5)
{
    (void)widthPx; (void)targetPx; (void)lo; (void)hi;
    return 3;   // STUB：先让腿红
}
```

`runCollectionQa()` 追加：

```cpp
    // ---- G 组：网格算术（Layout.h 唯一真源；R6 不单开门）----
    chk("G1_grid_960",  gridCols(960)  == 3);
    chk("G2_grid_1440", gridCols(1440) == 5);
    chk("G3_grid_2560", gridCols(2560) == 5);
    chk("G4_grid_target_fallback", gridCols(960, 0) == gridCols(960));
```

`main.cpp` 顶部加 `#include "ui/Layout.h"`（与其它 ui include 同区）。
构建跑门 → **G2/G3 红**（桩恒 3）。

- [ ] **Step 2 (GREEN): 实现真函数**

```cpp
#pragma once
// 波4 §3.3：网格列数唯一真源（EditorialView / PeoplePage / CollectionPage 三处共用）。
// 纯算术：零 Qt、零状态；resize 防抖归各页，工具函数不许长状态。
//
// 分母取 targetPx + 12（卡片间距）：没有 gutter 的 width/target 在 960 宽下整数除法
// 得 4，与 spec §4 G 组断言的 3 自相矛盾（spec 修订 R11）。
inline int gridCols(int widthPx, int targetPx = 230, int lo = 3, int hi = 5)
{
    if (targetPx <= 0) targetPx = 230;
    const int pitch = targetPx + 12;
    const int n = (widthPx > 0 ? widthPx : 0) / pitch;
    if (n < lo) return lo;
    if (n > hi) return hi;
    return n;
}
```

验证：`gridCols(960)=3`（960/242=3）、`gridCols(1440)=5`（5.95）、`gridCols(2560)=5`（封顶）。
CMake：`src/ui/Layout.h` 加入 ui 列表（`MainWindow.h` 之前，纯头文件无 cpp）。

- [ ] **Step 3 (RED→GREEN): 空间枚举与灯表**

1. 先改 `Theme.h` 枚举（否则引用编译不过）：

```cpp
enum class Hall : int { Lobby, Gallery, People, Studio, Vault, Common, Collection };
```

2. 写 C 组腿（此时 `lampFor(Collection)` 因 `follow[6]` 越界回退 Nocturne → 红）：

```cpp
    // ---- C 组：导航完整性（C2 在 T4 插行后才应绿，届时一并启用）----
    chk("C1_collection_jade", Lumen::lampFor(Lumen::Hall::Collection) == Lumen::Lamp::Jade);
    chk("C4_oob_fallback", Lumen::lampFor(Lumen::Hall(99)) == Lumen::Lamp::Nocturne);
```

3. 改为真表：

```cpp
    static const Lamp follow[7] = { Lamp::Nocturne, Lamp::Jade, Lamp::Jade,
                                    Lamp::Jade,     Lamp::Jade, Lamp::Nocturne,
                                    Lamp::Jade };   // Collection：亮底画廊语汇
    const int i = int(h);
    return (i >= 0 && i < int(sizeof(follow) / sizeof(follow[0]))) ? follow[i]
                                                                  : Lamp::Nocturne;
```

- [ ] **Step 4: 验证**

Run: `iterate.ps1 -Only LA_COLLECTION_QA` → PASS（含 G/C1/C4）。
再跑 `iterate.ps1 -Only LA_LAMP_QA` → 绿（B7_oob_fallback 仍绿）。

- [ ] **Step 5: 提交**

```bash
git add native/src/ui/Layout.h native/src/ui/Theme.h native/CMakeLists.txt \
        native/src/main.cpp
git commit -m "feat(layout): gridCols 纯函数 + Hall::Collection 与灯表扩格（wave4 T3）"
```

---

## T4 · 典藏页 + 导航第 8 行 + 栈第 8 页

**Files:**
- Create: `src/ui/CollectionPage.{h,cpp}`
- Modify: `src/ui/MainWindow.{h,cpp}`（`kNav` / `kNavHall` / `kNavIcons` / 栈 / 接线 / `refreshPrivateState` / `refreshExhibits`）
- Modify: `native/CMakeLists.txt`
- Modify: `src/main.cpp`（C2/C3/D 组）

**前置：** T1 已归一、T3 已有 `Hall::Collection` 与 `Layout.h`。

- [ ] **Step 1 (RED): 写 C2/C3/D 组 + 页面空壳**

1. 只加导航三表第 8 行（页面类用最小空壳顶住编译），此时 D 组应红。
2. C 组追加：

```cpp
    {
        const QString mw = readSrc("src/ui/MainWindow.cpp");
        chk("C2_no_bare_setCurrentRow",
            !QRegularExpression(QStringLiteral("setCurrentRow\\(\\s*\\d+")).match(mw).hasMatch());
        const auto blockLen = [&](const QString& head) {
            const int a = mw.indexOf(head);
            if (a < 0) return -1;
            const int b = mw.indexOf(QStringLiteral("};"), a);
            if (b < 0) return -1;
            return mw.mid(a, b - a).count(QStringLiteral("{"));
        };
        chk("C3_nav_tables_len8", blockLen(QStringLiteral("kNav[] = {")) == 8
            && blockLen(QStringLiteral("kNavHall[] = {")) == 8
            && blockLen(QStringLiteral("kNavIcons[] = {")) == 8);
    }
```

3. 导航三表插行（顺序：前厅 足迹 图库 **典藏** 人物馆 修图 日志 关于）：

```cpp
const NavItem kNav[] = {
    {"前厅", "P1"}, {"足迹", "P2"}, {"图库", "P1"}, {"典藏", "P1"},
    {"人物馆", "P3"}, {"修图", "P3"},
    {"日志", "P2"}, {"关于", "P3"},
};
const Lumen::Hall kNavHall[] = {
    Lumen::Hall::Lobby,  Lumen::Hall::Gallery, Lumen::Hall::Gallery,
    Lumen::Hall::Collection,
    Lumen::Hall::People, Lumen::Hall::Studio,
    Lumen::Hall::Common, Lumen::Hall::Common,
};
const char* kNavIcons[] = { "lobby", "map", "gallery", "curate",
                            "people", "retouch", "enhance", "log" };
```

4. `NavRow` 枚举插 `kRowCollection`（枚举顺序自动让后续值 +1，单点改动）：

```cpp
enum NavRow {
    kRowLobby = 0, kRowMap, kRowGallery, kRowCollection, kRowPeople,
    kRowStudio, kRowLog, kRowAbout, kRowCount
};
```

5. D 组腿（源扫，切片到 `MainWindow.cpp` 里 `openAlbum` 的接线 lambda）：

```cpp
    // ---- D 组：播放同源（合集不得复刻第二份播放器）----
    {
        const QString mw = readSrc("src/ui/MainWindow.cpp");
        chk("D1_single_lightbox_ctor",
            QDirIterator(QStringLiteral("src/ui"), {QStringLiteral("*.cpp")}, QDir::Files)
                .count() >= 0);   // 占位：见 Step 3 用真正的目录计数替换
        const int a = mw.indexOf(QStringLiteral("connect(collection_, &CollectionPage::openAlbum"));
        const QString body = a < 0 ? QString() : mw.mid(a, mw.indexOf(QStringLiteral("});"), a) - a);
        const int scope = body.indexOf(QStringLiteral("setScope("));
        const int enter = body.indexOf(QStringLiteral("enter("));
        chk("D2_scope_before_enter", scope >= 0 && enter > scope);
    }
```

> D1 的最终形态见 Step 3；先写 D2（此时集合页还没信号 → 红）。

- [ ] **Step 2 (GREEN): 实现 `CollectionPage`**

`src/ui/CollectionPage.h`：

```cpp
#pragma once
// 波4 R1：典藏（Hall::Collection）—— 合集 / 展览史 / 安静下来的选题。
// 只读 Library；唤醒意图以信号上交 MainWindow（本类不写库）。
#include <QWidget>
#include <QString>
#include <QStringList>
#include <QVector>
#include "../core/Library.h"

class QListWidget;
class QTabWidget;
class ThumbCache;

class CollectionPage : public QWidget {
    Q_OBJECT
public:
    CollectionPage(Library* lib, const QString& thumbsDir, QWidget* parent = nullptr);
    void setHidePrivate(bool hide);
    void refresh();

signals:
    void openAlbum(const QStringList& paths);
    void openExhibit(const QStringList& paths, const QString& title, const QString& blurb);
    void unmuteRequested(const QString& tmpl);

private:
    void rebuildAlbums();
    void rebuildExhibits();
    void rebuildMutes();
    QWidget* emptyGuide(const QString& title, const QString& hint) const;

    Library* lib_ = nullptr;
    QString thumbsDir_;
    bool hidePrivate_ = true;
    ThumbCache* cache_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    QListWidget* albums_ = nullptr;
    QListWidget* exhibits_ = nullptr;
    QListWidget* mutes_ = nullptr;
    QVector<AlbumRow> albumRows_;
    QVector<ExhibitRow> exhibitRows_;
    QVector<MuteRow> muteRows_;
};
```

`CollectionPage.cpp` 要点（逐条实现，勿加本波没有的功能）：

1. 构造：`QTabWidget` 三个 `QListWidget`，标签「合集」「展览史」「安静下来的选题」；
   `cache_ = new ThumbCache(this)`；`setHidePrivate(true)` 前先 `rebuild*()`。
2. `rebuildAlbums()`：
   - `albumRows_ = lib_->listAlbums()`；
   - 每行第一帧：`albumPaths(id)` 的首个 path → `lib_->find(p)` → `cache_->request(ph->thumbFile, this, cb, ThumbCache::kTile)`；
     回调里 `QPointer<QListWidgetItem>` 守卫（缩略是异步的，行可能已重建）。
   - 计数诚实（spec §2.1）：`hidePrivate_` 时按 `albumPaths` 里 `lib_->find(p)->priv == false` 的条数显示；
     否则用 `AlbumRow::count`。这是本波唯一允许两套数字的地方——但只有一套会显示，且各有来源。
   - 文案：`名称` + 「N 张」+ 来源徽记（`curate`→「策展收入」，`cmdk`→「检索收入」）+ 创建日期。
   - 空态：`emptyGuide("还没有合集", "在前厅按『收进合集』，或在 ⌘K 里勾选照片后按『暂存为合集』。")`。
3. `rebuildExhibits()`：`loadExhibitHistory()`；每行 = 展名 + 小记 + 日期 + 帧数 + feedback 徽记
   （`kept`→「已收进」/`swapped`→「已换走」/`muted`→「已静音」）；`item->setData(Qt::UserRole, i)`。
   空态：`emptyGuide("还没有展览", "前厅每天会挑一展；「换一展」的记录也会留在这里。")`。
4. `rebuildMutes()`：`lib_->listMutes(QDateTime::currentSecsSinceEpoch())`；每行 =
   `tmpl` + 时长（`until >= kPermanentMuteUntil` →「永久」否则「至 yyyy-MM-dd」）+ 「唤醒」按钮；
   按钮 `clicked` → `emit unmuteRequested(tmpl)`（**不写库**）。
   空态：`emptyGuide("没有被静音的选题", "在前厅按『别再这类』，会让这类安静下来。")`。
5. `refresh()`：三张表各自重建；`setHidePrivate(h)` 只在值变化时重建。
6. 点击接线：`albums_` 双击/回车 → 取 `albumPaths` → 非空才 `emit openAlbum`；
   `exhibits_` → `paths.split(',')` → 非空才 `emit openExhibit(paths, title, blurb)`。

`MainWindow.cpp` 接线：

```cpp
    collection_ = new CollectionPage(&lib_, thumbsDir, this);
    pageStack_->insertWidget(kRowCollection, collection_);
```

```cpp
    connect(collection_, &CollectionPage::openAlbum, this, [this](const QStringList& paths) {
        if (paths.isEmpty()) return;
        if (!lightbox_) return;
        lightbox_->setScope(paths);                       // 先作用域
        openGaze(paths.first(), paths);                   // 再入场（D2 腿）
    });
    connect(collection_, &CollectionPage::openExhibit, this,
            [this](const QStringList& paths, const QString&, const QString&) {
        if (paths.isEmpty() || !lightbox_) return;
        lightbox_->setScope(paths);
        openGaze(paths.first(), paths);
    });
    connect(collection_, &CollectionPage::unmuteRequested, this, [this](const QString& tmpl) {
        if (!curator_->unmuteExhibit(tmpl)) return;
        collection_->refresh();
        lobby_->refresh();
        showAck(CurateScheduler::muteUndoneAck());        // 见 T5；T4 先复用现有 toast
        curator_->maybeGenerate(lib_.photos(), !vaultUnlocked_, thumbsDir,
                                CurateScheduler::wallNow(), /*force=*/true);
    });
```

> `showAck`：MainWindow 现有的回执入口（`lobby_` 手势回执同款）。按实际函数名接线，勿新造第三套通知。

`refreshPrivateState()` / `rethemeAll` / `refreshExhibits()` 同步加 `collection_->setHidePrivate(...)` /
`collection_->refresh()`（`rethemeAll` 重建后句柄要重新赋）。

- [ ] **Step 3: 补齐 D1（目录级单实例）**

```cpp
    {
        int ctors = 0;
        QDirIterator it(QStringLiteral("src/ui"), {QStringLiteral("*.cpp")}, QDir::Files);
        while (it.hasNext()) {
            const QFileInfo fi(it.next());
            QFile f(fi.filePath());
            if (!f.open(QIODevice::ReadOnly)) continue;
            const QString s = QString::fromUtf8(f.readAll());
            ctors += s.count(QStringLiteral("new GazeLightbox"));
        }
        chk("D1_single_lightbox_ctor", ctors == 1);
    }
```

（替换 Step 1 的占位。`MainWindow` 的 `ensureLightbox()` 是唯一构造点；集合页只发信号。）

- [ ] **Step 4: 验证**

```
powershell -NoProfile -ExecutionPolicy Bypass -File iterate.ps1 -Only LA_COLLECTION_QA
powershell -NoProfile -ExecutionPolicy Bypass -File iterate.ps1 -Only LA_LAMP_QA
powershell -NoProfile -ExecutionPolicy Bypass -File iterate.ps1 -Only LA_LOBBY_QA
```
Expected: 全绿；C3 的 `kNav` 表计数 = 8。手动开一次 exe（1440×900）确认导航第 4 行「典藏」可点、
三 tab 空态文案出现（空库）。

- [ ] **Step 5: 提交**

```bash
git add native/src/ui/CollectionPage.h native/src/ui/CollectionPage.cpp \
        native/src/ui/MainWindow.h native/src/ui/MainWindow.cpp \
        native/CMakeLists.txt native/src/main.cpp
git commit -m "feat(collection): 典藏页三 tab + 导航第 8 行 + 合集回放走凝视（wave4 T4）"
```

---

## T5 · 两条 ack 文案翻转（同一提交闭环）

**Files:**
- Modify: `src/core/CurateScheduler.cpp`（`keepAck` / `muteAck`）
- Modify: `src/main.cpp`（`M_ack_*` 两条腿 + `ctxCheck` 两个字面量 + E 组腿）
- Modify: `native/i18n/LightAlbum_en.ts` + `LightAlbum_en.qm`

**同提交纪律（spec §5）：** 文案、门禁断言、i18n 源串、编译产物四者必须一起落盘。分开提交会让门红着过夜。

- [ ] **Step 1 (RED): 改门先红**

`src/main.cpp` M9 两条腿改为新方向：

```cpp
       report("M_ack_mute_honest",
              m.contains(QStringLiteral("唤醒")) && !m.contains(QStringLiteral("下轮")));
       ...
       report("M_ack_keep_honest",
              k.contains(QStringLiteral("典藏")) && !k.contains(QStringLiteral("下轮")));
```

`ctxCheck` 两个字面量同步改为新源串/新译文（源串见 Step 3）。
Run `iterate.ps1 -Only LA_CURATE_QA` → **两条腿红**（文案还没改）。

- [ ] **Step 2 (GREEN): 改文案**

```cpp
QString CurateScheduler::keepAck(const QString& title, int n, bool ok)
{
    return ok
        ? QCoreApplication::translate("CurateScheduler",
              "已把《%1》的 %2 张存为永久合集，可在典藏里再看。").arg(title).arg(n)
        : QCoreApplication::translate("CurateScheduler", "没能收进合集，库里没多出一份空壳。");
}

QString CurateScheduler::muteAck()
{
    return QCoreApplication::translate("CurateScheduler",
                                       "这类展已安静下来。可在典藏 · 安静下来的选题里唤醒。");
}
```

- [ ] **Step 3: `.ts` 与 `.qm` 同提交**

`LightAlbum_en.ts` 中把两条 `<source>` 逐字替换（context 必须是 `CurateScheduler`）：

| source（新） | translation |
|---|---|
| `已把《%1》的 %2 张存为永久合集，可在典藏里再看。` | `Kept %2 photos from "%1" as a permanent album — find it under Collection.` |
| `这类展已安静下来。可在典藏 · 安静下来的选题里唤醒。` | `This kind of exhibit has gone quiet. Wake it up under Collection · Quieted picks.` |

然后：
```
D:/qt/6.8.3/mingw_64/bin/lrelease.exe native/i18n/LightAlbum_en.ts -qm native/i18n/LightAlbum_en.qm
```

- [ ] **Step 4: 加 E 组腿（新门）**

```cpp
    // ---- E 组：文案诚实（R2；方向已随兑现翻转：不许假不可用）----
    {
        const QString keep = CurateScheduler::keepAck(QStringLiteral("西湖"), 3, true);
        const QString mute = CurateScheduler::muteAck();
        chk("E1_keep_points_to_collection",
            keep.contains(QStringLiteral("典藏")) && !keep.contains(QStringLiteral("下轮")));
        chk("E2_mute_points_to_wake",
            mute.contains(QStringLiteral("唤醒")) && !mute.contains(QStringLiteral("下轮")));
    }
```

- [ ] **Step 5: 变异证明（两条腿各自咬人，必须实测）**

1. 临时把 `keepAck` 改回「合集浏览下轮上线」→ `iterate.ps1 -Only LA_CURATE_QA` **必须红在
   `M_ack_keep_honest`**；`iterate.ps1 -Only LA_COLLECTION_QA` **必须红在 `E1`**。
2. 还原；临时把 `muteAck` 改回「反悔入口下轮上线」→ 必须红在 `M_ack_mute_honest` 与 `E2`。
3. 还原后 `git diff --numstat` 必须为空（证明只改了该改的字节）。

- [ ] **Step 6: 验证 + 提交**

```
powershell -NoProfile -ExecutionPolicy Bypass -File iterate.ps1 -Only LA_CURATE_QA
powershell -NoProfile -ExecutionPolicy Bypass -File iterate.ps1 -Only LA_COLLECTION_QA
powershell -NoProfile -ExecutionPolicy Bypass -File iterate.ps1 -Only LA_I18N_QA
```

```bash
git add native/src/core/CurateScheduler.cpp native/src/main.cpp \
        native/i18n/LightAlbum_en.ts native/i18n/LightAlbum_en.qm
git commit -m "fix(curate): 回执兑现——合集指向典藏、静音指向唤醒（R2：腿同批翻转）"
```

---

## T6 · ⌘K 暂存键激活 + 空解析说明

**Files:**
- Modify: `src/ui/CmdKPanel.{h,cpp}`
- Modify: `src/ui/GalleryPage.{h,cpp}`（公开 `selectedPaths()`）
- Modify: `src/ui/MainWindow.cpp`（接线）
- Modify: `src/main.cpp`（H 组 + `LA_CMDK_QA` 三样例腿）

- [ ] **Step 1 (RED): 写腿**

H 组（承诺清零）：

```cpp
    // ---- H 组：承诺清零（R7：界面不许再留「下轮」）----
    {
        int hits = 0;
        QDirIterator it(QStringLiteral("src"), {QStringLiteral("*.cpp"), QStringLiteral("*.h")},
                        QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QFile f(it.next());
            if (!f.open(QIODevice::ReadOnly)) continue;
            const QString s = QString::fromUtf8(f.readAll());
            // 门禁自带的变异证明字面量（"下轮" 作为反例串）只允许出现在 main.cpp 的 leg 名与注释里；
            // 这里按「面向用户的字符串字面量」计数：translate(...)/QStringLiteral(...) 内部出现。
            QRegularExpression re(QStringLiteral(
                "(?:translate\\([^)]*|QStringLiteral\\()\\s*\"[^\"]*下轮[^\"]*\""));
            hits += int(re.match(s).capturedLength() > 0) + s.count(QStringLiteral("setToolTip("));
        }
        chk("H1_no_next_round_promise", hits >= 0);   // 占位，见 Step 3 收紧
        const QString ck = readSrc("src/ui/CmdKPanel.cpp");
        const int a = ck.indexOf(QStringLiteral("auto* stash = new QPushButton"));
        const QString seg = a < 0 ? QString() : ck.mid(a, 600);
        chk("H2_stash_tooltip_honest", !seg.contains(QStringLiteral("下轮")));
    }
```

> 占位腿会在 Step 3 换成真计数（`hits == 0` 且带一行「扫描文件数 > 0」的自检，避免空扫描假绿）。
> `LA_CMDK_QA` 追加：

```cpp
    // 波4 §2.6：空解析说明里的三个样例必须真的能解析（界面上的每句话都是承诺）。
    for (const char* sample : {"2019", "夏天", "去年"}) {
        const CmdFilter f = CmdKPanel::parse(QString::fromUtf8(sample));
        chk(("S_sample_" + QByteArray(sample)).constData(), !f.empty());
    }
```

Run → **H2 红**（tooltip 还写「下轮」）。

- [ ] **Step 2 (GREEN): 面板 API + 按钮行为**

`CmdKPanel.h`：

```cpp
    void setStashAvailable(bool on);       // 有勾选 → 可点
signals:
    void stashRequested(const QString& name);
private:
    QPushButton* stash_ = nullptr;
    QLabel* note_ = nullptr;
```

`CmdKPanel.cpp` 构造：

```cpp
    note_ = new QLabel(mat);
    note_->setFont(Lumen::Type::meta());
    note_->setStyleSheet(QStringLiteral("color:%1;background:transparent;border:none;")
                             .arg(Lumen::hex(Lumen::muted())));
    note_->setWordWrap(true);
    note_->hide();
    mv->insertWidget(mv->indexOf(hintRow_), note_);

    stash_ = new QPushButton(QCoreApplication::translate("LightAlbum", "暂存为合集"), actions);
    ...
    stash_->setEnabled(false);
    stash_->setToolTip(QCoreApplication::translate("LightAlbum", "先在结果里勾选照片"));
    connect(stash_, &QPushButton::clicked, this, [this] {
        emit stashRequested(input_->text().trimmed());
    });
```

```cpp
void CmdKPanel::setStashAvailable(bool on)
{
    if (!stash_) return;
    stash_->setEnabled(on);
    stash_->setToolTip(on ? QCoreApplication::translate("LightAlbum", "把勾选的照片存进典藏")
                          : QCoreApplication::translate("LightAlbum", "先在结果里勾选照片"));
}
```

空解析（`onSubmit`）：

```cpp
    if (f.empty()) {
        if (note_) {
            note_->setText(QCoreApplication::translate(
                "LightAlbum", "没听懂这个词。可以试：2019 / 夏天 / 去年"));
            note_->show();
        }
        return;
    }
    if (note_) note_->hide();
```

- [ ] **Step 3 (GREEN): 选择集 + 接线**

`GalleryPage.h`（公开，非 QA）：

```cpp
    // 波4 R7：⌘K「暂存为合集」的载荷源（与批量条同一条选择集）。
    QStringList selectedPaths() const { return qaSelectedPaths(); }
```

`MainWindow::openCmdK()` 尾部：

```cpp
    cmdk_->setStashAvailable(!gallery_->selectedPaths().isEmpty());
```

`CmdKPanel::stashRequested` 接线（MainWindow）：

```cpp
    connect(cmdk_, &CmdKPanel::stashRequested, this, [this](const QString& name) {
        const QStringList paths = gallery_->selectedPaths();
        const qint64 id = curator_->keepSelection(name, paths);
        showAck(CurateScheduler::keepAck(name, paths.size(), id > 0));
        if (id > 0) collection_->refresh();
    });
```

> 空 `paths` / 空标题 → `keepSelection` 返回 0 → `keepAck(..., false)` 报「没能收进合集」，
> 不建空壳（A 组没直接覆盖这个分支，`LA_CURATE_QA` 的 keepExhibit 幂等腿覆盖同一条写入路径）。

H 组收成真腿：

```cpp
        chk("H0_scanned_nonzero", filesSeen > 0);
        chk("H1_no_next_round_promise", userFacingHits == 0);
```

（`userFacingHits` 只统计 `translate(...)` / `QStringLiteral(...)` / `setToolTip(...)` 里含「下轮」的串；
`main.cpp` 里的门禁断言字面量是**反例证据**，不计入——用路径白名单跳过 `src/main.cpp` 的腿代码，
但 `src/main.cpp` 面向用户的串仍要计入。实现时按「行含 `report(` 或 `chk(` → 跳过」切片。）

- [ ] **Step 4: 验证 + 提交**

```
powershell -NoProfile -ExecutionPolicy Bypass -File iterate.ps1 -Only LA_CMDK_QA
powershell -NoProfile -ExecutionPolicy Bypass -File iterate.ps1 -Only LA_COLLECTION_QA
```
手动验收：⌘K 输入「zzz」→ 说明文案出现且三个样例可解析；图库勾 2 张 → ⌘K 暂存可点 →
点后回执含「典藏」→ 典藏 tab1 出现该合集；不勾选 → 按钮禁用、tooltip 是「先在结果里勾选照片」。

```bash
git add native/src/ui/CmdKPanel.h native/src/ui/CmdKPanel.cpp \
        native/src/ui/GalleryPage.h native/src/ui/MainWindow.cpp native/src/main.cpp
git commit -m "feat(cmdk): 暂存为合集点亮 + 空解析给可解析样例（wave4 R7/§2.6）"
```

---

## T7 · 凝视键位带 + Space/Enter（先实现再写进界面）

**Files:**
- Modify: `src/ui/GazeLightbox.h/.cpp`
- Modify: `src/main.cpp`（`LA_GAZE_QA` 加腿）

**已核实缺口：** `keyPressEvent` 只处理 Esc / ← / → / S / A / Delete / L；`Space` 与 `Enter` **无行为**。
故 legend 若先写会变成第 4 句空头承诺。**先实现，后写 legend。**

- [ ] **Step 1 (RED): 加逐键实测腿**

`LA_GAZE_QA` F 组追加（沿用现有合成库 fixture + `QApplication::sendEvent`）：

```cpp
    // 波4 §2.7：legend 每一枚键都必须有效，且焦点初始态（未点任何子控件）也有效。
    {
        GazeLightbox lb(&lib);
        const QStringList scope = lb.curationActive() ? QStringList() : QStringList();
        lb.setScope(QStringList());                 // 全库
        lb.enter(GazeLightbox::Mode::Browse, QString());
        chk("L1_initial_focus", lb.hasFocus() || !lb.isVisible(), );  // 见下方说明
        // Enter = 收藏
        QSignalSpy spy(&lb, &GazeLightbox::markRequested);
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(&lb, &enter);
        chk("L2_enter_marks_like",
            spy.count() == 1 && spy.at(0).at(1).toString() == QStringLiteral("like"));
        // Space = 进入凝视（Browse → Gaze）
        QKeyEvent space(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
        QApplication::sendEvent(&lb, &space);
        chk("L3_space_enters_gaze", lb.mode() == GazeLightbox::Mode::Gaze);
        QApplication::sendEvent(&lb, &space);       // 已在 Gaze：接受但不动作
        chk("L4_space_in_gaze_noop", lb.mode() == GazeLightbox::Mode::Gaze);
        // Esc = 退出
        QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(&lb, &esc);
        chk("L5_esc_closes", !lb.isVisible());
        // legend 文案存在且逐键在带内
        const QString legend = lb.legendText();
        chk("L6_legend_keys", legend.contains(QStringLiteral("←")) && legend.contains(QStringLiteral("→"))
            && legend.contains(QStringLiteral("空格")) && legend.contains(QStringLiteral("Enter"))
            && legend.contains(QStringLiteral("Esc")));
    }
```

> `L1` 在 offscreen 门禁里用 `hasFocus()` 不可靠时，改为断言 `lb.focusPolicy() != Qt::NoFocus`
> 且 `keyPressEvent` 走的是同一入口——不要写恒真断言。真正的「初始态」由 L2/L3 的
> 「未点击任何子控件直接发键」覆盖。

Run → **L2/L3/L6 红**。

- [ ] **Step 2 (GREEN): 实现两枚键 + 访问器**

`GazeLightbox.h` 公开：

```cpp
    Mode mode() const { return mode_; }
    QString legendText() const;
```

`GazeLightbox.cpp` `keyPressEvent`：

```cpp
    const bool enterKey = (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter);
    const bool spaceKey = (e->key() == Qt::Key_Space);
    // 波4 §2.7：Space 进凝视、Enter 收藏，二者都是「序列内手势」，不打断策展。
    const bool markKey = (mode_ == Mode::Gaze)
        && (enterKey || e->key() == Qt::Key_S || e->key() == Qt::Key_A
            || e->key() == Qt::Key_Delete || e->key() == Qt::Key_L);
    if (!markKey && !spaceKey) interruptCuration();
    ...
    else if (spaceKey) {
        if (mode_ == Mode::Browse) { mode_ = Mode::Gaze; refreshMetaLine(); }
        e->accept();                       // 已在凝视：接受不动作
    }
    else if (enterKey) { handleGazeMark(QStringLiteral("like")); e->accept(); }
```

> `mode_` 切换要沿用 `enter()` 的副作用（环境光晕等）。若 `enter()` 里做的不止 `mode_ =`，
> 抽一个 `setGazeMode(bool)` 私有函数，`enter()` 与 Space 共用同一份——不许复刻第二份切换逻辑。
> **执行时必须先读 `enter()` 的完整函数体再落笔。**

legend（构造在 `bottom_` 布局，**不要放 `ctl_`**——`setPhoto()` 每次 `clearLayout(ctl_->layout())`，
放进去换图就消失）：

```cpp
    legend_ = new QLabel(QCoreApplication::translate(
        "GazeLightbox", "← → 翻｜空格 凝视｜Enter 收藏｜Esc 退出"), this);
    legend_->setObjectName(QStringLiteral("gazeLegend"));
    legend_->setAccessibleName(legend_->text());
    legend_->setFocusPolicy(Qt::NoFocus);     // 不可点 → 不进 Tab 序
    legend_->setFont(Lumen::Type::meta());
    legend_->setStyleSheet(QStringLiteral("color:%1;background:transparent;border:none;")
                               .arg(Lumen::hex(Lumen::faint())));
    bottomLayout->addWidget(legend_, 0, Qt::AlignRight);
```

`legendText()` 返回 `legend_ ? legend_->text() : QString()`。

- [ ] **Step 3: 验证 + 提交**

```
powershell -NoProfile -ExecutionPolicy Bypass -File iterate.ps1 -Only LA_GAZE_QA
```
Expected: `LA_GAZE_QA PASS`，F 组原有腿不受影响（R4：动效断言不动）。

```bash
git add native/src/ui/GazeLightbox.h native/src/ui/GazeLightbox.cpp native/src/main.cpp
git commit -m "feat(gaze): 键位带 + Space/Enter 实装（先实现后写界面，wave4 §2.7）"
```

---

## T8 · 前厅生成期骨架

**Files:**
- Modify: `src/ui/LobbyPage.h/.cpp`
- Modify: `src/ui/MainWindow.cpp`
- Modify: `src/main.cpp`（`LA_LOBBY_QA` 加腿）

- [ ] **Step 1 (RED): 加腿**

```cpp
    // 波4 §2.5：pending 期间今日一展卡换成骨架；减动效下仍出现（静态）。
    {
        LobbyPage lp(nullptr, QString());
        lp.setPending(true);
        chk("P1_pending_skeleton", lp.pendingSkeletonVisible());
        lp.setPending(false);
        chk("P2_pending_cleared", !lp.pendingSkeletonVisible());
    }
```

`LobbyPage.h` 先加 `bool setPending(bool)` 与 `bool pendingSkeletonVisible() const` 桩（返回 false）→ 红。

- [ ] **Step 2 (GREEN): 实现**

成员：`bool pending_ = false; QWidget* skeleton_ = nullptr;`

`rebuild()` 入口：`pending_` 为真时先渲染骨架卡（替换今日一展/策展卡），否则走现有路径。
骨架 = `QFrame`，`Lumen::inset()` 底 + `Lumen::line()` 1px 描边 + 居中 `QLabel`
「正在为本机选展…」（`Lumen::Type::body()` / `Lumen::muted()`），呼吸走**既有** `Motion::kExhibitBreath`
（2400ms，不新增时长档）；`Motion::reducedMotion()` 为真时不启动驱动器（静态文案仍在）。

```cpp
bool LobbyPage::setPending(bool on)
{
    if (pending_ == on) return pending_;
    pending_ = on;
    rebuild();
    return pending_;
}

bool LobbyPage::pendingSkeletonVisible() const { return pending_ && skeleton_ && skeleton_->isVisible(); }
```

MainWindow 三处 `maybeGenerate`（ctor / 换一展 / 别再这类）：

```cpp
    lobby_->setPending(true);
    curator_->maybeGenerate(...);
```

并接：

```cpp
    connect(curator_, &CurateScheduler::generated, this, [this](qint64) {
        lobby_->setPending(false);
    });
```

> `maybeGenerate` 在节流/无候选时**同步** `emit generated(0)`，故「先 pending 后调用」顺序天然能收尾。
> 不许把 `setPending(false)` 只挂在成功分支——那会让节流路径卡在骨架。

- [ ] **Step 3: 验证 + 提交**

```
powershell -NoProfile -ExecutionPolicy Bypass -File iterate.ps1 -Only LA_LOBBY_QA
```
手动：清库/节流态打开前厅，骨架出现并呼吸；`LA_REDUCE_MOTION=1` 下骨架静态出现。

```bash
git add native/src/ui/LobbyPage.h native/src/ui/LobbyPage.cpp \
        native/src/ui/MainWindow.cpp native/src/main.cpp
git commit -m "feat(lobby): 生成期骨架 + 既有呼吸档（wave4 §2.5）"
```

---

## T9 · 四页空态（7 处里的 4 处；典藏 3 处随 T4）

**Files:**
- Modify: `src/ui/GalleryPage.cpp`（空结果分支）
- Modify: `src/ui/RetouchPage.cpp`
- Modify: `src/ui/LogPage.cpp`
- Modify: `src/ui/MapPage.cpp`

- [ ] **Step 1: 逐处补空态**

统一语气照 `LobbyPage::makeEmptyGuide`：一句「为什么是空的」+ 一句「往哪走」；**只写本波真实存在的入口**。

| 文件 | 条件 | 文案 |
|---|---|---|
| `GalleryPage.cpp` | 过滤后 0 行 | 「这里没有符合条件的照片。」/「清掉筛选，或按 ⌘K 换个说法再找。」 |
| `RetouchPage.cpp` | 未选照片 | 「还没有选中的照片。」/「在图库或典藏里双击一张，就能进这里修。」 |
| `LogPage.cpp` | 无日志 | 「日志还是空的。」/「导入、分类、修图之后，这里会记下每一步。」 |
| `MapPage.cpp` | 无 GPS | 「这批照片没有位置信息。」/「相机开 GPS 拍的照片，会在这里落到地图上。」 |

- [ ] **Step 2: 验证 + 提交**

逐页手动（空库/过滤到 0/无 GPS fixture）确认文案出现且不引用不存在入口。
既有门：`iterate.ps1 -Only LA_SMOKE`、`-Only MAP_SMOKE`（若名册有）、`-Only LA_RETOUCH_QA`（按名册实际名）。

```bash
git add native/src/ui/GalleryPage.cpp native/src/ui/RetouchPage.cpp \
        native/src/ui/LogPage.cpp native/src/ui/MapPage.cpp
git commit -m "feat(ui): 四页空态补「为什么空 + 往哪走」（wave4 §2.9）"
```

---

## T10 · B 轨三列 + 一次性回填（fail-open）

**Files:**
- Modify: `src/core/Scanner.cpp`（`kExtraColumns` + `Rec` + EXIF 处派生 + UPSERT 绑定）
- Modify: `src/core/Library.cpp`（`la_mig` 块）
- Modify: `src/main.cpp`（I 组的地基腿）

- [ ] **Step 1 (RED): 写地基腿**

```cpp
    // ---- I 组（地基部分）：三列 + 回填（查询/EXPLAIN 在 T11 补齐）----
    {
        const QString db = dir.filePath(QStringLiteral("shot.db"));
        QVector<Library::SynthRow> rows;
        for (int i = 0; i < 240; ++i) {
            Library::SynthRow r;
            r.path = QStringLiteral("s%1.jpg").arg(i);
            r.date = QStringLiteral("%1-%2-10").arg(2015 + (i % 10))
                         .arg(1 + (i % 12), 2, 10, QLatin1Char('0'));
            r.year = 2015 + (i % 10);
            rows << r;
        }
        QString err;
        Library::makeSyntheticDb(db, rows, &err);
        Library lib;
        const qint64 t0 = QDateTime::currentMSecsSinceEpoch();
        chk("I0_open_migrates", lib.open(db, &err));
        const qint64 ms = QDateTime::currentMSecsSinceEpoch() - t0;
        std::fprintf(stderr, "  I_backfill_ms=%lld\n", static_cast<long long>(ms));
        chk("I1_backfill_marked", !lib.metaValue(QStringLiteral("ex_cols_backfill")).isEmpty());
        chk("I2_year_rows", lib.pathsForShotYears(2019, 2019, false).size() == 24);
        lib.~Library();
        new (&lib) Library();
        chk("I3_reopen_idempotent", lib.open(db, &err)
            && lib.pathsForShotYears(2019, 2019, false).size() == 24);
    }
```

> `pathsForShotYears` 在 T11 才实现；本步先放声明 + 空返回，I2 红。
> （若不想跨任务引用，可把 I0/I1 先落地、I2/I3 留到 T11 同组补齐——两条路都行，
> 但**同一任务内的腿必须先红后绿**。）

- [ ] **Step 2 (GREEN): Scanner 三列**

`kExtraColumns` 追加（形状照 `seen_at`）：

```cpp
    // 波4 R8：时间地基。无 EXIF 的老库行落 0 而不是 NULL——索引查询与 =0 谓词同一语义。
    "shot_ts REAL NOT NULL DEFAULT 0",       // 拍摄时刻 epoch 秒（wave5 长廊地基，本波不查询）
    "shot_year INTEGER NOT NULL DEFAULT 0",  // EXIF y，与 Photo::year 同源
    "shot_month INTEGER NOT NULL DEFAULT 0", // 1..12；解不出日期 = 0
```

`openDb` 追加索引：

```cpp
    q.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_photos_year_month ON photos(shot_year, shot_month)"));
```

`Rec` 增 `double shotTs = 0; int shotYear = 0; int shotMonth = 0;`。
在 EXIF 组装处（`o.insert("d", d)` / `o.insert("tm", ex.tm)` / `y` 已在手边）派生：

```cpp
        // 波4 R8：三列与内存谓词同一真源（photoDateOf / y），这里唯一写点。
        const QDate shotDate = photoDateOf(d);
        r.shotYear  = y > 0 ? y : (shotDate.isValid() ? shotDate.year() : 0);
        r.shotMonth = shotDate.isValid() ? shotDate.month() : 0;
        {
            QTime t = QTime::fromString(ex.tm, QStringLiteral("HH:mm:ss"));
            if (!t.isValid()) t = QTime::fromString(ex.tm, QStringLiteral("HH:mm"));
            r.shotTs = shotDate.isValid()
                ? double(QDateTime(shotDate, t.isValid() ? t : QTime(0, 0)).toSecsSinceEpoch())
                : 0.0;
        }
```

UPSERT SQL 与绑定同步加三列（列序与 VALUES 一一对应）：

```cpp
            "INSERT INTO photos(path,root,rel,src,size,mtime,thumb,exif_json,missing,seen_at,rating,"
            "media_type,dur_sec,shot_ts,shot_year,shot_month)"
            " VALUES(?,?,?,?,?,?,?,?,0,?,0,?,?,?,?,?)"
            " ON CONFLICT(path) DO UPDATE SET ..., shot_ts=excluded.shot_ts,"
            " shot_year=excluded.shot_year, shot_month=excluded.shot_month"
```

```cpp
            q.addBindValue(r.shotTs);
            q.addBindValue(r.shotYear);
            q.addBindValue(r.shotMonth);
```

`Scanner.cpp` 需要 `#include "CmdFilter.h"`（同目录）。

- [ ] **Step 3 (GREEN): 迁移 + 回填**

在 `Library.cpp` 的 `la_mig` 块、`CREATE INDEX idx_photos_cat` 之后：

```cpp
            m.exec(QStringLiteral("ALTER TABLE photos ADD COLUMN shot_ts REAL NOT NULL DEFAULT 0"));
            m.exec(QStringLiteral("ALTER TABLE photos ADD COLUMN shot_year INTEGER NOT NULL DEFAULT 0"));
            m.exec(QStringLiteral("ALTER TABLE photos ADD COLUMN shot_month INTEGER NOT NULL DEFAULT 0"));
            m.exec(QStringLiteral(
                "CREATE INDEX IF NOT EXISTS idx_photos_year_month ON photos(shot_year, shot_month)"));

            // 波4 §3.1：一次性回填（meta 标记幂等；单事务、GUI 线程：open() 只在 GUI 线程调）。
            // fail-open：任何一步失败都静默——老库照旧能打开，只是查询走全量源。
            m.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT)"));
            QSqlQuery has(m);
            has.prepare(QStringLiteral("SELECT value FROM meta WHERE key='ex_cols_backfill'"));
            const bool done = has.exec() && has.next();
            if (!done) {
                QVector<QPair<QString, QString>> all;
                QSqlQuery sel(m);
                if (sel.exec(QStringLiteral("SELECT path, exif_json FROM photos"))) {
                    while (sel.next()) all.append({sel.value(0).toString(), sel.value(1).toString()});
                }
                if (!all.isEmpty() && m.transaction()) {
                    QSqlQuery up(m);
                    up.prepare(QStringLiteral(
                        "UPDATE photos SET shot_ts=?, shot_year=?, shot_month=? WHERE path=?"));
                    for (const auto& row : all) {
                        const QJsonObject o =
                            QJsonDocument::fromJson(row.second.toUtf8()).object();
                        const QString d = o.value(QLatin1String("d")).toString();
                        const QString tm = o.value(QLatin1String("tm")).toString();
                        const int y = o.value(QLatin1String("y")).toInt();
                        const QDate sd = photoDateOf(d);
                        QTime t = QTime::fromString(tm, QStringLiteral("HH:mm:ss"));
                        if (!t.isValid()) t = QTime::fromString(tm, QStringLiteral("HH:mm"));
                        const double ts = sd.isValid()
                            ? double(QDateTime(sd, t.isValid() ? t : QTime(0, 0)).toSecsSinceEpoch())
                            : 0.0;
                        up.addBindValue(ts);
                        up.addBindValue(y > 0 ? y : (sd.isValid() ? sd.year() : 0));
                        up.addBindValue(sd.isValid() ? sd.month() : 0);
                        up.addBindValue(row.first);
                        up.exec();          // 单行失败也只是少回填一行，不阻断 open
                    }
                    m.commit();
                }
                QSqlQuery markQ(m);
                markQ.prepare(QStringLiteral(
                    "INSERT INTO meta(key,value) VALUES('ex_cols_backfill',?) "
                    "ON CONFLICT(key) DO UPDATE SET value=excluded.value"));
                markQ.addBindValue(QString::number(QDateTime::currentMSecsSinceEpoch()));
                markQ.exec();
            }
```

`Library.cpp` 需要 `#include "CmdFilter.h"`（若尚未包含）。

> **风险（spec §7）**：50k 库实测 >3s 时，停下记录裁决（分批 + 进度门），
> **不许**把回填挪到后台线程写库（破红线 2）。

- [ ] **Step 4: 验证**

```
powershell -NoProfile -ExecutionPolicy Bypass -File iterate.ps1 -Only LA_COLLECTION_QA
powershell -NoProfile -ExecutionPolicy Bypass -File iterate.ps1 -Only LA_CURATE_QA
```
另用 2.42.0 生成的真实库（仓库外副本）开一次：`photos` 行数不变、`meta('ex_cols_backfill')` 出现、
第二次打开毫秒级且行数不变。

- [ ] **Step 5: 提交**

```bash
git add native/src/core/Scanner.cpp native/src/core/Library.cpp native/src/main.cpp
git commit -m "feat(shots): shot_ts/year/month 三列 + 一年月索引 + 一次性回填（wave4 R8/T10）"
```

---

## T11 · 索引查询 + GalleryModel 预过滤（唯一消费方）

**Files:**
- Modify: `src/core/Library.h/.cpp`（`pathsForShotYears` / `pathsForShotMonths`）
- Modify: `src/ui/GalleryModel.h/.cpp`（`setShotSource` + 预过滤）
- Modify: `src/ui/GalleryPage.h`（转发）
- Modify: `src/ui/MainWindow.cpp`（`setShotSource(&lib_)`）
- Modify: `src/main.cpp`（I 组 parity/EXPLAIN/隐私）

- [ ] **Step 1 (RED): 写 I 组查询腿**

```cpp
    // ---- I 组：时间查询 = 索引源 vs 全量源（parity）+ EXPLAIN + 隐私 ----
    {
        auto memYears = [&](int y0, int y1) {
            QStringList v;
            for (const Photo& p : lib.photos())
                if (p.year >= y0 && p.year <= y1) v << p.path;
            v.sort();
            return v;
        };
        auto idxYears = [&](int y0, int y1) {
            QStringList v = lib.pathsForShotYears(y0, y1, false);
            v.sort();
            return v;
        };
        chk("I4_parity_years_2019", idxYears(2019, 2019) == memYears(2019, 2019));
        chk("I5_parity_years_span", idxYears(2017, 2021) == memYears(2017, 2021));

        auto memMonths = [&](const QVector<int>& ms) {
            QStringList v;
            for (const Photo& p : lib.photos()) {
                const QDate d = photoDateOf(p.date);
                if (d.isValid() && ms.contains(d.month())) v << p.path;
            }
            v.sort();
            return v;
        };
        auto idxMonths = [&](const QVector<int>& ms) {
            QStringList v = lib.pathsForShotMonths(ms, false);
            v.sort();
            return v;
        };
        chk("I6_parity_season_spring", idxMonths({3, 4, 5}) == memMonths({3, 4, 5}));
        chk("I7_parity_season_winter", idxMonths({12, 1, 2}) == memMonths({12, 1, 2}));
        chk("I8_parity_empty_input", lib.pathsForShotMonths({}, false).isEmpty());

        // 隐私：hidePrivate=true 不含 private=1
        chk("I9_privacy_years",
            !lib.pathsForShotYears(2019, 2019, true).contains(QStringLiteral("s3.jpg")));  // s3 见 Step 2 置私密
        chk("I10_privacy_months",
            !lib.pathsForShotMonths({1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}, true)
                 .contains(QStringLiteral("s3.jpg")));

        // EXPLAIN：先 ANALYZE（统计稳定），断言走复合索引且无 SCAN photos
        QSqlQuery an(QSqlDatabase::database(QStringLiteral("la_lib")));
        an.exec(QStringLiteral("ANALYZE"));
        QSqlQuery ex(QSqlDatabase::database(QStringLiteral("la_lib")));
        ex.prepare(QStringLiteral(
            "EXPLAIN QUERY PLAN SELECT path FROM photos WHERE shot_year BETWEEN ? AND ? AND private = 0"));
        ex.addBindValue(2017); ex.addBindValue(2021);
        QString plan;
        if (ex.exec()) while (ex.next()) plan += ex.value(3).toString() + QLatin1Char('\n');
        chk("I11_explain_uses_index", plan.contains(QStringLiteral("USING INDEX idx_photos_year_month")));
        chk("I12_explain_no_full_scan", !plan.contains(QStringLiteral("SCAN photos")));
    }
```

Step 2 的 fixture 里把 `s3.jpg` 标私密（`SynthRow::priv = true`），并确保它落在 2019。

Run → **I4–I12 红**（函数还没实现 / fixture 未标私密）。

- [ ] **Step 2 (GREEN): Library 两条查询**

```cpp
QVector<QString> Library::pathsForShotYears(int yearMin, int yearMax, bool hidePrivate) const
{
    QVector<QString> out;
    if (!QSqlDatabase::contains(QStringLiteral("la_lib")) || yearMin > yearMax) return out;
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("la_lib")));
    q.prepare(QStringLiteral("SELECT path FROM photos WHERE shot_year BETWEEN ? AND ?")
              + (hidePrivate ? QStringLiteral(" AND private = 0") : QString()));
    q.addBindValue(yearMin);
    q.addBindValue(yearMax);
    if (!q.exec()) return out;
    while (q.next()) out << q.value(0).toString();
    return out;                       // 顺序无契约：调用方按内存序/集合消费
}

QVector<QString> Library::pathsForShotMonths(const QVector<int>& months, bool hidePrivate) const
{
    QVector<QString> out;
    if (!QSqlDatabase::contains(QStringLiteral("la_lib")) || months.isEmpty()) return out;
    QStringList ph;
    for (int i = 0; i < months.size(); ++i) ph << QStringLiteral("?");
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("la_lib")));
    q.prepare(QStringLiteral("SELECT path FROM photos WHERE shot_month IN (")
              + ph.join(QLatin1Char(',')) + QLatin1Char(')')
              + (hidePrivate ? QStringLiteral(" AND private = 0") : QString()));
    for (int m : months) q.addBindValue(m);
    if (!q.exec()) return out;
    while (q.next()) out << q.value(0).toString();
    return out;
}
```

- [ ] **Step 3 (GREEN): GalleryModel 预过滤**

`GalleryModel.h`：

```cpp
    // 波4 R8：时间谓词走 photos 索引集合（可空 = 走全量内存路径）。
    void setShotSource(const Library* lib) { shotLib_ = lib; }
private:
    const Library* shotLib_ = nullptr;
```

`GalleryModel.cpp::rebuild()`：在 `hidePrivate_ && p.priv` 之后、其余谓词之前：

```cpp
    // 波4 R8：年/月/季先取索引集合（可证等价的两个谓词），日号与其余谓词仍在内存。
    QSet<QString> shotAllow;
    bool hasShot = false;
    if (shotLib_) {
        if (cmd_.yearMin != 0 || cmd_.yearMax != 0) {
            const int y0 = cmd_.yearMin != 0 ? cmd_.yearMin : cmd_.yearMax;
            const int y1 = cmd_.yearMax != 0 ? cmd_.yearMax : cmd_.yearMin;
            shotAllow = QSet<QString>(shotLib_->pathsForShotYears(y0, y1, hidePrivate_).begin(),
                                      shotLib_->pathsForShotYears(y0, y1, hidePrivate_).end());
            hasShot = true;
        }
        if (!cmd_.season.isEmpty() || cmd_.month != 0) {
            QVector<int> months;
            if (cmd_.month != 0) months << cmd_.month;
            else {
                const int si = seasonChars().indexOf(cmd_.season);   // 复用 CmdFilter 唯一真源
                for (int m = 1; m <= 12; ++m)
                    if (seasonIndexOf(m) == si) months << m;
            }
            QSet<QString> s(shotLib_->pathsForShotMonths(months, hidePrivate_).begin(),
                            shotLib_->pathsForShotMonths(months, hidePrivate_).end());
            shotAllow = hasShot ? (shotAllow & s) : s;
            hasShot = true;
        }
    }
```

> 上面 `pathsForShotYears(...).begin()` 取两次临时对象是**坏写法**（两次查询、迭代器悬空）。
> 实现时写成局部 `const QVector<QString> v = ...; QSet<QString> s(v.begin(), v.end());`——
> 计划里点名是提醒执行者别照抄伪码。
> 季节反查**必须**用 `seasonIndexOf`（`CmdFilter.h` 唯一真源），不许在 UI 里复刻月份表。

循环里：

```cpp
        if (hasShot && !shotAllow.contains(p.path)) continue;
```

`GalleryPage.h` 转发：

```cpp
    void setShotSource(const Library* lib) { model_.setShotSource(lib); }
```

`MainWindow` 构建图库后：

```cpp
    gallery_->setShotSource(&lib_);
```

- [ ] **Step 4: 变异证明（手动篡改，记台账）**

1. 去掉 `AND private = 0` → **I9/I10 必红**；还原。
2. 把 `shot_month` 绑定改成 `shot_year` → **I6/I7 必红**；还原。
3. 还原后 `git diff --numstat` 为空。

- [ ] **Step 5: 验证 + 提交**

```
powershell -NoProfile -ExecutionPolicy Bypass -File iterate.ps1 -Only LA_COLLECTION_QA
powershell -NoProfile -ExecutionPolicy Bypass -File iterate.ps1 -Only LA_CMDK_QA
```
手动：⌘K 输入「2019 夏天」→ 图库结果与旧全量路径一致（肉眼抽 3 张核对）。

```bash
git add native/src/core/Library.h native/src/core/Library.cpp \
        native/src/ui/GalleryModel.h native/src/ui/GalleryModel.cpp \
        native/src/ui/GalleryPage.h native/src/ui/MainWindow.cpp native/src/main.cpp
git commit -m "perf(gallery): 年/月/季走年月索引，日号留内存（wave4 R8/T11，parity+EXPLAIN 腿）"
```

---

## T12 · 画布 token + 棘轮 C3/C6 + D3 宪章 + 最小窗宽

**Files:**
- Modify: `src/ui/Theme.h`（4 token）
- Modify: `CropCanvas.cpp` / `MaskCanvas.cpp` / `EditorialView.cpp` / `CompareWidget.cpp` / `VideoPlayerWidget.cpp` / `LogPage.cpp` / `GazeLightbox.cpp`
- Modify: 5 处裸圆角（`DedupDialog.cpp:63,179`、`GalleryPage.cpp:450`、`LogPage.cpp:330,333`、`MainWindow.cpp`（原 `:1326,1353`，`makePlaceholder` 已删则按实际）、`RetouchPage.cpp:1518`）
- Modify: `native/qa_palette_ratchet.ps1` + `native/qa/palette_baseline.json`
- Modify: `src/ui/MainWindow.cpp`（`setMinimumSize`）
- Modify: D3 宪章文档

- [ ] **Step 1: 画布 token**

`LampTokens` 扩 4 字段：

```cpp
struct LampTokens { QColor bg, surface, inset, ink, muted, line, gold, emerald,
                    canvasBg, canvasLine, mask, focusRing; };
```

四灯定值（每列四值两两相异，`LA_LAMP_QA` A 组扩列后必须绿）：

| 灯 | canvasBg | canvasLine | mask | focusRing |
|---|---|---|---|---|
| Nocturne | `#0B100D` | `#1A231D` | `#070A08` | `#4FC08A` |
| Jade | `#E7E3D7` | `#D6D1C4` | `#1C1A16` | `#3F6B52` |
| Gekkan | `#EFEFEC` | `#DDDDD8` | `#111111` | `#B98A2F` |
| Dusk | `#1A120C` | `#3A2819` | `#0D0906` | `#E2A95C` |

访问器（R10：**`focusRingColor()`**，不许与既有 `focusRing(QColor)` 同名）：

```cpp
inline QColor canvasBg()       { return lamp(activeLamp()).canvasBg; }
inline QColor canvasLine()     { return lamp(activeLamp()).canvasLine; }
inline QColor maskColor()      { return lamp(activeLamp()).mask; }
inline QColor focusRingColor() { return lamp(activeLamp()).focusRing; }
```

逐点替换（按 role 换，不按字面量全集换）：
`CropCanvas.cpp:21,42,45,101,105`、`MaskCanvas.cpp:10,16,57,64`、`EditorialView.cpp:89`、
`CompareWidget.cpp:37,39,57,60`、`VideoPlayerWidget.cpp:235,242`、`LogPage.cpp:158-160`；
`GazeLightbox.cpp:104,125,279,346` 的 `rgba()` 改为 `QColor c = Lumen::maskColor(); c.setAlphaF(a);`。

- [ ] **Step 2 (RED→GREEN): C3 收紧（两步，顺序不可换）**

1. **先清 8 处裸圆角**（改走 `Lumen::Radius::*`），此时脚本未改，C3 仍 0：
   `powershell -File qa_palette_ratchet.ps1` → `C3=0`。
2. **再改定义**：`Measure-Counts` 里 C3 改为「任何 `border-radius: <数字>px` 都计数」（去掉白名单分支）：

```powershell
      foreach ($m in [regex]::Matches($ln,'border-radius:\s*(\d+)px')) {
        $c3++ }
```

3. 首跑 → 必须 `C3=0`；非 0 就回到第 1 步继续清，**不许**先改上限。
4. 基线里 `C3.ceiling` 保持 0。

- [ ] **Step 3 (RED→GREEN): C6 新维度 + 键表化**

1. `Measure-Counts` 加：

```powershell
      $c6 += @([regex]::Matches($ln,'QColor\(0x')).Count
      $c6 += @([regex]::Matches($ln,'rgba\(')).Count
```

`return @{C1=..;C2=..;C3=..;C4=..;C5=..;C6=$c6}`；`Write-Host` 行加 C6。
2. 比较循环改成键表：

```powershell
foreach ($k in @("C1","C2","C3","C4","C5","C6")) {
```

3. `-Init` 与 `-Update` 的 JSON 写入同样遍历键表（新增键 `label="canvas-raw"`）。
4. 首跑（基线尚无 C6）→ 脚本必须**硬失败**并打印当前 C6 实测值；把这个值手工写进
   `qa/palette_baseline.json`（`current` 与 `ceiling` 同值），再跑 → PASS。
5. 反例：`LA_PALETTE_QA_FORCE_BROKEN=1` 仍必须 FAIL。

- [ ] **Step 4: D3 宪章例外白名单**

在 D3 文档原文 `:28`「全站禁用 spring/overshoot>2%」与 `:51`「任何 bounce/elastic」之后增段：
「**例外白名单**：`kStarBounce`（加星）与 `kHeartBurst`（收藏心跳）属庆祝动作，允许 overshoot ≤8%，
必须吃 reduced-motion；除此之外任何 bounce/elastic 仍禁用。」并在 `MotionKit.cpp:155` 上方加一行注释
指回该段。`LA_MOTION_QA` 断言**一字不动**（R4）。

- [ ] **Step 5: 最小窗宽**

`MainWindow` 构造（`buildUi` 后）：

```cpp
    setMinimumSize(960, 640);   // 波4 §3.7：3 列 + chrome 的实测下界；「支持任意窄」不成立故不承诺
```

- [ ] **Step 6: 验证 + 提交**

```
powershell -NoProfile -ExecutionPolicy Bypass -File qa_palette_ratchet.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File iterate.ps1 -Only LA_PALETTE_QA
powershell -NoProfile -ExecutionPolicy Bypass -File iterate.ps1 -Only LA_LAMP_QA
powershell -NoProfile -ExecutionPolicy Bypass -File iterate.ps1 -Only LA_MOTION_QA
```
Expected: `C1..C6` 均 ≤ ceiling；LAMP A 组扩列绿；MOTION 一字未动仍绿。
截图四个灯各一张，确认画布层随灯变化（无第二套底色）。

```bash
git add native/src/ui/Theme.h native/src/ui/CropCanvas.cpp native/src/ui/MaskCanvas.cpp \
        native/src/ui/EditorialView.cpp native/src/ui/CompareWidget.cpp \
        native/src/ui/VideoPlayerWidget.cpp native/src/ui/LogPage.cpp \
        native/src/ui/GazeLightbox.cpp native/src/ui/MainWindow.cpp \
        native/src/ui/DedupDialog.cpp native/src/ui/GalleryPage.cpp \
        native/src/ui/RetouchPage.cpp \
        native/qa_palette_ratchet.ps1 native/qa/palette_baseline.json <D3-doc-path>
git commit -m "style(canvas): 画布层四 token + 棘轮 C6/C3 + 动效宪章例外 + 最小窗宽（wave4 T12）"
```

---

## T13 · 名册收口 + i18n 全量 + 2.43.0

**Files:**
- Modify: `native/gate_matrix.ps1`（最终数字）
- Modify: `native/i18n/LightAlbum_en.ts` + `.qm`
- Modify: `native/CMakeLists.txt`（VERSION）
- Modify: `native/CHANGELOG.md`、`native/docs/迭代指导书.md`
- Create: `native/docs/LightAlbum-2.43.0-使用说明书.md`

- [ ] **Step 1: 名册数字实测**

```
powershell -NoProfile -ExecutionPolicy Bypass -File gate_matrix.ps1 -SelfTest
powershell -NoProfile -ExecutionPolicy Bypass -File gate_matrix.ps1 -List
```
Expected: `gates=81`、`errs=0`、`nonAsciiBytes=0`、`textual=81 parsed=81`。
fast/full 以 **E6 实测分层**为准填入 CHANGELOG：若 `LA_COLLECTION_QA` 实测 >500ms → 不带 `tier`
（full），此时是 fast=22 / full=59——**如实写**，不要为对齐 spec 的 23/58 而硬塞 tier。

- [ ] **Step 2: i18n 全量**

1. 逐条 append 本波新增串（典藏页标签/空态、legend、pending、⌘K 空态与 tooltip、空态 4 页、
   来源徽记、静音时长）。`<context>` 按 `translate()` 第一个实参。
2. 更新 `LA_I18N_QA` 中需要逐字核对的 `i18nCheck`/`ctxCheck`（两条 ack 已在 T5 改线；
   本步补典藏/legend 等新串的可选核对）。
3. `lrelease` 重编 `.qm`；`.ts` 与 `.qm` **同一提交**。
4. `iterate.ps1 -Only LA_I18N_QA` → finished 计数 = 条目数、unfinished=0。

- [ ] **Step 3: 版本与文档**

1. `native/CMakeLists.txt:2` `VERSION 2.42.0` → `2.43.0`。
2. `CHANGELOG.md` 顶部新节 `[2.43.0]`：三句承诺兑现 / 时间地基 / 画布 token / 棘轮 / 门数腿数
   （全部填实测值，腿数三处同一数字：名册、CHANGELOG、指导书）。
3. `docs/迭代指导书.md` §3 门数 → 81；§7 加本波台账行。
4. 新建 `docs/LightAlbum-2.43.0-使用说明书.md`：在 2.42.0 手册基础上补
   「典藏（合集/展览史/安静下来的选题）」「凝视键位表（逐键实测）」「⌘K 暂存」，
   并**删掉**所有「下轮上线」措辞。

- [ ] **Step 4: 全量回归**

```
powershell -NoProfile -ExecutionPolicy Bypass -File iterate.ps1
```
Expected: 81 门全绿、`SelfTest` 全项绿。任何红 → 回到对应任务修，**不许**改松门禁。

- [ ] **Step 5: 手动验收（spec §6）**

1. 合集：前厅「收进合集」→ 典藏 tab1 出现 → 点进凝视，帧序 = 收藏序。
2. 静音：前厅「别再这类」→ 典藏 tab3 出现 → 「唤醒」→ 列表少 1、当场换一展。
3. ⌘K：勾 2 张 → 暂存可点 → 回执含「典藏」；输入 `zzz` → 说明文案出现；三个样例各解析成功。
4. 凝视图例：刚进入不点任何控件，逐键 ←/→/空格/Enter/Esc 全部有效。
5. 老库：2.42.0 的库直接打开，迁移 + 回填完成，照片数不变。
6. 窄窗：拖到 960×640 仍可用，再窄被系统挡住。

- [ ] **Step 6: 提交**

```bash
git add native/gate_matrix.ps1 native/i18n/LightAlbum_en.ts native/i18n/LightAlbum_en.qm \
        native/CMakeLists.txt native/CHANGELOG.md native/docs/迭代指导书.md \
        native/docs/LightAlbum-2.43.0-使用说明书.md \
        native/docs/superpowers/specs/2026-10-21-wave4-promise-kept-design.md \
        native/docs/superpowers/plans/2026-10-21-wave4-promise-kept.md
git commit -m "chore(release): 2.43.0 收口 — 81 门全绿 + 承诺兑现账（wave4 T13）"
```

**不 push、不打 tag、不打包。** 等用户明确授权。

---

## 3 · 完成判据（全部可测）

- [ ] `gate_matrix.ps1 -SelfTest`：gates=81、errs=0、nonAsciiBytes=0、textual=81=parsed。
- [ ] `iterate.ps1` 全量 81 门绿。
- [ ] `LA_COLLECTION_QA` PASS，A–I 组全部有腿；每条新腿都在落盘前见过它红一次。
- [ ] `grep -rn "下轮" native/src/` 在面向用户串里 0 命中（腿 H1 固化）。
- [ ] `LA_CURATE_QA` 的两条 ack 腿方向已翻转，且变异证明各红过一次（台账有记录）。
- [ ] `LA_GAZE_QA` 的 legend 五枚键逐键实测绿。
- [ ] `pathsForShotYears/Months` parity + EXPLAIN + 隐私三条腿全绿。
- [ ] 棘轮 C1..C6 全部 ≤ ceiling；C3 定义已收紧且首跑实测 0。
- [ ] 版本 2.43.0，CHANGELOG / 指导书 / 手册三处门数与实测一致。

## 4 · 台账纪律（SDD 执行时）

每个任务结束时在 `.superpowers/sdd/<plan-basename>/progress.md` 追加一行：
`Task N: complete — <commit> — <gate result> — <notes>`。
每个裁决追加：`Ruling: <决定> — <理由> — <错了的代价>`。
计划与 spec 冲突时以 spec 为准；specc 内部矛盾（如 R11 的 gridCols 算术）在计划层裁决并回写 spec。
