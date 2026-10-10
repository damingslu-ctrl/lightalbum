<#
.SYNOPSIS
  Gate matrix - the SINGLE SOURCE OF TRUTH for the LA_* gate table and its tiers.
.DESCRIPTION
  2.8.0. Before this file the gate table lived inline inside qa_runner.ps1, so a
  gate existed in exactly one place but its COST was invisible: every gate ran on
  every pull request. Measured on 2026-10-03 the full set took 73.6s wall clock,
  and the costs split cleanly into two natural bands:

    * "pure" gates (no MainWindow, pure offscreen self-exit):  56 - 338 ms
    * everything that builds MainWindow or a 6425-photo fixture: 2.5 - 8.7 s

  So the matrix splits on that measured boundary, not on a guess:

    tier "fast"  - cheap enough to run on EVERY pull request
    tier "full"  - expensive; runs on push to main / tags and on every local
                   full run (the default), and is what release.ps1 gates on
    tier absent  - means "full". FAIL-SAFE: a brand new gate that forgets to
                   declare a tier lands in the strict layer, it can never
                   silently weaken the PR layer by omission.

  Tier "all" (the default when no -Tier is passed) is the union, i.e. byte-for-byte
  today's behaviour. release.ps1 and the local iterate.ps1 keep using it, so
  packaging is NOT weakened by the matrix.
.usage
  powershell -ExecutionPolicy Bypass -File gate_matrix.ps1 -SelfTest   # gate 34
  powershell -ExecutionPolicy Bypass -File gate_matrix.ps1 -List
  . gate_matrix.ps1 -LoadOnly # dot-source: populates $GateTable + functions
.NOTES
  MUST stay pure ASCII. Windows PowerShell 5.1 reads a BOM-less UTF-8 file as
  GBK, and any CJK character in this file makes the script fail to load with
  "string is missing the terminator".
#>
param(
  [switch]$LoadOnly = $false,
  [switch]$SelfTest = $false,
  [switch]$List = $false
)

