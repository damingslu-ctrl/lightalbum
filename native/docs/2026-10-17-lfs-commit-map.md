# 2.40.0 发布记录：Git LFS 迁移与 old→new 提交映射

**日期**：2026-10-17（本机 Asia/Shanghai +08:00）
**范围**：`refs/remotes/origin/main`(`2e81d61`)..`main` 全部 **93** 枚未推送提交
**裁定来源**：用户在四条路（LFS 迁移 / 剔除 ONNX 改走 fetch / 不推送保持本地 / 换不受限托管）中选定 **LFS 迁移**

---

## 一、起因（实测，不是推断）

`git push origin main` 被 GitHub 拒绝，命中单对象 **100 MiB 硬限**：
`native/3rdparty/clip/bin/clip_vit_b32_img.onnx` = **351,614,324 B（335.33 MiB）**，
是被推范围内唯一越过该线的对象（`git cat-file --batch-all-objects` 全量扫，>104,857,600 B 者恰此一枚）。

关键约束：该限制按**被推范围内的每一个历史对象**评估，所以事后 `git rm --cached`
**修不了**——它只改树，不改历史里那枚 335 MiB blob。能修的只有改写历史（LFS 迁移或 filter-branch）。
这正是 R-77 当年「保持跟踪」裁定所接受的代价（`ahead 92 + one >50MB blob`），今日到期兑现。

## 二、执行的命令

```
git lfs install --local
git lfs migrate import -y \
  --include=native/3rdparty/clip/bin/clip_vit_b32_img.onnx \
  --include-ref=refs/heads/main --exclude-ref=refs/remotes/origin/main
```

- **显式 `--include` 单文件**，不用 `--include="*.onnx"`：`3rdparty/lama/bin/` 与 `3rdparty/face/bin/` 里的
  ONNX 从未被跟踪，通配会把注意力引到不存在的对象上；范围越小，误伤面越小。
- **不加 `--everything`**：只重写 `origin/main..main` 这 93 枚 ⇒ `2e81d61` 仍是新历史的祖先 ⇒ 推送是
  **快进（fast-forward）**，不是强推，远端已有历史一枚都不动。
- 首次运行失败并**未改写任何东西**（`git lfs migrate` 会先 `git fetch origin` 校准远端状态，本机无代理
  ⇒ `Recv failure: Connection was reset` ⇒ exit 2）。补 `--include-ref/--exclude-ref` 后无需联网，
  exit 0：`Rewriting commits: 100% (93/93)` / `Updating refs` / `Checkout` 全段完成。

## 三、改动面（逐条实测）

```
$ git diff --no-ext-diff --name-status refs/preLfs/oldmain main
A	.gitattributes
M	native/3rdparty/clip/bin/clip_vit_b32_img.onnx
```

**只有这两条**。93 枚提交的树内容除这一枚 blob 换成 134 B 指针外完全不变；
`git rev-list --count` 两侧都是 93（全仓 261→261）；逐枚按序配对后**主题 93/93 相同**（零 SUBJ-DIFF），
配对脚本与结果表在本文件第八节。作者/提交时间戳亦未变（改写只换父指针与树）。

**改写后同范围复扫（这是「推送现在能过」的直接证据，不是推断）**：

```
$ git rev-list --min-parents=2 --count origin/main..main
0                      # 线性历史 ⇒ 第八节的序号配对即拓扑配对
$ git rev-list --objects origin/main..main | <取 oid> | git cat-file --batch-check
                       # 筛 blob 且 objectsize > 104857600
0                      # 被推范围内 >100 MiB 的对象：一枚都没有
                       # 现范围最大三枚 blob = 517,173 / 510,066 / 499,248 B
```

迁移前同一枚 `clip_vit_b32_img.onnx` 的 blob 是 351,614,324 B（当时全仓 `--batch-all-objects`
扫 >100 MiB 者也恰只此一枚），GitHub 的拒绝正落在它身上；改写后它只剩 134 B 指针在库里。

## 四、备份与回退路径（改写前先建，事后核过）

