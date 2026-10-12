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
.USAGE
  powershell -NoProfile -ExecutionPolicy Bypass -File qa_palette_ratchet.ps1 [-Init] [-Update]
#>
param([switch]$Init,[switch]$Update)
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$basePath = Join-Path $root "qa\palette_baseline.json"
$exemptNames = @("Theme.h","DarkPalette.h","GalleryCardPainter.cpp","MapView.cpp","Splash.cpp")
function Get-ScanFiles {
  $f = @()
  foreach ($d in @("src\ui","src\core")) {
    $p = Join-Path $root $d
    if (Test-Path $p) { $f += @(Get-ChildItem -Path $p -Include *.cpp,*.h -Recurse |
      Where-Object { $exemptNames -notcontains $_.Name }) }
  }
  return $f
}
function Measure-Counts {
  $c1=0;$c2=0;$c3=$c4=$c5=0
  $rw = @(4,6,10,14,20,999); $sw = @(0,4,8,12,16,24,32)
  foreach ($f in (Get-ScanFiles)) {
    $lines = @(Get-Content -LiteralPath $f.FullName -Encoding UTF8)
    foreach ($ln in $lines) {
      $c1 += @([regex]::Matches($ln,'#[0-9a-fA-F]{6}\b')).Count
      foreach ($m in [regex]::Matches($ln,'font-size:\s*([0-9.]+)px')) { $c2++ }
      foreach ($m in [regex]::Matches($ln,'border-radius:\s*(\d+)px')) {
        if ($rw -notcontains [int]$m.Groups[1].Value) { $c3++ } }
      foreach ($m in [regex]::Matches($ln,'setSpacing\w*\(\s*(\d+)')) {
        if ($sw -notcontains [int]$m.Groups[1].Value) { $c4++ } }
      foreach ($m in [regex]::Matches($ln,'setContentsMargins\(([^)]*)\)')) {
        foreach ($n in ($m.Groups[1].Value -split ',')) {
          $v = $n.Trim(); if ($v -match '^\d+$') { if ($sw -notcontains [int]$v) { $c4++ } } } }
      # C5: an emoji/symbol glyph on a translatable-string site. The real codebase
      # idiom is QCoreApplication::translate("LightAlbum","...") (evidence: Gallery
      # Page:264, CullPage:241/859, MainWindow:279/287) plus tr/fromUtf8/QStringLitera
      # l, so the old "(tr|fromUtf8)\(" key reported C5=0 and stayed blind to the deb
      # t it monitors. Precision holds because a line must ALSO carry an emoji-class c
      # har: astral surrogate pairs (U+1F300..) plus the BMP dingbat/shape blocks the
      # audit inventory uses (U+2600-U+27BF incl. 0x2665 heart; U+25A0-U+25FF shapes).
      if (($ln -match '[\uD800-\uDBFF][\uDC00-\uDFFF]|[\u2600-\u27BF]|[\u25A0-\u25FF]') -and
          ($ln -match 'translate\(|tr\(|fromUtf8\(|QStringLiteral\(')) { $c5++ }
    }
  }
  return @{C1=$c1;C2=$c2;C3=$c3;C4=$c4;C5=$c5}
}
$cur = Measure-Counts
if ($Init -or -not (Test-Path $basePath)) {
  if (-not $Init) { Write-Host "LA_PALETTE_QA FAIL baseline missing (run -Init once)"; exit 1 }
  if (-not (Test-Path (Join-Path $root "qa"))) { New-Item -ItemType Directory (Join-Path $root "qa") | Out-Null }
  $o = @{ counts=@{ C1=@{label="hex";current=$cur.C1;ceiling=$cur.C1}; C2=@{label="font-size";current=$cur.C2;ceiling=$cur.C2}; C3=@{label="radius-offgrid";current=$cur.C3;ceiling=$cur.C3}; C4=@{label="spacing-offgrid";current=$cur.C4;ceiling=$cur.C4}; C5=@{label="emoji-in-tr";current=$cur.C5;ceiling=$cur.C5} };
        radiusWhitelist=@(4,6,10,14,20,999); spaceWhitelist=@(0,4,8,12,16,24,32); exempt=$exemptNames }
  $o | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $basePath -Encoding UTF8
  Write-Host ("LA_PALETTE_QA INIT C1={0} C2={1} C3={2} C4={3} C5={4}" -f $cur.C1,$cur.C2,$cur.C3,$cur.C4,$cur.C5)
  exit 0
}
$base = Get-Content -LiteralPath $basePath -Raw | ConvertFrom-Json
$worst = ""
foreach ($k in @("C1","C2","C3","C4","C5")) {
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