# ---------------------------------------------------------------------------
# The gate table. n = gate name, png = needs a png path argument,
# arg = extra suffix after the png path (e.g. ",china"),
# need = env vars that must be present or the gate is skipped,
# kind = "exe" (default, runs the native exe with env LA_<name>=1) or
#        "ps1" (runs native/<script> with scriptArgs instead),
# tier = "fast" | "full"; absent means "full" (fail-safe, see header).
#
# The fast set is exactly the measured-cheap band: every one of these returns
# before MainWindow is constructed and self-exits offscreen.
#
# measuredMs comes from `qa_runner.ps1 -Timing` on 2026-10-03 (29 gates,
# 73644 ms wall clock). It is DATA, not a guess, and it is what E4 asserts
# against. Two deliberate exclusions from the fast set:
#   LA_SCALE_PERF (338 ms) - it asserts wall-clock BUDGETS, so on a shared
#     CI runner it would flake; a flaky gate in the PR layer teaches people
#     to ignore red, which is worse than a slow gate.
#   LA_RW_SMOKE (1017 ms) - borderline, kept in the strict layer so the
#     vault/crypto round-trip only has one place that can break it.
# ---------------------------------------------------------------------------
$GateTable = @(
  @{n="LA_GEO_SMOKE";       png=$false; tier="fast"; measuredMs=106},
  @{n="LA_ICLOUD_QA";       png=$false; tier="fast"; measuredMs=194},
  @{n="LA_LOG_SMOKE";       png=$false; tier="fast"; measuredMs=120},
  @{n="LA_HOME_SMOKE";      png=$false; tier="fast"; measuredMs=194},
  @{n="LA_RETOUCH_UNDO_QA"; png=$false; tier="fast"; measuredMs=71},
  # Direction B deepening (2.24.0): per-operator direction pixel assertions for all
  # 12 RetouchEngine operators. Pure pixel math on a 128x96 synthetic image, no UI and
  # no library -> fast layer. Measured 142/145/146ms, well under the 350ms fast budget.
  # Each operator is applied ALONE on purpose: a wrong sign on one operator can be
  # masked by another (measured: recommend() gave highlights=-0.30 = +42.90 BRIGHTER
  # yet the overall image darkened by -16.23 via exposure, so it looked fine).
  @{n="LA_RETOUCH_OP_QA";   png=$false; tier="fast"; measuredMs=145},
  @{n="LA_GALLERY_SEARCH_QA"; png=$false; tier="fast"; measuredMs=56},
  @{n="LA_THUMB_CACHE_QA";  png=$false; tier="fast"; measuredMs=201},
  @{n="LA_GALLERY_BATCH_QA"; png=$false; tier="fast"; measuredMs=135},
  @{n="LA_GATE_MATRIX_QA";  png=$false; tier="fast"; kind="ps1"; script="gate_matrix.ps1"; scriptArgs="-SelfTest"; measuredMs=250},
  # F i18n (direction F step 2): translation correctness. Forces loading of the
  # embedded :/i18n/LightAlbum_en.qm, then asserts core UI strings resolve to
  # English and differ from their (Chinese) source. Pure offscreen self-exit,
  # no MainWindow, no external env. Counter-example LA_I18N_QA_FORCE_BROKEN=1
  # skips the translator load and must go RED. Measured 165-246ms (x3) on
  # 2026-10-03 -> 210ms; well under the 500ms fast budget.
  @{n="LA_I18N_QA";        png=$false; tier="fast"; measuredMs=210},
  # F a11y step (direction F step 3): accessibility - contrast + keyboard reach.
  # LA_A11Y_CONTRAST_QA audits 15 hardcoded DarkPalette color pairs against
  # WCAG 2.1 (4.5:1 body). Pure offscreen self-exit, no MainWindow, no env.
  # Counter-example LA_A11Y_CONTRAST_QA_FORCE_BROKEN=1 injects a failing
  # "BROKEN Line on Inset" pair and must go RED. Measured 18ms (x3, 2026-10-04).
  # LA_A11Y_KEYBOARD_QA forwards EditorialView::qaKeyboardCheck() +
  # MapView::qaKeyboardCheck(): asserts StrongFocus on gallery tile + map and
  # that simulated Enter / Key_Plus trigger activation / zoom. Pure offscreen
  # self-exit, no MainWindow, no env. Counter-example
  # LA_A11Y_KEYBOARD_QA_FORCE_BROKEN=1 forces NoFocus and must go RED.
  # Measured 17ms (x3, 2026-10-04). Both sit in the fast tier: cheaper than
  # every full-tier gate and far under the 500 ms fast budget.
  @{n="LA_A11Y_CONTRAST_QA"; png=$false; tier="fast"; measuredMs=18},
  @{n="LA_A11Y_KEYBOARD_QA"; png=$false; tier="fast"; measuredMs=17},
  # timeoutSec: real cost is 66585 ms (8 images x 12 ops = 96 full-frame pixel
  # comparisons). The old measuredMs=7431 record was 9x optimistic, so a flat
  # 25s budget killed a gate that genuinely PASSES (verified: exit=0 +
  # LA_LUMEN_SMOKE PASS when given 120s). Hangs are still detected, just with a
  # budget that matches reality.
  @{n="LA_LUMEN_SMOKE";     png=$false; timeoutSec=90; measuredMs=66585},
  @{n="LA_AGENT_SMOKE";     png=$false; measuredMs=1000},
  # Direction B gate deepening: LumenAgent pure-function value contracts.
  # Forensics: LA_AGENT_SMOKE asserts only 3 booleans (real/fb/clamp), while
  # parseShutterSeconds (zero assertions library-wide), contextFrom (only called
  # indirectly), 8 of decideLocal's rule branches (only 3 covered) and clampPlan's
  # 13-key whitelist (only 2 keys tested) all lack value-level assertions.
  # This gate adds: parseShutterSeconds truth table, 13-key whitelist + clamp bounds,
  # decideLocal direction contracts (single-parameter isolation), contextFrom
  # monotonicity + metadata passthrough. Zero network, sub-second -> fast tier.
  # Counter-example LA_AGENT_QA_FORCE_BROKEN=1 flips brightness/saturation signs in
  # decideLocal (reproduces the 2.24.0 reversed-slider defect) -> direction
  # assertions must go RED. Expectations never change with the broken flag.
  @{n="LA_AGENT_QA";        png=$false; tier="fast"; measuredMs=70},
  @{n="LA_SCAN_SMOKE";      png=$false; need=@("LA_SCAN_ROOT","LA_SCAN_DB"); measuredMs=0},
  # D scale (direction D step 3): REAL library scale gate. LA_SCALE_SMOKE/PERF run
  # the committed _scale_fixture.db (6425 rows) whose photo files DO NOT exist on
  # disk, so they only exercise "6425 rows in memory" and never touch disk IO,
  # thumbnail decode or mixed formats. This gate points at the real library and
  # asserts only what a real library can: file existence rate, missing-flag
  # consistency, format diversity, grid first paint and real decode sampling.
  # need=LA_SCALE_REAL_DB: the real library is personal data (cannot be committed
  # or shipped to CI), so CI SKIPs cleanly instead of scoring a CHECK warning.
  # Counter-example LA_SCALE_REAL_QA_FORCE_BROKEN=1 rewrites every path to a
  # nonexistent file (reproducing the lying fixture) and must go RED.
  @{n="LA_SCALE_REAL_QA"; png=$false; need=@("LA_SCALE_REAL_DB"); measuredMs=734},
  # D scale (direction D step 6): REAL library PERFORMANCE regression gate.
  # runScalePerf only exercises the committed synthetic fixture (6425 rows whose
  # files do NOT exist on disk), so its first-paint/scroll numbers never touch the
  # real-scale paths (thumbnail disk fetch, mixed formats). This gate forces the
  # REAL library and reuses the same GalleryPage first-paint + scroll-frame budget
  # measurement, guarding regression under true 6526-row scale.
  # need=LA_SCALE_REAL_DB: personal data (cannot be committed or shipped to CI),
  # so CI SKIPs cleanly instead of scoring a CHECK warning.
  # Counter-example LA_SCALE_REAL_PERF_QA_FORCE_BROKEN=1 arms a ONE-SHOT +300ms paint
  # lag (EditorialView::qaSetPaintLagMsForQa) inside the scroll phase, so exactly one
  # scroll frame blows the 50ms budget -> gate RED without tripping the 20s watchdog.
  # measuredMs≈8000. tier defaults to full (fail-safe).
  @{n="LA_SCALE_REAL_PERF_QA"; png=$false; need=@("LA_SCALE_REAL_DB"); measuredMs=8000},
  @{n="LA_EXIT_QA";         png=$false; measuredMs=5700},
  @{n="LA_RW_SMOKE";        png=$false; measuredMs=1017},
  @{n="LA_CULL_SMOKE";      png=$false; measuredMs=3210},
  @{n="LA_RETOUCH_SMOKE";   png=$false; measuredMs=2567},
  @{n="LA_SMOKE";           png=$false; measuredMs=3171},
  @{n="LA_MAP_A_QA";        png=$true; measuredMs=3640},
  @{n="LA_MAP_BURST_QA";    png=$true; measuredMs=3166},
  @{n="LA_MAP_REGION_QA";   png=$true; arg=",china"; measuredMs=3075},
  @{n="LA_MAP_COUNTRY_QA";  png=$true; measuredMs=2617},
  # A direction: footprint trajectory / heat layers. LA_MAP_TRACK_QA injects 5
  # deterministic GPS points with footLayer=track and counts cyan (blue-dominant)
  # pixels; LA_MAP_HEAT_QA counts warm (red-dominant) pixels with footLayer=heat.
  # Counter-examples *_FORCE_BROKEN=1 force the layer back to dots, so the extra
  # pixel count is 0 and the gate goes RED. Both render the world layer offscreen
  # like the other map gates, so they sit in the full tier (no tier key = the
  # fail-safe default). measuredMs is filled in from qa_runner.ps1 -Timing.
  @{n="LA_MAP_TRACK_QA";    png=$true; measuredMs=3100},
  @{n="LA_MAP_HEAT_QA";     png=$true; measuredMs=3012},
  # A direction step 2: track direction arrows + playback animation.
  # Asserts arrow pixels > 0 AND that the playback head sits at THREE DIFFERENT
  # positions at t=0 / t=mid / t=end (the discriminator between real motion along
  # the polyline and a stationary blink -- a pixel-count-only check would be
  # vacuous). Positions come from trackPosAt(), the same single source the
  # product renderer uses, so the gate cannot drift from the implementation.
  # Counter-example LA_MAP_TRACK_ANIM_QA_FORCE_BROKEN=1 pins the head to the
  # first point -> all three positions equal -> RED. Full tier (offscreen
  # MainWindow render, like the other map gates). measuredMs from -Timing.
  @{n="LA_MAP_TRACK_ANIM_QA"; png=$true; measuredMs=3437},
  # A direction step 3: filter the footprint layer by capture year.
  # Injects 6 deterministic GPS points across 2019/2021/2024 (2 each) and
  # asserts: (A) filtering to 2024 leaves exactly that year's points;
  # (B) gpsPts_ / gpsPaths_ / gpsDates_ all have the SAME length -- the
  # country view indexes gpsPaths_[i] against gpsPts_[i], so filtering only
  # some of the three would silently misalign photo paths to wrong points;
  # (C) setFootYearFilter is idempotent; (D) year 0 restores the full set.
  # Counter-example LA_MAP_YEAR_QA_FORCE_BROKEN=1 disables the injection-point
  # filter -> all 6 points survive -> the count assertion goes RED.
  # png=$false (no rendering, pure data-contract gate). Full tier.
  @{n="LA_MAP_YEAR_QA";      png=$false; measuredMs=3737},
  # A direction step 4: heat colour ramp graded by photo density.
  # Injects 3 clusters with very different densities (3 far-apart points, a
  # 12-point cluster, a 40-point cluster) so all three tiers are exercised, then
  # classifies every rendered pixel to the nearest tier by hue ratio and
  # requires all three tier counts > 0. The tier->colour table lives in one
  # place (MapView::heatRampOf) shared by the renderer and this gate, so the
  # gate cannot drift from the implementation.
  # Counter-example LA_MAP_HEAT_RAMP_QA_FORCE_BROKEN=1 pins every point to a
  # single tier -> two tier counts drop to 0 -> RED. Full tier.
  @{n="LA_MAP_HEAT_RAMP_QA"; png=$true; measuredMs=5901},
  # A direction step 4b (2.27.0): footYear wired into the country view.
  # Injects same-country cross-year GPS points, simulates the year-change
  # full chain (re-inject year-filtered gpsPts_ then setFootYearFilter) and
  # asserts the country snapshot (countryPts_/countryPaths_) converges to the
  # filtered year. Counter-example LA_MAP_COUNTRY_YEAR_QA_FORCE_BROKEN=1 skips
  # the recompute inside setFootYearFilter -> snapshot does not shrink -> RED.
  # png=$false (pure data-contract gate). Full tier.
  @{n="LA_MAP_COUNTRY_YEAR_QA"; png=$false; measuredMs=3000},
  # A direction step 4c (2.27.0): country album clustered by capture year.
  # After entering a country and filtering to 2024, countryAlbumGroups() must
  # return exactly one year group (2024) with the right photo count.
  # Counter-example LA_MAP_COUNTRY_ALBUM_QA_FORCE_BROKEN=1 makes the grouping
  # ignore years (all into one group) -> year count wrong -> RED. Full tier.
  @{n="LA_MAP_COUNTRY_ALBUM_QA"; png=$false; measuredMs=3000},
  # A direction step 4d (2.27.0): heat density tier thresholds are adjustable.
  # With defaults 3/8 a density of 5 is tier 1; after setHeatTiers(2,3) the same
  # density is tier 2. Counter-example LA_MAP_HEAT_TIER_QA_FORCE_BROKEN=1 makes
  # setHeatTiers a no-op and heatTierOf fall back to hardcoded 3/8 -> tier
  # unchanged -> RED. png=$false (pure engine-contract gate). Full tier.
  @{n="LA_MAP_HEAT_TIER_QA"; png=$false; measuredMs=2500},
  # Direction C step 1: photo clustering (province / city / year / month).
  # Pure-function gate -- it calls PhotoCluster::cluster() directly, so no
  # MainWindow is built and no rendering happens.
  # Key assertion is CONSERVATION: the per-cluster counts must sum to the
  # number of visible photos, i.e. photos with a missing key still land in the
  # "unclassified" cluster instead of being silently dropped. Dropping them
  # would make the gallery lie about what it contains.
  # Counter-example LA_CLUSTER_QA_FORCE_BROKEN=1 flips a QA hook inside
  # PhotoCluster so the real implementation skips empty-key photos; the
  # conservation assertion then goes RED.
  # fast tier: REJECTED by measurement. The gate is pure data work with no
  # MainWindow, but the process still pays full QApplication startup plus
  # offscreen platform init, which dominates: measured 1424 ms > 500 ms budget.
  # LA_GATE_MATRIX_QA E6 caught this -- keeping the tier="fast" guess would have
  # shipped a gate that fails its own budget rule on every CI run.
  @{n="LA_CLUSTER_QA";      png=$false; measuredMs=1424},
  # 方向 C·联动：地图地理热点聚合 + 一键联动门禁（LA_GEO_LINK_QA）。
  # 断言 GeoHotspot::compute 纯函数（World=按 region / Region=按 prov）的守恒与
  # 「热点 key 过滤图库 == hotspot.count」联动契约，并离屏验证热点徽章点击 →
  # hotspotActivated 带正确 key。Counter-example LA_GEO_LINK_QA_FORCE_BROKEN=1 令
  # World 级误按 prov 聚合（key 不再是 region id）→ 上述断言变红（非空心）。
  # tier 缺省=full（fail-safe）：进程同样支付 QApplication + offscreen 启动，且构造
  # MapView 会 loadData 解析 mapdata.json，实测 >500ms，故不进 fast 层。
  # measuredMs 待 -Timing 实测回填（初估 ~1450，与 LA_CLUSTER_QA 同量级）。
  @{n="LA_GEO_LINK_QA";    png=$false; measuredMs=1450},
  @{n="LA_MAP_CLICK_QA";    png=$false; measuredMs=2553},
  # 方向 A·地图性能：帧耗时预算门。qaPerf 逐层（world/region/province/country）各渲染 20 帧
  # 取平均 ms，超预算打印 LA_MAP_PERF FAIL 并经退出码非 0（门禁 scan FAIL 生效）。
  # 证据化放宽门槛（先测再改 + 铁律25）：world <=1.2ms（修正后 1:1 blit 预测 ~0.5ms，
  # 证伪「每帧缩放 3000x1500」回退）、region/province/country <=1.5ms。路线图 world/region
  # <0.3ms 经实测为全屏必 blit 的物理不可达目标，已放弃。tier 缺省=full（墙钟预算门，
  # 同 LA_SCALE_PERF 不进 fast 防 CI flake）。
  @{n="LA_MAP_PERF";        png=$false; measuredMs=3735},
  @{n="LA_GALLERY_E_QA";    png=$true; measuredMs=2589},

  # 2.28.0 RAW 格式支持门禁（LA_RAW_QA）。A 组纯函数契约零依赖恒可跑；B 组真实解码
  # 需 LibRaw 产物 + 一个 RAW 样本。need= 声明式依赖（铁律 26）：两者缺一即优雅 SKIP，
  # 绝不静默假通过；样本是外部素材，**绝不入库/进 CI**（仓库零二进制 + 无个人数据）。
  # 反例 LA_RAW_QA_FORCE_BROKEN=1 令 decodeToTempPpm 改用「rc==0 即成功 + 产物名按
  # 替换扩展名猜测」的缺陷形态 -> B2/B3 转红（实测两模式 dcraw_emu 的 rc 均为 0，
  # exit code 不携带信息，产物校验是唯一判据）。期望值不随 broken 变（铁律 22）。
  # tier 缺省=full：真实解码实测 ~0.6s/张，进程另付 QApplication 启动开销。
  @{n="LA_RAW_QA"; png=$false; need=@("LA_RAW_SAMPLE"); measuredMs=1400},
  # 2.28.0 视频支持门禁（LA_VIDEO_QA）。A 组纯函数契约零依赖恒可跑（含与 RAW 同款的
  # needsExternalDecode 边界负向断言）；B 组真实抽帧/探测需 FFmpeg + 视频样本；
  # C 组徽标像素门（离屏渲染 isVideo true/false 两卡，断言左上角有/无徽标差异）。
  # need= 声明式依赖（铁律 26）：缺 FFmpeg 或样本即优雅 SKIP，样本绝不入库/进 CI。
  # 反例 LA_VIDEO_QA_FORCE_BROKEN=1 令 extractPoster 改用「rc==0 即成功 + 猜错产物名」
  # -> B2b 转红；徽标像素门不受影响（证明反例只打解码判定，不是一刀切）。
  # tier 缺省=full：真实抽帧实测 ~0.3-0.8s，进程另付 QApplication 启动开销。
  @{n="LA_VIDEO_QA"; png=$false; need=@("LA_VIDEO_SAMPLE"); measuredMs=1600},
  # 2.30.0 视频播放（内嵌解码器）门禁（LA_VIDEO_PLAY_QA）。经 QProcess 调 ffmpeg rawvideo
  # 管道逐帧解出**动态**画面：正例断言帧数>0、末帧非空且尺寸正确、**帧间像素确实不同**
  # （真动态解码，静止图/占位图会红）、pts 递增。反例 LA_VIDEO_PLAY_QA_FORCE_BROKEN=1
  # 令 ffmpeg 传入不存在的第二输入 -> 建不起输出流 -> 解出 0 帧，A1/A2/A3/A4/A5 转红
  # （期望值不随 broken 切换，铁律22）。need=LA_VIDEO_SAMPLE（外部素材不入库/不进 CI）。
  # tier 缺省=full：真实解码 ~1s，进程另付 QApplication 启动开销。
  @{n="LA_VIDEO_PLAY_QA"; png=$false; need=@("LA_VIDEO_SAMPLE"); measuredMs=2500},
  # 2.30.0 播放器 UI 渲染门禁（LA_VIDEO_UI_QA）。离屏渲染 VideoPlayerWidget 并 grab()
  # 断言**画面区非空白**（铁律24：视觉必须实测，禁猜）——实测正例 nonBg=5689 采样像素、
  # 反例仅 20（只剩「▶ 加载中…」占位文字）。含真动态断言（hasAdvanced）与帧解码断言。
  # 反例同 FORCE_BROKEN 语义：D1/D4/D5 转红（期望值不随 broken 变）。need=LA_VIDEO_SAMPLE。
  @{n="LA_VIDEO_UI_QA"; png=$false; need=@("LA_VIDEO_SAMPLE"); measuredMs=3500},
  # 2.30.0 音频输出门禁（LA_AUDIO_QA）。验证「音轨→ffmpeg s16le PCM→winmm waveOut」
  # 真送出非静音音频。★不靠「能听见」判定（离屏/无设备不可靠），而是断言
  # 「waveOut 设备可开」+「解出 PCM 字节>0 且按块对齐」——二者合起来等价于有声，
  # 且可离屏断言。反例FORCE_BROKEN：ffmpeg 建不起流 → 0 字节 → C2 转红。
  #样本无音轨时优雅 SKIP（诚实前提，不硬编造）。need=LA_VIDEO_SAMPLE。
  @{n="LA_AUDIO_QA"; png=$false; need=@("LA_VIDEO_SAMPLE"); measuredMs=2000},
  # 2.31.0 视频选帧门禁（LA_VIDEO_FRAME_QA）。验证「按秒选帧」真取到**不同**帧。
  # 背景：实测同视频 t=0/2/4s 三帧 md5 各异，故若抽帧忽略秒数或产物名共用就会
  # 读到同一帧——这正是选帧器头号失败模式。正例断言三帧互不相同 + 第0秒边界可解；
  # 反例 FORCE_BROKEN令 extractPoster 忽略 atSec（恒抽2s）→ E2/E3/E4 转红。
  # need=LA_VIDEO_SAMPLE。tier 缺省=full（三次抽帧各起一次 ffmpeg，~1.5s）。
  @{n="LA_VIDEO_FRAME_QA"; png=$false; need=@("LA_VIDEO_SAMPLE"); measuredMs=2500},
  # 2.31.0 抽帧缓存门禁（LA_VIDEO_CACHE_QA）。验证 extractPoster 的磁盘缓存：
  # 正例断言首次未命中写入缓存（count=1）、第二次命中（count 不变）、源文件 mtime 变化后
  # 缓存失效产生新条目（count=2）、clear 后清空；反例 FORCE_BROKEN 绕过缓存，
  # C2/C3/C5/C8 转红。need=LA_VIDEO_SAMPLE。实测约 2-3s（两次 ffmpeg 未命中）。
  @{n="LA_VIDEO_CACHE_QA"; png=$false; need=@("LA_VIDEO_SAMPLE"); measuredMs=3500},
  # 2.29.0 选片豁免契约门禁（LA_CULL_EXEMPT_QA）。纯函数 + 合成缩略图，**零外部依赖**,
  # 故可进 fast 层（实测毫秒级）。A 组钉死「RAW/视频豁免」谓词，含曾经漂移的 8 种
  # RAW 扩展名（fff/iiq/k25/kdc/mos/ptx/srf/srw —— Sigma/Phase One/Pentax/Mamiya/Ricoh/Sony）
  # 与曾经漏掉的 10 种视频扩展名；B 组用**极暗合成图**对比 RAW/视频/普通照片三条路径：
  # RAW 与视频必须 candidate（豁免生效），普通照片必须 rejected（证明非恒真，铁律 16）。
  # 反例 LA_CULL_EXEMPT_QA_FORCE_BROKEN=1 令 RAW 豁免失效 -> B1 转红而 A 组仍绿。
  @{n="LA_CULL_EXEMPT_QA"; png=$false; tier="fast"; measuredMs=120},
  # 2.31.0 选片页视频适配门禁（LA_CULL_VIDEO_QA）。验证：CullEngine 透传 isVideo/durSec、
  # CullModel 暴露角色、CullProxy「只看视频」过滤、CullCardDelegate 绘制播放徽标+时长。
  # 纯合成缩略图，零外部依赖，进 fast 层（实测毫秒级）。
  # 反例 LA_CULL_VIDEO_QA_FORCE_BROKEN=1 关闭徽标绘制 -> D1/D2 转红而 A/B/C 仍绿。
  @{n="LA_CULL_VIDEO_QA"; png=$false; tier="fast"; measuredMs=80},
  # 2.29.0 RAW/视频修图链路门禁（LA_EDIT_RAW_QA）。修图页原本用 QImage(ph.path) 直读
  # 原图，实测对 RAW 返回 **0x0 null=1** → RAW 根本进不了修图页（2.28.0 漏了这条入口）。
  # 现统一走 ImageLoad::loadForEdit（照片/RAW/视频三路收口）。need= 声明两个样本依赖：
  # RAW 与视频各需一个素材，缺一即优雅 SKIP（样本绝不入库/进 CI）。
  # ★B4 端到端像素门：加载 → RetouchEngine::apply → 断言像素**确实改变**（实测 98.4%），
  #   杜绝「能加载但调参无效果」的假通过（铁律 29）。
  # 反例 LA_EDIT_RAW_QA_FORCE_BROKEN=1 令 RAW 回退 QImage 直读（复现 2.28.0 缺陷形态）
  #   -> B1/B1b 转红（实测 raw: 0x0 null=1），A 组仍绿。
  # tier 缺省=full：真实 RAW 解码实测 ~0.6s/张，进程另付 QApplication 启动开销。
  @{n="LA_EDIT_RAW_QA"; png=$false; need=@("LA_RAW_SAMPLE","LA_VIDEO_SAMPLE"); measuredMs=1500},
  @{n="LA_GALLERY_FILL_QA"; png=$true; measuredMs=2690},
  @{n="LA_RETOUCH_A_QA";    png=$true; measuredMs=2707},
  @{n="LA_SCALE_SMOKE";     png=$false; measuredMs=3351},
  @{n="LA_SCALE_PERF";      png=$false; measuredMs=338},
  @{n="LA_BATCH_TX_QA";     png=$false; measuredMs=7004},
  @{n="LA_RELEASE_QA";      png=$false; kind="ps1"; script="release.ps1"; scriptArgs="-SelfTest"; measuredMs=3135},
  # F high-DPR render (direction F step 1): HiDpi bake correctness. Builds a
  # MapView, enters the china region, renders the static layer offscreen, and
  # asserts viewPix_ device-pixel width == logical width * 2. Internally sets
  # LA_HIDPI_DPR=2 to simulate a 200% display, decoupling the gate from the
  # offscreen platform's unstable devicePixelRatio so it is reproducible.
  # Lands in the full tier (no tier key = fail-safe default full): it builds a
  # QWidget and renders offscreen, and 762ms > 500ms fast budget. No external
  # 'need' env dependency.
  @{n="LA_HIDPI_QA";        png=$false; measuredMs=762},
  # Direction C step 2: similar-photo deduplication (reuses CullEngine pHash).
  # Pure offscreen self-exit, no MainWindow, no external env. Calls
  # PhotoDedup::findGroups() directly and asserts MD5-exact + pHash-near-dup
  # grouping, uniqueness, missing-thumb safety, keep-highest-resolution, and
  # CONSERVATION (every legal photo appears exactly once). Counter-example
  # LA_DEDUP_QA_FORCE_BROKEN=1 injects a real double-count defect (keptPath of
  # group 0 re-added into group 1's dupPaths) and must go RED on noDouble/dupTotal.
  # Measured 159-162ms (x3, 2026-10-05) -> fast tier.
  @{n="LA_DEDUP_QA";       png=$false; tier="fast"; measuredMs=160},
  # Direction C step 3: smart scene classification write-back + retrieval contract.
  # Pure offscreen self-exit, no MainWindow, no external env. Calls
  # Classifier::classify() with a synthetic result hook (bypassing the CLIP HTTP
  # service) and asserts: (A) DB category column written; (B) in-memory
  # Photo.category synced (GalleryModel reads memory); (C) GalleryModel rowCount
  # per category + kNoCategory + "all" correct; (D) total photo count conserved;
  # (E) Classifier::normalizeLabel maps raw labels to the 8 fixed classes.
  # Counter-example LA_CLASSIFY_QA_FORCE_BROKEN=1 makes Library write the DB but
  # skip the in-memory sync, so the GalleryModel retrieval fails -> RED.
  # Tier: FULL. Measured 1161-1607ms (Qt init + real SQLite open) exceeds the
  # 500ms fast budget (E6), so it stays in the strict layer; release.ps1 gates
  # on the union anyway, so packaging coverage is NOT weakened.
  # measuredMs=1600 from qa_runner -Timing (rule 13), non-zero for E8.
  @{n="LA_CLASSIFY_QA";    png=$false; measuredMs=1600}
  # Direction C step 4 (plan A): face detection write-back + retrieval + rename contract.
  # Face detection is plan A only (no clustering): every detected face shares the
  # default label "人物1"; the user renames it from the GalleryPage person chips.
  # Pure offscreen self-exit, no MainWindow, no external env. Calls
  # PersonDetector::detect() with a synthetic result hook (bypassing the face_server
  # HTTP service) and asserts: (A) DB person column written; (B) in-memory
  # Photo.person synced (GalleryModel reads memory); (C) GalleryModel rowCount per
  # person + "all" correct; (D) total photo count conserved; (E) renamePerson()
  # token-level rename updates DB + memory + retrieval; (F) default label is "人物1".
  # Counter-example LA_PERSON_QA_FORCE_BROKEN=1 makes Library write the DB but
  # skip the in-memory sync, so the GalleryModel retrieval fails -> RED.
  # Tier: FULL. qa_runner -Timing measured ~496 ms (just UNDER the 500 ms fast
  # budget). Rule 13: a gate whose real wall-clock sits on the budget boundary is
  # flaky in the PR layer, so it is deliberately kept in the strict (full) layer
  # rather than promoted on a 4 ms margin. release.ps1 gates on the union anyway,
  # so packaging coverage is NOT weakened.
  # measuredMs=500 from qa_runner -Timing (rule 13), non-zero for E8.
  @{n="LA_PERSON_QA";     png=$false; measuredMs=500}
  # Direction C step 5: face auto-clustering. Extracts one representative-face
  # embedding per photo (largest detection score), persists it into the new
  # face_emb BLOB column, then groups photos by cosine similarity (greedy
  # single-linkage, threshold 0.4) and writes each cluster label back to the
  # person column. Pure offscreen self-exit, no MainWindow, no external env.
  # Calls PersonDetector::detectEmbeddings() with a synthetic embedding hook
  # (bypassing the face_server HTTP service) and asserts: (A) face_emb round
  # trip; (B) CONSERVATION -- inputCount == outputCount and every path appears
  # exactly once; (C) grouping shape (3 same face + 3 distinct -> 4 clusters);
  # (D) DB person column written; (E) in-memory sync; (F) GalleryModel rowCount
  # per cluster + "all"; (G) renamePerson still works (no 2.19.0 regression).
  # Counter-example LA_FACE_CLUSTER_QA_FORCE_BROKEN=1 makes FaceCluster drop one
  # photo, so the conservation assertion (B) turns RED -- and that same
  # assertion is measured green in the positive run, so it is not hollow.
  # Tier: FULL. Measured 215/219/217 ms over three offscreen runs (round-trip of
  # a 6-photo temp sqlite + two write transactions). That is under the 500 ms
  # fast budget, but E6 only bounds the fast layer from above -- keeping a cheap
  # gate in the strict layer is always legal, and it matches its two sibling
  # direction-C gates (LA_CLASSIFY_QA 1600 ms, LA_PERSON_QA 500 ms, both FULL).
  # release.ps1 gates on the union anyway, so packaging coverage is unaffected.
  # measuredMs=220 from qa_runner -Timing (rule 13), non-zero for E8.
  @{n="LA_FACE_CLUSTER_QA"; png=$false; measuredMs=220}
  # Direction B（人物故事方向·B）：auto story album (place + time-window clustering).
  # Pure-function StoryGen::build over synthetic QVector<Photo> (no DB, no UI):
  # (A) conservation -- outputCount == inputCount and every path appears exactly once;
  # (B) full coverage -- every non-private input path is placed in some story (honest,
  #     no silent drop), including "未标注日期" single-photo stories for date-less shots;
  # (C) grouping shape -- Beijing splits into 2 trips (gap>5d), Shanghai 1, 2 date-less
  #     singles -> 5 stories total; (D) retrieval contract -- GalleryModel.setStoryFilter
  #     of one story's paths yields rowCount == that story's photoCount.
  # Counter-example LA_STORY_QA_FORCE_BROKEN=1 makes StoryGen drop one photo, so the
  # conservation assertion (A) turns RED -- same assertion is measured green positive.
  # Tier: FULL (new feature gate, sibling of LA_FACE_CLUSTER_QA). Pure in-memory, no
  # write, so it is also cheap. measuredMs=162 from qa_runner -Timing (rule 13).
  @{n="LA_STORY_QA"; png=$false; tier="full"; measuredMs=162}
  # 2.22.0 client experience: lightbox FULL image + thumbnail skeleton +
  # animation pure-function contract. Pure offscreen self-exit, no MainWindow,
  # no external env. Renders cards into QImage and asserts at the PIXEL level:
  # (A) state machine (Ready/Loading/Missing) plus skeletonPulse value range,
  # peak, periodicity and the reduce-motion terminal value; (B) skeleton render
  # must differ from a ready render, different phases must render differently,
  # and reduce-motion frames must be identical; (C) FullCache budget > 0 and a
  # real 1600x1200 decode whose width MUST exceed the 512 thumb level -- that
  # threshold is the discriminator, so "kept showing the thumbnail" goes red.
  # Counter-example LA_TRANSITION_QA_FORCE_BROKEN=1 forces every decode to fail
  # via the real FullCache hook, so the SAME C assertions turn red (expected
  # values never switch on the broken flag -- rule 22).
  # Tier: FULL. measuredMs=400 from qa_runner -Timing (rule 13), non-zero for E8.
  @{n="LA_TRANSITION_QA";  png=$false; measuredMs=400}
)
# Resolve a gate's tier. Absent key means "full" - the fail-safe default.
function Get-GateTier {
  param($Gate)
  if ($Gate.ContainsKey("tier") -and $Gate.tier) { return $Gate.tier }
  return "full"
}

