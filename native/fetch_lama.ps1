<#
    fetch_lama.ps1 -- provision the LaMa ONNX model + ONNX Runtime dll for the
    erase bridge (inpaint.exe). Build-time human tool ONLY: the shipped app
    never touches the network (zero-cloud rule); this script is run manually.

    What it fetches (and what it NEVER fetches):
      - lama.onnx  : HuggingFace Carve/LaMa-ONNX, file lama_fp32.onnx, saved
                     locally AS lama.onnx (canonical free ONNX port of advimman
                     big-lama; local name must match EraseRunner / BUILD.md).
      - LICENSE    : the HF repo Carve/LaMa-ONNX ships NO LICENSE file (its
                     card metadata declares apache-2.0 but there is no blob to
                     fetch there). The export was produced by the
                     Carve-Photos/lama GitHub repo, which carries the Apache-2.0
                     LICENSE text; download THAT as the model's license file.
                     Placed at 3rdparty\lama\LICENSE (one level ABOVE bin,
                     mirroring the libraw/ffmpeg precedent).
      - onnxruntime.dll : Microsoft official GitHub release, pinned v1.19.2
                     win-x64 zip; the dll is copied out of the expanded zip.
      - inpaint.exe: NEVER downloaded. It must be built per inpaint/BUILD.md
                     (ONNX Runtime C ABI -- MSVC or MinGW g++ both work, the
                     ORT C API is cross-compiler stable). This script only
                     checks existence and optionally copies it in from a local
                     build directory passed via -InpaintDir.

    Fail-closed provenance (data-never-guess):
      $expectedSha below starts as the sentinel 'PIN-ON-FIRST-FETCH'. A real
      fetch run REFUSES to start until a human downloads lama_fp32.onnx once,
      computes certutil -hashfile lama_fp32.onnx SHA256, and pastes the hex
      into $expectedSha. No fallback URLs, no guessing.

    Usage:
      powershell -ExecutionPolicy Bypass -File fetch_lama.ps1
      powershell -ExecutionPolicy Bypass -File fetch_lama.ps1 -SelfTest
      powershell -ExecutionPolicy Bypass -File fetch_lama.ps1 -InpaintDir C:\dev\lama\build\Release

    NOTE: ASCII-only by project rule (iron rule 7): PS 5.1 without BOM parses
    this file as GBK, and non-ASCII comments cause "unterminated string".
