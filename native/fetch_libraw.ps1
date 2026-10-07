<#
    fetch_libraw.ps1 -- download LibRaw prebuilt binaries for RAW support.

    Why a script instead of committing binaries:
      This repo tracks ZERO binaries (no exe/dll/zip). LibRaw is a third-party
      LGPL/CDDL runtime dependency, fetched at build time, never committed.

    Why prebuilt instead of building from source:
      LibRaw ships only MSVC prebuilt Windows binaries. Because the decoder runs
      as a SEPARATE PROCESS (QProcess), the host compiler (MinGW) and the decoder
      compiler (MSVC) are fully decoupled -- no CRT/ABI binding. Verified working.

    Usage:
      powershell -ExecutionPolicy Bypass -File fetch_libraw.ps1
      powershell -ExecutionPolicy Bypass -File fetch_libraw.ps1 -Version 0.22.2
      powershell -ExecutionPolicy Bypass -File fetch_libraw.ps1 -SelfTest

    NOTE: ASCII-only by project rule (iron rule 7): PS 5.1 without BOM parses
    this file as GBK, and non-ASCII comments cause "unterminated string".
#>
[CmdletBinding()]
param(
    [string]$Version = '0.22.2',
    [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Definition
$destRoot = Join-Path $root '3rdparty\libraw'
$binDest = Join-Path $destRoot 'bin'

function Test-LibRawLayout {
    param([string]$BinDir)
    # dcraw_emu.exe is the only hard requirement for decode.
    if (-not (Test-Path (Join-Path $BinDir 'dcraw_emu.exe'))) { return $false }
    if (-not (Test-Path (Join-Path $BinDir 'libraw.dll'))) { return $false }
    return $true
}

if ($SelfTest) {
    # Contract: layout probe must reject an empty dir and accept a populated one.
    $tmp = Join-Path ([IO.Path]::GetTempPath()) ("la_libraw_st_" + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $tmp -Force | Out-Null
    try {
        if (Test-LibRawLayout $tmp) { Write-Host 'LA_LIBRAW_FETCH FAIL empty-accepted'; exit 1 }
        Set-Content -Path (Join-Path $tmp 'dcraw_emu.exe') -Value 'x'
        Set-Content -Path (Join-Path $tmp 'libraw.dll') -Value 'x'
        if (-not (Test-LibRawLayout $tmp)) { Write-Host 'LA_LIBRAW_FETCH FAIL populated-rejected'; exit 1 }
        Write-Host 'LA_LIBRAW_FETCH PASS contracts=2'
        exit 0
    } finally {
        Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
    }
}

if (Test-LibRawLayout $binDest) {
    Write-Host "LibRaw already present: $binDest"
    exit 0
}

$url = "https://www.libraw.org/data/LibRaw-$Version-Win64.zip"
$zip = Join-Path ([IO.Path]::GetTempPath()) ("LibRaw-$Version-Win64.zip")
$stage = Join-Path ([IO.Path]::GetTempPath()) ("la_libraw_" + [Guid]::NewGuid().ToString('N'))

Write-Host "Downloading $url"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing

New-Item -ItemType Directory -Path $stage -Force | Out-Null
try {
    Expand-Archive -Path $zip -DestinationPath $stage -Force
    $src = Get-ChildItem -Path $stage -Directory | Select-Object -First 1
    if (-not $src) { throw 'archive layout unexpected: no top-level dir' }
    $srcBin = Join-Path $src.FullName 'bin'

    New-Item -ItemType Directory -Path $binDest -Force | Out-Null
    # Ship only what we actually invoke. Keeps dist small and the notice auditable.
    foreach ($exe in @('dcraw_emu.exe', 'simple_dcraw.exe', 'raw-identify.exe', 'libraw.dll')) {
        $p = Join-Path $srcBin $exe
        if (Test-Path $p) { Copy-Item $p $binDest -Force }
    }
    # License texts must travel with the binaries (LGPL 2.1 / CDDL 1.0 dual license).
    foreach ($lic in @('LICENSE.LGPL', 'LICENSE.CDDL', 'COPYRIGHT')) {
        $p = Join-Path $src.FullName $lic
        if (Test-Path $p) { Copy-Item $p $destRoot -Force }
    }
} finally {
    Remove-Item -Recurse -Force $stage -ErrorAction SilentlyContinue
    Remove-Item -Force $zip -ErrorAction SilentlyContinue
}

if (-not (Test-LibRawLayout $binDest)) { throw "post-fetch layout check failed: $binDest" }
Write-Host "LibRaw $Version ready: $binDest"
exit 0