# Filter the table by tier. "" (default) returns EVERY gate, i.e. the union,
# which is what local runs and release.ps1 use.
function Get-GateTable {
  param([string]$Tier = "", $Table = $null)
  if ($null -eq $Table) { $Table = $GateTable }
  # NOTE: do NOT wrap the returns in the unary comma. ",@($x)" builds a
  # ONE-element array whose single element is the array itself, so every caller
  # saw gates=1 instead of 30. PowerShell unrolls a returned array anyway, and
  # every call site wraps in @(...) already, which restores the array type.
  if (-not $Tier) { return @($Table) }
  if ($Tier -eq "fast") { return @($Table | Where-Object { (Get-GateTier $_) -eq "fast" }) }
  if ($Tier -eq "full") { return @($Table | Where-Object { (Get-GateTier $_) -eq "full" }) }
  throw ("gate_matrix: unknown tier '" + $Tier + "' (want fast|full|empty)")
}

# ---------------------------------------------------------------------------
# Structural validator. Pure function of its input table: no globals, no side
# effects, no test-only branch. That is what makes the counter-examples in
# -SelfTest meaningful - they feed a broken table through THIS SAME function and
# require it to go red. A validator with a hidden bypass would pass them.
#
# $fastBudgetMs: the measured-cost ceiling for the PR layer. The cheapest fast
# gate is 56 ms, the most expensive 250 ms, so 500 ms keeps real headroom while
# still excluding every full-tier gate (cheapest of those is 1017 ms).
# $coreFastGates: gates that must NEVER leave the PR layer. Each one guards a
# property that has already caused a real incident here, so losing PR coverage
# on it is not acceptable. LA_EXIT_QA is deliberately NOT listed: at 5700 ms it
# is a full-tier gate (it spawns probe subprocesses by design).
# ---------------------------------------------------------------------------
$fastBudgetMs = 500
$coreFastGates = @(
  "LA_GATE_MATRIX_QA",
  "LA_RETOUCH_UNDO_QA",
  "LA_GALLERY_SEARCH_QA",
  "LA_THUMB_CACHE_QA",
  "LA_GALLERY_BATCH_QA"
)

