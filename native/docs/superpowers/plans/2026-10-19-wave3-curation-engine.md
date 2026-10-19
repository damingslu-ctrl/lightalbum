# 波3「主动」· 智能策展系统「常设展」实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 落地 spec `native/docs/superpowers/specs/2026-10-19-wave3-curation-engine-design.md`（本地 5a7aa9f）：策展管家 v1（12 模板选题 + 无偏好打分 + 频控 + 前厅「今日一展」卡 + 凝视序列 + 三手势 + 收藏合集）+ 废片巡检批次 + `LA_CURATE_QA`；门禁 79→80；收口 2.42.0。

**Architecture:** 新增 `core/CurateState.h`（纯数据）+ `core/CurateEngine.{h,cpp}`（**全纯函数**：候选池/分桶/12 模板表/选题/打分/成展/频控/废片巡检/画质分）；`core/Library` 补 `seenAt` 载入 + 新建 `exhibits`/`albums`/`album_items` 三表与读写 API（GUI 线程唯一写者）；后台线程跑纯计算 → `Qt::QueuedConnection` 回写（沿用 `AiWriter` 单写者语义）；`ui/LobbyPage` +`makeCurateCard()`（E4 开馆层 + 三手势）；`ui/GazeLightbox` +`setCuration()`（自动逐帧 + 2400ms 呼吸停 + 展名/小记字幕）；`ui/Theme.h` +`Elev::E3/E4`；`ui/Icons.h` +馆长图标组；`core/MotionKit` +`kExhibitBreath`；`MainWindow` 接线节流生成/三手势/refresh 汇合。门禁物形承 `runLobbyQa`（`main.cpp:1349`）。

**Tech Stack:** Qt 6.8.3 MinGW C++17（CMake 3.21）；PowerShell 门禁；lrelease = `D:/qt/6.8.3/mingw_64/bin/lrelease.exe`。

**Spec:** `native/docs/superpowers/specs/2026-10-19-wave3-curation-engine-design.md`（唯一论证基座；§0.1 的七条文档冲突裁决 R1–R7 在本计划中**逐条落地**，下方「计划裁决」只记对 spec 的**工程细化**，不改需求）。

## Global Constraints

- **门禁必须经 PowerShell**：`powershell.exe -NoProfile -ExecutionPolicy Bypass -File "<abs>\qa_runner.ps1" -Tier fast|full|<empty=all> -Only LA_X -Timing -Result <json>`；git-bash 下 Qt exe stdout 被吞（伪证据），一律走 PS。
- **`gate_matrix.ps1` 新增/修改行必须纯 ASCII**（E11/E12 在岗；PS5.1 GBK 解码下 CJK 注释曾吞 12 门）。
- **R-06 红线**：用户真实照片永不进仓；新 QA 全用合成图/内存 Library/临时目录。
- **铁律 20 同源**：门禁直调产品纯函数（`Curate::*`）；反例只经 `LA_CURATE_QA_FORCE_BROKEN` 在门禁入口注入。
- **铁律 21 注入点过滤**：私密照不进候选池/合集；`hidePrivate` 是**函数参数**不是视图开关。
- **Library GUI 线程单写者**（记忆 `ai-single-writer-gui-thread`）：后台只算，不触库；回写经 `Qt::QueuedConnection`。
- **构建**：`cd native && export PATH="/c/Users/damin/.workbuddy/binaries/python/envs/default/Lib/site-packages/cmake/data/bin:/d/qt/Tools/mingw1310_64/bin:/d/qt/6.8.3/mingw_64/bin:$PATH" && cmake --build build -j`；不新增依赖。
- **棘轮**：新 UI 颜色一律 `Lumen::` 访问器/工厂（禁裸 hex）；font-size 一律 `Type::css()/Type::*`（7 档）；radius∈{4,6,10,14,20,999}；spacing 走 `Space::`/`Hall`；禁 translate() 行内 emoji（C5）。
- **i18n**：`.ts` 手编（`lupdate` 禁用），`lrelease` 后 `.qm` 与 `.ts` **同一提交**（模板 f92ecb4）；比对本先反解 XML 实体。
- **命名/文案语义**：剔底片=`skip`·收藏=`like`·锁定=`lock`；`seenAt` 是**扫描/入库时间**，文案**不得**说成「拍摄于」。
- **push 纪律**：全程本地提交，**绝不推送**。
- **SDD ledger**：`<repo-root>/.superpowers/sdd/2026-10-19-wave3-curation-engine/progress.md` 首行 `# SDD ledger — plan: native/docs/superpowers/plans/2026-10-19-wave3-curation-engine.md`。

