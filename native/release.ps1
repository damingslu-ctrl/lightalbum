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
$staging = Join-Path $root $stagingName

# Fresh staging (relocate any existing aside instead of deleting)
if (Test-Path $staging) {
  $ts = Get-Date -Format "yyyyMMddHHmmss"
  Move-Item $staging "$staging-$ts" -Force
}
New-Item -ItemType Directory -Path $staging | Out-Null

# 1) copy only the executable
Copy-Item $exeSrc (Join-Path $staging "LightAlbum.exe") -Force

# 2) deploy Qt via windeployqt (clean: only required DLLs + plugins)
$env:PATH = "D:/qt/6.8.3/mingw_64/bin;" + $env:PATH
windeployqt.exe --release --no-translations --no-qml --no-opengl-sw (Join-Path $staging "LightAlbum.exe") | Out-Null

# 3) offscreen platform plugin for CI/smoke
Copy-Item "D:/qt/6.8.3/mingw_64/plugins/platforms/qoffscreen.dll" (Join-Path $staging "platforms/qoffscreen.dll") -Force

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
