<#
.SYNOPSIS
  Next-gen iteration orchestrator - makes the whole client SOP executable:
  build -> run all gates (qa_runner.ps1) -> emit a Markdown + console report.
  Optional: -Package runs release.ps1 after a green run.
  All gates are verified self-exiting; see docs/client_iter_guide.md section 2.3/2.4.
.USAGE
  powershell -ExecutionPolicy Bypass -File iterate.ps1 [-Build] [-Package] [-Only <gate>] [-Timeout 25]
.EXAMPLE
  powershell -ExecutionPolicy Bypass -File iterate.ps1            # build + gates + report
  powershell -ExecutionPolicy Bypass -File iterate.ps1 -NoBuild   # skip build, just gates
  powershell -ExecutionPolicy Bypass -File iterate.ps1 -Only LA_GEO_SMOKE
#>
param(
  [string]$Exe = "D:\Test\workbuddy\2026-09-18-09-00-13\native\build\LightAlbum.exe",
  [string]$Cmake = "C:/Users/damin/.workbuddy/binaries/python/envs/default/Lib/site-packages/cmake/data/bin/cmake.exe",
  [switch]$Build = $true,
  [switch]$NoBuild = $false,
  [switch]$Package = $false,
  [string]$Only = "",
  [int]$Timeout = 25,
  [string]$Report = ""
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
Push-Location $root | Out-Null
$qaRunner  = Join-Path $root "qa_runner.ps1"
$release   = Join-Path $root "release.ps1"
$resultJson = Join-Path $env:TEMP ("qa_result_" + (Get-Date -Format "yyyyMMdd_HHmmss") + ".json")
if (-not $Report) { $Report = Join-Path $env:TEMP ("LightAlbum_qa_" + (Get-Date -Format "yyyyMMdd_HHmmss") + ".md") }

$doBuild = $Build -and -not $NoBuild
$buildOk = $true
$buildNote = "skipped (-NoBuild)"
if ($doBuild) {
  if (-not (Test-Path $Cmake)) {
    Write-Host "BUILD FAIL: cmake not found at $Cmake"; $buildOk = $false; $buildNote = "cmake missing"
  } else {
    Write-Host "==> Building (cmake --build build --target LightAlbum -j 8) ..."
    $bOut = & $Cmake --build build --target LightAlbum -j 8 2>&1
    $buildOk = ($LASTEXITCODE -eq 0) -and (-not ($bOut -match "error:"))
    $buildNote = if ($buildOk) { "ok" } else { "FAILED (exit=$LASTEXITCODE)" }
    $bOut | ForEach-Object { Write-Host ("    " + $_) }
  }
}

# Run the gate runner (it prints live and also writes a machine-readable JSON result).
# NOTE: -Only and its value MUST be two separate argv tokens. A single glued
# string like "-Only LA_GEO_SMOKE" is passed as ONE argument to the child
# powershell -File parser, which then never binds qa_runner.ps1's -Only param.
$qaArgs = @("-ExecutionPolicy","Bypass","-File",$qaRunner,"-Exe",$Exe,"-Timeout",$Timeout,"-Result",$resultJson)
if ($Only) { $qaArgs += "-Only"; $qaArgs += $Only }
& powershell @qaArgs

# Consume the JSON result (decoupled from PowerShell stream redirection).
$data = $null
if (Test-Path $resultJson) {
  try { $data = Get-Content $resultJson -Raw | ConvertFrom-Json } catch { $data = $null }
}
if (-not $data) {
  Write-Host "ORCHESTRATOR ERROR: qa_runner produced no JSON at $resultJson"
  exit 2
}
$gates = @()
foreach ($g in $data.gates) { $gates += @{ name = $g.name; mark = $g.mark; detail = $g.detail } }
$sum = @{ pass = [int]$data.summary.pass; fail = [int]$data.summary.fail;
         check = [int]$data.summary.check; skip = [int]$data.summary.skip }

$verdict = if (-not $buildOk) { "NO-GO (build failed)" }
           elseif ($sum.fail -gt 0) { "NO-GO (gates red)" }
           else { "GO" }

# Emit Markdown report
$ts = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
$md = @()
$md += "# LightAlbum Iteration QA Report"
$md += ""
$md += "- Generated: $ts"
$md += "- Exe: $Exe"
$md += "- Build: $buildNote"
$md += "- Verdict: **$verdict**"
$md += ""
$md += "## Summary"
$md += ""
$md += "| PASS | FAIL | CHECK | SKIP |"
$md += "|---|---|---|---|"
$md += ("| {0} | {1} | {2} | {3} |" -f $sum.pass, $sum.fail, $sum.check, $sum.skip)
$md += ""
$md += "## Gates"
$md += ""
$md += "| Gate | Result | Detail |"
$md += "|---|---|---|"
foreach ($g in $gates) {
  $d = if ($g.detail) { $g.detail } else { "" }
  $md += ("| {0} | {1} | {2} |" -f $g.name, $g.mark, $d)
}
$failRows = @($gates | Where-Object { $_.mark -eq "[FAIL]" })
if ($failRows.Count -gt 0) {
  $md += ""
  $md += "## Red gates"
  $md += ""
  foreach ($g in $failRows) { $md += ("- **{0}**: {1}" -f $g.name, $g.detail) }
}
$md += ""
$md | Set-Content -Path $Report -Encoding UTF8

# Console summary
Write-Host ""
Write-Host "=== ITERATION VERDICT: $verdict ==="
Write-Host ("Gates: PASS={0} FAIL={1} CHECK={2} SKIP={3}" -f $sum.pass, $sum.fail, $sum.check, $sum.skip)
Write-Host "Report: $Report"

# Optional packaging after a green run
if ($Package) {
  if ($verdict -ne "GO") {
    Write-Host "PACKAGE SKIPPED: not GO (build/gates red)."
  } elseif (-not (Test-Path $release)) {
    Write-Host "PACKAGE SKIPPED: $release not found."
  } else {
    Write-Host "==> Packaging (release.ps1, -SkipGate: iterate already verified GO) ..."
    & powershell -ExecutionPolicy Bypass -File $release -SkipGate 2>&1 | ForEach-Object { Write-Host ("    " + $_) }
  }
}

if ($verdict -ne "GO") { exit 1 } else { exit 0 }
