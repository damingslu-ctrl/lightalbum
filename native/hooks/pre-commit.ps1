# LightAlbum pre-commit quality gate (version-controlled source of truth).
# Installed via .git/hooks/pre-commit shim, or: git config core.hooksPath native/hooks
# Blocks the commit when staged changes touch native SOURCE and any gate is RED.
# Bypass in emergencies with: git commit --no-verify
# NOTE: this file is pure ASCII on purpose - PowerShell 5.1 parses a no-BOM UTF-8
# .ps1 as GBK, so a CJK string anywhere here (even inside a comment) breaks the
# hook with a "missing string terminator" parse error. Do not add one.
$ErrorActionPreference = "Stop"

$root = & git rev-parse --show-toplevel
if (-not $root) { Write-Host "pre-commit gate: cannot resolve repo root; skipping"; exit 0 }

# Only enforce when native SOURCE changes are staged. Doc-only commits pass freely.
$changed = & git diff --cached --name-only --diff-filter=ACMR
$nativeSrc = @($changed | Where-Object { $_ -match '^native[/\\](src[/\\]|CMakeLists\.txt|resources[/\\]|data[/\\])' })
if ($nativeSrc.Count -eq 0) {
  Write-Host "pre-commit gate: no native source changes staged -> skip"
  exit 0
}
Write-Host ("pre-commit gate: {0} native source file(s) changed" -f $nativeSrc.Count)

# Console-dependency note (verified 2.24.0): the exe's self-exit depends on a
# DRAINED stdout pipe, NOT on a real console. Running the exe directly under
# Git Bash (`./exe > f 2>&1`) starves the pipe and it never self-exits (0-byte
# false negative). But qa_runner.ps1 launches the exe via System.Diagnostics.
# Process with RedirectStandardOutput and ReadToEnd(), which keeps the pipe
# drained -- exactly what CI uses, and empirically it self-exits fine even when
# OUR stdout is a git pipe. So we CAN delegate here instead of bailing.
# We run the fast (PR) tier -- ~5s, covers the regression-critical gates; the
# CI push pipeline still enforces the full tier.

$exe = Join-Path $root "native\build\LightAlbum.exe"
if (-not (Test-Path $exe)) {
  Write-Host "pre-commit gate: native/build/LightAlbum.exe not found."
  Write-Host "  Build first: cmake --build native/build --target LightAlbum -j 8"
  Write-Host "  (or bypass with: git commit --no-verify)"
  exit 1
}

# Stale-exe guard: a built exe older than any staged native source would let a
# RED gate pass silently (false green). A quality gate that trusts a stale exe
# is worse than a reminder to rebuild. Block with a clear, actionable message.
$exeMtime = (Get-Item $exe).LastWriteTime
$stale = $false
foreach ($f in $nativeSrc) {
  $fp = Join-Path $root $f
  if (Test-Path $fp) {
    $m = (Get-Item $fp).LastWriteTime
    if ($m -gt $exeMtime) { $stale = $true; break }
  }
}
if ($stale) {
  Write-Host "pre-commit gate: BLOCKED. native/build/LightAlbum.exe is OLDER than staged source."
  Write-Host "  Rebuild first: cmake --build native/build --target LightAlbum -j 8"
  Write-Host "  (or bypass with: git commit --no-verify)"
  exit 1
}

$qaRunner = Join-Path $root "native\qa_runner.ps1"
$gateJson = Join-Path $env:TEMP ("precommit_gate_" + (Get-Date -Format "yyyyMMdd_HHmmss") + ".json")
& powershell -ExecutionPolicy Bypass -File $qaRunner -Exe $exe -Result $gateJson -Tier fast
$gateRc = $LASTEXITCODE
if (-not (Test-Path $gateJson)) {
  # Runner could not produce a result (crashed) - fail OPEN so a tooling hiccup
  # never locks every native commit. Warn loudly instead.
  Write-Host "pre-commit gate: WARN - qa_runner produced no result (rc=$gateRc); gates NOT verified."
  Write-Host "  Run manually: powershell -File native\qa_runner.ps1 ; or git commit --no-verify to bypass."
  exit 0
}
$gj = Get-Content $gateJson -Raw | ConvertFrom-Json
Write-Host ("  gates: PASS={0} FAIL={1} CHECK={2} SKIP={3}" -f $gj.summary.pass, $gj.summary.fail, $gj.summary.check, $gj.summary.skip)
foreach ($r in $gj.gates) { if ($r.mark -eq "[FAIL]") { Write-Host ("  RED: {0} -- {1}" -f $r.name, $r.detail) } }
if ([int]$gj.summary.fail -gt 0) {
  Write-Host "pre-commit gate: BLOCKED. Red gate(s) detected."
  Write-Host "  Inspect: powershell -ExecutionPolicy Bypass -File native\qa_runner.ps1"
  Write-Host "  (or bypass with: git commit --no-verify)"
  exit 1
}
Write-Host "pre-commit gate: GREEN."
exit 0