## 计划裁决（对 spec 的工程细化，不改需求）

1. **`Now` 注入 vs `QA` 复现**：所有时间取值经 `Curate::Now`（epochSec, y/m/d）**参数注入**，产品侧由 `CurateClock::now()` 提供一处实现；门禁传固定 `Now{1760000000,2026,10,19}` ⇒ 生成**逐字节可复现**（spec §3-C 断言的地基）。
2. **画质分只在展内**（spec R7 注的落地形式）：`score()` 签名**不含** thumbsDir，物理上不可访问图像 ⇒ 编译期即排除越界；`compose()` 才有 thumbsDir。门禁 H 组以「两库 quality 全同 vs 全异 → 全局选题逐字节相同」守此界。
3. **`Buckets` 的形状**：只做**索引**（`QHash<QString, QVector<int>>` 按模板声明的键），不复制 Photo（大库内存）；键命名钉死：`cat:<8类名>` / `person:<名>` / `y:<年>` / `ym:<年-月>` / `city:<市>` / `tag:<模板辅助标记>`。
4. **`Exhibit` 落库形状**：`paths` 列存**逗号串**（与 `Photo::person` 既有格式一致，避免引 JSON 依赖）；解析容忍空/坏值（fail-open 空集，不崩）。
5. **R3 节流的实现点**：`MainWindow::maybeGenerateExhibit()` 在**构造完成 + 每次 `refreshExhibits()`** 调用；判据 `now - lastExhibitAt > 7*86400`；生成在 `QThread`（`QtConcurrent::run` 亦可，取项目既有范式）。
6. **E4 的物理实现**：`Elev::apply(widget, Elev::E4)` 在既有 `Elev::apply`（Theme.h）里加一支：1px `Lumen::gold()` 描边 + 内发光（`QGraphicsDropShadowEffect` 或自绘径向 alpha≤10%）——**与 E1/E2 同函数**，不另起体系。
7. **「别再这类」反悔入口**：v1 **只回写冷却**；设置页反悔 UI **不做**，在冷却生效的 toast 里如实写「（反悔入口下轮上线）」——不假装可用（承波2「暂存为合集」诚实占位纪律）。
8. **`LA_CURATE_QA` 层**：`fast`（纯函数 + 合成 fixture，无图像 IO 的组占比高；含 thumbsDir 的 I 组用 QTemporaryDir 合成缩略图，实测若 >500ms 则整门移 full——由 E6 自己咬住，届时**以 `-Timing` 实测值**决定并在门禁注释记账）。

---

## 数据前置事实（本计划依赖，已核实，逐条给 file:line）