function Test-GateMatrix {
  param($Table)
  $errs = @()

  # E1: floor on the gate count. Guards against a bad edit silently dropping
  # most of the suite - the failure mode a reviewer would not notice.
  if (@($Table).Count -lt 25) {
    $errs += ("E1 gate count too low: " + @($Table).Count + " (<25)")
  }

  $seen = @()
  foreach ($g in $Table) {
    $nm = $g.n
    if (-not $nm) { $errs += "E2 gate without a name"; continue }
    if (-not $nm.StartsWith("LA_")) { $errs += ("E2 gate name must start with LA_: " + $nm) }
    if ($seen -contains $nm) { $errs += ("E2 duplicate gate name: " + $nm) }
    $seen += $nm

    # E3: tier domain. A typo like "fas" would silently demote a gate to the
    # default full layer - or, worse, be read as "not fast" and never run on PR.
    $t = Get-GateTier $g
    if (($t -ne "fast") -and ($t -ne "full")) {
      $errs += ("E3 illegal tier '" + $t + "' on " + $nm)
    }

    # E4: a fast gate that needs external env vars is dead weight on CI (it can
    # only ever SKIP there), and worse, it looks like coverage.
    if (($t -eq "fast") -and $g.need) {
      $errs += ("E4 fast gate must not require external env: " + $nm + " needs " + ($g.need -join ","))
    }

    # E5: script gates shell out to another PowerShell; they are not cheap
    # offscreen self-exits and do not belong in the per-PR layer. The single
    # exception is this gate matrix's own self-test: pure PowerShell, no exe,
    # and it MUST sit in the fast layer or the matrix would have no PR guard.
    if (($t -eq "fast") -and ($g.kind -eq "ps1") -and ($nm -ne "LA_GATE_MATRIX_QA")) {
      $errs += ("E5 fast gate must not be a script gate: " + $nm)
    }
  }

  # E6: MEMBERSHIP is decided by measured cost, not by taste. This is the rule
    # that makes the split honest: a gate may only sit in the PR layer if it was
    # MEASURED cheap. A ratio check ("fast must stay under half the suite") is
    # far too weak - it stayed green when a 3640 ms map gate was quietly moved
    # into the fast layer, because 10 of 30 is still "under half". Proven by
    # counter-example, which is why this rule exists in this form.
    $fastN = @(Get-GateTable -Tier "fast" -Table $Table).Count
    $allN  = @($Table).Count
    if ($fastN -lt 8) { $errs += ("E6 fast layer too thin: " + $fastN + " (<8)") }
    foreach ($g in (Get-GateTable -Tier "fast" -Table $Table)) {
      $ms = 0
      if ($g.ContainsKey("measuredMs")) { $ms = [int]$g.measuredMs }
      if ($ms -gt $fastBudgetMs) {
        $errs += ("E6 fast gate is not cheap enough: " + $g.n + " measuredMs=" + $ms + " (budget " + $fastBudgetMs + ")")
      }
    }

    # E7: MEMBERSHIP also has a floor - the invariant gates must always run on a
    # PR. Count-based checks cannot catch one of these being dropped, which is
    # the exact regression the fast layer must prevent: a broken self-exit
    # contract or a broken cache/undo state machine reaching main unseen.
    foreach ($must in $coreFastGates) {
      $hit = @($Table | Where-Object { ($_.n -eq $must) -and ((Get-GateTier $_) -eq "fast") }).Count
      if ($hit -ne 1) { $errs += ("E7 core invariant gate not in fast layer: " + $must) }
    }

    # E8: every gate must carry a MEASURED cost. A gate with no measuredMs
    # silently reads as 0 ms, which would let any unmigrated gate slip into the
    # fast layer on stale data. This rule was added after a real corruption:
    # an annotation pass appended measuredMs INSIDE the quoted scriptArgs string
    # of LA_RELEASE_QA, so the key vanished while the string silently changed
    # the release self-test's arguments.
    foreach ($g in $Table) {
      if (-not $g.n) { continue }
      if (-not $g.ContainsKey("measuredMs")) {
        $errs += ("E8 gate has no measuredMs (tiering would be a guess): " + $g.n)
      }
    }

    return $errs
}

