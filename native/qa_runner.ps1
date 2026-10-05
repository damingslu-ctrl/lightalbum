<#
.SYNOPSIS
  Next-gen iteration gate runner - runs all LA_* gates from native PowerShell.
  CRITICAL: native Windows Qt exe output is SILENTLY SWALLOWED under Git Bash/MSYS
  (exit 0 / 0 bytes false negative). ALL gates MUST run via PowerShell. Never run
  gates from Git Bash. See docs/client_iter_guide.md (section 2) for the full story.
.USAGE
  powershell -ExecutionPolicy Bypass -File qa_runner.ps1 [-Exe <path>] [-Only <gate>]
                                        [-Timeout 25] [-Tier fast|full] [-Timing]
.EXAMPLE
  powershell -ExecutionPolicy Bypass -File qa_runner.ps1                 # run all gates
  powershell -ExecutionPolicy Bypass -File qa_runner.ps1 -Only LA_GEO_SMOKE
  powershell -ExecutionPolicy Bypass -File qa_runner.ps1 -Timing          # per-gate wall clock
  powershell -ExecutionPolicy Bypass -File qa_runner.ps1 -Tier fast      # PR layer only
#>
param(
  [string]$Exe = "",
  [string]$Only = "",
  [int]$Timeout = 25,
  # 2.8.0: per-gate wall clock. Printed inline and written to the result
  # JSON as ms, so gate tiering is decided from MEASURED cost, never a guess.
  [switch]$Timing = $false,
  # 2.8.0: which layer of the gate matrix to run. "" (default) = all gates,
  # i.e. byte-for-byte the pre-2.8.0 behaviour, which is what release.ps1 and
  # a local full run use. "fast" = the cheap PR layer, "full" = the strict
  # layer (push to main / tags). The table lives in gate_matrix.ps1.
  [string]$Tier = "",
  [string]$Result = (Join-Path $env:TEMP "qa_client\qa_result.json")
)
$ErrorActionPreference = "Stop"
$tmp = Join-Path $env:TEMP "qa_client"; New-Item -ItemType Directory -Force -Path $tmp | Out-Null

# Portable default: exe is <scriptDir>/build/LightAlbum.exe (scriptDir = native/).
if (-not $Exe) { $Exe = Join-Path $PSScriptRoot "build/LightAlbum.exe" }

# 2.8.0: the gate table is NOT defined here any more. gate_matrix.ps1 is the
# single source of truth for both the gate list and its tiers, so a gate can
# never exist in one file and be tiered in another. It is dot-sourced (not
# shelled out) so Get-GateTable is available without paying a second
# PowerShell startup on every run.
# Per-gate fields: n / png / arg / need / kind / script / scriptArgs / tier / measuredMs.
$matrix = Join-Path $PSScriptRoot "gate_matrix.ps1"
if (-not (Test-Path $matrix)) { throw ("gate matrix missing: " + $matrix) }
. $matrix -LoadOnly
$gates = @(Get-GateTable -Tier $Tier)
$tierTxt = if ($Tier) { $Tier } else { "all" }
Write-Host ("Gate matrix: tier={0} gates={1}" -f $tierTxt, $gates.Count)

function Eval($lines, $exitCode, $killed) {
  # A killed gate means the native exe did NOT self-exit within $Timeout: this
  # is an INFRA failure (console/redirect issue or a hung gate), never a healthy
  # result. On CI it MUST FAIL the job loudly, not silently pass as CHECK.
  if ($killed) { return "KILLED" }
  # Match FAIL as a STANDALONE UPPERCASE token, case-sensitively. A plain
  # -match "FAIL" is case-insensitive and also matches substrings of healthy
  # fields: LA_BATCH_TX_QA prints "... PASS rows=6 failAt=-1 ..." and was scored
  # RED purely because of the "failAt" key. Every real failure in this codebase
  # is emitted as its own uppercase token ("LA_XXX FAIL reason=...", "LA_GEO_SMOKE
  # FAIL"), so \bFAIL\b with -cmatch keeps genuine reds and drops false ones.
  if (@($lines -cmatch '\bFAIL\b').Count -gt 0) { return "RED" }
  if ($exitCode -ne 0) { return "RED" }
  $greenPat = "PASS|OK|DONE|done|rings=|Loaded|loaded="
  if (@($lines -match $greenPat).Count -gt 0) { return "GREEN" }
  if ($lines.Count -eq 0) { return "NO-OUTPUT" }
  return "CHECK"
}