| 事实 | 证据 |
|---|---|
| `photos` 表列：`path,root,rel,src,size,mtime,thumb,exif_json,missing,seen_at` + 迁移列 `category,cat_score,cat_at,rating,person,media_type,dur_sec` + `private,mark,face_emb` | `Scanner.cpp:34-61`、`Library.cpp:29-40` |
| `Photo` 结构字段（含 `category/person/priv/mark/year/date/city/prov/region/hasGps/la/lo/rating/mediaType`） | `Library.h:55-74` |
| `seen_at` 列存在但 `Library::open` 未 SELECT（**R6 前置**） | `Library.cpp:60-62` |
| clipinfer 8 类：人像/风光/夜景·星空/城市·建筑/微距·花卉/动物/美食/黑白（+ 其他兜底） | `Classifier.cpp:45-51,98` |
| 人物＝`Photo::person` 逗号名串；聚合 `PeoplePage::buildRooms` | `PeoplePage.cpp:100`、`PeoplePage.h:42` |
| pHash/hamming：`CullEngine::computePhash/hamming`（**不落库，现算**） | `CullEngine.h:102-103` |
| 模糊/曝光：`CullEngine::blurScore/sharpnessScore/exposureStats` | `CullEngine.h:105-108` |
| 去重：`PhotoDedup::describe/group`；`DedupDesc{path,md5,phash,pixels,ok}` | `PhotoDedup.h:32-38,58,65` |
| GPS 反查：`GeoIndex::resolve`；EXIF：`ExifReader::readExif` + `fillMtimeFallback` | `GeoIndex.h:24`、`ExifReader.h:25`、`ExifReader.cpp:353-361` |
| LobbyPage：ctor `LobbyPage(const QVector<Photo>*, const QString& thumbsDir, QWidget*)`；纯函数 `onThisDay/recent`；`makeHeroCard()`；`rebuild()` | `LobbyPage.h:21,34,38`、`LobbyPage.cpp:235-273,199,221-231` |
| GazeLightbox：ctor `(Library*,QWidget*)`；`setPhoto/enter/navigate/setScope`；`Mode{Browse,Gaze}`；无自动序列；文本面仅 `title_`+`meta_` | `GazeLightbox.h:41,50-72`、`.cpp:468,127-140,194-203` |
| Theme.h：`Lamp` :36、`Hall` :38、`Space`(4px 网格) :256、`Tier` 7 档 :262、accessors :124-148、**`Elev{E1,E2}` :291** | `Theme.h` |
| Icons：`Icons::make(kind,color,size=22)`；kinds `home,map,gallery,cull,retouch,enhance,log,lobby,people,lamp`（**无 star/heart/curate**） | `Icons.h:12,84` |
| MotionKit 预设表 + `Pulse` 曲线 | `MotionKit.cpp:118-199,45-48`、`.h:66-73` |
| MainWindow 刷新汇合 `refreshExhibits()`；Lobby 构造 :422 / 栈 :450；导航 7 行 :114-117 | `MainWindow.cpp:791-796,422,450,114-117` |
| 门禁范式 `runLobbyQa()` :1349；分发 :9278；FORCE_BROKEN :1351；打印 :1459-1462；`return pass?0:1` :1464 | `main.cpp` |
| 合集**不存在**：「暂存为合集」是 `setEnabled(false)` 占位 | `CmdKPanel.cpp:386-390` |

---

### Task 1: 数据前置 — `Photo::seenAt` 载入 + `Library` 三表与读写 API

**Files:**
- Modify: `src/core/Library.h`（`Photo` +`qint64 seenAt=0`；新增 API 声明）
- Modify: `src/core/Library.cpp`（`open()` SELECT 增 `seen_at`；建表/迁移；API 实现）
- Test: `src/main.cpp` 内 `runCurateQa()` 的 **A0 组**（本任务先落最小腿；其余组后续任务加）

**Interfaces:**
- Produces: `Photo::seenAt`（qint64, 扫描/入库秒）；`Library::addExhibit(const Exhibit&)->qint64`；`Library::updateExhibitFeedback(qint64 id, const QString& fb)`；`Library::loadExhibits()->QVector<ExhibitRow>`；`Library::lastExhibitAt()->qint64`；`Library::muteTemplate(const QString& tid)`；`Library::loadMuteState()->QStringList`；`Library::createAlbum(const QString& name, const QStringList& paths, const QString& source)->qint64`；`Library::listAlbums()->QVector<AlbumRow>`
- Consumes: 无（最底层）

- [ ] **Step 1: 写失败测试（A0 组，先红）**

在 `src/main.cpp` 的 `runCurateQa()`（本任务新建，见 Step 3）中加：
```cpp
// A0: seenAt 载入腿 —— 对合成库断言 Photo::seenAt 非零且等于写入值
QString dir = QDir::tempPath() + "/la_curate_qa_t1";
QDir().mkpath(dir);
Library lib(QDir::tempPath() + "/la_curate_qa_t1.sqlite");
lib.upsertSynthetic(QStringList{dir + "/a.jpg"}, /*seenAt=*/1760000000.0);  // 测试辅助，见 Step 4
auto ps = lib.photos();
bool a0 = !ps.isEmpty() && ps[0].seenAt == 1760000000LL;
report("A0_seenAt_loaded", a0);
```
Run: `powershell -File qa_runner.ps1 -Only LA_CURATE_QA -Timing`
Expected: FAIL（`Photo::seenAt` 尚不存在 → 编译失败即红；编译通过但断言 false 亦是红）