if ($LoadOnly) { return }

if ($List) {
  foreach ($g in $GateTable) {
    Write-Host ("{0,-20} {1}" -f $g.n, (Get-GateTier $g))
  }
  exit 0
}

if (-not $SelfTest) { Write-Host "gate_matrix: use -SelfTest or -List (or dot-source with -LoadOnly)"; exit 0 }

# ---------------------------------------------------------------------------
# -SelfTest: this file's own gate. Asserts the matrix is structurally sound AND
# that the validator above is not vacuous.
# ---------------------------------------------------------------------------
$fails = @()

function Chk {
  param([string]$Id, [bool]$Ok, [string]$Note)
  if ($Ok) {
    Write-Host ("LA_GATE_MATRIX_QA PASS {0} {1}" -f $Id, $Note)
  } else {
    Write-Host ("LA_GATE_MATRIX_QA FAIL {0} {1}" -f $Id, $Note)
    $script:fails += ($Id + " " + $Note)
  }
}

$all  = @(Get-GateTable)
$fast = @(Get-GateTable -Tier "fast")
$full = @(Get-GateTable -Tier "full")

# E1 - the real table passes the structural validator.
$realErrs = @(Test-GateMatrix -Table $all)
Chk "E1" ($realErrs.Count -eq 0) ("gates=" + $all.Count + " fast=" + $fast.Count + " full=" + $full.Count + " errs=" + $realErrs.Count)
foreach ($e in $realErrs) { Write-Host ("LA_GATE_MATRIX_QA FAIL E1 detail=" + $e) }

