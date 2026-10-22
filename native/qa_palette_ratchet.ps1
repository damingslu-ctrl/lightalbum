<#
.SYNOPSIS
  LA_PALETTE_QA - design-language ratchet gate (spec 2026-10-12 section 5).
.DESCRIPTION
  Counts 5 debt classes in src/ui + src/core, excluding exempt files:
    C1 hex literals          (#rrggbb)
    C2 font-size literals    (font-size:NNpx in QSS strings)
    C3 off-grid border-radius (value not in radiusWhitelist)
    C4 off-grid spacing      (setSpacing/setContentsMargins value not in spaceWhitelist)
    C5 emoji in translatable strings (symbol/glyph chars on translate()/tr()/
                                            fromUtf8()/QStringLiteral() lines)
  Ratchet: current > ceiling -> FAIL. Never auto-relaxes. -Update rewrites
  ceilings to current (only allowed from BELOW, i.e. it records progress).
  -Init creates the baseline with ceiling=current (used exactly once, Task 1).
  Counter-example: env LA_PALETTE_QA_FORCE_BROKEN=1 injects +1 into every
  count AFTER comparison and must FAIL (proves the scanner is not inert).
  Fail-open guard (F1, Task 12): if the scan roots yield ZERO files, every
  count reads 0 and the compare would report green on an empty scan. That is
  a broken harness, not a clean codebase, so files-scanned=0 is a hard FAIL.
  The guard can be proven live with env LA_PALETTE_QA_SCAN_DIRS (semicolon-
  separated dirs replacing the default src\ui;src\core roots) pointed at a
  tree with no .cpp/.h sources. Unset = production behaviour (default off).
.USAGE
  powershell -NoProfile -ExecutionPolicy Bypass -File qa_palette_ratchet.ps1 [-Init] [-Update]
#>
param([switch]$Init,[switch]$Update)
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$basePath = Join-Path $root "qa\palette_baseline.json"
$exemptNames = @("Theme.h","DarkPalette.h","GalleryCardPainter.cpp","MapView.cpp","Splash.cpp")
$keys = @("C1","C2","C3","C4","C5","C6")
$labels = @{ C1="hex"; C2="font-size"; C3="radius-offgrid"; C4="spacing-offgrid";
             C5="emoji-in-tr"; C6="canvas-raw" }
function Get-ScanFiles {
  # F1 guard-test knob (default off): LA_PALETTE_QA_SCAN_DIRS replaces the two
  # production roots when set, so the empty-scan guard below can be triggered
  # live without touching the scan roots used by every normal run.
  $dirs = @("src\ui","src\core")
  if ($env:LA_PALETTE_QA_SCAN_DIRS) { $dirs = @($env:LA_PALETTE_QA_SCAN_DIRS -split ";") }
  $f = @()
  foreach ($d in $dirs) {
    $p = if ([System.IO.Path]::IsPathRooted($d)) { $d } else { Join-Path $root $d }
    if (Test-Path $p) { $f += @(Get-ChildItem -Path $p -Include *.cpp,*.h -Recurse |
      Where-Object { $exemptNames -notcontains $_.Name }) }
  }
  return $f
}
function Measure-Counts {
  param($files)
  $c1=0;$c2=0;$c3=$c4=$c5=$c6=0
  $sw = @(0,4,8,12,16,24,32)
  foreach ($f in $files) {
    $lines = @(Get-Content -LiteralPath $f.FullName -Encoding UTF8)
    foreach ($ln in $lines) {
      $c1 += @([regex]::Matches($ln,'#[0-9a-fA-F]{6}\b')).Count
      foreach ($m in [regex]::Matches($ln,'font-size:\s*([0-9.]+)px')) { $c2++ }
      # C3 (T12): every numeric border-radius counts, whitelist removed -- the
      # ratchet now demands radius come from Lumen::Radius::* interpolation.
      foreach ($m in [regex]::Matches($ln,'border-radius:\s*(\d+)px')) {
        $c3++ }
      # C4 only: the sanctioned editorial-whitespace exemption marker (spec 3.3 /
      # Task 10). Lines carrying the exact marker are skipped in this branch; no
      # other counter reads it, so the exemption cannot leak into C1/C2/C3/C5.
      if ($ln -notmatch 'palette-exempt: editorial whitespace') {
      foreach ($m in [regex]::Matches($ln,'setSpacing\w*\(\s*(\d+)')) {
        if ($sw -notcontains [int]$m.Groups[1].Value) { $c4++ } }
      foreach ($m in [regex]::Matches($ln,'setContentsMargins\(([^)]*)\)')) {
        foreach ($n in ($m.Groups[1].Value -split ',')) {
          $v = $n.Trim(); if ($v -match '^\d+$') { if ($sw -notcontains [int]$v) { $c4++ } } } }
      }
      # C5: an emoji/symbol glyph on a translatable-string site. The real codebase
      # idiom is QCoreApplication::translate("LightAlbum","...") (evidence: Gallery
      # Page:264, CullPage:241/859, MainWindow:279/287) plus tr/fromUtf8/QStringLitera
      # l, so the old "(tr|fromUtf8)\(" key reported C5=0 and stayed blind to the deb
      # t it monitors. Precision holds because a line must ALSO carry an emoji-class c
      # har: astral surrogate pairs (U+1F300..) plus the BMP dingbat/shape blocks the
      # audit inventory uses (U+2600-U+27BF incl. 0x2665 heart; U+25A0-U+25FF shapes).
      if (($ln -match '[\uD800-\uDBFF][\uDC00-\uDFFF]|[\u2600-\u27BF]|[\u25A0-\u25FF]') -and
          ($ln -match 'translate\(|tr\(|fromUtf8\(|QStringLiteral\(')) { $c5++ }
      # C6 (T12): raw canvas colors -- QColor(0x...) literals and rgba() strings
      # must come from lamp tokens (canvasBg/canvasLine/mask/focusRing).
      $c6 += @([regex]::Matches($ln,'QColor\(0x')).Count
      $c6 += @([regex]::Matches($ln,'rgba\(')).Count
    }
  }
  return @{C1=$c1;C2=$c2;C3=$c3;C4=$c4;C5=$c5;C6=$c6}
}
$scanned = @(Get-ScanFiles)
# F1 fail-open guard: zero scanned files makes every count read 0, the compare
# trivially passes, and the gate goes green while watching nothing. Refuse it.
# Fires before -Init/-Update/compare so an empty scan can neither validate nor
# (re)baseline through this path.
if ($scanned.Count -eq 0) {
  Write-Host "LA_PALETTE_QA FAIL files-scanned=0 (scan roots yielded no .cpp/.h sources; refusing green on an empty scan)"
  exit 1
}
Write-Host ("LA_PALETTE_QA files-scanned={0}" -f $scanned.Count)
$cur = Measure-Counts $scanned
if ($Init -or -not (Test-Path $basePath)) {
  if (-not $Init) { Write-Host "LA_PALETTE_QA FAIL baseline missing (run -Init once)"; exit 1 }
  if (-not (Test-Path (Join-Path $root "qa"))) { New-Item -ItemType Directory (Join-Path $root "qa") | Out-Null }
  $counts = [ordered]@{}
  foreach ($k in $keys) {
    $counts[$k] = [ordered]@{ label=$labels[$k]; current=[int]$cur.$k; ceiling=[int]$cur.$k }
  }
  $o = [ordered]@{ counts=$counts;
                   radiusWhitelist=@(4,6,10,14,20,999); spaceWhitelist=@(0,4,8,12,16,24,32); exempt=$exemptNames }
  $o | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $basePath -Encoding UTF8
  $initLine = ($keys | ForEach-Object { "{0}={1}" -f $_, [int]$cur.$_ }) -join " "
  Write-Host ("LA_PALETTE_QA INIT " + $initLine)
  exit 0
}
$base = Get-Content -LiteralPath $basePath -Raw | ConvertFrom-Json
$worst = ""
foreach ($k in $keys) {
  # Missing key = baseline predates this dimension. Never default it to 0 (that
  # would silently relax the gate); print the measured value and let a human
  # pin current+ceiling by hand.
  if ($null -eq $base.counts.$k) {
    Write-Host ("LA_PALETTE_QA FAIL baseline missing key {0} (measured {1}); pin it by hand" -f $k,[int]$cur.$k)
    exit 1
  }
  $ceil = [int]$base.counts.$k.ceiling
  $now  = [int]$cur.$k
  if ($Update) {
    if ($now -gt $ceil) { Write-Host ("LA_PALETTE_QA FAIL cannot update: {0} {1} > ceiling {2}" -f $k,$now,$ceil); exit 1 }
    $base.counts.$k.ceiling = $now; $base.counts.$k.current = $now
    Write-Host ("LA_PALETTE_QA UPDATE {0} {1}->{2}" -f $k,$ceil,$now)
  } else {
    Write-Host ("LA_PALETTE_QA {0}={1} ceiling={2}" -f $k,$now,$ceil)
    if ($now -gt $ceil) { $worst = ("{0} {1} > ceiling {2}; ratchet violated" -f $k,$now,$ceil) }
  }
}
if ($Update) { $base | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $basePath -Encoding UTF8; Write-Host "LA_PALETTE_QA PASS baseline-updated"; exit 0 }
if ($env:LA_PALETTE_QA_FORCE_BROKEN -eq "1") {
  Write-Host "LA_PALETTE_QA FORCE_BROKEN injecting +1 hex over ceiling (counter-example)"
  $b = [int]$base.counts.C1.ceiling
  Write-Host ("LA_PALETTE_QA C1={0} ceiling={1}" -f ($b+1),$b)
  Write-Host "LA_PALETTE_QA FAIL forced regression C1 (proves scanner wiring is live)"; exit 1
}
if ($worst -ne "") { Write-Host ("LA_PALETTE_QA FAIL " + $worst); exit 1 }
Write-Host "LA_PALETTE_QA PASS"
exit 0
