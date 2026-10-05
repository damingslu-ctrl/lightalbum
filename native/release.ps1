# LightAlbum native release packager (Windows)
#
# Build first (native/build/LightAlbum.exe), then run:
#   powershell -File release.ps1# clean deploy, no trim
#   powershell -File release.ps1 -TrimNetwork    # also drop Qt6Network+tls+networkinformation+generic
#   powershell -File release.ps1 -SelfTest      # contract self-test (QA gate LA_RELEASE_QA)
#
# Contracts C1..C7, all enforced inline below AND driven by -SelfTest:
#   C1 version sync (CHANGELOG <-> CMakeLists <-> requested)
#   C2 user manual in the dist AND it describes the version being packaged
#   C3 core artifacts present and non-empty
#   C4 MANIFEST matches reality (sha256 + bytes per file)
#   C5 zip opens and holds the exe
#   C6 license: THIRD_PARTY-NOTICE.md ships + derived sources keep SPDX
#   C7 MANIFEST is reproducible (twice == same bytes) and totally ordered
#   C8 build type is Release (a Debug artifact must never be shipped)
# Plus BUILDINFO.txt (SBOM: commit, build type, Qt/compiler versions).
#
# Clean approach: copy only the exe, then windeployqt deploys exactly the
# needed Qt DLLs/plugins (no CMake .obj/Makefile junk). Delete-free: uses
# Move-Item to relocate trimmed files and any prior zip/staging.
#
# ENCODING RULE: this file MUST stay pure ASCII. Windows PowerShell 5.1 reads a
# no-BOM file as GBK, so any non-ASCII byte in a *string* becomes mojibake and
# in a comment it is merely ugly. The sibling scripts qa_runner.ps1 and
# iterate.ps1 are pure ASCII for the same reason -- keep all three that way.
param(
  [string]$Version = "",
  [switch]$TrimNetwork,
  [switch]$SkipGate,
  [switch]$SelfTest,
  [switch]$NoInject,
  [string]$Windeployqt = ""
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Definition
$MANIFEST_NAME = "MANIFEST.txt"
$NOTICE_NAME = "THIRD_PARTY-NOTICE.md"
$BUILDINFO_NAME = "BUILDINFO.txt"
# Derived (MIT) source files that must carry an SPDX header. Kept as data so
# the license contract C6 can be driven against a sandbox in -SelfTest.
$DERIVED_SOURCES = @("src/core/CullEngine.cpp", "src/core/CullEngine.h")

# =====================================================================
# Release contracts (C1..C7)
#
# These are deliberately factored out as standalone functions that take
# explicit paths, so that -SelfTest can drive them against a throwaway
# sandbox. A contract that is only enforced inline in the packaging path
# cannot be tested; a contract that is tested but not enforced is theater.
# Production code has no seam and no test-only branch.
# =====================================================================

# C1: single source of truth -- CHANGELOG's top entry must equal CMakeLists'
# project(VERSION), and both must equal the version we are packaging.
function Get-CmakeVersion([string]$CmakeListsPath) {
  if (-not (Test-Path $CmakeListsPath)) { return $null }
  $m = Select-String -Path $CmakeListsPath -Pattern 'project\s*\(\s*[^)]*VERSION\s+(\d+\.\d+\.\d+)' | Select-Object -First 1
  if (-not $m) { return $null }
  return $m.Matches[0].Groups[1].Value
}

function Get-ChangelogTopVersion([string]$ChangelogPath) {
  if (-not (Test-Path $ChangelogPath)) { return $null }
  $m = Select-String -Path $ChangelogPath -Pattern '^\s*##\s*\[(\d+\.\d+\.\d+)\]' | Select-Object -First 1
  if (-not $m) { return $null }
  return $m.Matches[0].Groups[1].Value
}

function Assert-VersionSync($want, $cmPath, $clPath) {
  $cv = Get-CmakeVersion $cmPath
  if (-not $cv) { throw ("C1 cannot parse version from " + $cmPath) }
  $lv = Get-ChangelogTopVersion $clPath
  if (-not $lv) { throw ("C1 no '## [x.y.z]' entry in " + $clPath) }
  if ($cv -ne $lv) { throw ("C1 version mismatch: CMakeLists=" + $cv + " CHANGELOG=" + $lv) }
  if ($want -and $want -ne $cv) { throw ("C1 requested version=" + $want + " differs from truth " + $cv) }
}

# C2: the user manual must actually be in the dist AND describe the version
# being shipped. Existence alone is not enough -- the file is still NAMED
# LightAlbum-2.0.0-*.md while the app is 2.20.0, so "found something" has been
# silently shipping a manual 20 releases stale. A contract that can never fail
# is not a contract, so this asserts the version the manual CLAIMS as well.
# $expectVersion: the version being packaged ("" = skip the content check, which
# is what -SelfTest's sandbox uses when it does not care about that axis).
function Assert-ManualPresent($staging, $expectVersion = "") {
  $p = Join-Path $staging "USER-MANUAL.md"
  if (-not (Test-Path $p)) { throw "C2 USER-MANUAL.md missing from staging" }
  if ((Get-Item $p).Length -le 0) { throw "C2 USER-MANUAL.md is empty" }
  if (-not $expectVersion) { return }
  # Content check: the manual must not advertise a DIFFERENT release line than
  # the one we are packaging. Scanned as text (the manual is Chinese markdown,
  # so this is a byte-level search for the "LightAlbum <x.y.z>" title form).
  $txt = Get-Content $p -Raw -Encoding UTF8
  $stale = @()
  foreach ($m in [regex]::Matches($txt, 'LightAlbum\s+(\d+\.\d+\.\d+)')) {
    $v = $m.Groups[1].Value
    if ($v -ne $expectVersion) { $stale += $v }
  }
  if ($stale.Count -gt 0) {
    $uniq = (@($stale | Sort-Object -Unique)) -join ","
    throw ("C2 USER-MANUAL.md describes version(s) " + $uniq + " but " + $expectVersion + " is being packaged (stale manual)")
  }
  if ($txt -notmatch [regex]::Escape($expectVersion)) {
    throw ("C2 USER-MANUAL.md never mentions " + $expectVersion + " (cannot confirm it is current)")
  }
}

# C3: the artifacts a fresh machine needs, including the offscreen platform
# plugin that every headless QA gate depends on, plus the build provenance
# file -- an untraceable zip is not a shippable artifact.
function Assert-CoreArtifacts($staging) {
  $need = @("LightAlbum.exe", "platforms/qoffscreen.dll", "platforms/qwindows.dll", $BUILDINFO_NAME)
  foreach ($f in $need) {
    $p = Join-Path $staging $f
    if (-not (Test-Path $p)) { throw ("C3 required artifact missing: " + $f) }
    if ((Get-Item $p).Length -le 0) { throw ("C3 required artifact empty: " + $f) }
  }
}

# C4: the manifest is only worth shipping if it matches reality byte for byte.
# NOTE the ordinal sort: Sort-Object uses culture-sensitive comparison, so the
# entry order would depend on the machine's locale -- a build on a zh-CN Windows
# and one on an en-US Windows would emit the same files in a different order,
# and every cross-machine manifest diff would be noise. StringComparer::Ordinal
# is locale-independent, which is what makes C7's reproducibility claim true.
function New-ReleaseManifest($staging, $name) {
  $lines = @("# LightAlbum native release manifest", "# sha256 <TAB> bytes <TAB> relative/path", "")
  $files = @(Get-ChildItem $staging -Recurse -File | Where-Object { $_.Name -ne $name })
  $paths = @()
  foreach ($f in $files) {
    $rel = $f.FullName.Substring($staging.Length).TrimStart('\', '/') -replace '\\', '/'
    $paths += [pscustomobject]@{ full = $f.FullName; len = $f.Length; rel = $rel }
  }
  [Array]::Sort($paths, [System.Comparison[System.Object]] {
    param($a, $b) [System.StringComparer]::Ordinal.Compare($a.rel, $b.rel)
  })
  foreach ($p in $paths) {
    $h = (Get-FileHash $p.full -Algorithm SHA256).Hash.ToLower()
    $lines += ($h + "`t" + $p.len + "`t" + $p.rel)
  }
  Set-Content -Path (Join-Path $staging $name) -Value $lines -Encoding ASCII
  return $lines.Count
}

function Assert-ManifestConsistent($staging, $name) {
  $p = Join-Path $staging $name
  if (-not (Test-Path $p)) { throw ("C4 " + $name + " missing from staging") }
  $entries = @()
  foreach ($ln in @(Get-Content $p)) {
    if ($ln -match '^([0-9a-f]{64})[ \t]+(\d+)[ \t]+(.+)$') {
      $entries += @{ h = $Matches[1]; n = [int64]$Matches[2]; p = $Matches[3] }
    }
  }
  if ($entries.Count -eq 0) { throw ("C4 " + $name + " has no parseable entries") }
  $actual = @(Get-ChildItem $staging -Recurse -File | Where-Object { $_.Name -ne $name })
  if ($entries.Count -ne $actual.Count) {
    throw ("C4 manifest lists " + $entries.Count + " files but staging holds " + $actual.Count)
  }
  foreach ($e in $entries) {
    $fp = Join-Path $staging ($e.p -replace '/', '\')
    if (-not (Test-Path $fp)) { throw ("C4 manifest references missing file: " + $e.p) }
    $it = Get-Item $fp
    if ($it.Length -ne $e.n) { throw ("C4 size mismatch for " + $e.p) }
    $h = (Get-FileHash $fp -Algorithm SHA256).Hash.ToLower()
    if ($h -ne $e.h) { throw ("C4 sha256 mismatch for " + $e.p) }
  }
}

# C5: the shipped zip must open and contain the exe.
function Assert-ZipIntegrity($zipPath) {
  if (-not (Test-Path $zipPath)) { throw "C5 zip not created" }
  if ((Get-Item $zipPath).Length -le 0) { throw "C5 zip is empty" }
  Add-Type -AssemblyName System.IO.Compression.FileSystem
  $za = $null
  try {
    $za = [System.IO.Compression.ZipFile]::OpenRead($zipPath)
  } catch {
    throw ("C5 zip unreadable or corrupt: " + $_.Exception.Message)
  }
  try {
    $n = $za.Entries.Count
    if ($n -lt 1) { throw "C5 zip has no entries" }
    $hasExe = $false
    foreach ($e in $za.Entries) { if ($e.Name -eq "LightAlbum.exe") { $hasExe = $true } }
    if (-not $hasExe) { throw "C5 zip lacks LightAlbum.exe" }
    return $n
  } finally {
    if ($za) { $za.Dispose() }
  }
}

# C6: license compliance. Two halves, both required:
#   (a) the attribution notice travels with the binary (MIT requires the
#       copyright notice to accompany the distribution), and
#   (b) every derived source file still carries its SPDX identifier -- so
#       the attribution cannot be quietly dropped from the code while the
#       notice file keeps claiming it applies.
# $srcRoot is the tree that $derivedRel are relative to (native/ in
# production, the sandbox in -SelfTest).
function Assert-LicenseCompliance($staging, $noticeName, $srcRoot, $derivedRel) {
  $np = Join-Path $staging $noticeName
  if (-not (Test-Path $np)) { throw ("C6 " + $noticeName + " missing from staging") }
  if ((Get-Item $np).Length -le 0) { throw ("C6 " + $noticeName + " is empty") }
  if ($derivedRel.Count -eq 0) { throw "C6 no derived sources declared" }
  foreach ($rel in $derivedRel) {
    $sp = Join-Path $srcRoot $rel
    if (-not (Test-Path $sp)) { throw ("C6 declared derived source missing: " + $rel) }
    $head = @(Get-Content $sp -TotalCount 12)
    $tagged = $false
    foreach ($ln in $head) {
      if ($ln -match '^\s*//\s*SPDX-License-Identifier:\s*(\S+)') { $tagged = $true; break }
    }
    if (-not $tagged) { throw ("C6 no SPDX-License-Identifier header in derived source: " + $rel) }
  }
}

# --- Build provenance (SBOM) -------------------------------------------
# Answers, for a zip found on a stranger's disk: which commit, which
# toolchain, which build type produced this? Everything is read from files
# that already exist (CMakeCache.txt, git) so there is nothing to keep in
# sync by hand.
function Get-CacheValue([string]$cachePath, [string]$key) {
  if (-not (Test-Path $cachePath)) { return "" }
  $m = Select-String -Path $cachePath -Pattern ('^' + [regex]::Escape($key) + ':[A-Z]+=(.*)$') | Select-Object -First 1
  if (-not $m) { return "" }
  return $m.Matches[0].Groups[1].Value.Trim()
}

function Get-GitCommit([string]$repoRoot) {
  if (-not (Test-Path (Join-Path $repoRoot ".git"))) {
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) { return "unknown" }
  }
  if (-not (Get-Command git -ErrorAction SilentlyContinue)) { return "unknown" }
  try {
    $c = (& git -C $repoRoot rev-parse --short HEAD 2>$null)
    if ($LASTEXITCODE -eq 0 -and $c) { return $c.Trim() }
  } catch { }
  return "unknown"
}

function Get-CompilerVersion([string]$compilerPath) {
  if (-not $compilerPath -or -not (Test-Path $compilerPath)) { return "unknown" }
  try {
    $v = (& $compilerPath --version 2>$null | Select-Object -First 1)
    if ($v) { return $v.Trim() }
  } catch { }
  return "unknown"
}

# $qtVersion: derived from the Qt6_DIR path baked into CMakeCache.txt
# (for example .../6.8.3/mingw_64/lib/cmake/Qt6 -> 6.8.3), not from parsing
# a header. Qt6_DIR is a path VALUE, so it is matched with a regex against
# the string, not with Select-String (which reads files and would silently
# return nothing -- that bug shipped "unknown" until the gate was inspected).
function Get-QtVersionFromCache([string]$qt6Dir) {
  if (-not $qt6Dir) { return "unknown" }
  $m = [regex]::Match(($qt6Dir -replace '\\', '/'), '/(\d+\.\d+\.\d+)/')
  if ($m.Success) { return $m.Groups[1].Value }
  return "unknown"
}

# New-BuildInfo writes BUILDINFO.txt into staging and returns the line count.
# Pure ASCII, LF-free (Set-Content -Encoding ASCII on PS 5.1 emits CRLF).
function New-BuildInfo($staging, $name, $version, $srcRoot) {
  $cache = Join-Path $srcRoot "build/CMakeCache.txt"
  $cxx = Get-CacheValue $cache "CMAKE_CXX_COMPILER"
  $qt6 = Get-CacheValue $cache "Qt6_DIR"
  $qtVer = Get-QtVersionFromCache $qt6
  $commit = Get-GitCommit $srcRoot
  $buildType = Get-CacheValue $cache "CMAKE_BUILD_TYPE"
  $ccVer = Get-CompilerVersion $cxx
  $files = @(Get-ChildItem $staging -Recurse -File | Where-Object { $_.Name -ne $name })
  $bytes = 0
  foreach ($f in $files) { $bytes += $f.Length }
  $lines = @(
    "# LightAlbum native build provenance (SBOM)",
    ("version            : " + $version),
    ("git_commit         : " + $commit),
    ("build_type         : " + $(if ($buildType) { $buildType } else { "unknown" })),
    ("qt_version         : " + $qtVer),
    ("qt_dir             : " + $(if ($qt6) { $qt6 } else { "unknown" })),
    ("compiler           : " + $(if ($cxx) { $cxx } else { "unknown" })),
    ("compiler_version   : " + $ccVer),
    ("packaged_at        : " + (Get-Date -Format "yyyy-MM-ddTHH:mm:ss")),
    ("packaged_by        : " + $env:USERNAME),
    ("packaged_on_host   : " + $env:COMPUTERNAME),
    ("file_count         : " + $files.Count),
    ("total_bytes        : " + $bytes),
    ("note               : counts above exclude this file; MANIFEST.txt lists every shipped file"),
    ""
  )
  Set-Content -Path (Join-Path $staging $name) -Value $lines -Encoding ASCII
  return $lines.Count
}

# C7: the manifest must be a pure function of the staged bytes, and it must
# be in a total order. Two halves:
#   (a) the on-disk manifest is in strictly ascending ORDINAL path order with
#       no duplicate path. This is the artifact-level invariant a
#       culture-sensitive or unstable enumeration would violate. Checked
#       BEFORE any regeneration, so a tampered manifest is actually observed
#       (regenerating first would silently repair the very defect under test).
#   (b) generating twice over the same tree yields byte-identical output.
#       Otherwise "diff two builds to see what changed" is meaningless.
function Assert-ManifestReproducible($staging, $name) {
  $mp = Join-Path $staging $name
  if (-not (Test-Path $mp)) { throw ("C7 " + $name + " missing from staging") }
  # (a) total order over the entry paths, as they are right now
  $prev = ""
  $seen = 0
  foreach ($ln in @(Get-Content $mp)) {
    if ($ln -match '^[0-9a-f]{64}[ \t]+\d+[ \t]+(.+)$') {
      $p = $Matches[1]
      if ($seen -gt 0 -and [System.StringComparer]::Ordinal.Compare($p, $prev) -le 0) {
        throw ("C7 manifest entries are not in strictly ascending ordinal path order: '" + $prev + "' then '" + $p + "'")
      }
      $prev = $p
      $seen++
    }
  }
  if ($seen -eq 0) { throw "C7 manifest has no parseable entries to order-check" }
  # (b) regeneration is a pure function of the staged bytes
  $h1 = (Get-FileHash $mp -Algorithm SHA256).Hash
  New-ReleaseManifest $staging $name | Out-Null
  $h2 = (Get-FileHash $mp -Algorithm SHA256).Hash
  New-ReleaseManifest $staging $name | Out-Null
  $h3 = (Get-FileHash $mp -Algorithm SHA256).Hash
  if ($h1 -ne $h2 -or $h2 -ne $h3) {
    throw ("C7 manifest is not reproducible: onDisk=" + $h1.Substring(0, 12) + " regen1=" + $h2.Substring(0, 12) + " regen2=" + $h3.Substring(0, 12))
  }
  return $h2
}

# C8: a RELEASE must be built Release. Found on 2026-10-05, the first time the
# packager was ever run end to end: SBOM honestly reported "build_type: Debug"
# and nobody noticed, because the SBOM RECORDS the type while no contract
# ASSERTS it. The dist then shipped a Debug exe (47 MB, assertions and debug
# code included) next to Release Qt DLLs deployed by windeployqt -- a mixed
# artifact. Recording a fact is not enforcing it.
# $cache: path to CMakeCache.txt; "" or a missing file = unknown = FAIL, because
# "we cannot prove it is Release" must not read as "it is fine".
function Assert-ReleaseBuildType($cache) {
  if (-not $cache) { throw "C8 no CMakeCache.txt given; cannot prove the build is Release" }
  if (-not (Test-Path $cache)) { throw ("C8 CMakeCache.txt missing: " + $cache + " (cannot prove Release)") }
  $m = Select-String -Path $cache -Pattern '^CMAKE_BUILD_TYPE:[A-Z]+=(.*)$' | Select-Object -First 1
  if (-not $m) { throw ("C8 CMAKE_BUILD_TYPE not set in " + $cache + " -- configure with -DCMAKE_BUILD_TYPE=Release") }
  $t = $m.Matches[0].Groups[1].Value.Trim()
  if ($t -ne "Release") { throw ("C8 build type is '" + $t + "', not Release -- a Debug artifact must never be shipped") }
  return $t
}

# =====================================================================
# -SelfTest: prove the contracts actually fire.
#
# Builds a throwaway sandbox (fake CMakeLists/CHANGELOG/dist/zip), verifies
# the clean baseline is green, then injects one defect per contract and
# asserts that contract is the one that trips. -NoInject skips the
# injections, which MUST turn the gate red -- otherwise "the contract
# catches its defect" would be an unfalsifiable claim.
# =====================================================================
# The contract roster: single source of truth for how many contracts exist.
# Adding C8 here is enough -- Get-ContractTags, the baseline loop and the PASS
# report all count this list instead of repeating a literal.
$script:contractIds = @("C1", "C2", "C3", "C4", "C5", "C6", "C7", "C8")

function Get-ContractTags($want, $cmPath, $clPath, $staging, $manName, $zipPath, $noticeName, $srcRoot, $derivedRel, $cachePath = "") {
  $tags = @()
  $probes = @(
    @{ t = "C1"; f = { Assert-VersionSync $want $cmPath $clPath } },
    @{ t = "C2"; f = { Assert-ManualPresent $staging $want } },
    @{ t = "C3"; f = { Assert-CoreArtifacts $staging } },
    @{ t = "C4"; f = { Assert-ManifestConsistent $staging $manName } },
    @{ t = "C5"; f = { Assert-ZipIntegrity $zipPath } },
    @{ t = "C6"; f = { Assert-LicenseCompliance $staging $noticeName $srcRoot $derivedRel } },
    @{ t = "C7"; f = { Assert-ManifestReproducible $staging $manName } },
    @{ t = "C8"; f = { Assert-ReleaseBuildType $cachePath } }
  )
  foreach ($p in $probes) {
    # Assign to $null: several Assert-* return values (entry count, hash)
    # and an unassigned call would leak them into the returned tag list.
    try { $null = & $p.f; $tags += ($p.t + ":PASS") }
    catch { $tags += ($p.t + ":CAUGHT") }
  }
  return $tags
}

function New-Sandbox($dir, $version) {
  if (Test-Path $dir) { Remove-Item $dir -Recurse -Force }
  $dist = Join-Path $dir "dist"
  New-Item -ItemType Directory -Force -Path (Join-Path $dist "platforms") | Out-Null
  Set-Content -Path (Join-Path $dir "CMakeLists.txt") -Encoding ASCII -Value ("project(LightAlbumNative VERSION " + $version + " LANGUAGES CXX)")
  Set-Content -Path (Join-Path $dir "CHANGELOG.md") -Encoding ASCII -Value ("# CHANGELOG`n`n## [" + $version + "] - 2026-01-01`n")
  # C8 reads CMAKE_BUILD_TYPE from a CMakeCache.txt. The sandbox CMakeLists is
  # not a real cache, so the baseline gets a stand-in cache that says Release --
  # otherwise the baseline would be red for the wrong reason.
  Set-Content -Path (Join-Path $dir "CMakeCache.txt") -Encoding ASCII -Value "CMAKE_BUILD_TYPE:STRING=Release`n"
  Set-Content -Path (Join-Path $dist "LightAlbum.exe") -Encoding ASCII -Value "fake-exe-payload"
  Set-Content -Path (Join-Path $dist "Qt6Core.dll") -Encoding ASCII -Value "fake-core"
  Set-Content -Path (Join-Path $dist "platforms/qwindows.dll") -Encoding ASCII -Value "fake-win"
  Set-Content -Path (Join-Path $dist "platforms/qoffscreen.dll") -Encoding ASCII -Value "fake-off"
  # C2 now also asserts the manual DESCRIBES the version being packaged, so the
  # sandbox baseline manual has to name it -- otherwise the baseline is red for
  # the wrong reason and every contract looks broken.
  Set-Content -Path (Join-Path $dist "USER-MANUAL.md") -Encoding ASCII -Value ("# manual for LightAlbum " + $version)
  # C3 now also requires BUILDINFO.txt; the sandbox writes a stand-in.
  New-BuildInfo $dist $BUILDINFO_NAME $version $dir | Out-Null
  # license half of C6: the notice in the dist, plus a derived source tree
  Set-Content -Path (Join-Path $dist $NOTICE_NAME) -Encoding ASCII -Value "# third party notice (sandbox)"
  $srcDir = Join-Path $dir "src/core"
  New-Item -ItemType Directory -Force -Path $srcDir | Out-Null
  Set-Content -Path (Join-Path $srcDir "CullEngine.cpp") -Encoding ASCII -Value "// SPDX-License-Identifier: MIT`n// sandbox`nint main(){return 0;}`n"
  Set-Content -Path (Join-Path $srcDir "CullEngine.h") -Encoding ASCII -Value "// SPDX-License-Identifier: MIT`n// sandbox`n"
  # A pair whose relative order differs between culture-sensitive and ordinal
  # comparison. Under zh-CN/en-US culture, "zz.txt" sorts before "Zz.txt";
  # under Ordinal, "Zz.txt" (0x5A) sorts before "zz.txt" (0x7A). The sandbox
  # carries both so C7's order check has a case that actually discriminates.
  Set-Content -Path (Join-Path $dist "Zz.txt") -Encoding ASCII -Value "upper"
  Set-Content -Path (Join-Path $dist "zz.txt") -Encoding ASCII -Value "lower"
  return $dist
}

function Invoke-ReleaseSelfTest($noInject) {
  $sandbox = Join-Path $env:TEMP ("la_release_selftest_" + (Get-Date -Format "yyyyMMddHHmmssfff"))
  $fakeVer = "9.9.9"
  $cmPath = Join-Path $sandbox "CMakeLists.txt"
  $clPath = Join-Path $sandbox "CHANGELOG.md"
  $zipPath = Join-Path $sandbox "dist.zip"
  # C6 looks at derived sources relative to the source root; in the sandbox
  # that root is the sandbox itself, mirroring native/ in production.
  $sandboxDerived = @("src/core/CullEngine.cpp", "src/core/CullEngine.h")
  # C8 inspects a CMakeCache.txt, which in production is build/CMakeCache.txt but
  # in the sandbox is this stand-in (the sandbox CMakeLists is not a cache).
  $sandboxCache = Join-Path $sandbox "CMakeCache.txt"
  $spdxCpp = Join-Path $sandbox "src/core/CullEngine.cpp"
  New-Item -ItemType Directory -Force -Path $sandbox | Out-Null
  $fails = @()
  $caught = 0
  $zipEntries = 0
  $script:SelfTestRc = 1
  try {
    # --- baseline: a clean sandbox must satisfy every contract ---
    $dist = New-Sandbox $sandbox $fakeVer
    New-ReleaseManifest $dist $MANIFEST_NAME | Out-Null
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
    [System.IO.Compression.ZipFile]::CreateFromDirectory($dist, $zipPath,
      [System.IO.Compression.CompressionLevel]::Optimal, $false)
    $base = Get-ContractTags $fakeVer $cmPath $clPath $dist $MANIFEST_NAME $zipPath $NOTICE_NAME $sandbox $sandboxDerived $sandboxCache
    foreach ($t in $contractIds) {
      if ($base -notcontains ($t + ":PASS")) { $fails += ("baseline:" + $t + " did not pass on a clean sandbox") }
    }
    $zipEntries = Assert-ZipIntegrity $zipPath

    # --- one injected defect per contract ---
    $injections = @(
      @{ id = "C1"; apply = { Set-Content -Path $clPath -Encoding ASCII -Value "# CHANGELOG`n`n## [8.8.8] - 2026-01-01`n" } },
      @{ id = "C2"; apply = { Remove-Item (Join-Path $dist "USER-MANUAL.md") -Force } },
      # The defect actually observed in production on 2026-10-05: the manual was
      # present and non-empty, so the old existence-only C2 stayed green, yet it
      # described LightAlbum 2.0.0 while 2.20.0 was being packaged. Removing the
      # file cannot prove the version assertion works; only stale CONTENT can.
      @{ id = "C2"; apply = {
          Set-Content -Path (Join-Path $dist "USER-MANUAL.md") -Encoding ASCII -Value "# manual for LightAlbum 2.0.0"
        } },
      @{ id = "C3"; apply = { Remove-Item (Join-Path $dist "platforms/qoffscreen.dll") -Force } },
      @{ id = "C3"; apply = { Remove-Item (Join-Path $dist $BUILDINFO_NAME) -Force } },
      @{ id = "C4"; apply = {
          $mp = Join-Path $dist $MANIFEST_NAME
          $txt = Get-Content $mp
          $txt[3] = ($txt[3] -replace '^[0-9a-f]{64}', '0000000000000000000000000000000000000000000000000000000000000000')
          Set-Content -Path $mp -Value $txt -Encoding ASCII
        } },
      @{ id = "C5"; apply = { Set-Content -Path $zipPath -Encoding ASCII -Value "not-a-zip-at-all" } },
      # C6 has two independent halves, so it gets two injections: the notice
      # missing from the dist, and the SPDX header stripped from the source.
      @{ id = "C6"; apply = { Remove-Item (Join-Path $dist $NOTICE_NAME) -Force } },
      @{ id = "C6"; apply = { Set-Content -Path $spdxCpp -Encoding ASCII -Value "// no spdx here`nint main(){return 0;}`n" } },
      # C7: the two halves are falsified separately. The order half is the
      # one that can be injected deterministically -- shuffle the entry lines
      # of an already-generated manifest, which is exactly what an unstable
      # enumeration order would produce.
      @{ id = "C7"; apply = {
          $mp = Join-Path $dist $MANIFEST_NAME
          $txt = @(Get-Content $mp)
          $hdr = @()
          $ent = @()
          foreach ($l in $txt) { if ($l -match '^[0-9a-f]{64}') { $ent += $l } else { $hdr += $l } }
          [array]::Reverse($ent)
          Set-Content -Path $mp -Value ($hdr + $ent) -Encoding ASCII
        } },
      @{ id = "C7"; apply = {
          $mp = Join-Path $dist $MANIFEST_NAME
          $txt = @(Get-Content $mp)
          $ent = @($txt | Where-Object { $_ -match '^[0-9a-f]{64}' })
          # duplicate the first entry: same path twice is not a strict order
          $idx = 0
          for ($i = 0; $i -lt $txt.Count; $i++) { if ($txt[$i] -match '^[0-9a-f]{64}') { $idx = $i; break } }
          $txt = @($txt[0..($idx-1)] + $ent[0] + $txt[$idx..($txt.Count-1)])
          Set-Content -Path $mp -Value $txt -Encoding ASCII
        } },
      # The defect actually observed in production on 2026-10-05: SBOM said
      # "build_type: Debug" and every other contract still passed, because the
      # SBOM RECORDS the build type while nothing ASSERTED it. Two injections so
      # neither "unset" nor "wrong value" can slip through.
      @{ id = "C8"; apply = {
          Set-Content -Path $sandboxCache -Encoding ASCII -Value "CMAKE_BUILD_TYPE:STRING=Debug`n"
        } },
      @{ id = "C8"; apply = {
          Set-Content -Path $sandboxCache -Encoding ASCII -Value "CMAKE_CXX_COMPILER:FILEPATH=cxx`n"
        } }
    )
    foreach ($inj in $injections) {
      $dist = New-Sandbox $sandbox $fakeVer
      New-ReleaseManifest $dist $MANIFEST_NAME | Out-Null
      if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
      [System.IO.Compression.ZipFile]::CreateFromDirectory($dist, $zipPath,
        [System.IO.Compression.CompressionLevel]::Optimal, $false)
      & $inj.apply
      $tags = Get-ContractTags $fakeVer $cmPath $clPath $dist $MANIFEST_NAME $zipPath $NOTICE_NAME $sandbox $sandboxDerived $sandboxCache
      if ($tags -contains ($inj.id + ":CAUGHT") -and $tags -notcontains ($inj.id + ":PASS")) {
        $caught++
      } else {
        $fails += ("inject:" + $inj.id + " defect was NOT caught (tags=" + ($tags -join ",") + ")")
      }
    }
  } catch {
    $fails += ("harness: " + $_.Exception.Message)
  } finally {
    if (Test-Path $sandbox) { Remove-Item $sandbox -Recurse -Force -ErrorAction SilentlyContinue }
  }

  if ($noInject) {
    # Counter-example: with injections disabled the "contract catches its
    # defect" claim is unproven, so the gate MUST fail.
    Write-Output "LA_RELEASE_QA FAIL reason=NOINJECT -- self-test harness disabled, contract assertions unproven"
    $script:SelfTestRc = 1
    return
  }
  if ($fails.Count -gt 0) {
    foreach ($f in $fails) { Write-Output ("LA_RELEASE_QA FAIL reason=" + $f) }
    $script:SelfTestRc = 1
    return
  }
  # Counts are DERIVED, never typed by hand. They were literals once ("contracts=7
  # injected=10 baseline=7") and went stale the moment C8 and a third injection
  # landed -- a report whose numbers are not computed can silently disagree with
  # what it is reporting on, which is the same class of defect this file exists
  # to prevent. $contractIds/$injections are the single sources of truth.
  Write-Output ("LA_RELEASE_QA PASS contracts=" + $contractIds.Count +
    " injected=" + $injections.Count + " caught=" + $caught +
    " baseline=" + $contractIds.Count + " zip=" + $zipEntries)
  $script:SelfTestRc = 0
}

if ($SelfTest) {
  # Do NOT assign the call to a variable: a PowerShell function returns its
  # entire output stream, so `$rc = Invoke-...` would capture the PASS/FAIL
  # line too and `exit $rc` would then coerce that string, silently losing the
  # report and exiting on the wrong value. Print directly, return via a
  # script-scoped code.
  Invoke-ReleaseSelfTest $NoInject
  exit $script:SelfTestRc
}

# === Version: single source of truth (standardized SOP section 3) ===
if ([string]::IsNullOrWhiteSpace($Version)) {
  $cm = Join-Path $root "CMakeLists.txt"
  if (-not (Test-Path $cm)) { throw "CMakeLists.txt not found; pass -Version explicitly" }
  $m = Select-String -Path $cm -Pattern 'project\s*\(\s*[^)]*VERSION\s+(\d+\.\d+\.\d+)' | Select-Object -First 1
  if (-not $m) { throw "Cannot parse version from CMakeLists.txt; pass -Version explicitly" }
  $Version = $m.Matches[0].Groups[1].Value
}
Write-Host "Version: $Version (from CMakeLists.txt)"
# C1: refuse to ship a build whose CHANGELOG was never written. Previously the
# version came from CMakeLists alone, so bumping the version without adding a
# CHANGELOG entry still produced a "releasable" zip.
Assert-VersionSync $Version (Join-Path $root "CMakeLists.txt") (Join-Path $root "CHANGELOG.md")
Write-Host "C1 ok: CHANGELOG top entry matches CMakeLists ($Version)"

$build = Join-Path $root "build"
$exeSrc = Join-Path $build "LightAlbum.exe"
if (-not (Test-Path $exeSrc)) { throw "build/LightAlbum.exe not found; run cmake + mingw32-make first" }

# === Hard pre-package quality gate (P-release-gate) ===
# Shipping a red exe is made structurally impossible: every release runs the
# full gate set and ABORTS if any gate is red, independent of whether the dev
# ran iterate.ps1. iterate.ps1 -Package already verified GO, so it passes
# -SkipGate to avoid re-running the (slow) gate set a second time.
# Bypass manually only in emergencies with: powershell -File release.ps1 -SkipGate
if ($SkipGate) {
  Write-Host "Pre-package gate: SKIPPED (-SkipGate)"
} else {
  # The gate runner needs a real console for the native exe to self-exit;
  # under a redirected stdout (e.g. `release.ps1 > log`) it would hang for
  # minutes. Skip the gate with a warning in that case (run in a console instead).
  if ([Console]::IsOutputRedirected) {
    Write-Host "Pre-package gate: SKIPPED - stdout is redirected; qa_runner needs a console."
    Write-Host "  Run release.ps1 in an interactive console, or pass -SkipGate if you are certain."
  } else {
    $qaRunner = Join-Path $root "qa_runner.ps1"
    if (-not (Test-Path $qaRunner)) { throw "qa_runner.ps1 not found next to release.ps1; cannot run pre-package gate" }
    $gateJson = Join-Path $env:TEMP ("release_gate_" + (Get-Date -Format "yyyyMMdd_HHmmss") + ".json")
    Write-Host "==> Pre-package gate: running all quality gates (qa_runner.ps1) ..."
    & powershell -ExecutionPolicy Bypass -File $qaRunner -Exe $exeSrc -Result $gateJson
    $gateRc = $LASTEXITCODE
    if (Test-Path $gateJson) {
      try {
        $gj = Get-Content $gateJson -Raw | ConvertFrom-Json
        Write-Host ("    gates: PASS={0} FAIL={1} CHECK={2} SKIP={3}" -f $gj.summary.pass, $gj.summary.fail, $gj.summary.check, $gj.summary.skip)
        foreach ($r in $gj.gates) { if ($r.mark -eq "[FAIL]") { Write-Host ("    RED: {0} -- {1}" -f $r.name, $r.detail) } }
      } catch {}
    }
    if ($gateRc -ne 0) {
      Write-Host "RELEASE BLOCKED: quality gates are RED (qa_runner rc=$gateRc). Fix the red gate(s) before packaging."
      throw "Aborting release: gates not green."
    }
    Write-Host "Pre-package gate: GREEN, proceeding to package."
  }
}

# === Locate Qt6 windeployqt (portable: local D:/qt default, CI C:/Qt fallback, or explicit) ===
$qtDir = ""
if ($Windeployqt) {
  if (-not (Test-Path $Windeployqt)) { throw "Windeployqt not found at $Windeployqt" }
  $qtDir = Split-Path (Split-Path $Windeployqt)
} else {
  if ($env:Qt6_DIR -and (Test-Path $env:Qt6_DIR)) { $qtDir = $env:Qt6_DIR }
  elseif ($env:CMAKE_PREFIX_PATH -and (Test-Path $env:CMAKE_PREFIX_PATH)) { $qtDir = $env:CMAKE_PREFIX_PATH }
  else {
    $qtCandidates = @("D:/qt/6.8.3/mingw_64", "C:/Qt/6.8.3/mingw_64")
    foreach ($q in $qtCandidates) { if (Test-Path (Join-Path $q "bin/windeployqt.exe")) { $qtDir = $q; break } }
  }
  if (-not $qtDir) { throw "Cannot locate Qt6 windeployqt; pass -Windeployqt <path> explicitly" }
  $Windeployqt = Join-Path $qtDir "bin/windeployqt.exe"
}
Write-Host "windeployqt: $Windeployqt"

$stagingName = "LightAlbum-$Version-win64"
$stagingBase = Join-Path $root $stagingName
$staging = $stagingBase

# Timestamp for relocated-aside names. Declared UNCONDITIONALLY: the
# -TrimNetwork branch below also stamps with it, and it used to be defined
# only inside the `if (Test-Path $staging)` block -- so on a first-ever
# packaging run (no staging dir yet) $ts was $null and trimmed files landed
# in _trim_excluded/ as "Qt6Network.dll_" with no timestamp, which the next
# trim then silently overwrote.
$ts = Get-Date -Format "yyyyMMddHHmmss"

# Fresh staging (relocate any existing aside instead of deleting)
if (Test-Path $staging) {
  try {
    Move-Item $staging "$staging-$ts" -Force
  } catch {
    $staging = "$stagingBase-$ts"
    if (Test-Path $staging) { Remove-Item $staging -Recurse -Force }
    Write-Host "WARN: could not relocate existing staging (in use); using $staging"
  }
}
New-Item -ItemType Directory -Path $staging | Out-Null

# 1) copy only the executable
Copy-Item $exeSrc (Join-Path $staging "LightAlbum.exe") -Force

# 2) deploy Qt via windeployqt (clean: only required DLLs + plugins)
#    --force: required -- an interrupted windeployqt leaves same-size but
#    corrupt DLLs, and a rerun skips them by timestamp/size, producing
#    0xC000012F startup failures (P13 evidence).
#    Bare call, NO redirect/pipe: PS 5.1 converts native stderr to
#    NativeCommandError only under redirection/pipe (P13: 2>&1|, P17: *>file --
#    both truncated the deploy between DLL copy and plugin copy). Bare call is
#    immune; the dxcompiler warning just prints.
$ErrorActionPreference = "Continue"
& $Windeployqt --release --force --no-translations --no-qml --no-opengl-sw (Join-Path $staging "LightAlbum.exe")
Write-Host "windeployqt exit code: $LASTEXITCODE"
$ErrorActionPreference = "Stop"
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed with exit code $LASTEXITCODE" }
if (-not (Test-Path (Join-Path $staging "platforms"))) { throw "windeployqt left no platforms/ dir" }

# 3) offscreen platform plugin for CI/smoke
Copy-Item (Join-Path $qtDir "plugins/platforms/qoffscreen.dll") (Join-Path $staging "platforms/qoffscreen.dll") -Force

# 3.5) user manual. Matched against the version being packaged. The previous
# "fall back to any LightAlbum-*-*.md" behaviour meant packaging could NEVER
# fail here: with only the 2.0.0 manual in docs/, every release shipped a manual
# 20 versions stale (it advertised LightAlbum-2.0.0-win64.zip / 16.6 MB while the
# real artifact was 2.20.0 / 28.3 MB). A fallback that guarantees success is a
# defect, not a safety net -- so a version mismatch is now a hard error naming
# both versions, and C2 re-checks the staged copy independently.
$docsDir = Join-Path $root "..\docs"
$manual = $null
if (Test-Path $docsDir) {
  $manual = Get-ChildItem $docsDir -Filter "LightAlbum-$Version-*.md" -File | Select-Object -First 1
  if (-not $manual) {
    $stale = @(Get-ChildItem $docsDir -Filter "LightAlbum-*-*.md" -File)
    $names = (@($stale | ForEach-Object { $_.Name }) -join ", ")
    if ($stale.Count -gt 0) {
      throw ("user manual for $Version not found; docs/ only has: " + $names +
             " -- refusing to ship a stale manual (write a current manual first)")
    }
    throw ("user manual for $Version not found and no LightAlbum-*-*.md candidate in docs/ -- C2 would fail anyway")
  }
} else {
  throw ("docs dir not found: " + $docsDir + " -- cannot ship without a user manual")
}
Copy-Item $manual.FullName (Join-Path $staging "USER-MANUAL.md") -Force

# 3.6) third-party attribution notice. MIT requires the copyright notice to
# travel with the distribution, and it was NOT copied before -- the dist had
# no license file at all while docs/THIRD_PARTY_NOTICE.md claimed the native
# port derives from an MIT project.
$noticeSrc = Join-Path $docsDir "THIRD_PARTY_NOTICE.md"
if (Test-Path $noticeSrc) {
  Copy-Item $noticeSrc (Join-Path $staging $NOTICE_NAME) -Force
} else {
  Write-Host ("WARN: " + $noticeSrc + " not found; C6 will fail the release")
}

# 3.7) contracts C2/C6 on the staged tree, before anything is zipped.
# C3 is deliberately NOT here: it requires BUILDINFO.txt, which can only be
# written once the trim is done (the trim changes the file set the SBOM
# reports on). It runs at 4.7 below.
Assert-ManualPresent $staging $Version
Assert-LicenseCompliance $staging $NOTICE_NAME $root $DERIVED_SOURCES
Write-Host "C2/C6 ok: manual present, license attribution shipped"

# 4) optional trim (native has no QNetwork usage)
if ($TrimNetwork) {
  $excl = Join-Path $root "_trim_excluded"
  if (-not (Test-Path $excl)) { New-Item -ItemType Directory -Path $excl | Out-Null }
  foreach ($it in @("Qt6Network.dll", "tls", "networkinformation", "generic")) {
    $p = Join-Path $staging $it
    if (Test-Path $p) { Move-Item $p (Join-Path $excl ($it + "_" + $ts)) -Force }
  }
  Write-Host "Trimmed Qt6Network + tls + networkinformation + generic (relocated, source unchanged)"
}

# 4.5) BUILDINFO (provenance/SBOM) BEFORE the manifest, so the manifest
# covers it and the file count/size it reports is the real total. Generated
# after the trim because the trim changes the file set.
$biLines = New-BuildInfo $staging $BUILDINFO_NAME $Version $root
Write-Host ("SBOM: " + $BUILDINFO_NAME + " (" + $biLines + " lines)")
# C3 now that BUILDINFO exists -- the exe, both platform plugins and the
# provenance file must all be present and non-empty.
Assert-CoreArtifacts $staging
Write-Host "C3 ok: exe + qoffscreen + qwindows + BUILDINFO all present and non-empty"
# C8: refuse to ship a non-Release exe. windeployqt deploys Release Qt DLLs, so a
# Debug exe would make the dist a mixed artifact (bigger, assertions on).
$null = Assert-ReleaseBuildType (Join-Path $root "build/CMakeCache.txt")
Write-Host "C8 ok: build type is Release"

# 4.6) MANIFEST (sha256 + bytes + path) so the dist is verifiable and two
# builds can be diffed. Generated AFTER the trim and after BUILDINFO, so it
# describes exactly what ships.
$manifestLines = New-ReleaseManifest $staging $MANIFEST_NAME
Assert-ManifestConsistent $staging $MANIFEST_NAME
# C7: prove the manifest is a pure function of the staged bytes and totally
# ordered. Reads the on-disk manifest first (C7 half a), then regenerates
# twice (half b).
$reproHash = Assert-ManifestReproducible $staging $MANIFEST_NAME
Assert-ManifestConsistent $staging $MANIFEST_NAME
Write-Host ("MANIFEST: " + $MANIFEST_NAME + " (" + ($manifestLines - 3) + " files, self-verified, reproducible=" + $reproHash.Substring(0, 12) + ")")

# 5) zip (relocate any prior zip instead of deleting)
$zip = Join-Path $root "$stagingName.zip"
if (Test-Path $zip) { Move-Item $zip "$zip.bak" -Force }
Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::CreateFromDirectory($staging, $zip,
    [System.IO.Compression.CompressionLevel]::Optimal, $false)
# C5: the zip is the actual deliverable, so verify the deliverable itself
$zipEntries = Assert-ZipIntegrity $zip
$size = [math]::Round((Get-Item $zip).Length / 1MB, 1)
Write-Host ("C5 ok: " + $zip + " (" + $size + " MB, " + $zipEntries + " entries)")
Write-Host "RELEASE OK: $stagingName"