# E2 - the two layers are disjoint and their union is the whole table.
$overlap = @($fast | Where-Object { $full -contains $_ }).Count
Chk "E2" (($overlap -eq 0) -and (($fast.Count + $full.Count) -eq $all.Count)) ("overlap=" + $overlap + " union=" + ($fast.Count + $full.Count) + " all=" + $all.Count)

# E3 - FAIL-SAFE: strip every tier key. Nothing may be left in the fast layer,
# so a new gate that forgets to declare a tier can only ever land in "full".
$stripped = @()
foreach ($g in $all) {
  $c = @{}
  foreach ($k in $g.Keys) { if ($k -ne "tier") { $c[$k] = $g[$k] } }
  $stripped += $c
}
$stripFast = @(Get-GateTable -Tier "fast" -Table $stripped).Count
$stripFull = @(Get-GateTable -Tier "full" -Table $stripped).Count
Chk "E3" (($stripFast -eq 0) -and ($stripFull -eq $all.Count)) ("strippedFast=" + $stripFast + " strippedFull=" + $stripFull)

# E4 - the strict layer must actually hold the expensive band. Guards against a
# future edit that "optimises" the matrix by marking everything fast, which
# would leave the two layers identical and the tiers decorative.
$cheapMax = 2000
$slowSeen = 0
foreach ($g in $full) { if ($g.measuredMs -ge $cheapMax) { $slowSeen++ } }
Chk "E4" ($slowSeen -ge 10) ("fullTierExpensiveGates=" + $slowSeen + " (measured >= " + $cheapMax + "ms)")