- [ ] **Step 2: 补 `Photo::seenAt` 与 SELECT**

`Library.h:55-74` 结构体加 `qint64 seenAt = 0;`；`Library.cpp:60-62` 的 SELECT 列清单加 `seen_at`，并在行装配处 `p.seenAt = qint64(row.value("seen_at").toDouble());`。

- [ ] **Step 3: 落 `runCurateQa()` 骨架 + env 分发**

比照 `runLobbyQa`（`main.cpp:1349`）：
```cpp
static int runCurateQa() {
  int checks = 0, failed = 0;
  const bool broken = !qEnvironmentVariableIsEmpty("LA_CURATE_QA_FORCE_BROKEN");
  auto report = [&](const char* n, bool ok) { ++checks; if (!ok) ++failed;
      fprintf(stdout, "  LA_CURATE_QA %s %s\n", ok ? "PASS" : "FAIL", n); };
  // ... A0 组 ...
  fprintf(stdout, "LA_CURATE_QA %s checks=%d failed=%d broken=%d\n",
          failed == 0 ? "PASS" : "FAIL", checks, failed, broken ? 1 : 0);
  return failed == 0 ? 0 : 1;
}
```
分发（`main.cpp:9278` 同款）：`if (qEnvironmentVariableIsSet("LA_CURATE_QA")) return runCurateQa();`

- [ ] **Step 4: 建表 + 迁移 + 读写 API**

`Library.cpp` 构造函数尾部（`Library.cpp:29-40` 同区）追加幂等建表（spec §4 DDL 原文照抄）；新增：
```cpp
qint64 Library::lastExhibitAt() { /* SELECT max(generated_at) FROM exhibits */ }
qint64 Library::addExhibit(const Exhibit& e) { /* INSERT ... ; paths 存逗号串 */ }
```
**合成库辅助**（仅 QA 用，命名 `upsertSynthetic`，生产零调用）供 A0 腿使用。

- [ ] **Step 5: 跑测试转绿 + 构建**

Run: `powershell -File qa_runner.ps1 -Only LA_CURATE_QA -Timing`  → Expected: `PASS checks=1 failed=0`

- [ ] **Step 6: 注册门禁（纯 ASCII 一行）+ 提交**

`gate_matrix.ps1`（`LA_PEOPLE_QA` 行后）：`@{n="LA_CURATE_QA"; png=$false; tier="fast"; measuredMs=300},`（measuredMs 待 T11 回填真值）
```bash
git add native/src/core/Library.h native/src/core/Library.cpp native/src/main.cpp native/gate_matrix.ps1
git commit -m "feat(curate): Photo::seenAt 载入 + exhibits/albums/album_items 建表（spec §4 R5/R6）"
```

---

### Task 2: `core/CurateState.h` 纯数据结构

**Files:** Create: `src/core/CurateState.h`
**Interfaces:** Produces: `Curate::{TemplateId,Direction,Weights,Theme,Exhibit,BadShotRecord,BadShotBatch,Now,Buckets,ExhibitHistory,MuteState,AlbumRow,ExhibitRow,SkipReason}`（spec §2.1 原文 + Step 2 的两个索引结构）
**Consumes:** 无

- [ ] **Step 1: 写失败测试** — `runCurateQa` 加 A 组首腿：`A_templateTableCount`（此刻编译失败即红）。Run → Expected FAIL
- [ ] **Step 2: 落头文件** — 照 spec §2.1 逐字落；补：
```cpp
struct Buckets { QHash<QString, QVector<int>> byKey; };   // 见计划裁决 3
struct ExhibitHistory { QStringList shownPaths; QHash<QString,qint64> lastShownByKey;
                        QHash<QString,qint64> cooldownUntil; };   // key = "person:X"/"tmpl:X"
struct MuteState { QStringList mutedTemplates; bool globallyMuted = false; };
struct ExhibitRow { qint64 id; QString tmpl,title,blurb,primary,paths,feedback; qint64 at; };
struct AlbumRow  { qint64 id; QString name,source; qint64 createdAt; int count; };
struct SkipReason { QString code; };   // 非空即缺源（R7）
```
- [ ] **Step 3: 构建 + 提交** — `feat(curate): CurateState.h 纯数据结构（spec §2.1）`

