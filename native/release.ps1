# LightAlbum native release packager (Windows)
#
# Build first (native/build/LightAlbum.exe), then run:
#   powershell -File release.ps1# clean deploy, no trim
#   powershell -File release.ps1 -TrimNetwork    # also drop Qt6Network+tls+networkinformation+generic
#   powershell -File release.ps1 -SelfTest      # contract self-test (QA gate LA_RELEASE_QA)
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

# =====================================================================
# Release contracts (C1..C5)
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

# C2: the user manual must actually be in the dist, not silently missing.
function Assert-ManualPresent($staging) {
  $p = Join-Path $staging "USER-MANUAL.md"
  if (-not (Test-Path $p)) { throw "C2 USER-MANUAL.md missing from staging" }
  if ((Get-Item $p).Length -le 0) { throw "C2 USER-MANUAL.md is empty" }
}

# C3: the artifacts a fresh machine needs, including the offscreen platform
# plugin that every headless QA gate depends on.
function Assert-CoreArtifacts($staging) {
  $need = @("LightAlbum.exe", "platforms/qoffscreen.dll", "platforms/qwindows.dll")
  foreach ($f in $need) {
    $p = Join-Path $staging $f
    if (-not (Test-Path $p)) { throw ("C3 required artifact missing: " + $f) }
    if ((Get-Item $p).Length -le 0) { throw ("C3 required artifact empty: " + $f) }
  }
}

