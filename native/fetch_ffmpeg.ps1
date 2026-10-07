<#
    fetch_ffmpeg.ps1 -- download FFmpeg prebuilt binaries for video support.

    Why prebuilt instead of building from source:
      FFmpeg's configure is large and the MinGW cross-build is fragile. Because
      the decoder runs as a SEPARATE PROCESS (QProcess), the host compiler
      (MinGW) and the decoder compiler (gcc/MSYS2) are fully decoupled.

    Licensing note (READ BEFORE REDISTRIBUTING):
      The gyan.dev "essentials" build is configured with
      --enable-gpl --enable-version3 --enable-libx264, i.e. it is GPL-3.0, NOT
      LGPL. We only use it to decode video and extract one JPEG frame, so the
      x264 encoder is dead weight, but the GPL trigger happens at BUILD time.
      Shipping it alongside LightAlbum therefore requires the GPL-3.0 text to
      travel with it, which release.ps1 enforces.

    Size note:
      ffmpeg.exe ~100MB + ffprobe.exe ~100MB, fully static (no DLLs).
      ffplay.exe (~107MB) is deliberately NOT shipped -- we never play video.
      Expect the release package to grow by roughly 200MB.

    Usage:
      powershell -ExecutionPolicy Bypass -File fetch_ffmpeg.ps1
      powershell -ExecutionPolicy Bypass -File fetch_ffmpeg.ps1 -SelfTest

    NOTE: ASCII-only by project rule (iron rule 7): PS 5.1 without BOM parses
    this file as GBK, and non-ASCII comments cause "unterminated string".
#>
[CmdletBinding()]
param(
    [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Definition
$destRoot = Join-Path $root '3rdparty\ffmpeg'
$binDest = Join-Path $destRoot 'bin'

function Test-FFmpegLayout {
    param([string]$BinDir)
    # Both are required: ffmpeg extracts the poster, ffprobe reads metadata.
    if (-not (Test-Path (Join-Path $BinDir 'ffmpeg.exe'))) { return $false }
    if (-not (Test-Path (Join-Path $BinDir 'ffprobe.exe'))) { return $false }
    return $true
}

if ($SelfTest) {
    # Contract: layout probe must reject an empty dir and accept a populated one.
    $tmp = Join-Path ([IO.Path]::GetTempPath()) ('la_ffmpeg_st_' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $tmp -Force | Out-Null
    try {
        if (Test-FFmpegLayout $tmp) { Write-Host 'LA_FFMPEG_FETCH FAIL empty-accepted'; exit 1 }
        Set-Content -Path (Join-Path $tmp 'ffmpeg.exe') -Value 'x'
        # Only ffmpeg present -> must still reject (ffprobe missing).
        if (Test-FFmpegLayout $tmp) { Write-Host 'LA_FFMPEG_FETCH FAIL partial-accepted'; exit 1 }
        Set-Content -Path (Join-Path $tmp 'ffprobe.exe') -Value 'x'
        if (-not (Test-FFmpegLayout $tmp)) { Write-Host 'LA_FFMPEG_FETCH FAIL full-rejected'; exit 1 }
        Write-Host 'LA_FFMPEG_FETCH PASS contracts=3'
        exit 0
    } finally {
        Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
    }
}

if (Test-FFmpegLayout $binDest) {
    Write-Host "FFmpeg already present: $binDest"
    exit 0
}

# gyan.dev publishes versioned packages under /builds/packages/; the
# "release-essentials" alias always points at the newest one.
$url = 'https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip'
$zip = Join-Path ([IO.Path]::GetTempPath()) 'ffmpeg-release-essentials.zip'
$stage = Join-Path ([IO.Path]::GetTempPath()) ('la_ffmpeg_' + [Guid]::NewGuid().ToString('N'))

Write-Host "Downloading $url (about 110MB, please wait)"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing

New-Item -ItemType Directory -Path $stage -Force | Out-Null
try {
    Expand-Archive -Path $zip -DestinationPath $stage -Force
    $src = Get-ChildItem -Path $stage -Directory | Select-Object -First 1
    if (-not $src) { throw 'archive layout unexpected: no top-level dir' }
    $srcBin = Join-Path $src.FullName 'bin'

    New-Item -ItemType Directory -Path $binDest -Force | Out-Null
    # ffplay.exe intentionally omitted (we never play video; saves ~107MB).
    foreach ($exe in @('ffmpeg.exe', 'ffprobe.exe')) {
        $p = Join-Path $srcBin $exe
        if (Test-Path $p) { Copy-Item $p $binDest -Force }
    }
    # GPL-3.0 text MUST travel with the binaries.
    foreach ($f in @('LICENSE', 'README.txt')) {
        $p = Join-Path $src.FullName $f
        if (Test-Path $p) { Copy-Item $p $destRoot -Force }
    }
} finally {
    Remove-Item -Recurse -Force $stage -ErrorAction SilentlyContinue
    Remove-Item -Force $zip -ErrorAction SilentlyContinue
}

if (-not (Test-FFmpegLayout $binDest)) { throw "post-fetch layout check failed: $binDest" }
Write-Host "FFmpeg ready: $binDest"
exit 0