#>
[CmdletBinding()]
param(
    [switch]$SelfTest,
    # Local directory that contains a prebuilt inpaint.exe (inpaint/BUILD.md
    # output). Defaults to the deployment dir itself, i.e. "already placed".
    [string]$InpaintDir
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Definition
$lamaRoot = Join-Path $root '3rdparty\lama'
$binDest = Join-Path $lamaRoot 'bin'
$licenseDest = Join-Path $lamaRoot 'LICENSE'
$modelDest = Join-Path $binDest 'lama.onnx'
$ortDllDest = Join-Path $binDest 'onnxruntime.dll'
$inpaintDest = Join-Path $binDest 'inpaint.exe'

if (-not $InpaintDir) { $InpaintDir = $binDest }

# --- provenance pins ---------------------------------------------------------
# Pinned 2026-10-11 by the first human fetch of lama_fp32.onnx from
# https://huggingface.co/Carve/LaMa-ONNX (certutil -hashfile SHA256 over the
# downloaded bytes, 208044816 total). The sentinel 'PIN-ON-FIRST-FETCH' is now
# retired BY this value: every real fetch must reproduce this exact digest or
# the script throws and no byte lands in the deployment dir.
$expectedSha = '1FAEF5301D78DB7DDA502FE59966957EC4B79DD64E16F03ED96913C7A4EB68D6'

$hfTreeHint = 'https://huggingface.co/Carve/LaMa-ONNX/tree/main'
$urlModel = 'https://huggingface.co/Carve/LaMa-ONNX/resolve/main/lama_fp32.onnx'
# HF repo has no LICENSE blob (verified via its API file tree); Apache-2.0 text
# comes from the exporting repo Carve-Photos/lama (see header notes).
$urlLicense = 'https://raw.githubusercontent.com/Carve-Photos/lama/main/LICENSE'
$urlOrtZip = 'https://github.com/microsoft/onnxruntime/releases/download/v1.19.2/onnxruntime-win-x64-1.19.2.zip'

# --- shared check mechanism (production AND -SelfTest use these) ------------

function Test-LamaLayout {
    param([string]$BinDir)
    # All three are required for the executor to run at all.
    if (-not (Test-Path (Join-Path $BinDir 'lama.onnx'))) { return $false }
    if (-not (Test-Path (Join-Path $BinDir 'onnxruntime.dll'))) { return $false }
    if (-not (Test-Path (Join-Path $BinDir 'inpaint.exe'))) { return $false }
    return $true
}

function Get-Sha256HexUpper {
    param([string]$Path)
    return (Get-FileHash -Path $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

function Invoke-ProvenanceChecks {
    param(
        [string]$ModelPath,
        [string]$LicensePath,
        [string]$ExpectedSha
    )
    # Hard LICENSE check (mirrors release.ps1 libraw/ffmpeg staged-license gate):
    # a model with unknown provenance must never ship.
    if (-not ((Test-Path -LiteralPath $LicensePath) -and ((Get-Item -LiteralPath $LicensePath).Length -gt 0))) {
        throw 'LaMa LICENSE missing -- cannot ship a model with unknown provenance'
    }
    # SHA-256 pin comparison over the model bytes.
    $actual = Get-Sha256HexUpper -Path $ModelPath
    if ($actual -ne $ExpectedSha.ToUpperInvariant()) {
        throw "lama.onnx SHA-256 mismatch: expected $ExpectedSha but file hashes $actual -- do NOT ship guessed model bytes"
    }
}

function Invoke-DownloadToTemp {
    param([string]$Url, [string]$OutFile, [string]$HintUrl)
    # No fallback URLs by design: a 404 means the upstream layout moved and a
    # human must look at the current file list, not a silent guess.
    try {
        Invoke-WebRequest -Uri $Url -OutFile $OutFile -UseBasicParsing
    } catch {
        throw "download failed: $Url ($($_.Exception.Message)) -- check the current file list at $HintUrl before retrying"
    }
}

# --- SelfTest: no network, exercises the SAME check mechanism ----------------

if ($SelfTest) {
    $tmp = Join-Path ([IO.Path]::GetTempPath()) ('la_lama_st_' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $tmp -Force | Out-Null
    try {
        $fakeModel = Join-Path $tmp 'lama.onnx'
        $fakeLicense = Join-Path $tmp 'LICENSE'
        Set-Content -Path $fakeModel -Value 'fake-model-bytes-for-selftest-only'
        Set-Content -Path $fakeLicense -Value 'fake license text (selftest)'
        Set-Content -Path (Join-Path $tmp 'inpaint.exe') -Value 'x'

        $caught = 0

        # (1) Happy path: pin the fake file's OWN computed hash at runtime;
        #     this is the production comparison path, not a parallel toy.
        $pin = Get-Sha256HexUpper -Path $fakeModel
        Invoke-ProvenanceChecks -ModelPath $fakeModel -LicensePath $fakeLicense -ExpectedSha $pin
        Write-Host 'fetch_lama selftest: happy path OK (LICENSE + SHA mechanism wired)'

        # (2) Inject LICENSE deletion -> must throw with the LICENSE message.
        Remove-Item -LiteralPath $fakeLicense -Force
        $hit = $false
        try {
            Invoke-ProvenanceChecks -ModelPath $fakeModel -LicensePath $fakeLicense -ExpectedSha $pin
        } catch {
            if ($_.Exception.Message -match 'LaMa LICENSE missing') { $hit = $true } else { throw }
        }
        if (-not $hit) { Write-Host 'fetch_lama FAIL: LICENSE-missing injection did not throw'; exit 1 }
        $caught++
        Write-Host 'fetch_lama selftest: LICENSE-missing injection caught'

        # (3) Inject SHA expectation corruption -> must throw with the mismatch message.
        Set-Content -Path $fakeLicense -Value 'fake license text (selftest)'
        $hit = $false
        try {
            Invoke-ProvenanceChecks -ModelPath $fakeModel -LicensePath $fakeLicense -ExpectedSha ('0' * 64)
        } catch {
            if ($_.Exception.Message -match 'SHA-256 mismatch') { $hit = $true } else { throw }
        }
        if (-not $hit) { Write-Host 'fetch_lama FAIL: SHA-mismatch injection did not throw'; exit 1 }
        $caught++
        Write-Host 'fetch_lama selftest: SHA-mismatch injection caught'

        if ($caught -ne 2) { Write-Host "fetch_lama FAIL caught=$caught expected=2"; exit 1 }
        Write-Host "fetch_lama OK contracts=2 caught=$caught"
        exit 0
    } finally {
        Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
    }
}

# --- Real fetch mode (the ONLY network path in this script) ------------------

# Fail-closed gate FIRST: never start a ~198MiB download we cannot verify.
if ($expectedSha -eq 'PIN-ON-FIRST-FETCH') {
    throw ('lama.onnx SHA-256 is not pinned: the sentinel PIN-ON-FIRST-FETCH is still in fetch_lama.ps1. ' +
        'A human must download lama_fp32.onnx once from ' + $urlModel + ', compute ' +
        '"certutil -hashfile lama_fp32.onnx SHA256", paste the hex into $expectedSha in this script, ' +
        'then re-run. Refusing to fetch unpinned model bytes (data-never-guess).')
}

[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

if (Test-LamaLayout $binDest) {
    Write-Host "LaMa layout already present: $binDest (re-running provenance checks)"
    Invoke-ProvenanceChecks -ModelPath $modelDest -LicensePath $licenseDest -ExpectedSha $expectedSha
    Write-Host 'LaMa already fetched and verified -- nothing to do'
    exit 0
}

New-Item -ItemType Directory -Path $binDest -Force | Out-Null
$stage = Join-Path ([IO.Path]::GetTempPath()) ('la_lama_' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stage -Force | Out-Null
try {
    # 1. Model weights (~198MiB / 208,044,816B) land in STAGING first; they are saved AS lama.onnx only AFTER
    #    verification, so a SHA-mismatch throw can never leave bad bytes in the deploy dir (Minor#1).
    $tmpModel = Join-Path $stage 'lama_fp32.onnx'
    Write-Host "Downloading model (about 198 MiB, please wait): $urlModel"
    Invoke-DownloadToTemp -Url $urlModel -OutFile $tmpModel -HintUrl $hfTreeHint

    # 2. Model LICENSE (one level above bin, alongside the other 3rdparty licenses).
    Write-Host "Downloading license: $urlLicense"
    Invoke-DownloadToTemp -Url $urlLicense -OutFile $licenseDest -HintUrl $hfTreeHint

    # 2b. Verify-before-land: run the SAME shared provenance path (as -SelfTest) against the
    #     STAGED model bytes; only a verified model is then allowed to move into bin/.
    Invoke-ProvenanceChecks -ModelPath $tmpModel -LicensePath $licenseDest -ExpectedSha $expectedSha
    Move-Item -LiteralPath $tmpModel -Destination $modelDest -Force

    # 3. onnxruntime.dll from the pinned Microsoft release zip.
    $ortZip = Join-Path $stage 'onnxruntime-win-x64-1.19.2.zip'
    $ortStage = Join-Path $stage 'ort'
    Write-Host "Downloading ONNX Runtime zip: $urlOrtZip"
    Invoke-DownloadToTemp -Url $urlOrtZip -OutFile $ortZip -HintUrl 'https://github.com/microsoft/onnxruntime/releases/tag/v1.19.2'
    New-Item -ItemType Directory -Path $ortStage -Force | Out-Null
    Expand-Archive -Path $ortZip -DestinationPath $ortStage -Force
    $ortDll = Get-ChildItem -Path $ortStage -Recurse -Filter 'onnxruntime.dll' | Select-Object -First 1
    if (-not $ortDll) { throw 'onnxruntime zip layout unexpected: onnxruntime.dll not found inside' }
    Copy-Item -LiteralPath $ortDll.FullName -Destination $ortDllDest -Force

    # 4. inpaint.exe: never downloaded; existence check + optional local copy.
    if (-not (Test-Path $inpaintDest)) {
        $candidate = Join-Path $InpaintDir 'inpaint.exe'
        # When -InpaintDir defaults to the deployment dir there is nothing to
        # copy from; only copy when a different source dir was given.
        $sameDir = ($InpaintDir.TrimEnd('\', '/') -ieq $binDest.TrimEnd('\', '/'))
        if ((Test-Path $candidate) -and (-not $sameDir)) {
            Write-Host "Copying inpaint.exe from local build dir: $InpaintDir"
            Copy-Item -LiteralPath $candidate -Destination $inpaintDest -Force
        }
    }
    if (-not (Test-Path $inpaintDest)) {
        throw ("inpaint.exe missing at $binDest -- it is NEVER downloaded. Build it per inpaint/BUILD.md " +
            '(MSVC + ONNX Runtime v1.19.2, x64) and re-run with -InpaintDir pointing at the build output dir.')
    }

    # 5. Provenance gates on the freshly fetched bytes (same mechanism as SelfTest).
    Invoke-ProvenanceChecks -ModelPath $modelDest -LicensePath $licenseDest -ExpectedSha $expectedSha
} finally {
    Remove-Item -Recurse -Force $stage -ErrorAction SilentlyContinue
}

if (-not (Test-LamaLayout $binDest)) { throw "post-fetch layout check failed: $binDest" }
Write-Host "LaMa ready: $binDest"
exit 0
