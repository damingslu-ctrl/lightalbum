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
  # timeoutSec: R3 in-op parallel + float: re-measured via -Timing (was 66585
  # serial CPU). Timed runs (qa_runner -Timing -Only LA_LUMEN_SMOKE):
  # 3942 / 3930 / 3884 / 3908 ms, all PASS; conservative max 3942 recorded.
  # Rule: M <= 30000 -> smallest 15s multiple >= 3xM (11.8s) with a 45s floor
  # => timeoutSec=45. Hangs are still detected, just with a budget that
  # matches the parallelized reality.
  @{n="LA_LUMEN_SMOKE";     png=$false; timeoutSec=45; measuredMs=3942},
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
  # measuredMs ~8000. tier defaults to full (fail-safe).
  @{n="LA_SCALE_REAL_PERF_QA"; png=$false; need=@("LA_SCALE_REAL_DB"); measuredMs=8000},
  @{n="LA_EXIT_QA";         png=$false; measuredMs=5700},
  @{n="LA_RW_SMOKE";        png=$false; measuredMs=1017},
  @{n="LA_CULL_SMOKE";      png=$false; measuredMs=3210},
  # P25 duel arbitration (implemented in CullPage::runDuelQa, dispatched by the
  # CullPage ctor when the env var holds a png path). It existed but was NOT in
  # the matrix, and it had no verdict at all: it printed raw numbers and called
  # requestExit(0), while the runner's greenPat substring "OK" matched the
  # lowercase "ok" of markOk=0 -> the gate could never go RED (E34). Now it
  # prints PASS/FAIL over groups/members0/skipDelta/markOk/gold. Proven
  # non-vacuous through the shipped hook: LA_CULL_DUEL_QA_FORCE_BROKEN=1 makes
  # the duel verdict never write a mark -> "FAIL ... skipDelta=0 markOk=0".
  @{n="LA_CULL_DUEL_QA";    png=$true; measuredMs=1910},
  # Geo data structural smoke (MapView::loadGeoJson). Also pre-existing but
  # unregistered, and doubly broken: no verdict (greenPat matched its "rings="
  # so rings=0 passed) and no self-exit (it kept running into app.exec() and was
  # killed -> that is why 2.2.1 dropped it from the runner). Now it asserts
  # every layer non-empty AND zhKeys == province-geometry count (a missing
  # Chinese province name goes RED), and main.cpp self-exits it. Proven
  # non-vacuous through the shipped hook: LA_MAP_SMOKE_FORCE_BROKEN=1 drops one
  # zh key (the real shape of an untranslated province) ->
  # "FAIL ... zhKeys=232 zhComplete=0"; qa_runner reports [FAIL] for it.
  @{n="LA_MAP_SMOKE";       png=$false; measuredMs=930},
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
  # Direction C, linkage: map geo-hotspot aggregation + one-click jump gate
  # (LA_GEO_LINK_QA). Asserts GeoHotspot::compute as a pure function (World groups by
  # region / Region groups by prov) for conservation, plus the contract "gallery filtered
  # by the hotspot key == hotspot.count", and offscreen-verifies that clicking a hotspot
  # badge emits hotspotActivated with the correct key.
  # Counter-example LA_GEO_LINK_QA_FORCE_BROKEN=1 makes the World level aggregate by prov
  # (the key is then no longer a region id) -> those assertions go RED (not vacuous).
  # Tier defaults to FULL (fail-safe): the process pays QApplication + offscreen startup,
  # and constructing MapView parses mapdata.json through loadData, measured >500 ms, so it
  # cannot sit in the fast layer.
  # measuredMs=287 from qa_runner -Timing after the 2.33.0 encoding fix (the earlier
  # 1450 ms estimate was never measured -- this gate had been silently unregistered).
  # It stays in the strict layer on purpose: one sample near half the fast budget is not
  # evidence, and the fail-safe default is full (E3).
  @{n="LA_GEO_LINK_QA";    png=$false; measuredMs=287},
  @{n="LA_MAP_CLICK_QA";    png=$false; measuredMs=2553},
  # Direction A, map performance: frame-cost budget gate. qaPerf renders 20 frames per layer
  # (world/region/province/country) and averages ms; over budget it prints LA_MAP_PERF FAIL
  # AND exits non-zero (so the runner FAIL scan bites).
  # Evidence-based threshold relaxation (measure first, rule 25): world <=1.6 ms. Base-load
  # world mean-of-20 measures ~1.15 ms; under a loaded cluster (parallel gates) it drifts to
  # ~1.55 ms, so 1.6 leaves headroom for noise WITHOUT going hollow. The "scale 3000x1500
  # every frame" regression signature is ~2.30 ms/frame, still caught. region/province/
  # country <=1.5 ms. The roadmap target world/region <0.3 ms turned out physically
  # unreachable (a full-screen blit is mandatory) and was dropped.
  # Tier defaults to FULL: like LA_SCALE_PERF it is a wall-clock budget gate, kept out of
  # fast to avoid CI flake.
  @{n="LA_MAP_PERF";        png=$false; measuredMs=2438},
  @{n="LA_GALLERY_E_QA";    png=$true; measuredMs=2589},

  # 2.28.0 RAW format support gate (LA_RAW_QA). Group A is a zero-dependency pure-function
  # contract and always runs; group B decodes for real and needs the LibRaw build plus one
  # RAW sample. need= declares that dependency (rule 26): if either is missing the gate SKIPs
  # cleanly and never fakes a pass. The sample is external material and must NEVER be
  # committed or shipped to CI (zero binaries in the repo, no personal data).
  # Counter-example LA_RAW_QA_FORCE_BROKEN=1 makes decodeToTempPpm fall back to the defective
  # shape "rc==0 means success + guess the artifact name by swapping the extension" -> B2/B3
  # go RED. Measured: dcraw_emu returns rc=0 in both modes, so the exit code carries no
  # information and artifact validation is the only judgement. Expected values do not switch
  # with the broken flag (rule 22).
  # Tier defaults to FULL: real decode measures ~0.6 s per image, plus QApplication startup.
  @{n="LA_RAW_QA"; png=$false; need=@("LA_RAW_SAMPLE"); measuredMs=2605},
  # 2.28.0 video support gate (LA_VIDEO_QA). Group A is a zero-dependency pure-function
  # contract (it carries the same needsExternalDecode boundary assertion as RAW); group B
  # extracts and probes for real and needs FFmpeg plus a video sample; group C is the badge
  # pixel gate (renders isVideo true/false cards offscreen and asserts the top-left badge
  # differs). need= declares the dependency (rule 26): missing FFmpeg or sample SKIPs
  # cleanly, and the sample is never committed or shipped to CI.
  # Counter-example LA_VIDEO_QA_FORCE_BROKEN=1 makes extractPoster use "rc==0 means success +
  # wrongly guessed artifact name" -> B2b goes RED while the badge pixel group is untouched,
  # which proves the counter-example only attacks the decode judgement (not a blanket switch).
  # Tier defaults to FULL: real frame extraction measures ~0.3-0.8 s, plus QApplication startup.
  @{n="LA_VIDEO_QA"; png=$false; need=@("LA_VIDEO_SAMPLE"); measuredMs=1252},
  # 2.30.0 video playback (embedded decoder) gate (LA_VIDEO_PLAY_QA). Drives ffmpeg rawvideo
  # through QProcess and decodes frame by frame into genuinely MOVING pictures: the green case
  # asserts frames>0, the last frame non-empty with the correct size, that CONSECUTIVE FRAMES
  # REALLY DIFFER (a still image or placeholder would go RED) and increasing pts.
  # Counter-example LA_VIDEO_PLAY_QA_FORCE_BROKEN=1 hands ffmpeg a second input that does not
  # exist -> no output stream can be built -> 0 frames decoded, A1/A2/A3/A4/A5 go RED (the
  # expected values never switch with the broken flag, rule 22). need=LA_VIDEO_SAMPLE (the
  # external asset is never committed or shipped to CI).
  # Tier defaults to FULL: real decode ~1 s, plus QApplication startup.
  @{n="LA_VIDEO_PLAY_QA"; png=$false; need=@("LA_VIDEO_SAMPLE"); measuredMs=1748},
  # 2.30.0 player UI rendering gate (LA_VIDEO_UI_QA). Renders VideoPlayerWidget offscreen and
  # grab()s it, asserting THE PICTURE AREA IS NOT BLANK (rule 24: visuals must be measured,
  # never guessed) -- measured green nonBg=5689 sampled pixels versus 20 under the
  # counter-example (only the "loading" placeholder text remains). Also carries a real-motion
  # assertion (hasAdvanced) and the frame-decode assertions.
  # Counter-example keeps the same FORCE_BROKEN semantics: D1/D4/D5 go RED (expected values do
  # not switch with the broken flag). need=LA_VIDEO_SAMPLE.
  @{n="LA_VIDEO_UI_QA"; png=$false; need=@("LA_VIDEO_SAMPLE"); measuredMs=5067},
  # 2.30.0 audio output gate (LA_AUDIO_QA). Verifies that audio track -> ffmpeg s16le PCM ->
  # winmm waveOut really sends non-silent audio. It does NOT judge by "can you hear it"
  # (unreliable offscreen / device-less); it asserts the waveOut device opens AND the decoded
  # PCM byte count > 0 and block-aligned -- together equivalent to audible, and assertable
  # offscreen. Counter-example FORCE_BROKEN: ffmpeg cannot build the stream -> 0 bytes -> C2
  # goes RED. When the sample carries no audio track the gate SKIPs cleanly (honest premise,
  # nothing fabricated). need=LA_VIDEO_SAMPLE.
  @{n="LA_AUDIO_QA"; png=$false; need=@("LA_VIDEO_SAMPLE"); measuredMs=3664},
  # 2.31.0 frame-picker gate (LA_VIDEO_FRAME_QA). Verifies that picking by second really
  # returns DIFFERENT frames. Background: measured, the frames at t=0/2/4 s of the same video
  # have distinct md5, so if extraction ignored the second or shared one artifact name every
  # pick would read the same frame -- the frame picker's number-one failure mode. The green
  # case asserts the three frames differ and that second 0 is decodable; counter-example
  # FORCE_BROKEN makes extractPoster ignore atSec (always extracts 2 s) -> E2/E3/E4 go RED.
  # need=LA_VIDEO_SAMPLE. Tier defaults to FULL (three extractions, one ffmpeg each, ~1.5 s).
  @{n="LA_VIDEO_FRAME_QA"; png=$false; need=@("LA_VIDEO_SAMPLE"); measuredMs=740},
  # 2.31.0 extraction cache gate (LA_VIDEO_CACHE_QA). Verifies extractPoster's disk cache: the
  # green case asserts the first call misses and writes an entry (count=1), the second hits
  # (count unchanged), a source mtime change invalidates the cache and produces a new entry
  # (count=2), and clear empties it. Counter-example FORCE_BROKEN bypasses the cache ->
  # C2/C3/C5/C8 go RED. need=LA_VIDEO_SAMPLE. Measured ~2-3 s (two cache misses).
  @{n="LA_VIDEO_CACHE_QA"; png=$false; need=@("LA_VIDEO_SAMPLE"); measuredMs=502},
  # 2.29.0 cull exemption contract gate (LA_CULL_EXEMPT_QA). Pure functions + synthetic
  # thumbnails, ZERO external dependencies, so it belongs in the fast layer (measured in ms).
  # Group A pins down the "RAW/video exempt" predicate, including the 8 RAW extensions that
  # once drifted (fff/iiq/k25/kdc/mos/ptx/srf/srw -- Sigma/Phase One/Pentax/Mamiya/Ricoh/Sony)
  # and the 10 video extensions once omitted; group B compares the RAW, video and plain-photo
  # paths on a VERY DARK synthetic image: RAW and video must come out candidate (exemption in
  # effect) while the plain photo must be rejected -- which proves the predicate is not a
  # tautology (rule 16). Counter-example LA_CULL_EXEMPT_QA_FORCE_BROKEN=1 disables the RAW
  # exemption -> B1 goes RED while group A stays green.
  @{n="LA_CULL_EXEMPT_QA"; png=$false; tier="fast"; measuredMs=74},
  # 2.31.0 cull page video adaptation gate (LA_CULL_VIDEO_QA). Verifies that CullEngine passes
  # isVideo/durSec through, CullModel exposes the roles, CullProxy's "video only" filter works
  # and CullCardDelegate paints the play badge plus the duration. Pure synthetic thumbnails,
  # zero external dependencies, fast layer (measured in ms).
  # Counter-example LA_CULL_VIDEO_QA_FORCE_BROKEN=1 disables badge painting -> D1/D2 go RED
  # while A/B/C stay green.
  @{n="LA_CULL_VIDEO_QA"; png=$false; tier="fast"; measuredMs=84},
  # 2.29.0 RAW/video retouch pipeline gate (LA_EDIT_RAW_QA). The retouch page used to read the
  # original straight through QImage(ph.path), which measured 0x0 null=1 for RAW -> RAW could
  # never reach the retouch page at all (2.28.0 missed this entry point). Photo, RAW and video
  # now converge on ImageLoad::loadForEdit. need= declares both sample dependencies: RAW and
  # video each need one asset, and a missing one SKIPs cleanly (samples are never committed or
  # shipped to CI). B4 is the end-to-end pixel gate: load -> RetouchEngine::apply -> assert the
  # pixels REALLY CHANGED (measured 98.4%), ruling out the fake pass where loading works but
  # the adjustment has no effect (rule 29). Counter-example LA_EDIT_RAW_QA_FORCE_BROKEN=1 sends
  # RAW back to direct QImage reading (the 2.28.0 defect shape) -> B1/B1b go RED (measured
  # raw: 0x0 null=1) while group A stays green.
  # Tier defaults to FULL: real RAW decode measures ~0.6 s per image, plus QApplication startup.
  @{n="LA_EDIT_RAW_QA"; png=$false; need=@("LA_RAW_SAMPLE","LA_VIDEO_SAMPLE"); measuredMs=1581},
  @{n="LA_GALLERY_FILL_QA"; png=$true; measuredMs=2690},
  # R1-T4 re-measure: qaShootA now counts enhOps (13 wired ops), so the pre-R1
  # record measuredMs=2707 was stale; 1113 was that -Timing value. R3-T4 re-measure
  # (qaShootA also asserts hasEraseCancel): 1122 from qa_runner -Timing.
  @{n="LA_RETOUCH_A_QA";    png=$true; measuredMs=1122},
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
  # 2.33.0 privacy gate: vault lock state must reach the three entries that
  # previously ignored it - lightbox paging (BrowseScope::nextVisible, the same
  # pure function PhotoLightbox::navigate calls), the cull page (CullModel chips
  # counts / lock area / marked list + CullProxy grid), and the dedup dialog
  # (private photos must not even enter the injection-point input set, or they
  # get grouped, rendered and "mark rest as delete"-ed).
  # Every check is a LOCKED/UNLOCKED pair, so a filter that never runs fails the
  # locked half and a filter that always runs fails the unlocked half - the two
  # halves make each block self-checking, no extra hook needed. The dialog block
  # additionally has a real-defect hook: LA_PRIVACY_QA_FORCE_BROKEN=1 disables
  # the injection-point filter and must go RED on C2..C6.
  # Measured 161-179ms (x3, 2026-10-10) -> same band as LA_DEDUP_QA -> fast tier.
  @{n="LA_PRIVACY_QA"; png=$false; tier="fast"; measuredMs=170},
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
  # default label (the CJK literal for "person 1"); the user renames it from the GalleryPage
  # person chips.
  # Pure offscreen self-exit, no MainWindow, no external env. Calls
  # PersonDetector::detect() with a synthetic result hook (bypassing the face_server
  # HTTP service) and asserts: (A) DB person column written; (B) in-memory
  # Photo.person synced (GalleryModel reads memory); (C) GalleryModel rowCount per
  # person + "all" correct; (D) total photo count conserved; (E) renamePerson()
  # token-level rename updates DB + memory + retrieval; (F) the default label is the CJK
  # literal for "person 1".
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
  # measuredMs=165 from qa_runner -Timing (rule 13), non-zero for E8.
  # 2.35.0: two-pass (greedy seed + centroid refine); groups O(order)/C(centroid)/
  # S(same-person)/D(different-person) added; FORCE_MERGE collapses all -> C/D RED.
  @{n="LA_FACE_CLUSTER_QA"; png=$false; measuredMs=165}
  # 2.33.0 thread-safety single-writer gate. AI async write-backs (classify / person /
  # cluster) used to call Library mutators from the QtConcurrent worker thread: the
  # "la_rw" QSqlDatabase handle is bound to the GUI thread (UB to use cross-thread) and
  # the mutators sync photos_/pathIx_ that GalleryModel reads by reference (torn read).
  # Fix = single writer: workers compute, AiWriter marshals the write to the GUI thread.
  # The gate drives AiWriter from a REAL worker thread (QtConcurrent::run) and asserts
  # (A) the write has NOT landed before processEvents (proves it is queued, not inline);
  # (B) after processEvents the DB + in-memory row are written; (C) Library::
  # lastWriteThreadId() == GUI thread id (the load-bearing thread-ownership check);
  # and repeats C for the Classifier and PersonDetector synthetic composition paths.
  # Counter-example LA_GUI_THREAD_QA_FORCE_BROKEN=1 flips AiWriter to write on the
  # calling (worker) thread -- the pre-fix shape. The expectations are UNCHANGED (rule
  # 22): lastWriteThreadId != GUI and "leaked before event loop" both turn RED, so the
  # green run's thread assertion is measured failable and not hollow (rules 34/35).
  # Tier: FULL (real threads + event loop; sibling of the other direction-C gates).
  # measuredMs=197 from qa_runner -Timing (rule 13), non-zero for E8.
  @{n="LA_GUI_THREAD_QA";   png=$false; measuredMs=197}
  # Person-story direction, variant B: auto story album (place + time-window clustering).
  # Pure-function StoryGen::build over synthetic QVector<Photo> (no DB, no UI):
  # (A) conservation -- outputCount == inputCount and every path appears exactly once;
  # (B) full coverage -- every non-private input path is placed in some story (honest,
  #     no silent drop), including the date-less bucket's single-photo stories (the CJK label
  #     for "no date recorded") for shots without EXIF dates;
  # (C) grouping shape -- Beijing splits into 2 trips (gap>5d), Shanghai 1, 2 date-less
  #     singles -> 5 stories total; (D) retrieval contract -- GalleryModel.setStoryFilter
  #     of one story's paths yields rowCount == that story's photoCount.
  # Counter-example LA_STORY_QA_FORCE_BROKEN=1 makes StoryGen drop one photo, so the
  # conservation assertion (A) turns RED -- same assertion is measured green positive.
  # Tier: FULL (new feature gate, sibling of LA_FACE_CLUSTER_QA). Pure in-memory, no
  # write, so it is also cheap. measuredMs=162 from qa_runner -Timing (rule 13).
  @{n="LA_STORY_QA"; png=$false; tier="full"; measuredMs=162}
  # 2.34.0 motion engine (MotionKit): pure-function truth source, offscreen, no
  # external assets. Asserts (A) curve contracts -- endpoints f(0)==0/f(1)==1,
  # monotone curves never decrease, OutBack overshoots >1, Pulse is periodic with
  # range [0.25,1], Lumen::skeletonPulse delegates to engine Pulse point-for-point,
  # and the reduce-motion hook qaForceTerminal collapses every curve to 1;
  # (B) stagger offsets are monotone with stagger(0)==0 and total<=1 gives 0;
  # (C) preset table completeness -- >=5 presets, duration>0, non-empty keys,
  # evaluate dim == key count, unknown name falls back to a non-empty preset;
  # (D) the overshoot-return segment -- StarBounce glow evaluate(f=2)==return_to;
  # (E) particle trajectory pure fn + PIXEL discriminator -- two motion phases of
  # the NavFade offsetY bar must render differently, and under reduce-motion the
  # two phases must render identically (no flicker).
  # Counter-example LA_MOTION_QA_FORCE_BROKEN=1 sets qaForceTerminal(true) so every
  # curve collapses to its terminal value -> the SAME A-group start/range assertions
  # turn RED (expectations never branch on broken, rule 22; measured green positive).
  # Tier: FULL (feature gate, sibling of LA_STORY_QA). Pure in-memory, cheap.
  @{n="LA_MOTION_QA"; png=$false; tier="full"; measuredMs=131}
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
  @{n="LA_TRANSITION_QA";  png=$false; measuredMs=400},
  # 2.35.0 EraseBridge: hermetic contract gate (uses deterministic inpaint_stub.exe).
  # Asserts (A) state machine queued->running->done; (B) mask-out byte conservation
  # + mask-in change ratio >= 10%; (C) child process PID persists across submissions;
  # (D) after qaKillChild crash, next submit auto-restarts and completes; (E) non-blocking --
  # while the stub sleeps 220ms (LA_ERASE_STUB_DELAY_MS) a 5ms timer must log >=20 ticks inside
  # the running->terminal window (RED-proven against the pre-2.36.0 sync impl). The stub answers
  # streamed (LA_ERASE_STUB_CHUNK_KB=8 + CHUNK_MS=3) so the host body-read loop is proven against
  # deterministic pipe fragmentation (single-read impl was RED-proven: short rgb, every task).
  # Counterexample
  # LA_ERASE_QA_FORCE_BROKEN=1 arms EraseRunner::qaInjectBadSize BEFORE the first submit;
  # run() then returns an out-of-bounds-copy (null) QImage, applyResult rejects it, and the
  # done/pixel/recovery assertions turn RED together (rule 22: expectations never branch).
  # Tier: FULL (QProcess lifecycle, no external env). measuredMs=12390 from qa_runner -Timing
  # (rule 13; re-measured after the 2.36.0 async + streamed-body rig; history 2210 sync -> 3141 async).
  # R3-T4 re-measure: G1/G2 groups pushed the gate to 12390ms (was 3667); well under the 60s budget.
  # timeoutSec=60: 2 load-scaled QProcess children; headroom vs a ~2x-slower host (cf LA_LUMEN_SMOKE=45).
  # E1-group sub-assert: qaSetForceNoExecutor(true) -> fresh bridge parks its task as blocked_no_model (modelReady false, no process spawn); passes in both arms (rule 22).
  @{n="LA_ERASE_QA"; png=$false; timeoutSec=60; measuredMs=12390},
  # 2.35.0 opt-in real LaMa quality gate. need=LA_LAMA_MODEL (path to fetched lama.onnx):
  # absent -> qa_runner [SKIP] (non-regression, same as LA_SCAN_SMOKE/LA_RAW_QA). When the
  # env IS set but inpaint.exe is missing/resolves to the stub -> honest FAIL (rule 34).
  # Asserts M-A mask-out conservation (1px dilate tolerance), M-B plausible mask-in fill,
  # M-C idempotence. measuredMs=0 = never yet run opted-in (precedent: LA_SCAN_SMOKE
  # carries 0); E8 demands the KEY, not a guessed number. Backfill from -Timing the
  # first time the gate is actually run with a fetched model (rule 25/31: data, not a guess).
  @{n="LA_ERASE_MODEL_QA"; png=$false; need=@("LA_LAMA_MODEL"); measuredMs=18377; timeoutSec=90},
  # Design-language v2 ratchet gate (spec 2026-10-12 section 5). Static scan of
  # src/ui + src/core against qa/palette_baseline.json; five counts, ceiling only
  # descends. Script gate => FULL tier by rule E9d (fast script gates rejected).
  # Counter-example env LA_PALETTE_QA_FORCE_BROKEN=1 injects +1 hex and must go
  # RED. measuredMs=1984 from qa_runner -Timing full run (1984 matrix; 1987/1990/2072
  # standalone x3, rule 13). 2026-10-12 Task 12: F1 fail-open guard added to the script
  # (files-scanned=0 -> FAIL). Provenance: 1900 was the T1 measured value, not a
  # placeholder; the ~85ms delta is host noise (the scan predates the guard).
  @{n="LA_PALETTE_QA"; png=$false; kind="ps1"; script="qa_palette_ratchet.ps1"; measuredMs=1984},
  # 2.38.0 R1: one-tap enhancement wiring gate (13 LumenEngine explicit ops wired into
  # src/ui + event hookup in RetouchPage.cpp). FAST tier exe gate: rules E5/E9d bar ps1
  # gates from the fast layer, so the spec section 2.4 "fast tier" intent is delivered as
  # an exe static-scan gate instead of a script gate (plan-level ruling 1). F1 fail-open
  # guard: 0 files scanned -> FAIL. Counter-example LA_LUMEN_OPS_UI_QA_FORCE_BROKEN=1
  # injects a phantom op name (13/14 coverage) -> RED. measuredMs=155 backfilled in
  # R1-T4 from qa_runner -Timing (rule 25/31; E8 only requires the key to exist).
  # 155 <= the 500 ms E6 fast budget, so the FAST tier stands (plan-level ruling:
  # demote to full only if >500).
  @{n="LA_LUMEN_OPS_UI_QA"; png=$false; tier="fast"; measuredMs=155},
  # 2.38.0 R2: HEIC/WebP EXIF container extraction gate (full tier). Byte-exact synthetic
  # fixtures (WebP RIFF/EXIF x2 variants, minimal HEIC ftyp/meta/iinf/iloc/mdat) with fixed
  # expectations, plus always-on counter-examples: out-of-range box size and a self-
  # referential child box must not crash and must fall back to the file's own mtime date.
  # Real-sample arm is env-gated INSIDE the process (LA_HEIC_SAMPLE); the gate carries no
  # need= by design (plan-level ruling 2), so the synthetic part always runs. measuredMs=124
  # backfilled in R2-T2 from qa_runner -Timing (rules 25/31; E8 requires the key).
  @{n="LA_EXIF_CONTAINER_QA"; png=$false; measuredMs=124},
  # 2.40.0 round4 T1+T4a: AiProto frame-protocol contract gate (gate #75, LA_FACE_AI_QA).
  # Hermetic: faceinfer_stub.exe drives the full frame spectrum. T1 direct-QProcess arms:
  # G1 READY 1 handshake; G2 READY 0 then every REQ answered ERR <id> nomodel;
  # G4 RES JSON body streamed in 64-byte flushed fragments must be read fully by the
  # body loop (EraseProto short-read lesson); G5 ERR frames structured (token+id+code).
  # T4a adds the REAL AiSubprocessRunner arms (owner-thread QProcess base, all runner
  # entries called from worker threads because spec 2.2 forbids the GUI thread):
  # G3 full RES roundtrip equality through the runner (REQ det -> RES JSON byte-exact
  #   plus parsed deep-equal; body arrives via the one-shot FRAG hook = runner-side
  #   fragmented-stream read-full leg);
  # G5p parseReq whole-token regression (T1 review carry-over): in-app "REQX" rejected
  #   and "REQ" still accepted, wire REQX line answered ERR badframe by the stub, plus
  #   the sIsMainThread predicate (guard behind the GUI-thread refusal);
  # G6 HANG -> Timeout (crash-aware read bail, not atEnd()-only spinning) -> auto-restart
  #   once -> second call green with full body;
  # G7 concurrent second caller gets Busy inside the stub's 3s slow-frame window
  #   (single-flight invariant, spec 2.2: no queueing, no deadlock);
  # G8 all exe candidates missing -> NoExe (ensureReady and call agree, never spawns).
  # Counterexample LA_FACE_AI_QA_FORCE_BROKEN=1 corrupts ONLY the fixed expectation
  # table so ALL groups go RED together (rule 22: the stub branches on env, the
  # expectations never do; identical exit path).
  # Tier: FULL. The plan-level ruling was "fast per plan, demote to full only if >500"
  # (T1 skeleton measured 433ms); with the real runner arms live the measured cost is
  # dominated by G6's 1500ms timeout budget + G7's 3s stub sleep, far over the 500ms
  # E6 fast budget. LA_ERASE_QA (runner gate, full tier, measuredMs 12390) is the
  # isomorphic precedent; fail-safe rule "absent tier = full" applies. timeoutSec=30
  # kept (worst observed arm ~7s, plus cold-disk margin; self-exit timeout still INFRA-RED).
  # measuredMs=6992 from real qa_runner -Timing x5 (rule 31): 7000 (cold, right after
  # linking) / 6992 / 6923 / 6906 / 6911 ms; conservative max-of-warm recorded.
  @{n="LA_FACE_AI_QA"; png=$false; timeoutSec=30; measuredMs=6992},
  # 2.40.0 round4 T3: real face-model end-to-end quality gate (gate #78, LA_FACE_MODEL_QA).
  # Opt-in: need=@("LA_FACE_MODEL","LA_FACE_IMG") -- absent -> qa_runner [SKIP] (privacy
  # red line R-06: real face images and gold JSONs never enter the repo; the gate only
  # reads them from env paths, same SKIP semantics as the LA_ERASE_MODEL_QA row above).
  # When opted in, the app spawns the REAL faceinfer.exe (det+rec ORT sessions; no
  # runner yet -- thread lifecycle belongs to Task 4/#75 chain) and asserts the five
  # spec section 3 columns per fixture against its sibling <stem>.gold.json:
  # exact face count, per-face greedy 1:1 IoU >= 0.98, mean 5-point kps distance <= 2.0
  # source px, embedding cosine >= 0.999, |score delta| <= 0.02.
  # Env opted in but preconditions broken (exe missing / READY != 1 / gold missing)
  # -> honest FAIL, never silent pass (rule 34).
  # Counterexample LA_FACE_MODEL_QA_FORCE_BROKEN=1 corrupts ONLY the parsed expectation
  # table (bbox right edge +30% width, embedding[0] +10.0) so IoU and cosine columns
  # go RED together; the comparison flow and exit path never branch on the env (rule 22).
  # Tier: full. timeoutSec=120 covers cold ORT session load on slow hosts.
  # measuredMs=22464 from real qa_runner -Timing runs WITH env mounted (rule 25/31):
  # T3 21082/20772, T5 re-verify 22464 with the canonical 5-fixture set (adds one
  # 3-face image), conservative max recorded (app bootstrap dominates; the faceinfer
  # READY handshake itself measured 435-1954 ms). First green 2026-10-13 T3: 4 fixtures,
  # 48/48 faces PASS all five columns; T5 2026-10-14: 5 fixtures, 51/51 faces PASS.
  # Counterexample arm verified RED (exit 1, same exit path, expectation table corrupted
  # only). Fixture screening note (T5 R-11): faces whose cross-runtime det-kps noise
  # reaches ~0.15px push embedding cosine below 0.999 - kept-out-of-set boundary fixtures
  # are documented in faceinfer.cpp, not gate regressions.
  @{n="LA_FACE_MODEL_QA"; png=$false; need=@("LA_FACE_MODEL","LA_FACE_IMG"); measuredMs=22464; timeoutSec=120},
  # 2.40.0 round4 T7: CLIP-track AiProto contract gate (gate #76, LA_CLIP_AI_QA).
  # Hermetic: clipinfer_stub.exe drives the frame spectrum with kind="jpg" (the only
  # legal CLIP-track token). G1..G8 mirror gate #75 one-for-one:
  # G1 READY 1 handshake; G2 READY 0 then every REQ answered ERR <id> nomodel (keepalive);
  # G3 full RES roundtrip through the real AiSubprocessRunner (byte-exact + parsed
  #   deep-equal; body arrives via the one-shot FRAG hook);
  # G4 RES JSON body streamed in 64-byte flushed fragments must be read fully;
  # G5 ERR frames structured (token+id+code, id strictly matches the REQ);
  # G5p parseReq whole-token regression ("REQX" rejected, "REQ" accepted), wire REQX
  #   answered ERR badframe, plus the sIsMainThread predicate;
  # G6 HANG -> Timeout (1500ms budget) -> auto-restart once -> second call green;
  # G7 concurrent second caller gets Busy inside the stub's 3s slow-frame window;
  # G8 all exe candidates missing -> NoExe (probe and call agree, never spawns).
  # T7 adds two prompts-state legs:
  # G9 stub LA_CLIP_PROMPTS_BAD=1 -> READY 0 + every REQ ERR badprompts (badprompts
  #   outranks nomodel, same ordering as the real binary);
  # G10 REAL clipinfer.exe + throwaway temp model dir (1-byte junk clip_vit_b32_img.onnx
  #   + synthetic 16436-byte prompts.bin: magic/N/D all correct, matrix SHA deliberately
  #   wrong) -> the prompts-before-ORT startup order must answer READY 0 + ERR badprompts
  #   within seconds. The counterexample genuinely bites (rule 34): a startup-order
  #   regression to ORT-first would answer loadfail, and a missing prompts check would
  #   mislabel or hang on the 351MB tower -- both red here. Temp dir deleted after the leg.
  # Counterexample LA_CLIP_AI_QA_FORCE_BROKEN=1 corrupts ONLY the fixed expectation
  # table so ALL ten groups go RED together (rule 22; identical exit path).
  # Tier: FULL (same demotion note as #75): the plan ruling was "fast per plan, demote
  # to full only if >500"; live arms G6 (1500ms timeout) + G7 (3s stub sleep) + G10
  # (real-exe spawn with ORT dll load) put the measured cost far over the E6 500ms
  # fast budget. timeoutSec=30 kept per the T7 dispatch.
  # measuredMs=8019 from real qa_runner -Timing x4 (rules 25/31): 8019 / 7102 / 7149 /
  # 7011 ms; conservative max-of-warm recorded.
  @{n="LA_CLIP_AI_QA"; png=$false; timeoutSec=30; measuredMs=8019},
  # 2.40.0 round4 T7: real CLIP model end-to-end quality gate (gate #77, LA_CLIP_MODEL_QA).
  # Opt-in: need=@("LA_CLIP_MODEL","LA_CLIP_GOLD_DIR") -- absent -> qa_runner [SKIP]
  # (privacy red line R-06: real photos and gold JSONs never enter the repo; the gate
  # reads them only from env paths; fixture list comes from LA_CLIP_IMGS, comma-
  # separated files or directories, name-sorted).
  # When opted in, the app spawns the REAL clipinfer.exe (CLIP ViT-B-32 image tower
  # ORT session + verbatim classifier_server.py decision rules; no runner yet -- the
  # thread lifecycle belongs to the #75 chain) and asserts the five spec section 4
  # columns per fixture against <GOLD_DIR>/<full-basename>.gold.json (review I-3/T6-M3:
  # gold name INCLUDES the extension, matching the producer basename+".gold.json", so
  # a.png/a.JPG can never collide on one gold; the count cross-check below pins the
  # roster):
  # label verbatim equal, note verbatim equal (null aligned), |score delta| <= 0.005,
  # top3 per-pair label equal + |prob delta| <= 0.02, |sat delta| <= 1.0.
  # R-14b/R-55 adjudication: the sat path is the PIL default-bicubic (Catmull-Rom) fixed-point
  # kernel ported verbatim from libImaging Resample.c (the brief's "bilinear" was void;
  # the dispatch's "reducing_gap=3.0 prefilter" does not exist -- Pillow 12.3.0 resize
  # defaults reducing_gap=None, source-verified). Measured 13/13 fixtures green: worst sat
  # delta 0.1 (quantization last digit), worst score delta 0.0026 (IMG_9387 gold 0.8588
  # vs mine 0.8614; score delta nonzero on 5/13, the other 8 exact 0.0000), label/note
  # columns exact on every fixture -> the strict band is kept, not widened.
  # Cross-check (review I-4): the enumerated image count must equal the *.gold.json count
  # in GOLD_DIR; mismatch -> "fixture/gold count mismatch imgs=X golds=Y" red. This kills
  # the silent-fewer-samples path on the directory mount form (LA_CLIP_IMGS=<dir> verified
  # live, archived t7f1-m77-dirform.txt). PASS line prints imgs=N golds=N ok=N.
  # Env opted in but preconditions broken (exe missing / model dir missing / gold
  # missing / READY != 1) -> honest FAIL, never silent pass (rule 34).
  # Counterexample LA_CLIP_MODEL_QA_FORCE_BROKEN=1 corrupts ONLY the parsed expectation
  # table, two parallel tamper arms (rule 22/34, exit path identical):
  # (1) MONO_SAT_THRESHOLD=0.0 simulation -- sentinel IMG_8409 red on LABEL + NOTE (gold
  #     has none, mine gains low_saturation(3.8)) + SCORE_D=0.06650 (per T7 dispatch);
  # (2) OTHER_SCORE_THRESHOLD-never-fires simulation (review I-2, axis-12 criterion 3
  #     reverse proof) -- synthetic_lowconf gold expects (top1 class, top1 prob, note
  #     null); real shell still answers the low_confidence branch -> LABEL + NOTE red,
  #     SCORE_D=0.00000 by construction (same source prob: the red IS the branch).
  #     Measured (t7f1-m77-otheroff.txt): both fixtures red, 11/13 still ok=1, exit 1.
  # Low-confidence fixture exception (review I-5, recorded in clip-gold-margins.txt and
  # ai_export/gen_clip_synthetic_lowconf.py): the producer GAP_MIN=0.10 CANNOT be met by
  # any score<0.20 sample (8-class softmax: gap <= (8*top1-1)/7 < 0.0857), so the
  # low_confidence family takes a documented GAP_MIN exception but must still clear
  # 2*0.02=0.04 ordering margin -- the gate loudly asserts gold screen.gap_top1_top2
  # > 0.04 for every low_confidence gold (missing field or <=0.04 -> FAIL, no silent
  # order-flip fragility).
  # Tier: full. timeoutSec=120 covers cold ORT tower load on slow hosts (#78 isomorphic).
  # measuredMs=13787 from real qa_runner -Timing WITH env mounted, max-of-warm (rules
  # 25/31): T7f1 wave x3 = 13787 / 13252 / 13457 ms (file-list form x2 + directory form,
  # supersedes T7 x3 = 10877 / 13723 / 11197; ready_ms swings 528..3105 with ORT tower
  # page-cache state, which is gate-intrinsic cost, so the conservative max is recorded).
  @{n="LA_CLIP_MODEL_QA"; png=$false; need=@("LA_CLIP_MODEL","LA_CLIP_GOLD_DIR"); measuredMs=13787; timeoutSec=120}
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