---

### Task 3: `CurateEngine` 12 模板表 + `candidatePool` + `bucket`

**Files:** Create: `src/core/CurateEngine.{h,cpp}`；Modify: `CMakeLists.txt`（LA_SOURCES 加两行）
**Interfaces:**
- Produces: `const QVector<TemplateSpec>& templateTable()`（恒 12，`TemplateSpec{id,nameKey,sourceId,direction}`）；`candidatePool(const QVector<Photo>&,bool,const QStringList& recent,const Now&)->QVector<const Photo*>`；`Buckets bucket(const QVector<const Photo*>&)`
- Consumes: `CurateState.h`

- [ ] **Step 1: 写失败测试（A/B 组）**
```cpp
report("A_templateTable_12", templateTable().size() == 12);
report("A_direction_down_only_sweep",
       std::count_if(templateTable().begin(), templateTable().end(),
        [](auto&s){ return s.direction==Direction::Down; }) == 1);
// B: 私密 + 近4周已展出被排除
QVector<Photo> all = makeSyntheticPhotos(30);   // 含 2 张 priv=true, 3 张在 recent
auto pool = candidatePool(all, /*hidePrivate=*/true, recent, Now{1760000000,2026,10,19});
report("B_excl_priv", none_of(pool, [](auto*p){ return p->priv; }));
report("B_excl_recent", none_of(pool, [&](auto*p){ return recent.contains(p->path); }));
report("B_keeps_rest", pool.size() == 25);
```
Run → Expected FAIL（未定义）
- [ ] **Step 2: 实现** — 模板表按 spec §2.3 逐条（12 行，`nameKey` 用 i18n key 字面量）；`candidatePool` 三重过滤；`bucket` 按计划裁决 3 的键命名填索引。
- [ ] **Step 3: 绿 + 提交** — `feat(curate): 12 模板表 + candidatePool + bucket（spec §2.2/§2.3）`

---

### Task 4: 选题器 `propose` + 打分器 `score`（无偏好）+ 频控 `frequencyAllows`

**Files:** Modify: `src/core/CurateEngine.{h,cpp}`
**Interfaces:**
- Produces: `QVector<Theme> propose(TemplateId, const Buckets&, const QVector<Photo>&, const Now&)`；`double score(const Theme&, const Weights&, const ExhibitHistory&, const Now&)`；`bool frequencyAllows(const Theme&, const ExhibitHistory&, const MuteState&, const Now&)`
- Consumes: Task 3

- [ ] **Step 1: 写失败测试（C/D/E/F/G/K 组）**
```cpp
// C: 新鲜感单调 + 确定性
auto s1 = score(t, w, histOld, now), s2 = score(t, w, histNew, now);
report("C_fresh_monotone", s1 > s2);                 // 久未展出 > 刚展出
// E: 无偏好学习 —— 两套收藏历史 → 逐字节相同
report("E_pref_zero", w.pref == 0.0);
report("E_hist_independent", qFuzzyCompare(score(t,w,hA,now), score(t,w,hB,now)));
// G: 冷启动
report("G_cold_lt50_skip", proposeColdStart(49).valid == false);
report("G_cold_lt200_two_tmpl", coldStartTemplates(150).size() == 2);
// K: 缺源 → 空 + skipReason（清空城市字段后在 CitySlice 上题）
report("K_missing_source_skips", proposeCitySliceNoCity().theme.paths.isEmpty());
```
Run → Expected FAIL
- [ ] **Step 2: 实现** — `propose` 每模板一支；`score = w1·fresh + w2·pref(0) − w4·repeat`（**无 thumbsDir 形参**，见计划裁决 2）；`frequencyAllows` 三判（周 ≤1 / `person:X` 60 天冷却 / muted）。
- [ ] **Step 3: A 组补 FORCE_BROKEN 反例臂** — `broken` 时把 `w.pref` 置 1.0 并让 `E_pref_zero` 红。
- [ ] **Step 4: 绿 + 提交** — `feat(curate): 选题/打分(无偏好)/频控（spec §2.2 R2/R3）`

