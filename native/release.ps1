# LightAlbum native release packager (Windows)
# Build first (native/build/LightAlbum.exe), then run:
#   powershell -File release.ps1                 # clean deploy, no trim
#   powershell -File release.ps1 -TrimNetwork    # also drop Qt6Network+tls+networkinformation+generic
# Clean approach: copy only the exe, then windeployqt deploys exactly the
# needed Qt DLLs/plugins (no CMake .obj/Makefile junk). Delete-free: uses
# Move-Item to relocate trimmed files and any prior zip/staging.
param(
  [string]$Version = "2.0.0",
  [switch]$TrimNetwork
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Definition
$build = Join-Path $root "build"
$exeSrc = Join-Path $build "LightAlbum.exe"
if (-not (Test-Path $exeSrc)) { throw "build/LightAlbum.exe not found; run cmake + mingw32-make first" }

$stagingName = "LightAlbum-$Version-win64"
$stagingBase = Join-Path $root $stagingName
$staging = $stagingBase

# Fresh staging (relocate any existing aside instead of deleting)
if (Test-Path $staging) {
  $ts = Get-Date -Format "yyyyMMddHHmmss"
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
#    --force: 必须加 —— 中断过的 windeployqt 会留下「同尺寸但损坏」的 DLL，
#    重跑默认按时间戳/尺寸跳过，导致 0xC000012F 启动失败（P13 实证）。
#    用全路径调用（字面值，规避 & $var 在本环境偶发 BadExpression）。
#    输出重定向到日志（*> 而非 2>&1 | Out-Null）：dxcompiler 警告写 stderr 时，
#    PowerShell 管道会把原生命令中断当成 NativeCommandError，导致部署半成品。
# Start-Process + RedirectStandard*: bare file redirects, no PS 5.1
# NativeCommandError conversion of native stderr (dxcompiler warning was
# killing the deploy between DLL copy and plugin copy -- P17 evidence).
$ErrorActionPreference = "Continue"
# Direct invocation, NO redirect/pipe: PS 5.1 converts native stderr to
# NativeCommandError only under redirection/pipe (P13: 2>&1|, P17: *>file --
# both truncated the deploy between DLL copy and plugin copy). Bare call is
# immune; dxcompiler warning just prints.
& "D:/qt/6.8.3/mingw_64/bin/windeployqt.exe" --release --force --no-translations --no-qml --no-opengl-sw (Join-Path $staging "LightAlbum.exe")
Write-Host "windeployqt exit code: $LASTEXITCODE"
$ErrorActionPreference = "Stop"
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed with exit code $LASTEXITCODE" }
if (-not (Test-Path (Join-Path $staging "platforms"))) { throw "windeployqt left no platforms/ dir" }

# 3) offscreen platform plugin for CI/smoke
Copy-Item "D:/qt/6.8.3/mingw_64/plugins/platforms/qoffscreen.dll" (Join-Path $staging "platforms/qoffscreen.dll") -Force

# 3.5) user manual (matched via ASCII wildcard; copied as USER-MANUAL.md)
$docsDir = Join-Path $root "..\docs"
$manual = Get-ChildItem $docsDir -Filter "LightAlbum-2.0.0-*.md" -File | Select-Object -First 1
if ($manual) { Copy-Item $manual.FullName (Join-Path $staging "USER-MANUAL.md") -Force }

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

# 5) zip (relocate any prior zip instead of deleting)
$zip = Join-Path $root "$stagingName.zip"
if (Test-Path $zip) { Move-Item $zip "$zip.bak" -Force }
Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::CreateFromDirectory($staging, $zip,
    [System.IO.Compression.CompressionLevel]::Optimal, $false)
$size = [math]::Round((Get-Item $zip).Length / 1MB, 1)
Write-Host "Created $zip ($size MB)"