# E11 - ENCODING. PS 5.1 decodes a no-BOM script with the host ANSI codepage (GBK on a
# zh-CN host), so a UTF-8 CJK comment can consume the ASCII delimiter that follows it and
# a table entry VANISHES WITHOUT ANY PARSE ERROR. That is exactly how 12 media gates
# (LA_RAW_QA / LA_VIDEO_* / LA_AUDIO_QA / LA_EDIT_RAW_QA / LA_CULL_EXEMPT_QA /
# LA_CULL_VIDEO_QA / LA_GEO_LINK_QA / LA_MAP_PERF) stopped running while the file still
# looked complete and every other assertion stayed green. The source must be pure ASCII.
$bytes = [System.IO.File]::ReadAllBytes($PSCommandPath)
$hi = 0
foreach ($b in $bytes) { if ($b -gt 127) { $hi++ } }
Chk "E11" ($hi -eq 0) ("nonAsciiBytes=" + $hi)

# E12 - the SYMPTOM, caught independently of the cause: count the entry literals in the
# GateTable source text and require the parsed table to hold exactly that many. Any silent
# loss (encoding, or a stray bracket ending the array early) shows up here even with E11
# clean, so the roster can never again be smaller than the file claims.
$text = [System.Text.Encoding]::ASCII.GetString($bytes)
$start = $text.IndexOf('$GateTable = @(')
$lit = ""
if ($start -ge 0) {
  $lit = $text.Substring($start)
  $end = $lit.IndexOf("`n)")
  if ($end -ge 0) { $lit = $lit.Substring(0, $end) }
}
$txtN = ([regex]::Matches($lit, '@\{n=')).Count
Chk "E12" (($start -ge 0) -and ($txtN -eq $all.Count)) ("textual=" + $txtN + " parsed=" + $all.Count)

Write-Host ("LA_GATE_MATRIX_QA PASS gates={0} fast={1} full={2} budget={3}ms core={4}" -f `
            $all.Count, $fast.Count, $full.Count, $fastBudgetMs, $coreFastGates.Count)
if ($fails.Count -gt 0) {
  Write-Host ("LA_GATE_MATRIX_QA FAIL fails={0}" -f $fails.Count)
  exit 1
}
Write-Host ("LA_GATE_MATRIX_QA PASS fails=0")
exit 0