---

### Task 5: 成展器 `compose` + 画质分 `qualityScore`

**Files:** Modify: `src/core/CurateEngine.{h,cpp}`
**Interfaces:**
- Produces: `Theme compose(const Theme&, const QVector<Photo>&, const QString& thumbsDir)`；`double qualityScore(const Photo&, const QString& thumbsDir)`
- Consumes: `CullEngine::blurScore/exposureStats`（`CullEngine.h:105-106`）

- [ ] **Step 1: 写失败测试（H 组 + compose 腿）**
```cpp
// H: 画质不越界 —— 两库 quality 全同 vs 全异 → 全局选题逐字节相同
auto gA = globalSelect(libQualityAllEqual), gB = globalSelect(libQualityAllDiff);
report("H_quality_not_global", themeIds(gA) == themeIds(gB));
// compose: 主图 = 展内画质最高
report("compose_primary_is_best", composed.primaryPath == bestQualityPathIn(composed.paths));
report("compose_size_5to9", composed.paths.size() >= 5 && composed.paths.size() <= 9);
```
Run → Expected FAIL
- [ ] **Step 2: 实现** — `compose` 对**展内帧**调 `qualityScore`（`blurScore`+`exposureStats` 合成归一化），主图取最高、陪展类内多样（用 `CullEngine::computePhash/hamming` 去冗余）；`qualityScore` 只在此路径被调用。
- [ ] **Step 3: 绿 + 提交** — `feat(curate): 成展器 + 画质分（spec R7，画质仅展内）`

---

### Task 6: 废片巡检 `inspectBadShots`

**Files:** Modify: `src/core/CurateEngine.{h,cpp}`
**Interfaces:** Produces: `BadShotBatch inspectBadShots(const QVector<Photo>&, const QString& thumbsDir, const Now&)`；`QStringList badShotWhy(...)`
**Consumes:** `CullEngine::blurScore`、`PhotoDedup`/`computePhash`、`Photo::seenAt`

- [ ] **Step 1: 写失败测试（I 组）**
```cpp
// I: 只打包不删除 + 整批反悔回原态
auto batch = inspectBadShots(all, thumbs, now);
report("I_never_deletes", all.size() == countFilesOnDisk(dbDir));       // 磁盘张数不变
lib.setMarks(paths(batch.items), "skip");
report("I_pack_marked", lib.allMarked(paths(batch.items), "skip"));
lib.setMarks(paths(batch.items), "");                                    // 整批反悔
report("I_undo_restores", lib.allMarked(paths(batch.items), ""));
```
Run → Expected FAIL
- [ ] **Step 2: 实现** — 三条规则（连拍冗余 = 邻域内数量 >2；糊图 = `blurScore < 80`，承 `CullEngine.h:88`；截图过期 = 文件名/尺寸启发 + `seenAt` > 阈值）；**只产出记录，不调用任何删除 API**。
- [ ] **Step 3: 绿 + 提交** — `feat(curate): 废片巡检批次（永不删除，可整批反悔；spec 红线3）`

---

### Task 7: 编排与回写 — 节流生成 + 后台线程 + GUI 单写者

**Files:** Create: `src/core/CurateScheduler.{h,cpp}`（薄编排）；Modify: `CMakeLists.txt`
**Interfaces:** Produces: `CurateScheduler::maybeGenerate(Library&, const QVector<Photo>&, bool hidePrivate)`（7 天节流 + `QtConcurrent::run`）；完成信号 `generated(qint64 exhibitId)`
**Consumes:** Task 3-6 + Task 1 的 Library API

- [ ] **Step 1: 写失败测试** — 门禁侧：`report("R3_throttle_7d", scheduler.shouldGenerate(lastAt=now-6*86400, now) == false)` 且 8 天 → true（`shouldGenerate` 为纯函数，可直测）。
- [ ] **Step 2: 实现** — 生成在后台跑纯函数；结果用 `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` 回主线程落库（**不在后台触 Library**）。
- [ ] **Step 3: 绿 + 提交** — `feat(curate): 7 天节流编排 + 后台生成 + QueuedConnection 回写（spec R3）`

---