$pass=0;$fail=0;$check=0;$skip=0;$kill=0
$results = @()
$swTotal = [System.Diagnostics.Stopwatch]::StartNew()
$prevGate = ""
foreach ($g in $gates) {
  if ($Only -and $g.n -ne $Only) { continue }
  # skip gates whose required env vars are absent (e.g. LA_SCAN_SMOKE needs LA_SCAN_ROOT/LA_SCAN_DB)
  if ($g.need) {
    $missing = @($g.need | Where-Object { -not (Test-Path "Env:$_") })
    if ($missing.Count -gt 0) {
      Write-Host ("{0,-20} [SKIP] (needs {1})" -f $g.n, ($missing -join ","))
      $results += @{ name = $g.n; mark = "[SKIP]"; detail = ("needs " + ($missing -join ",")); ms = 0 }
      $skip++
      continue
    }
  }
  # clear previous gate var so gates do not cross-trigger
  if ($prevGate) { Remove-Item -Path "Env:$prevGate" -ErrorAction SilentlyContinue }
  $prevGate = $g.n

  $pngArg = ""
  if ($g.png) { $pngArg = (Join-Path $tmp ($g.n + ".png")) + $g.arg }
  $val = if ($g.png) { $pngArg } else { "1" }
  $env:QT_QPA_PLATFORM = "offscreen"
  Set-Item -Path "Env:$($g.n)" -Value $val
  # 2.7.0: tell the app which gate is running, so the self-exit watchdog can
  # attribute a hang to a gate name instead of just saying "something hung".
  Set-Item -Path "Env:LA_GATE" -Value $g.n

  $swGate = [System.Diagnostics.Stopwatch]::StartNew()
  # Launch via raw System.Diagnostics.Process. This avoids the Start-Process
  # 'Path'/'PATH' case-clash dictionary collision that breaks on this machine.
  $psi = New-Object System.Diagnostics.ProcessStartInfo
  if ($g.kind -eq "ps1") {
    # Script gate: assert on packaging/release logic instead of app behavior.
    # It runs the SAME powershell host as this script, so quoting must stay
    # PS 5.1-safe (no ternary, ASCII only).
    $sp = Join-Path $PSScriptRoot $g.script
    if (-not (Test-Path $sp)) { throw ("script gate target missing: " + $sp) }
    $psi.FileName = "powershell.exe"
    $psi.Arguments = "-NoProfile -ExecutionPolicy Bypass -File `"$sp`" " + $g.scriptArgs
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.CreateNoWindow = $true
  } else {
    $psi.FileName = $Exe
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.RedirectStandardInput = $true
    $psi.CreateNoWindow = $true
  }
  $proc = New-Object System.Diagnostics.Process
  $proc.StartInfo = $psi
  $proc.Start() | Out-Null
  # Per-gate timeout override: a gate may declare timeoutSec in gate_matrix.ps1
  # when its real cost legitimately exceeds the global -Timeout. Rationale:
  # LA_LUMEN_SMOKE does 96 full-frame pixel comparisons (8 images x 12 ops) and
  # was measured at 66585 ms, so a flat 25s budget killed a gate that actually
  # PASSES. Hanging is still detected, just with a truthful budget.
  # NOTE: gate entries are hashtables, so PSObject.Properties does NOT list
  # their keys -- test the key directly via ContainsKey.
  $gateTimeout = $Timeout
  if ($g.ContainsKey("timeoutSec") -and $g["timeoutSec"] -gt 0) {
      $gateTimeout = [int]$g["timeoutSec"]
  }
  $done = $proc.WaitForExit($gateTimeout*1000)
  $killed = -not $done
  if ($killed) { try { $proc.Kill() } catch {} }
  $raw = ""
  try { $raw = $proc.StandardOutput.ReadToEnd() + $proc.StandardError.ReadToEnd() } catch {}
  $lines = @($raw -split "`r?`n" | Where-Object { $_ -match 'LA_|^GEO' } | Where-Object { $_.Trim() -ne "" })
  $verdict = Eval $lines $proc.ExitCode $killed
  $mark = switch ($verdict) { "GREEN"{"[OK]"} "RED"{"[FAIL]"} "KILLED"{"[KILL]"} "NO-OUTPUT"{"[NOOUT]"} default{"[??]"} }
  $detail = ""
  if ($verdict -eq "KILLED") {
    if ($g.kind -eq "ps1") {
      $detail = ("TIMEOUT/KILLED: script gate did not finish within " + $gateTimeout + "s")
    } else {
      $detail = ("TIMEOUT/KILLED: exe did not self-exit within " + $gateTimeout + "s (console/redirect or hung gate)")
    }
  } elseif ($verdict -eq "RED") {
    $detail = (@($lines | Where-Object { $_ -cmatch '\bFAIL\b' } | Select-Object -First 1) -join " ").Trim()
  } elseif ($verdict -eq "CHECK") {
    $detail = (@($lines | Select-Object -First 1) -join " ").Trim()
  }
  $swGate.Stop()
  $ms = [int]$swGate.ElapsedMilliseconds
  $results += @{ name = $g.n; mark = $mark; detail = $detail; ms = $ms }
  $markTxt = "{0,-20} {1}" -f $g.n, $mark
  if ($Timing) { $markTxt = "{0,-20} {1} ({2}ms)" -f $g.n, $mark, $ms }
  Write-Host $markTxt
  $lines | ForEach-Object { Write-Host ("    " + $_) }
  switch ($verdict) { "GREEN"{$pass++} "RED"{$fail++} "KILLED"{$fail++;$kill++} default{$check++} }
}
if ($prevGate) { Remove-Item -Path "Env:$prevGate" -ErrorAction SilentlyContinue }
Write-Host ("`n=== SUMMARY: PASS={0} FAIL={1} CHECK={2} SKIP={3} KILL={4} ===" -f $pass,$fail,$check,$skip,$kill)
Write-Host "CHECK = has output but no explicit green marker; verify per docs/client_iter_guide.md section 3."
Write-Host "KILL  = exe did not self-exit within timeout (INFRA failure); counts as RED, blocks CI."
Write-Host "SKIP  = required env vars absent (e.g. LA_SCAN_ROOT/LA_SCAN_DB); not a regression."

# Machine-readable result for the orchestrator (iterate.ps1): decouples from
# PowerShell stream redirection (Write-Host on info stream 6 is not capturable
# from -File child processes in PS 5.1).
$swTotal.Stop()
$totalMs = [int]$swTotal.ElapsedMilliseconds
Write-Host ("TOTAL wall clock: {0} ms (tier={1}, gates={2})" -f $totalMs, $tierTxt, $gates.Count)
$obj = @{
  summary = @{ pass = $pass; fail = $fail; check = $check; skip = $skip; totalMs = $totalMs }
  tier = $tierTxt
  gates   = $results
}
try { $obj | ConvertTo-Json -Compress | Set-Content -Path $Result -Encoding UTF8 } catch {}

if ($fail -gt 0) { exit 1 } else { exit 0 }