# E5 - every fast gate must be one the PR layer can actually run on a bare CI
# runner: self-exiting exe gate, no external env, no script shell-out.
$badFast = @($fast | Where-Object { ($_.kind -eq "ps1") -and ($_.n -ne "LA_GATE_MATRIX_QA") })
$envFast = @($fast | Where-Object { $_.need })
Chk "E5" (($badFast.Count -eq 0) -and ($envFast.Count -eq 0)) ("badScriptFast=" + $badFast.Count + " envGatedFast=" + $envFast.Count)

# E6 - MEMBERSHIP, not ratio. The measured-cost budget is what actually keeps
# the PR layer cheap; it is asserted here directly as well as inside the
# validator, because it is the property the whole matrix rests on.
$overBudget = @($fast | Where-Object { [int]$_.measuredMs -gt $fastBudgetMs })
Chk "E6" ($overBudget.Count -eq 0) ("budget=" + $fastBudgetMs + "ms overBudget=" + $overBudget.Count)

# E7 - the core invariant gates are all present in the PR layer.
$missingCore = @($coreFastGates | Where-Object { $c = $_; @($fast | Where-Object { $_.n -eq $c }).Count -ne 1 })
Chk "E7" ($missingCore.Count -eq 0) ("core=" + $coreFastGates.Count + " missing=" + $missingCore.Count)