### Task 8: `ui/Theme.h` +`Elev::E3/E4`；`Icons.h` 馆长图标组；`MotionKit` +`kExhibitBreath`

**Files:** Modify: `src/ui/Theme.h`（`Elev::Level` 加 `E3,E4`，`Elev::apply` 加两支）；Modify: `src/ui/Icons.h`（+`curate/star/reject/swap`）；Modify: `src/core/MotionKit.{h,cpp}`（+`kExhibitBreath` 2400ms，复用 `Pulse`）
**Interfaces:** Produces: `Elev::E4`（1px `Lumen::gold()` 描边 + 内发光 α≤10%）；`Icons::make("curate"|"star"|"reject"|"swap", color, size)`
**Consumes:** 既有 `Elev::apply`（`Theme.h:291`）、`MotionKit` 预设表

- [ ] **Step 1: 写失败测试** — `report("Elev_E4_exists", (int)Elev::E4 == 4);` + 门禁断言 E3/E4 在 `Elev::apply` 的 switch 里各有一支（grep 计数腿）。Run → FAIL
- [ ] **Step 2: 实现** — E3=shadow-l；E4=金描边+内发光（spec §2.4 规范）；四图标 1.5px 描边 24 格；`kExhibitBreath` 用既有 `Pulse` 曲线（**不新造曲线**）。
- [ ] **Step 3: 绿 + 提交** — `feat(theme): Elev::E3/E4 + 馆长图标组 + kExhibitBreath（V4 §四/V3 动宪）`

---

### Task 9: `LobbyPage`「今日一展」卡 + 三手势 + ⌘K E4 retro-apply

**Files:** Modify: `src/ui/LobbyPage.{h,cpp}`（+`makeCurateCard()`、+`setExhibit(const Exhibit&)`、+信号 `keepExhibit/swapExhibit/muteTemplate/openCuration`）；Modify: `src/ui/CmdKPanel.cpp`（E4 描边）
**Interfaces:** Produces: `LobbyPage::setExhibit(const Curate::Exhibit&)`；信号 `openCuration(QStringList paths, QString title, QString blurb)`
**Consumes:** Task 8（E4/Icons/Motion）、Task 2（Exhibit）

- [ ] **Step 1: 写失败测试（J 组）** — 卡片文案由 `Curate` 模板拼接（非 LLM）：`report("J_copy_from_template", card.title == templateTitle(e.theme.id, e.theme.reasons));` + `report("J_copy_deterministic", buildCardText(e) == buildCardText(e));`
- [ ] **Step 2: 实现** — 在 `rebuild()`（`LobbyPage.cpp:221-231`）里插 `col->addWidget(makeCurateCard())`（E4 开馆层 + `kExhibitBreath` 微光）；三手势按钮 → 信号；`Exhibit::valid==false` 时**整卡不出现**（冷启动 <50，spec G）。
- [ ] **Step 3: 绿 + 构建 + 提交** — `feat(lobby): 今日一展卡 + 三手势（spec §2.4，E4 首用）`

---

### Task 10: 凝视序列 — `GazeLightbox::setCuration` + 呼吸停

**Files:** Modify: `src/ui/GazeLightbox.{h,cpp}`（+`setCuration(const Exhibit&, thumbsDir)`、+逐帧 timer、+展名/小记字幕）
**Interfaces:** Produces: `GazeLightbox::setCuration(const Curate::Exhibit&, const QString& thumbsDir)`（`setScope(exhibit.paths)` + 自动逐帧 + 2400ms 驻足 + 字幕）
**Consumes:** Task 2、既有 `navigate()`（`.cpp:468`）、`MotionKit::kExhibitBreath`

- [ ] **Step 1: 写失败测试** — 序列为纯状态机：`report("seq_advance_wraps", SeqState{3,2}.advance().idx == 0);` + `report("seq_respects_pause", SeqState{3,1}.paused(true).advance().idx == 1);`
- [ ] **Step 2: 实现** — 自动逐帧 `QTimer`（2400ms）+ 换帧走既有 `LightboxSlide`；**字幕复用 `meta_`**（`GazeLightbox.cpp:134-140`）显示「展名 · 小记」，不新建文本层；任意键/点击**可打断**（承 D3 禁则）。
- [ ] **Step 3: 绿 + 构建 + 提交** — `feat(gaze): 策展序列（自动逐帧 + 2400ms 呼吸停 + 展名小记字幕；spec R1）`