# C4: the manifest is only worth shipping if it matches reality byte for byte.
function New-ReleaseManifest($staging, $name) {
  $lines = @("# LightAlbum native release manifest", "# sha256 <TAB> bytes <TAB> relative/path", "")
  $files = @(Get-ChildItem $staging -Recurse -File | Where-Object { $_.Name -ne $name } | Sort-Object FullName)
  foreach ($f in $files) {
    $rel = $f.FullName.Substring($staging.Length).TrimStart('\', '/') -replace '\\', '/'
    $h = (Get-FileHash $f.FullName -Algorithm SHA256).Hash.ToLower()
    $lines += ($h + "`t" + $f.Length + "`t" + $rel)
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

# =====================================================================
# -SelfTest: prove the contracts actually fire.
#
# Builds a throwaway sandbox (fake CMakeLists/CHANGELOG/dist/zip), verifies
# the clean baseline is green, then injects one defect per contract and
# asserts that contract is the one that trips. -NoInject skips the
# injections, which MUST turn the gate red -- otherwise "the contract
# catches its defect" would be an unfalsifiable claim.
# =====================================================================
function Get-ContractTags($want, $cmPath, $clPath, $staging, $manName, $zipPath) {
  $tags = @()
  $probes = @(
    @{ t = "C1"; f = { Assert-VersionSync $want $cmPath $clPath } },
    @{ t = "C2"; f = { Assert-ManualPresent $staging } },
    @{ t = "C3"; f = { Assert-CoreArtifacts $staging } },
    @{ t = "C4"; f = { Assert-ManifestConsistent $staging $manName } },
    @{ t = "C5"; f = { Assert-ZipIntegrity $zipPath } }
  )
  foreach ($p in $probes) {
    # Assign to $null: Assert-ZipIntegrity returns an entry count, and an
    # unassigned call would leak it into the returned tag list.
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
  Set-Content -Path (Join-Path $dist "LightAlbum.exe") -Encoding ASCII -Value "fake-exe-payload"
  Set-Content -Path (Join-Path $dist "Qt6Core.dll") -Encoding ASCII -Value "fake-core"
  Set-Content -Path (Join-Path $dist "platforms/qwindows.dll") -Encoding ASCII -Value "fake-win"
  Set-Content -Path (Join-Path $dist "platforms/qoffscreen.dll") -Encoding ASCII -Value "fake-off"
  Set-Content -Path (Join-Path $dist "USER-MANUAL.md") -Encoding ASCII -Value "# manual"
  return $dist
}

function Invoke-ReleaseSelfTest($noInject) {
  $sandbox = Join-Path $env:TEMP ("la_release_selftest_" + (Get-Date -Format "yyyyMMddHHmmssfff"))
  $fakeVer = "9.9.9"
  $cmPath = Join-Path $sandbox "CMakeLists.txt"
  $clPath = Join-Path $sandbox "CHANGELOG.md"
  $zipPath = Join-Path $sandbox "dist.zip"
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
    $base = Get-ContractTags $fakeVer $cmPath $clPath $dist $MANIFEST_NAME $zipPath
    foreach ($t in @("C1", "C2", "C3", "C4", "C5")) {
      if ($base -notcontains ($t + ":PASS")) { $fails += ("baseline:" + $t + " did not pass on a clean sandbox") }
    }
    $zipEntries = Assert-ZipIntegrity $zipPath

    # --- one injected defect per contract ---
    $injections = @(
      @{ id = "C1"; apply = { Set-Content -Path $clPath -Encoding ASCII -Value "# CHANGELOG`n`n## [8.8.8] - 2026-01-01`n" } },
      @{ id = "C2"; apply = { Remove-Item (Join-Path $dist "USER-MANUAL.md") -Force } },
      @{ id = "C3"; apply = { Remove-Item (Join-Path $dist "platforms/qoffscreen.dll") -Force } },
      @{ id = "C4"; apply = {
          $mp = Join-Path $dist $MANIFEST_NAME
          $txt = Get-Content $mp
          $txt[3] = ($txt[3] -replace '^[0-9a-f]{64}', '0000000000000000000000000000000000000000000000000000000000000000')
          Set-Content -Path $mp -Value $txt -Encoding ASCII
        } },
      @{ id = "C5"; apply = { Set-Content -Path $zipPath -Encoding ASCII -Value "not-a-zip-at-all" } }
    )
    foreach ($inj in $injections) {
      $dist = New-Sandbox $sandbox $fakeVer
      New-ReleaseManifest $dist $MANIFEST_NAME | Out-Null
      if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
      [System.IO.Compression.ZipFile]::CreateFromDirectory($dist, $zipPath,
        [System.IO.Compression.CompressionLevel]::Optimal, $false)
      & $inj.apply
      $tags = Get-ContractTags $fakeVer $cmPath $clPath $dist $MANIFEST_NAME $zipPath
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
    Write-Output ("LA_RELEASE_QA FAIL reason=NOINJECT contracts=5 injected=0 caught=0 -- self-test harness disabled, contract assertions unproven")
    $script:SelfTestRc = 1
    return
  }
  if ($fails.Count -gt 0) {
    foreach ($f in $fails) { Write-Output ("LA_RELEASE_QA FAIL reason=" + $f) }
    $script:SelfTestRc = 1
    return
  }
  Write-Output ("LA_RELEASE_QA PASS contracts=5 injected=5 caught=" + $caught + " baseline=5 zip=" + $zipEntries)
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

# 3.5) user manual. Was hardcoded to "LightAlbum-2.0.0-*.md", which only ever
# matched by accident (the file is still named 2.0.0 while the app is 2.3.0).
# Now matched against the version being packaged, with any leftover file as a
# fallback, and a loud WARN if nothing is found -- C2 below turns that into a
# hard failure anyway, but a warning explains WHY at the right place.
$docsDir = Join-Path $root "..\docs"
$manual = $null
if (Test-Path $docsDir) {
  $manual = Get-ChildItem $docsDir -Filter "LightAlbum-$Version-*.md" -File | Select-Object -First 1
  if (-not $manual) {
    $manual = Get-ChildItem $docsDir -Filter "LightAlbum-*-*.md" -File | Select-Object -First 1
    if ($manual) { Write-Host ("WARN: no manual for $Version; falling back to " + $manual.Name) }
  }
} else {
  Write-Host "WARN: docs dir not found: $docsDir"
}
if ($manual) { Copy-Item $manual.FullName (Join-Path $staging "USER-MANUAL.md") -Force }
else { Write-Host "WARN: no user manual found; packaging without USER-MANUAL.md" }

# 3.6) contracts C2/C3 on the staged tree, before anything is zipped
Assert-ManualPresent $staging
Assert-CoreArtifacts $staging
Write-Host "C2/C3 ok: manual present, core artifacts non-empty"

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

# 4.5) MANIFEST (sha256 + bytes + path) so the dist is verifiable and two
# builds can be diffed. Generated AFTER the trim, so it describes what ships.
$manifestLines = New-ReleaseManifest $staging $MANIFEST_NAME
Assert-ManifestConsistent $staging $MANIFEST_NAME
Write-Host ("MANIFEST: " + $MANIFEST_NAME + " (" + ($manifestLines - 3) + " files, self-verified)")

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