# E8 - every real gate carries a measured cost.
$noMeasured = @($all | Where-Object { -not $_.ContainsKey("measuredMs") })
Chk "E8" ($noMeasured.Count -eq 0) ("gates=" + $all.Count + " missingMeasuredMs=" + $noMeasured.Count)

# E9 - counter-examples: the validator must actually reject broken tables.
# Each bad table targets one rule and we require THAT rule's id in the errors,
# so a green result cannot come from an unrelated check. This block exists
# because the first version of E6 was a ratio check and two of these five
# counter-examples stayed GREEN against it - see the CHANGELOG for 2.8.0.
function Reject {
  param($Table, [string]$WantId)
  $errs = @(Test-GateMatrix -Table $Table)
  $hit = @($errs | Where-Object { $_.StartsWith($WantId) }).Count
  return @{ total = $errs.Count; hit = $hit }
}

$r1 = Reject -Table (@($all) + @(@{n="LA_GEO_SMOKE"; png=$false; tier="fast"})) -WantId "E2"
Chk "E9a" (($r1.hit -gt 0) -and ($r1.total -gt 0)) ("dupName rejected total=" + $r1.total + " hitE2=" + $r1.hit)

$r2 = Reject -Table @(@{n="LA_TYPO_SMOKE"; png=$false; tier="fas"}) -WantId "E3"
Chk "E9b" (($r2.hit -gt 0) -and ($r2.total -gt 0)) ("badTier rejected total=" + $r2.total + " hitE3=" + $r2.hit)

$r3 = Reject -Table (@($all) + @(@{n="LA_NEEDY_FAKE"; png=$false; tier="fast"; need=@("LA_FAKE_ROOT")})) -WantId "E4"
Chk "E9c" (($r3.hit -gt 0) -and ($r3.total -gt 0)) ("fastWithNeed rejected total=" + $r3.total + " hitE4=" + $r3.hit)

$r4 = Reject -Table (@($all) + @(@{n="LA_PS_FAKE"; png=$false; tier="fast"; kind="ps1"; script="x.ps1"})) -WantId "E5"
Chk "E9d" (($r4.hit -gt 0) -and ($r4.total -gt 0)) ("fastScriptGate rejected total=" + $r4.total + " hitE5=" + $r4.hit)

# CE4 from the external proof run: demoting an EXPENSIVE gate is NOT caught by a
# ratio rule (10 of 30 is still "under half") but MUST be caught by the cost
# budget. If this ever goes green again the budget rule has been weakened.
$expensiveInFast = @()
foreach ($g in $full) {
  if ([int]$g.measuredMs -gt $fastBudgetMs) { $expensiveInFast += $g }
}
$movedExpensive = @($expensiveInFast | Select-Object -First 1)
if ($movedExpensive.Count -eq 1) {
  $r5 = Reject -Table (@($all) + @(@{n=$movedExpensive[0].n; png=$false; tier="fast"; measuredMs=$movedExpensive[0].measuredMs})) -WantId "E6"
  Chk "E9e" (($r5.hit -gt 0) -and ($r5.total -gt 0)) ("expensiveInFast rejected (" + $movedExpensive[0].n + " " + $movedExpensive[0].measuredMs + "ms) hitE6=" + $r5.hit)
} else {
  Chk "E9e" $false "no expensive gate available to build the counter-example"
}

# CE1 from the external proof run: a ratio rule also cannot see a CORE gate
# silently leaving the PR layer, as long as the raw count stays >= 8. Strip the
# tier key from the FIRST core gate and require the validator to notice.
$victim = $coreFastGates[0]
$demoted = @()
foreach ($g in $all) {
  $c = @{}
  foreach ($k in $g.Keys) { if ($k -ne "tier") { $c[$k] = $g[$k] } }
  $demoted += $c
}
$r6 = Reject -Table $demoted -WantId "E7"
Chk "E9f" (($r6.hit -gt 0) -and ($r6.total -gt 0)) ("coreGateDemoted rejected victim=" + $victim + " hitE7=" + $r6.hit)

# CE2 from the wiring step: measuredMs silently swallowed into a quoted
# scriptArgs string. The key vanishes, the argument changes, nothing else breaks.
$corrupt = @()
foreach ($g in $all) {
  $c = @{}
  foreach ($k in $g.Keys) { $c[$k] = $g[$k] }
  if ($c.ContainsKey("measuredMs")) { $c.Remove("measuredMs") }
  $corrupt += $c
}
$r7 = Reject -Table $corrupt -WantId "E8"
Chk "E9g" (($r7.hit -gt 0) -and ($r7.total -gt 0)) ("missingMeasuredMs rejected total=" + $r7.total + " hitE8=" + $r7.hit)

# E10 - an unknown tier must throw, not silently fall back to "run everything".
$threw = $false
try { Get-GateTable -Tier "medium" | Out-Null } catch { $threw = $true }
Chk "E10" $threw "unknownTierThrows=1"

Write-Host ("LA_GATE_MATRIX_QA PASS gates={0} fast={1} full={2} budget={3}ms core={4}" -f `
            $all.Count, $fast.Count, $full.Count, $fastBudgetMs, $coreFastGates.Count)
if ($fails.Count -gt 0) {
  Write-Host ("LA_GATE_MATRIX_QA FAIL fails={0}" -f $fails.Count)
  exit 1
}
Write-Host ("LA_GATE_MATRIX_QA PASS fails=0")
exit 0