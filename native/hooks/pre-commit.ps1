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

# The gate runner (qa_runner.ps1) needs a real console for the native exe to
# self-exit; under git's piped hook stdout it would hang ~25s per gate
# (minutes total) or die. So we only RUN the gate when stdout is a console.
# Under git's pipe we skip fast (fail-open) and point to the reliable gates:
# CI workflow (runs iterate.ps1) and release.ps1 (gates before packaging).
if ([Console]::IsOutputRedirected) {
  Write-Host "pre-commit gate: stdout is redirected (git pipe) - cannot run console-dependent gates here."
  Write-Host "  Verify manually:  powershell -File native\qa_runner.ps1   (or: native\iterate.ps1 -NoBuild)"
  Write-Host "  Reliable auto-gate: CI runs iterate.ps1; release.ps1 gates before packaging."
  exit 0
}

$exe = Join-Path $root "native\build\LightAlbum.exe"
if (-not (Test-Path $exe)) {
  Write-Host "pre-commit gate: native/build/LightAlbum.exe not found."
  Write-Host "  Build first: cmake --build native/build --target LightAlbum -j 8"
  Write-Host "  (or bypass with: git commit --no-verify)"
  exit 1
}

$qaRunner = Join-Path $root "native\qa_runner.ps1"
$gateJson = Join-Path $env:TEMP ("precommit_gate_" + (Get-Date -Format "yyyyMMdd_HHmmss") + ".json")
& powershell -ExecutionPolicy Bypass -File $qaRunner -Exe $exe -Result $gateJson
$gateRc = $LASTEXITCODE
if (-not (Test-Path $gateJson)) {
  # Runner could not produce a result (crashed/no console) - fail OPEN so a
  # tooling hiccup never locks every native commit. Warn loudly instead.
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
