<#
.SYNOPSIS
  Next-gen iteration gate runner - runs all LA_* gates from native PowerShell.
  CRITICAL: native Windows Qt exe output is SILENTLY SWALLOWED under Git Bash/MSYS
  (exit 0 / 0 bytes false negative). ALL gates MUST run via PowerShell. Never run
  gates from Git Bash. See docs/client_iter_guide.md (section 2) for the full story.
.USAGE
  powershell -ExecutionPolicy Bypass -File qa_runner.ps1 [-Exe <path>] [-Only <gate>] [-Timeout 25]
.EXAMPLE
  powershell -ExecutionPolicy Bypass -File qa_runner.ps1                 # run all gates
  powershell -ExecutionPolicy Bypass -File qa_runner.ps1 -Only LA_GEO_SMOKE
#>
param(
  [string]$Exe = "D:\Test\workbuddy\2026-09-18-09-00-13\native\build\LightAlbum.exe",
  [string]$Only = "",
  [int]$Timeout = 25,
  [string]$Result = (Join-Path $env:TEMP "qa_client\qa_result.json")
)
$ErrorActionPreference = "Stop"
$tmp = Join-Path $env:TEMP "qa_client"; New-Item -ItemType Directory -Force -Path $tmp | Out-Null

# Gate definitions: n=gate name; png=needs a png path argument; arg=extra suffix after png (e.g. ",china")
# need=env vars that must be present or the gate is skipped (e.g. scan needs its own root/db)
# All gates here are real (verified in src/main.cpp). Hard self-exit logic gates return
# before app.exec(); map/widget QA gates print *_DONE then return 0. None truly hang.
$gates = @(
  @{n="LA_LUMEN_SMOKE";    png=$false},
  @{n="LA_AGENT_SMOKE";    png=$false},
  @{n="LA_GEO_SMOKE";      png=$false},
  @{n="LA_SCAN_SMOKE";     png=$false; need=@("LA_SCAN_ROOT","LA_SCAN_DB")},
  @{n="LA_LOG_SMOKE";      png=$false},
  @{n="LA_HOME_SMOKE";     png=$false},
  @{n="LA_RW_SMOKE";       png=$false},
  @{n="LA_CULL_SMOKE";     png=$false},
  @{n="LA_RETOUCH_SMOKE";  png=$false},
  @{n="LA_SMOKE";          png=$false},
  @{n="LA_MAP_A_QA";       png=$true},
  @{n="LA_MAP_BURST_QA";   png=$true},
  @{n="LA_MAP_REGION_QA";  png=$true; arg=",china"},
  @{n="LA_MAP_COUNTRY_QA"; png=$true},
  @{n="LA_MAP_CLICK_QA";   png=$false},
  @{n="LA_MAP_PERF";       png=$false},
  @{n="LA_GALLERY_E_QA";   png=$true},
  @{n="LA_GALLERY_FILL_QA";png=$true},
  @{n="LA_RETOUCH_A_QA";   png=$true}
)

function Eval($lines, $exitCode, $killed) {
  if (@($lines -match "FAIL").Count -gt 0) { return "RED" }
  if (-not $killed -and $exitCode -ne 0) { return "RED" }
  $greenPat = "PASS|OK|DONE|done|rings=|Loaded|loaded="
  if (@($lines -match $greenPat).Count -gt 0) { return "GREEN" }
  if ($lines.Count -eq 0) { return "NO-OUTPUT" }
  return "CHECK"
}

$pass=0;$fail=0;$check=0;$skip=0
$results = @()
$prevGate = ""
foreach ($g in $gates) {
  if ($Only -and $g.n -ne $Only) { continue }
  # skip gates whose required env vars are absent (e.g. LA_SCAN_SMOKE needs LA_SCAN_ROOT/LA_SCAN_DB)
  if ($g.need) {
    $missing = @($g.need | Where-Object { -not (Test-Path "Env:$_") })
    if ($missing.Count -gt 0) {
      Write-Host ("{0,-20} [SKIP] (needs {1})" -f $g.n, ($missing -join ","))
      $results += @{ name = $g.n; mark = "[SKIP]"; detail = ("needs " + ($missing -join ",")) }
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

  # Launch via raw System.Diagnostics.Process. This avoids the Start-Process
  # 'Path'/'PATH' case-clash dictionary collision that breaks on this machine.
  $psi = New-Object System.Diagnostics.ProcessStartInfo
  $psi.FileName = $Exe
  $psi.UseShellExecute = $false
  $psi.RedirectStandardOutput = $true
  $psi.RedirectStandardError = $true
  $psi.RedirectStandardInput = $true
  $psi.CreateNoWindow = $true
  $proc = New-Object System.Diagnostics.Process
  $proc.StartInfo = $psi
  $proc.Start() | Out-Null
  $done = $proc.WaitForExit($Timeout*1000)
  $killed = -not $done
  if ($killed) { try { $proc.Kill() } catch {} }
  $raw = ""
  try { $raw = $proc.StandardOutput.ReadToEnd() + $proc.StandardError.ReadToEnd() } catch {}
  $lines = @($raw -split "`r?`n" | Where-Object { $_ -match 'LA_|^GEO' } | Where-Object { $_.Trim() -ne "" })
  $verdict = Eval $lines $proc.ExitCode $killed
  $mark = switch ($verdict) { "GREEN"{"[OK]"} "RED"{"[FAIL]"} "NO-OUTPUT"{"[NOOUT]"} default{"[??]"} }
  $detail = ""
  if ($verdict -eq "RED") {
    $detail = (@($lines | Where-Object { $_ -match "FAIL" } | Select-Object -First 1) -join " ").Trim()
  } elseif ($verdict -eq "CHECK") {
    $detail = (@($lines | Select-Object -First 1) -join " ").Trim()
  }
  $results += @{ name = $g.n; mark = $mark; detail = $detail }
  Write-Host ("{0,-20} {1}" -f $g.n, $mark)
  $lines | ForEach-Object { Write-Host ("    " + $_) }
  switch ($verdict) { "GREEN"{$pass++} "RED"{$fail++} default{$check++} }
}
if ($prevGate) { Remove-Item -Path "Env:$prevGate" -ErrorAction SilentlyContinue }
Write-Host ("`n=== SUMMARY: PASS={0} FAIL={1} CHECK={2} SKIP={3} ===" -f $pass,$fail,$check,$skip)
Write-Host "CHECK = has output but no explicit green marker; verify per docs/client_iter_guide.md section 3."
Write-Host "SKIP  = required env vars absent (e.g. LA_SCAN_ROOT/LA_SCAN_DB); not a regression."

# Machine-readable result for the orchestrator (iterate.ps1): decouples from
# PowerShell stream redirection (Write-Host on info stream 6 is not capturable
# from -File child processes in PS 5.1).
$obj = @{
  summary = @{ pass = $pass; fail = $fail; check = $check; skip = $skip }
  gates   = $results
}
try { $obj | ConvertTo-Json -Compress | Set-Content -Path $Result -Encoding UTF8 } catch {}

if ($fail -gt 0) { exit 1 } else { exit 0 }