| 资产 | 值 | 核实方式 |
|---|---|---|
| bundle | `D:/git-backups/lightalbum-preLfs-2026-10-17.bundle`（366,769,298 B） | `git bundle verify` 通过，列出 `v2.36.0`/`v2.37.0`/`v2.40.0` 与旧 HEAD，「records a complete history」 |
| 旧 tip | `refs/preLfs/oldmain` = `55b9cd503e30481856fe45926ea2a56f2f5c1840` | `git cat-file -t` = commit |
| 旧附注标签 | `refs/preLfs/tag-v2.40.0` = `ba9fa8e615b193b796ab418e623ad0fdad422192` | `peel^{}{commit}` = `55b9cd5…` |
| 模型完整性锚 | `clip_vit_b32_img.onnx` sha256 = `0c172d0f3c412302294962140942875abb78115b1dffdbc393ddd7fecc9c90c6` | 与 `native/fetch_ai.ps1:113` 钉死值逐字符相同（迁移前后各算一次） |

**一条必须记下的行为特征**：`git lfs migrate` 的 `Updating refs` 阶段会改写**范围内所有 ref**，
预建的 `refs/preLfs/main` 因此被同步刷成了新 tip——这不是事故，是它的语义。旧 tip 靠上表
`refs/preLfs/oldmain` + `refs/preLfs/tag-v2.40.0` + bundle 三处保住。**下次先建 backup ref 时，
要预期它本身也被重写**，保旧只能靠 tag 反查或 bundle。

**另一条**：`Checkout` 阶段跑时**不应用 LFS smudge 过滤器**，于是工作区的
`clip_vit_b32_img.onnx` 落地成 134 B 指针文件（`git status` 仍干净——clean 过滤器把它还原成指针，
两边自洽）。已用 `git lfs checkout native/3rdparty/clip/bin/clip_vit_b32_img.onnx` 还原为
351,614,324 B 并复验 sha256。**任何在此之后跑的门都必须在还原后的工作区上跑**，否则 #76/#77 会读到指针当模型。

## 五、迁移后复验（同一台机、同一棵改写后的树）

| 项 | 结果 |
|---|---|
| `gate_matrix.ps1 -SelfTest` | `gates=78 fast=19 full=59 nonAsciiBytes=0 textual=78 parsed=78 fails=0` |
| 全量套件 第 1 跑（五路 opt-in env 全挂） | `PASS=65 FAIL=1 CHECK=0 SKIP=12 KILL=0`，墙钟 126,938 ms —— 唯一红是 `LA_MAP_PERF world=1.80ms > budget=1.60ms` |
| `LA_MAP_PERF` 单跑 | `world=0.75ms`（预算 1.60 ms），PASS |
| 全量套件 第 2 跑（无任何并发工作） | `PASS=66 FAIL=0 CHECK=0 SKIP=12 KILL=0`，墙钟 **115,904 ms** |
| `release.ps1 -SelfTest` | `contracts=10 injected=22 caught=22 baseline=10 zip=9` |
| 四路真模型/契约门 | `#75 tamper=0 g1..g9=1`、`#76 tamper=0 g1..g11=1`、`#77 imgs=13 golds=13 ok=13`、`#78 imgs=6 faces=30` 全 `[OK]` |

第 1 跑那枚红的根因**不是代码**：控制端在计时窗口内并发起了一段 186 次 `git` 子进程的取映射表脚本，
把 1.6 ms 预算的渲染腿挤到 1.80 ms。源码字节在两跑之间完全没动（改写只换那枚 ONNX），
且同树同判据的第 2 跑回到 66/12 与迁移前发布态读数一致。
`LA_MAP_PERF` 是 ms 级预算门，**天生吃并发**——这已登记为第五轮 `measuredMs` 单口径重标候选的一部分。

## 六、对开发与打包的实际影响（诚实口径）

1. **全新 `git clone` 不再自带那 335 MiB 塔**。要跑 #77/#76 的真模型腿或打包 C10 的 clip 装载闭包，
   需 `git lfs install` + `git lfs pull`（或用 `fetch_ai.ps1` 从上游取回并校验）。
2. **R-77 例外的形状变了**：`native/3rdparty/clip/bin` 跟踪的**路径数仍是 2**，但
   `clip_vit_b32_img.onnx` 在库里是 134 B 的 LFS 指针，真实字节在 LFS 存储中。
   「clone 即可跑 #77」这句当年的理由，现在要加一个前提：客户端装了 LFS。
3. **打包侧零影响**：`release.ps1` 用 `Copy-Item` 从**文件系统**
   `native/3rdparty/clip/bin/` 上料（`release.ps1:1156-1170`），不走 `git archive`，
   只要工作区已 smudge 就是整塔字节；C10 的存在/非空/装载腿照旧。