---

### Task 11: MainWindow 接线（三手势落库 / 收藏合集 / 反悔文案 / refresh 汇合）+ 门禁回填

**Files:** Modify: `src/ui/MainWindow.cpp`（构造后 `maybeGenerateExhibit()`；信号接线；`refreshExhibits()` 汇合 `lobby_->setExhibit`）
**Interfaces:** Consumes: Task 7（scheduler）、Task 9（Lobby 信号）、Task 1（Library）
- [ ] **Step 1: 写失败测试** — 收藏落合集：`report("keep_creates_album", lib.listAlbums().size() == 1 && lib.listAlbums()[0].source == "curate");`；「别再这类」只回写冷却且 toast 含「反悔入口下轮上线」。
- [ ] **Step 2: 实现** — `keepExhibit`→`createAlbum`+`updateExhibitFeedback("kept")`；`swapExhibit`→`updateExhibitFeedback("swapped")`（连续 3 次隐式降权**留波5**，本轮只记录）；`muteTemplate`→`muteTemplate(tid)`+toast 诚实文案；`openCuration`→`ensureLightbox()->setCuration(...)`。
- [ ] **Step 3: 回填 `measuredMs`**（以 `-Timing` 实测值，注释记账）+ 全量回归 + 提交 — `feat(curate): MainWindow 接线（三手势/合集/刷新汇合）（spec §3）`

---

### Task 12: 收口 2.42.0（版本 / CHANGELOG / 指导书 / 手册 / 全量门 / 记忆）

**Files:** Modify: `CMakeLists.txt:2`（`2.41.0`→`2.42.0`）；Modify: `native/CHANGELOG.md`（`## [2.42.0]`）；Modify: `native/docs/迭代指导书.md`（§3 门数 79→80 + §7 台账新行）；Create: `docs/LightAlbum-2.42.0-使用说明书.md`
- [ ] **Step 1: 版本 bump + 重建**（`CMAKE_PROJECT_VERSION:STATIC=2.42.0` 核验）
- [ ] **Step 2: `gate_matrix.ps1 -SelfTest`** → 期望 `gates=80 fast=23 full=57 errs=0`；全量套件 `PASS/FAIL/SKIP` 记账；**新增 0 SKIP**
- [ ] **Step 3: i18n** — 12 展名模板 + 三手势 + 卡/空态文案入 `.ts`，`lrelease` 出 `.qm`，**同一提交**
- [ ] **Step 4: 文档** — CHANGELOG 段（含 R1–R7 七条裁决与「数据面现成只对 AI 三源成立」的诚实记录）+ 指导书 §3/§7 + 新手册
- [ ] **Step 5: 台账 + 记忆 + 精确提交**（仅列名文件；无 `git add -A`；**不 push**）

---

## Self-Review（本计划对 spec 的覆盖自查）

- **spec §0.1 R1–R7**：R1→T10(序列)+T12(记录)；R2→T4(E 组)；R3→T7；R4→T8+T9+T11(⌘K)；R5→T1；R6→T1(A0)；R7→T3(表)+T5(quality 仅展内)+门禁 K 组。**全覆盖**。
- **spec §3 门禁 A..L 组**：A→T3/T4，B→T3，C/D/E/F/G/K→T4，H→T5，I→T6，J→T9，L(反例矩阵)→T4/T8/T11 逐组补臂。**全覆盖**。
- **占位扫描**：无 TBD/TODO；每个代码步给了可执行片段或钉死签名。
- **类型一致**：`TemplateSpec/Theme/Exhibit/BadShotBatch/SkipReason` 定义于 T2，其后仅消费；`Exhibit::paths` 逗号串（T1 落库 = T9/T10 消费）一致；`Now` 注入一致（T3 起全程带 `Now`）。
- **发现并修正**：spec §2.2 原把 `w3·画质` 写进全局 `score()`，与 R7 注冲突 → spec 已改为「全局 score 不含画质，画质只进 `compose`」，本计划 T4/T5 按改后版本落（`score()` **无** thumbsDir 形参，编译期守界）。