4. **GitHub LFS 免费额度**：1 GiB 存储 / 1 GiB 月下载。本次上传 335.33 MiB ≈ 存储的 32.7%，
   一次「带 LFS 的完整克隆」就吃掉当月额度的约三分之一。这是长期成本，不是本次一次性成本。

## 七、文中旧哈希引用的处置

CHANGELOG / 指导书 / `gate_matrix.ps1` / spec 里散落着约 57 处**旧哈希**引用（如 `d6e4a51`、`5aad39c`、
`576b7a2`）。本记录**不改写它们**：一是量大且各条都嵌在自己的证据叙述里，改写会让「当时看到的是哪个数」
失去抓手；二是 `gate_matrix.ps1` 有在册的纯 ASCII 铁律。做法是**在此追加一张 old→new 全表**，
读旧引用时按表换算。旧哈希在本机仍可解析（`refs/preLfs/oldmain` 仍锚着旧链），
但在 GitHub 上**只有新列有效**——远端读者请用新哈希。

## 八、old → new 提交映射全表（93 行，按拓扑序）

| # | 旧（迁移前，仅本机可解析） | 新（`main` 上，推送后唯一有效） | 主题 |
|---|---|---|---|
| 1 | `9a2dd53f7` | `9617d3ceb` | docs(design): 高级感设计审视报告（升级改造子轨0）——真缺陷 D1-D7 截图实证 + 结构性设计债量化（434 hex/53 off-token/34 emoji 图标/零阴影体系）+ P0-P4 升级路线 |
| 2 | `e3f9bc5a6` | `5869cd28a` | docs(design): P1 设计语言 v2 spec（五节评审定稿）+ 审视报告 D5 勘误指针 |
| 3 | `8412be20f` | `74d4b3b51` | docs(plan): P1 设计语言 v2 实施计划（12 任务，门禁先行+四类棘轮归零） |
| 4 | `864ecf701` | `77effccc4` | docs(plan): P1 计划 pre-flight 修订——Type::smallText 统一拼写（避 Windows RPC small 宏） |
| 5 | `274d68e66` | `a4a6a8de7` | feat(gates): LA_PALETTE_QA ratchet gate first (design v2 spec s5) - 5 counts, ceilings C1=415/C2=68/C3=71/C4=117/C5=0, FORCE_BROKEN counterexample + CE9, roster 71->72 |
| 6 | `58c5f4494` | `98324d01c` | fix(gates): LA_PALETTE_QA C5 counts translate()-idiom emoji sites (spec s5 intent, audit 34) |
| 7 | `db010f856` | `299f02113` | feat(theme): design v2 language layer - 8 new tokens + CAT1..8 + glow presets + Radius/Space/Type/Elev helpers + btnViolet/btnOutline (additive only) |
| 8 | `1bb6a5113` | `c491ba639` | fix(theme): retire #8da996 MUTED-typo — 22 sites now interpolate Lumen::MUTED; C1 ceiling 415->394 |
| 9 | `e4e3a4312` | `4a06483ee` | fix(theme): retire MUTED-typo at remaining 2 QColor-triplet sites (GalleryPage MotionFx glow) — T3 full-repo sweep closed |
| 10 | `910c0aff6` | `8119d1497` | fix(i18n): resync GPS-hint catalog entries to %1 source keys (T3 sweep fallout); typo hex purged from shipped catalogs |
| 11 | `7ab2d6917` | `a694305a2` | refactor(theme): RetouchPage onto Lumen tokens + shared factories — C1 394->309 (ratchet locked) |
| 12 | `d3042c1b7` | `ba87456cb` | refactor(theme): MapPage+CullPage onto tokens + btnOutline/btnGhost/chip single-source — C1 309->179 (ratchet locked) |
| 13 | `42221c720` | `278fc8daf` | refactor(theme): GalleryPage CAT1..8 named board + chip/btn/input single-source, DedupDialog tokens + btnOutline(CORAL) — C1 179->99 (ratchet locked) |
| 14 | `e27f6a355` | `68bd9de39` | fix(theme): person-chip text INK->PANEL on CAT fills (T6 reviewer Major companion, WCAG 6.1:1 min) |
| 15 | `8d901d55b` | `d84386fb4` | refactor(theme): hex long-tail zeroed — MainWindow/LogPage/HomePage/videos/core onto tokens, CAT chart series, INK_IMG board, C1 99->0 (ratchet dead-zone locked) |
| 16 | `183bc7c06` | `b3d5c9680` | refactor(type): font literals to Type:: six-tier everywhere — C2 58->0 (locked), serif leaks downgraded |
| 17 | `5151d8a7e` | `fadbee362` | fix(type): MainWindow placeholder title display()->title(false) — sans preserved per spec §2.1 Display-lock (T8 review Major) |
| 18 | `27cf00628` | `dd3b99606` | refactor(shape): radii onto Radius{} 6-step grid incl. Theme.h recipes; resting borders -> 1px; C3 33->0 (locked); hover-ring deviations enumerated for T12 |
| 19 | `a4013263b` | `5d35acd72` | refactor(space): off-grid spacing onto Space:: grid + editorial-whitespace marker (spec s3.3) — C4 117->0 (locked) |
| 20 | `7ef06bcc7` | `b5d5df2c7` | feat(elevation): first drop-shadows in repo history - E2 on dedup/lightbox (spec 3.4), menus deferred - shadow cannot paint on native popups |
| 21 | `8ccc42ae3` | `d554cff6c` | docs(p1): closure - changelog 2.37.0 entry, guide roster 72-gate wording, measuredMs backfill; design-language-v2 P1 complete (C1..C4 ratchets at zero, elevation born) |
| 22 | `8f5e0ebb8` | `dd9f478d9` | docs(provenance): 1900 was T1 measured not placeholder - correct measuredMs wording in roster comment/CHANGELOG/guide (delta is host noise; scan predates F1 guard); LA_PALETTE_QA re-verified GO |
| 23 | `1b97d050c` | `1faa92156` | fix(palette): final-review M1/M2 - MapView jade/gold glow literals -> EMERALD_HI/GOLD tokens (spec 1.6, per-site alpha preserved, jade drift 5fdca6->6adba6 sanctioned normalization), Theme scrollbar handle #24312a -> LINE (spec 1.7) |
| 24 | `f15de6068` | `7c631f0a5` | release-prep(2.37.0): CMakeLists VERSION 2.37.0 + CHANGELOG 定稿日期 + 新增 2.37.0 使用说明书（设计语言 v2 P1：C1-C4 棘轮归零/elevation 诞生/72 门名册） |
| 25 | `e3879d390` | `556b4e523` | fix(release): iterate -Package 打包腿裸调用 + rc 传播 |
| 26 | `d17474d9c` | `73d0646cb` | release(2.37.0): 设计语言 v2 · P1 整轨（单源化棘轮 + elevation 诞生），正式打包发布 |
| 27 | `f01a22209` | `0fbbe2df9` | docs(spec): 引擎升级第二轮设计规格（R1 Lumen 13 显式算子产品化 / R2 HEIC+WebP EXIF 补全 / R3 Erase residual 收尾） |
| 28 | `b6bdd6fc6` | `a91f63be0` | docs(plan): 引擎升级第二轮实施计划（R1 显式算子接线 / R2 HEIC+WebP EXIF / R3 Erase 淘汰+取消，11 任务，门禁先行） |
| 29 | `5ab2716c6` | `3be2f2c5a` | feat(retouch): 一键增强 13 算子接线（异步+撤销集成+导出重放+诚实反馈） |
| 30 | `7840392f1` | `b441433d3` | feat(gates): LA_LUMEN_OPS_UI_QA 静态接线门（R1 先行；RED 实证见 task-1-report，接线落地后 green-on-arrival） |
| 31 | `b3a2b057b` | `5e2d96a45` | fix(retouch): 一键增强算子源改增强层基图（消除几何二次烘焙）+ 红眼 0 命中诚实 no-op 不入 ops_/历史 |
| 32 | `41a0386e7` | `eb8bd5d29` | fix(gates): qaShootA 诚实判定 + enhOps==13 断言（旧版恒 exit 0 的空心门修复；对未接线必红） |
| 33 | `daf4bce7b` | `cff0ad697` | chore(gates): R1 实测回填（LA_LUMEN_OPS_UI_QA/LA_RETOUCH_A_QA -Timing）+ 反例 13/14 复验 |
| 34 | `a695de68d` | `3d489d4bc` | feat(gates): LA_EXIF_CONTAINER_QA 合成夹具门（WebP/HEIC/反例；实现前必红） |
| 35 | `1563ebbd2` | `c3f603e5f` | feat(exif): HEIC/WebP 容器 EXIF 抽取（ISOBMFF box walker + RIFF chunk；失败走 mtime 回退） |
| 36 | `410ebf6f5` | `961967798` | fix(exif): extractHeicExif iloc 越界读钳位（R2-T6 review Important #1/#2） |
| 37 | `e03911d4d` | `087c136b4` | test(erase): LA_ERASE_QA G1 淘汰断言（对无淘汰实现必红） |
| 38 | `17a3332cd` | `86951087f` | feat(erase): 任务表淘汰（kTasksKeep=8, running 永不淘汰）+ 陈旧注释订正 |
| 39 | `e34fbed06` | `0bafec0d3` | feat(erase): 任务取消内核（分片等待观测 atomic cancelTarget + kill 复位 + cancelled 终态定格） |
| 40 | `eab595733` | `c80d1ab69` | feat(retouch): 消除任务取消按钮（进行中显隐）+ 门实测回填（LA_ERASE_QA/LA_RETOUCH_A_QA） |
| 41 | `ac5ceac32` | `db0b175d4` | release-prep(2.38.0): 版本 bump + CHANGELOG 三路线收口 + 指导书门数 74 |
| 42 | `b0f4c4a90` | `25a960631` | fix(round2-final): 评审批修 — 三检测算子诚实no-op(I3) + HEIC extent 溢出钳位(I5) + LA_LUMEN_OPS_UI_QA 反例去分支(铁律22,I4) + LA_ERASE_QA timeoutSec=60 + EXIF/预览诚实文档(I1/I2 措辞) |
| 43 | `5240cb44b` | `952da1a3f` | docs(round2-final): 收尾反例注释订正 — LA_LUMEN_OPS_UI_QA 头注改述铁律22去分支机制(I4 后语义) + LA_ERASE_QA 名册 timeout 注释语法闭合，纯注释零行为改动 |
| 44 | `17bb0f2b5` | `0d0ddb8f0` | docs(spec): 引擎升级第三轮设计规格（LumenEngine 算子内并行 + 量化安全 float，路线6性能子集/巡查排名#4） |
| 45 | `8c87f4980` | `b175f880e` | docs(plan): 引擎升级第三轮实施计划（10 任务：helper+blur 首杠杆+DET 子断言 / 映射族分批并行 / halo 暂存降块局部 / redeye cnt 原子上和 / lens 瓦片 / §4.1 float / 反例双实证 / measuredMs 回填 / 2.39.0 收口） |
| 46 | `255d53aa6` | `15eed79ce` | feat(lumen): lumenParallelRows 行块执行器 + gaussianBlur 算子内并行 + LA_LUMEN_SMOKE 串行/并行逐字节决定性子断言（R3-T1，FORCE_DIFF 反例） |
| 47 | `bb6d0697f` | `5107c07fb` | perf(lumen): PIL 基础映射族逐行并行 (pointRgb/pointGray/chopGray/invertGray/toL8/rgb2hsv/hsv2rgb) |
| 48 | `511567be4` | `84a385a81` | perf(lumen): 中层映射族逐行并行 (enhance*/channelGain/blend/screen/composite/unsharp 应用/blackWhite mix) |
| 49 | `c21e63dde` | `adec35df9` | perf(lumen): halo 滤波族逐行并行 (median3/median3G/smoothG/smoothFilter/minFilter/medianFilter，暂存降为块局部) |
| 50 | `0ad98197c` | `bdbd07abd` | perf(lumen): redeye 双 pass + vignette 径向逐行并行（cnt 线程局部求和原子合并，与串行逐位等值） |
| 51 | `a114c7104` | `de5aef24f` | perf(lumen): lensCorrect 按瓦片并行（blockingMap over N×N tiles） |
| 52 | `9be6a4753` | `1ff53d124` | fix(lumen): lensCorrect 尊重 LA_LUMEN_THREADS=1 串行退化（DET 串行臂对 lens 恢复串行语义） |
| 53 | `94c9c3689` | `6e772927e` | perf(lumen): §4.1 量化后终末颜色算术 double→float（gaussianBlur 累加器/hsv2rgb/enhanceBrightness/channelGain/blend/vignette 增益） |
| 54 | `62a46c682` | `ca7336b68` | chore(gate): LA_LUMEN_SMOKE measuredMs 实测回填 66585->3942（R3 算子内并行提速）+ timeoutSec 90->45 |
| 55 | `25c01a93d` | `c8aa728b5` | release-prep(2.39.0): VERSION bump + CHANGELOG 引擎三轮节 + 指导书 §3/§7 口径（roster 74 不变，LA_LUMEN_SMOKE 3942ms） |
| 56 | `5699291ed` | `4e408f838` | fix(lumen): 终审收尾微补（DET 空跑可见化/rmax2 比较回 double/注释订正×3） |
| 57 | `fc72f488f` | `06a0d0a5b` | docs(spec): 引擎升级第四轮设计规格（AI 索引去外部化：faceinfer+clipinfer 随包 ONNX 子进程 / AiSubprocessRunner 底座 / HTTP 桥净退役 / 平价容差契约 / roster 74→78 / 2.40.0） |
| 58 | `16bfc6308` | `806d82f81` | docs(plan): 第四轮实施计划（12 任务：AiProto/桩/#75 → SCRFD → 对齐嵌入/#78 → Runner+PersonDetector 净替换 → CLIP 导出 → clipinfer/#76/#77 → ClipRunner → fetch_ai → C10 → 2.40.0 收口 → 终审） |
| 59 | `b1a5df7b6` | `d06d55988` | feat(faceinfer): AiProto 帧协议 + faceinfer 工程骨架/桩 + 第 75 门 LA_FACE_AI_QA G1/2/4/5（第四轮 T1） |
| 60 | `4f13a646b` | `3f7aa558a` | fix(gate): LA_FACE_AI_QA measuredMs 318->433 对齐 -Timing 溯源注释（T1 终审 Important #1） |
| 61 | `ae798aa1d` | `22265d5fd` | feat(faceinfer): SCRFD-10GF 检测 C++ 移植 + oracle 黄金夹具脚本（第四轮 T2） |
| 62 | `fcbd2bdaa` | `3713cba16` | feat(faceinfer): Umeyama 对齐+ArcFace 嵌入移植 + REQ 端到端应答 + 第 78 门 LA_FACE_MODEL_QA（第四轮 T3） |
| 63 | `640317ff7` | `69fded285` | feat(core): AiSubprocessRunner+FaceRunner 底座落地 + AiProto 全词比对收紧 + 第 75 门 G3/G5p/G6/G7/G8 补全（第四轮 T4a） |
| 64 | `f92ecb439` | `706dccacd` | refactor(face): PersonDetector/MainWindow HTTP 桥净替换 → FaceRunner 子进程门面（第四轮 T4b） |
| 65 | `eedf5c4cb` | `1e707907f` | chore(faceinfer): 第四轮 face 轨实测留档（T5 收口） |
| 66 | `d3e36d2df` | `90d7ff666` | feat(ai_export): CLIP ViT-B-32 ONNX 导出 + prompts.bin(LAPROMPTv1) + oracle 黄金夹具脚本（第四轮 T6） |
| 67 | `1c165f2c7` | `787a1c2be` | feat(clipinfer): CLIP ViT-B-32 推理子进程 + 桩 + 第 76/77 门（第四轮 T7） |
| 68 | `fee850639` | `3ad83e998` | docs(qa): #77 金侧配对名语义钉死（多点扩展名行为，第四轮 T7 收口补充） |
| 69 | `1ea893764` | `eb66d4cab` | docs(spec,plan): R-75 订正 clip 侧 REQ body = UTF-8 图像绝对路径（非 JPEG 字节流） |
| 70 | `38eff7e3a` | `e6ee9b308` | docs(spec): R-76 订正 §7 名册分档与 §5/§6 三处预言（#76 实测落 full、need 双 env、badprompts 码、bin 不入库） |
| 71 | `553adf655` | `dd603218e` | docs(spec): R-77 裁定 clip/bin 既有跟踪例外（不 rm --cached；T9 判据改分区断言） |
| 72 | `2cd488125` | `0713b14e2` | fix(qa): #77 断言面补全（other-off 反例 / 金名↔图数交叉校验 / ok 字段 / sat 诊断）+ I-1 记录订正 |
| 73 | `ad832d027` | `968d56da2` | fix(clipfix): 金名改完整 basename + 低置信合成器入库 ai_export（R-06：产物不入库）+ .gitignore scratch |
| 74 | `438942c78` | `5a5ffd83b` | fix(qa): #77 measuredMs 溯源补档（目录形态 x3 亲测 18067/15896/16634），撤回无原文的 13457 |
| 75 | `92b112f58` | `e8f384c66` | docs(spec): R-87 §4 low_confidence 族 GAP_MIN 记档例外 + >0.04 留量护栏（复核席认可的不可能性证明） |
| 76 | `f99e91ba0` | `17c230574` | feat(cliprunner): Classifier 净替换为 clipinfer 子进程门面 + #76 G11 生产路径腿（T8） |
| 77 | `c5f71cae8` | `9bc63e77a` | feat(ai-provision): fetch_ai.ps1 + 3rdparty/{face,clip} 三件套就位（许可文本入库、bin 只落盘）（T9） |
| 78 | `4b9b3a162` | `b809d7515` | fix(classifier): T8 修复波第 1 轮——F-1 classify 返回值诚实化 + F-2 进度守恒收紧 |
| 79 | `24c47fdce` | `9194fc793` | feat(release): T10 —— release.ps1 C10 契约 + face/clip 随包装配腿闭环 |
| 80 | `75a81dc53` | `72e89eefb` | fix(faceinfer): R-99 夹具轨挖出 Critical×2 —— JPEG 插件路径 + EXIF 方向摆正 |
| 81 | `5aad39c1c` | `3b92d834b` | release-prep(2.40.0): 版本 bump + CHANGELOG 路线4 节 + 指导书 §3/§7 口径 + 2.40.0 使用说明书（roster 78 = fast 19 + full 59，PASS=66/SKIP=12/KILL=0） |
| 82 | `d6e4a5196` | `497bfd644` | docs(release-honesty): T12 修复波 FIX-1..5 + FIX-10..13 + fetch face-notice 反例臂 —— 文档宣称追认真源，C10 补 face 内容腿 |
| 83 | `cfee1da45` | `2fa53b923` | fix(ui+i18n): 三条「AI 引擎未就绪」诚实换代 + 补第 14 条孪生断言（T12 FIX-6/7） |
| 84 | `be7b8fdab` | `161065494` | feat(qa): LA_FACE_AI_QA 补 G9 face 轨子进程生产路径腿（T12 FIX-8） |
| 85 | `7cc5f7e84` | `d2d575a35` | chore(comments): MainWindow 识别人物按钮注释订正 face_server → faceinfer.exe（T12 FIX-9） |
| 86 | `9526f1dd2` | `6fc0d849e` | test(qa): G9 加 LA_FACE_FORCE_ERR 全失败子轮，咬住 face 轨「收尾恒 return true」（T12 波2 FIX-16） |
| 87 | `27c751c7d` | `a445c9c30` | fix(core+ui+i18n): face 轨返回值诚实化 + R-20 三态恢复（T12 波2 FIX-14/15/17） |
| 88 | `7baef9cdb` | `194f27376` | docs(T12 波2 FIX-18): 修复波追认 + face 轨降级语义补齐 —— 文档宣称与真源对齐 |
| 89 | `3daec6cbb` | `15ee1227e` | fix(qa+ui): G9 取消子轮补 `stopped` 判别腿 + 三条成功文案改报实际写回数（T12 波2 FIX-19/20） |
| 90 | `9b49389d9` | `9282eace8` | fix(core+qa): 复审 Minors 四条全落牙齿 FIX-21 —— G9 六子轮 + NOEMB 桩臂（T12 复审收口） |
| 91 | `229ce68ad` | `143d35015` | docs(T12 复审波): 修复波扩至 FIX-1..21 + 三枚变异探针证据入文档 + spec §8 补载荷残缺行 |
| 92 | `576b7a294` | `ebaba975f` | test(qa)+fix(core): T12 第 4 波 FIX-22..29 —— 两条 Important 补牙齿（取消=零 REQ 哨兵 + NOEMB 载荷自检） |
| 93 | `55b9cd503` | `9d4fab8cd` | release-prep(2.40.0): 引擎升级第四轮 · 路线4 AI-index 去外部化，正式打包发布 |

配对方法：`git rev-list --reverse origin/main..<ref>` 两侧各 93 枚，按序号一一配对，
再逐枚 `git show -s --format=%s` 交叉校验主题完全相同（`subject mismatches = 0`）。
仓是线性历史（无 merge 提交），故序号配对即拓扑配对。
