<#
    fetch_ai.ps1 -- provision the round-4 local AI runtime payload for the two
    bundled ONNX subprocesses:
        faceinfer.exe  (SCRFD-10GF detection.onnx + ArcFace w600k_r50 recognition.onnx)
        clipinfer.exe  (clip_vit_b32_img.onnx image tower + prompts.bin)
    plus the pinned ONNX Runtime v1.19.2 dll, into native\3rdparty\{face,clip}\.
    Build-time human tool ONLY: the shipped app never touches the network
    (zero-cloud rule); this script is run manually. Structure blueprint =
    native\fetch_lama.ps1 (param table / Test-*Layout / Get-Sha256HexUpper /
    Invoke-ProvenanceChecks / verify-before-land / -SelfTest counterexamples).
    One mechanism, never two (R-27).

    What it lands (exact layout, consumed verbatim by release.ps1 C10 in T10):
      3rdparty\face\LICENSE                     (version-controlled text, hard-checked)
      3rdparty\face\THIRD_PARTY-NOTICE.md       (singular, R-28b; lama precedent)
      3rdparty\face\bin\detection.onnx          (SHA-pinned, from -LocalSource)
      3rdparty\face\bin\recognition.onnx        (SHA-pinned, from -LocalSource)
      3rdparty\face\bin\faceinfer.exe           (build product, copied, never downloaded)
      3rdparty\face\bin\onnxruntime.dll         (SHA-pinned v1.19.2 copy)
      3rdparty\clip\LICENSE                     (open_clip MIT, verbatim upstream text)
      3rdparty\clip\THIRD_PARTY-NOTICE.md       (four mandatory entries, R-54)
      3rdparty\clip\bin\clip_vit_b32_img.onnx   (export product, verify-only fast path)
      3rdparty\clip\bin\prompts.bin             (export product, verify-only fast path)
      3rdparty\clip\bin\clipinfer.exe           (build product, copied, never downloaded)
      3rdparty\clip\bin\onnxruntime.dll         (same pinned copy as face/bin)

    What it NEVER fetches:
      - the model weights are NOT downloaded. face weights come from -LocalSource
        (R-25: the local source is the primary path, this run must go green with
        no network at all); clip artifacts are EXPORT products of
        ai_export\export_clip_onnx.py -- there is no second download for them
        (R-56), so a missing clip artifact is answered with the reproducible
        offline export command, never with a URL.
      - faceinfer.exe / clipinfer.exe are build products (faceinfer/, clipinfer/
        CMake projects, inpaint/BUILD.md family). Missing build output -> throw
        with the exact cmake command line (fail-closed precedent fetch_lama.ps1:231),
        never a silent skip.
      - LICENSE / THIRD_PARTY-NOTICE.md are human-authored attribution text under
        version control. Missing -> throw (a model whose attribution did not
        travel must never ship; release.ps1 C9 precedent).

    Exe integrity policy (R-109): the two exes are deliberately NOT pinned by
    SHA-256 -- pinning a rebuild output would turn every recompile red. Instead
    the script hashes the source BEFORE the copy and the destination AFTER it and
    asserts the two digests are equal (in-place copy-integrity self-check), and
    prints both. The five model/dll digests below stay hard-pinned.

    R-77 partition assertion (NOT a violation, do not "fix" it):
      git ls-files native/3rdparty/face/bin  -> 0 files   (new policy, clean)
      git ls-files native/3rdparty/clip/bin  -> 2 files   (clip_vit_b32_img.onnx +
          prompts.bin were committed by T6 in d3e36d2 and STAY tracked). Untracking
          them would only change the tree state (history payload and future push
          size stay identical; the real history decision belongs to R-49, a user
          gate) while turning "clone-and-run gate #77" into "clone must fetch
          first" -- and R-56 measured this onnx to be the ONLY copy on the machine.
      So both .gitignore lines are added anyway (tracked wins over ignore; the
      lines guide future readers) and nothing is ever git rm --cached here.

    Usage:
      powershell -NoProfile -ExecutionPolicy Bypass -File native\fetch_ai.ps1
      powershell -NoProfile -ExecutionPolicy Bypass -File native\fetch_ai.ps1 -SelfTest
      powershell -NoProfile -ExecutionPolicy Bypass -File native\fetch_ai.ps1 -ClipOnnx D:\out\clip_vit_b32_img.onnx

    NOTE: ASCII-only by project rule (iron rule 7 / E11 precedent): PS 5.1 without
    BOM parses this file as GBK and CJK comments silently drop registration lines.
#>
[CmdletBinding()]
param(
    [switch]$SelfTest,
    # R-25: local source dir holding the buffalo_l family ONNX files. Primary path.
    [string]$LocalSource = 'D:\lightalbum_ai\models\face',
    # Optional explicit sources for the clip export products (R-56 tier 2).
    [string]$ClipOnnx = '',
    [string]$ClipPrompts = '',
    # Pinned ORT v1.19.2 dll source (local extract of the official zip).
    [string]$OrtSource = 'D:\la-tools\onnxruntime-win-x64-1.19.2\lib\onnxruntime.dll',
    # Build output dirs for the two exes (empty = repo defaults below).
    [string]$FaceInferDir = '',
    [string]$ClipInferDir = '',
    # Network is OFF by design. Only the ORT dll zip has a download leg, and only
    # when a human explicitly asks for it.
    [switch]$AllowNetwork
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Definition
$thirdRoot = Join-Path $root '3rdparty'
$faceRoot = Join-Path $thirdRoot 'face'
$clipRoot = Join-Path $thirdRoot 'clip'
$faceBin = Join-Path $faceRoot 'bin'
$clipBin = Join-Path $clipRoot 'bin'
$faceLicense = Join-Path $faceRoot 'LICENSE'
$faceNotice = Join-Path $faceRoot 'THIRD_PARTY-NOTICE.md'
$clipLicense = Join-Path $clipRoot 'LICENSE'
$clipNotice = Join-Path $clipRoot 'THIRD_PARTY-NOTICE.md'

if (-not $FaceInferDir) { $FaceInferDir = Join-Path (Split-Path -Parent $root) 'faceinfer\build' }
if (-not $ClipInferDir) { $ClipInferDir = Join-Path (Split-Path -Parent $root) 'clipinfer\build' }

# --- provenance pins (controller-measured, dispatch section 2, verbatim) ------
# Digests are the lowercase hex of the pinned values; Assert-ShaPin normalizes
# case on both sides. Byte sizes in comments are the measured totals.
$faceModels = @(
    # 16,923,827 B. detection.onnx == det_10g.onnx (byte-identical pair, SCRFD-10GF).
    @{ Name = 'detection.onnx'; Sha = '5838f7fe053675b1c7a08b633df49e7af5495cee0493c7dcf6697200b85b5b91';
       Aliases = @('detection.onnx', 'det_10g.onnx') },
    # 174,383,860 B. recognition.onnx == w600k_r50.onnx (byte-identical pair, ArcFace).
    @{ Name = 'recognition.onnx'; Sha = '4c06341c33c2ca1f86781dab0e829f88ad5b64be9fba56e56bc9ebdefc619e43';
       Aliases = @('recognition.onnx', 'w600k_r50.onnx') }
)
$clipArtifacts = @(
    # 351,614,324 B (image tower only, hence smaller than the 605,143,316 B safetensors).
    @{ Name = 'clip_vit_b32_img.onnx'; Sha = '0c172d0f3c412302294962140942875abb78115b1dffdbc393ddd7fecc9c90c6' },
    # 16,436 B. NOTE: the matrix digest embedded INSIDE prompts.bin
    # (d06822a29f77911c5559d1f770793123da9f4f8e438a78bb4a82ec9dee9fc481) is a
    # startup validation constant used by clipinfer -- it is NOT this file's
    # SHA-256. Never mix the two.
    @{ Name = 'prompts.bin'; Sha = 'd7f65e31385a430ab160315891c12b2b2b6e6cd416365d495a7a62c5b06af2c1' }
)
$ortDllSha = '14119125df2dcf9ff3e083afdba5fcc4b09b4186d8762404eb7b1fbccde3fcf2'   # 11,234,848 B
$clipExportHint = '& D:\lightalbum_ai\venv_cake\Scripts\python.exe ai_export\export_clip_onnx.py'

# Required NOTICE entries (R-54). T10's C10 only asserts the NOTICE is NON-EMPTY,
# which cannot catch a missing stb credit or a soft-faced license, so the
# existence assertions live here and are exercised by -SelfTest.
$faceNoticePatterns = @('non-commercial research', 'MIT', 'onnxruntime')
$clipNoticePatterns = @('stb_image', 'MIT', 'CC-BY-4.0', 'onnxruntime')

$faceBinFiles = @('detection.onnx', 'recognition.onnx', 'faceinfer.exe', 'onnxruntime.dll')
$clipBinFiles = @('clip_vit_b32_img.onnx', 'prompts.bin', 'clipinfer.exe', 'onnxruntime.dll')
# Same ORT zip URL wording as fetch_lama.ps1 (single source of truth for the dll).
$urlOrtZip = 'https://github.com/microsoft/onnxruntime/releases/download/v1.19.2/onnxruntime-win-x64-1.19.2.zip'

# --- shared check mechanism (production AND -SelfTest use these) --------------

function Get-Sha256HexUpper {
    param([string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

function Assert-ShaPin {
    # One comparison criterion for every pinned byte: hash FIRST, land LATER
    # (R-26 verify-before-land). The throw carries expected AND actual.
    param([string]$Name, [string]$Path, [string]$ExpectedSha)
    $want = $ExpectedSha.ToUpperInvariant()
    $actual = Get-Sha256HexUpper -Path $Path
    if ($actual -ne $want) {
        throw "$Name SHA-256 mismatch: expected $ExpectedSha but file hashes $actual -- do NOT ship guessed model bytes (nothing was landed)"
    }
    return $actual
}

function Test-AiPackageLayout {
    # LICENSE + NOTICE sit ONE LEVEL ABOVE bin (lama precedent: 3rdparty/lama/LICENSE).
    param([string]$PackageRoot, [string[]]$BinFiles)
    $bin = Join-Path $PackageRoot 'bin'
    if (-not (Test-Path -LiteralPath (Join-Path $PackageRoot 'LICENSE'))) { return $false }
    if (-not (Test-Path -LiteralPath (Join-Path $PackageRoot 'THIRD_PARTY-NOTICE.md'))) { return $false }
    foreach ($f in $BinFiles) {
        if (-not (Test-Path -LiteralPath (Join-Path $bin $f))) { return $false }
    }
    return $true
}

function Invoke-ProvenanceChecks {
    param(
        [string]$PackageName,
        [string]$LicensePath,
        [string]$NoticePath,
        [string[]]$NoticePatterns,
        $ShaPairs
    )
    # Hard LICENSE check (mirrors release.ps1 C9 / the libraw-ffmpeg staged-license gate):
    # a model with unknown provenance must never ship.
    if (-not ((Test-Path -LiteralPath $LicensePath) -and ((Get-Item -LiteralPath $LicensePath).Length -gt 0))) {
        throw "$PackageName LICENSE missing or empty at $LicensePath -- cannot ship a model with unknown provenance. fetch_ai.ps1 never downloads license text: restore the version-controlled file."
    }
    # Hard NOTICE check (R-28b: singular THIRD_PARTY-NOTICE.md, one level above bin).
    if (-not ((Test-Path -LiteralPath $NoticePath) -and ((Get-Item -LiteralPath $NoticePath).Length -gt 0))) {
        throw "$PackageName THIRD_PARTY-NOTICE.md missing or empty at $NoticePath -- attribution must travel with the binaries (R-54)."
    }
    # R-54 mandatory-entry existence assertions.
    foreach ($pat in $NoticePatterns) {
        $hits = @(Select-String -LiteralPath $NoticePath -Pattern ([regex]::Escape($pat))).Count
        if ($hits -lt 1) {
            throw "$PackageName notice missing required entry '$pat' in $NoticePath -- C10 only checks the NOTICE is non-empty, so this is the assertion that catches a dropped credit (R-54)."
        }
    }
    # SHA-256 pins over the model/dll bytes. Exes are intentionally absent here
    # (R-109: no exe digest pin, copy integrity is checked pre/post copy instead).
    foreach ($p in $ShaPairs) {
        Assert-ShaPin -Name "$PackageName/$($p.Name)" -Path $p.Path -ExpectedSha $p.Sha | Out-Null
    }
}

function Resolve-FaceModel {
    # Tier 1 present+verified (verify-only) / tier 2 copy from -LocalSource after
    # hashing the SOURCE first / tier 3 missing source -> throw with the remedy.
    param([string]$Name, [string]$Sha, [string[]]$Aliases, [string]$SourceDir, [string]$DestPath)
    if (Test-Path -LiteralPath $DestPath) {
        $h = Assert-ShaPin -Name "face/$Name" -Path $DestPath -ExpectedSha $Sha
        Write-Host "  face\$Name : present+verified ($h) -- verify-only, no copy"
        return 'verified-in-place'
    }
    $src = $null
    foreach ($a in $Aliases) {
        $c = Join-Path $SourceDir $a
        if (Test-Path -LiteralPath $c) { $src = $c; break }
    }
    if (-not $src) {
        throw ("face source missing: none of [$($Aliases -join ', ')] found under -LocalSource $SourceDir. " +
            "Place the buffalo_l family ONNX files there (upstream repo hf-mirror.com/immich-app/buffalo_l; " +
            "insightface terms: the pretrained models are for non-commercial research only) and re-run. " +
            "fetch_ai.ps1 never downloads face weights (zero-cloud rule).")
    }
    $pre = Assert-ShaPin -Name "face/$Name (source $src)" -Path $src -ExpectedSha $Sha
    New-Item -ItemType Directory -Path (Split-Path -Parent $DestPath) -Force | Out-Null
    Copy-Item -LiteralPath $src -Destination $DestPath -Force
    $post = Assert-ShaPin -Name "face/$Name (landed)" -Path $DestPath -ExpectedSha $Sha
    Write-Host "  face\$Name : copied from $src (pre=$pre post=$post)"
    return 'copied-and-verified'
}

function Resolve-ClipArtifact {
    # R-56 three-tier order; tier 1 (verify-only) is the default fast path on this
    # machine and the ONLY no-network path: there is NO second download for the
    # clip export products. Do not copy the face three-step shape over this one.
    param([string]$Name, [string]$Sha, [string]$DestPath, [string]$SourcePath)
    if (Test-Path -LiteralPath $DestPath) {
        $h = Assert-ShaPin -Name "clip/$Name" -Path $DestPath -ExpectedSha $Sha
        Write-Host "  clip\$Name : present+verified ($h) -- verify-only, no copy, no download"
        return 'verified-in-place'
    }
    if ($SourcePath) {
        if (-not (Test-Path -LiteralPath $SourcePath)) {
            throw "clip source missing: $SourcePath (given via -ClipOnnx/-ClipPrompts) does not exist for $Name"
        }
        $pre = Assert-ShaPin -Name "clip/$Name (source $SourcePath)" -Path $SourcePath -ExpectedSha $Sha
        New-Item -ItemType Directory -Path (Split-Path -Parent $DestPath) -Force | Out-Null
        Copy-Item -LiteralPath $SourcePath -Destination $DestPath -Force
        $post = Assert-ShaPin -Name "clip/$Name (landed)" -Path $DestPath -ExpectedSha $Sha
        Write-Host "  clip\$Name : copied from $SourcePath (pre=$pre post=$post)"
        return 'copied-and-verified'
    }
    throw ("clip $Name missing at $DestPath and no -ClipOnnx/-ClipPrompts source given. " +
        "This file is an EXPORT product -- there is no second download for it (R-56), so no URL fallback is offered. " +
        "Reproduce it offline on this machine with: $clipExportHint " +
        "(preconditions, both already on this host: the 605,143,316 B open_clip_model.safetensors blob under " +
        "D:\lightalbum_ai\hf_cache\models--laion--CLIP-ViT-B-32-laion2B-s34B-b79K\snapshots\1a25a446712ba5ee05982a381eed697ef9b435cf " +
        "and venv_cake torch 2.6.0; OUT_DIR defaults to native\3rdparty\clip\bin).")
}

function Resolve-OrtDll {
    # Tier 1 present+verified / tier 2 pinned local extract / tier 3 the already
    # verified lama copy (identical bytes, same pin) / tier 4 download ONLY with
    # -AllowNetwork (staged, hashed before landing, then moved).
    # The digest is a PARAMETER, never read from the script-level pin, so the
    # sandbox -SelfTest path can run the identical mechanism against its own bytes
    # (R-27: production and SelfTest share one criterion).
    param([string]$DestPath, [string]$ExpectedSha, [string]$SourcePath, [string]$CrossCheckPath, [switch]$Network)
    $name = "onnxruntime.dll -> $DestPath"
    if (Test-Path -LiteralPath $DestPath) {
        $h = Assert-ShaPin -Name $name -Path $DestPath -ExpectedSha $ExpectedSha
        Write-Host "  $name : present+verified ($h)"
        return 'verified-in-place'
    }
    $src = $null
    if ($SourcePath -and (Test-Path -LiteralPath $SourcePath)) { $src = $SourcePath }
    elseif (Test-Path -LiteralPath $CrossCheckPath) {
        $cross = Assert-ShaPin -Name "$name (cross-check copy)" -Path $CrossCheckPath -ExpectedSha $ExpectedSha
        $src = $CrossCheckPath
        Write-Host "  $name : -OrtSource unused, taking the already verified pinned copy $CrossCheckPath ($cross)"
    }
    if ($src) {
        $pre = Assert-ShaPin -Name "$name (source)" -Path $src -ExpectedSha $ExpectedSha
        New-Item -ItemType Directory -Path (Split-Path -Parent $DestPath) -Force | Out-Null
        Copy-Item -LiteralPath $src -Destination $DestPath -Force
        $post = Assert-ShaPin -Name "$name (landed)" -Path $DestPath -ExpectedSha $ExpectedSha
        Write-Host "  $name : copied from $src (pre=$pre post=$post)"
        return 'copied-and-verified'
    }
    if (-not $Network) {
        throw ("onnxruntime.dll missing at $DestPath and no local source: pass -OrtSource (default $OrtSource) " +
            "or reuse the verified 3rdparty\lama\bin copy, or add -AllowNetwork to download the pinned v1.19.2 zip.")
    }
    $stage = Join-Path ([IO.Path]::GetTempPath()) ('la_ai_ort_' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $stage -Force | Out-Null
    try {
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        $zip = Join-Path $stage 'onnxruntime-win-x64-1.19.2.zip'
        Write-Host "  downloading pinned ORT zip: $urlOrtZip"
        Invoke-WebRequest -Uri $urlOrtZip -OutFile $zip -UseBasicParsing
        $ortStage = Join-Path $stage 'ort'
        New-Item -ItemType Directory -Path $ortStage -Force | Out-Null
        Expand-Archive -Path $zip -DestinationPath $ortStage -Force
        $dll = Get-ChildItem -Path $ortStage -Recurse -Filter 'onnxruntime.dll' | Select-Object -First 1
        if (-not $dll) { throw 'onnxruntime zip layout unexpected: onnxruntime.dll not found inside' }
        # verify-before-land: the staged dll must reproduce the pin before it moves.
        $pre = Assert-ShaPin -Name "$name (staged download)" -Path $dll.FullName -ExpectedSha $ExpectedSha
        New-Item -ItemType Directory -Path (Split-Path -Parent $DestPath) -Force | Out-Null
        Copy-Item -LiteralPath $dll.FullName -Destination $DestPath -Force
        $post = Assert-ShaPin -Name "$name (landed)" -Path $DestPath -ExpectedSha $ExpectedSha
        Write-Host "  $name : downloaded and verified (pre=$pre post=$post)"
        return 'downloaded-and-verified'
    } finally {
        Remove-Item -Recurse -Force $stage -ErrorAction SilentlyContinue
    }
}

function Copy-BuiltExe {
    # R-109: exe bytes carry NO pinned digest (pinning a rebuild output turns every
    # recompile red). Copy integrity is self-checked instead: hash the source BEFORE
    # the copy, hash the destination AFTER it, assert the two are equal, print both.
    # Missing build output -> throw with the reproducible build command (R-29b /
    # fetch_lama.ps1:231 fail-closed precedent: NEVER silently skip).
    param([string]$Name, [string]$DestPath, [string[]]$SourceDirs, [string]$Project, [string]$CMakeExtra, [string]$ParamHint)
    $src = $null
    foreach ($d in $SourceDirs) {
        $c = Join-Path $d $Name
        if (Test-Path -LiteralPath $c) { $src = $c; break }
    }
    if (-not $src) {
        if (Test-Path -LiteralPath $DestPath) {
            $h = Get-Sha256HexUpper -Path $DestPath
            Write-Host "  $Name : already placed, build dirs gone (dest=$h; exe is deliberately not SHA-pinned, R-109)"
            return 'already-placed'
        }
        throw ("$Name missing at $DestPath and no build output under [$($SourceDirs -join ', ')] -- it is NEVER " +
            "downloaded. Build it first: cmake -S $Project -B $Project\build -G `"MinGW Makefiles`" $CMakeExtra " +
            "and then cmake --build $Project\build -j 8 (the usage lines are quoted from $Project/CMakeLists.txt), " +
            "then re-run fetch_ai.ps1 with $ParamHint pointing at the build output directory.")
    }
    $pre = Get-Sha256HexUpper -Path $src
    if (Test-Path -LiteralPath $DestPath) {
        $have = Get-Sha256HexUpper -Path $DestPath
        if ($have -eq $pre) {
            Write-Host "  $Name : already placed and identical (pre=$pre post=$have)"
            return 'identical-in-place'
        }
        Write-Host "  $Name : placed copy differs from the build (dest=$have build=$pre) -- refreshing"
    }
    New-Item -ItemType Directory -Path (Split-Path -Parent $DestPath) -Force | Out-Null
    Copy-Item -LiteralPath $src -Destination $DestPath -Force
    $post = Get-Sha256HexUpper -Path $DestPath
    if ($post -ne $pre) {
        throw "$Name copy-integrity check failed: source hashed $pre before the copy but the landed file hashes $post -- remove $DestPath and retry"
    }
    Write-Host "  $Name : copied from $src (pre=$pre post=$post)"
    return 'copied-and-selfchecked'
}

# --- SelfTest: no network, no real tree, SAME check mechanism (R-27 / R-110) ---
# Every injection runs against a throwaway sandbox. The real native\3rdparty\**
# is never damaged and verified landed bytes are never deleted to be rebuilt.

if ($SelfTest) {
    $tmp = Join-Path ([IO.Path]::GetTempPath()) ('la_ai_st_' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $tmp -Force | Out-Null
    try {
        $sFace = Join-Path $tmp 'face'
        $sClip = Join-Path $tmp 'clip'
        $sFaceBin = Join-Path $sFace 'bin'
        $sClipBin = Join-Path $sClip 'bin'
        $sSrc = Join-Path $tmp 'LocalSource'
        $sBuild = Join-Path $tmp 'build'
        foreach ($d in @($sFaceBin, $sClipBin, $sSrc, $sBuild)) {
            New-Item -ItemType Directory -Path $d -Force | Out-Null
        }

        # Sandbox attribution text carrying every entry the real files must carry.
        $faceNoticeTxt = @(
            '# THIRD_PARTY NOTICE (selftest fixture)',
            'library code is MIT licensed; the pretrained models provided with this',
            'library are for non-commercial research only.',
            'detection = SCRFD-10GF (det_10g), recognition = ArcFace w600k_r50.',
            'runtime: onnxruntime.dll v1.19.2 MIT.'
        ) -join [Environment]::NewLine
        $clipNoticeTxt = @(
            '# THIRD_PARTY NOTICE (selftest fixture)',
            'weights: open_clip MIT; training data LAION-2B CC-BY-4.0.',
            'runtime: onnxruntime.dll v1.19.2 MIT.',
            'stb_image - v2.30 - public domain image loader - http://nothings.org/stb'
        ) -join [Environment]::NewLine
        Set-Content -LiteralPath (Join-Path $sFace 'LICENSE') -Value 'insightface research-use terms (selftest)'
        Set-Content -LiteralPath (Join-Path $sFace 'THIRD_PARTY-NOTICE.md') -Value $faceNoticeTxt
        Set-Content -LiteralPath (Join-Path $sClip 'LICENSE') -Value 'open_clip MIT (selftest)'
        Set-Content -LiteralPath (Join-Path $sClip 'THIRD_PARTY-NOTICE.md') -Value $clipNoticeTxt

        # Fake payload bytes; the pin is the hash of these OWN bytes, i.e. exactly
        # the production comparison path, not a parallel toy (fetch_lama precedent).
        Set-Content -LiteralPath (Join-Path $sSrc 'detection.onnx') -Value 'fake-det-bytes'
        $faceShaDetect = Get-Sha256HexUpper -Path (Join-Path $sSrc 'detection.onnx')
        Set-Content -LiteralPath (Join-Path $sSrc 'recognition.onnx') -Value 'fake-rec-bytes'
        $faceShaRec = Get-Sha256HexUpper -Path (Join-Path $sSrc 'recognition.onnx')
        Set-Content -LiteralPath (Join-Path $tmp 'clip_src.onnx') -Value 'fake-clip-tower-bytes'
        $clipShaOnnx = Get-Sha256HexUpper -Path (Join-Path $tmp 'clip_src.onnx')
        Set-Content -LiteralPath (Join-Path $tmp 'prompts_src.bin') -Value 'fake-prompts-bytes'
        $clipShaPrompts = Get-Sha256HexUpper -Path (Join-Path $tmp 'prompts_src.bin')
        Set-Content -LiteralPath (Join-Path $tmp 'onnxruntime.dll') -Value 'fake-ort-bytes'
        $ortSha = Get-Sha256HexUpper -Path (Join-Path $tmp 'onnxruntime.dll')
        Set-Content -LiteralPath (Join-Path $sBuild 'faceinfer.exe') -Value 'MZ-fake-faceinfer'
        Set-Content -LiteralPath (Join-Path $sBuild 'clipinfer.exe') -Value 'MZ-fake-clipinfer'

        $facePairs = @(
            @{ Name = 'detection.onnx'; Path = (Join-Path $sFaceBin 'detection.onnx'); Sha = $faceShaDetect },
            @{ Name = 'recognition.onnx'; Path = (Join-Path $sFaceBin 'recognition.onnx'); Sha = $faceShaRec },
            @{ Name = 'onnxruntime.dll'; Path = (Join-Path $sFaceBin 'onnxruntime.dll'); Sha = $ortSha }
        )
        $clipPairs = @(
            @{ Name = 'clip_vit_b32_img.onnx'; Path = (Join-Path $sClipBin 'clip_vit_b32_img.onnx'); Sha = $clipShaOnnx },
            @{ Name = 'prompts.bin'; Path = (Join-Path $sClipBin 'prompts.bin'); Sha = $clipShaPrompts },
            @{ Name = 'onnxruntime.dll'; Path = (Join-Path $sClipBin 'onnxruntime.dll'); Sha = $ortSha }
        )

        $caught = 0
        $msgs = @()

        function Wait-Throw {
            # Runs one injection: the throw MUST fire AND carry the expected keyword,
            # otherwise the counterexample is not a counterexample (iron rule #34).
            param([string]$Label, [string]$Keyword, [scriptblock]$Body)
            $script:hitMsg = $null
            try {
                & $Body | Out-Null
            } catch {
                $script:hitMsg = $_.Exception.Message
            }
            if (-not $script:hitMsg) {
                Write-Host "fetch_ai FAIL: $Label injection did not throw at all"
                exit 1
            }
            if ($script:hitMsg -notmatch ([regex]::Escape($Keyword))) {
                Write-Host "fetch_ai FAIL: $Label threw the wrong message (want keyword '$Keyword')"
                Write-Host "  actual: $script:hitMsg"
                exit 1
            }
            $script:caught++
            $script:msgs += ("[$Label] " + $script:hitMsg)
            Write-Host "fetch_ai selftest: $Label injection caught"
            Write-Host "  throw: $script:hitMsg"
        }

        # (0) Happy path: land everything into the sandbox through the PRODUCTION
        #     resolvers, then run the shared provenance pass over it.
        Resolve-FaceModel -Name 'detection.onnx' -Sha $faceShaDetect -Aliases @('detection.onnx', 'det_10g.onnx') `
            -SourceDir $sSrc -DestPath (Join-Path $sFaceBin 'detection.onnx') | Out-Null
        Resolve-FaceModel -Name 'recognition.onnx' -Sha $faceShaRec -Aliases @('recognition.onnx', 'w600k_r50.onnx') `
            -SourceDir $sSrc -DestPath (Join-Path $sFaceBin 'recognition.onnx') | Out-Null
        Resolve-ClipArtifact -Name 'clip_vit_b32_img.onnx' -Sha $clipShaOnnx `
            -DestPath (Join-Path $sClipBin 'clip_vit_b32_img.onnx') `
            -SourcePath (Join-Path $tmp 'clip_src.onnx') | Out-Null
        Resolve-ClipArtifact -Name 'prompts.bin' -Sha $clipShaPrompts `
            -DestPath (Join-Path $sClipBin 'prompts.bin') `
            -SourcePath (Join-Path $tmp 'prompts_src.bin') | Out-Null
        Resolve-OrtDll -DestPath (Join-Path $sFaceBin 'onnxruntime.dll') -ExpectedSha $ortSha `
            -SourcePath (Join-Path $tmp 'onnxruntime.dll') `
            -CrossCheckPath (Join-Path $tmp 'onnxruntime.dll') | Out-Null
        Resolve-OrtDll -DestPath (Join-Path $sClipBin 'onnxruntime.dll') -ExpectedSha $ortSha `
            -SourcePath (Join-Path $tmp 'onnxruntime.dll') `
            -CrossCheckPath (Join-Path $tmp 'onnxruntime.dll') | Out-Null
        $faceExe = Copy-BuiltExe -Name 'faceinfer.exe' -DestPath (Join-Path $sFaceBin 'faceinfer.exe') `
            -SourceDirs @((Join-Path $sBuild 'none'), $sBuild) -Project 'faceinfer' `
            -CMakeExtra '-DONNXRUNTIME_ROOT=...' -ParamHint '-FaceInferDir'
        $clipExe = Copy-BuiltExe -Name 'clipinfer.exe' -DestPath (Join-Path $sClipBin 'clipinfer.exe') `
            -SourceDirs @((Join-Path $sBuild 'none'), $sBuild) -Project 'clipinfer' `
            -CMakeExtra '-DONNXRUNTIME_ROOT=...' -ParamHint '-ClipInferDir'
        Write-Host "  exe copy status: faceinfer=$faceExe clipinfer=$clipExe"
        Write-Host '  exe landed digests (R-109 pre/post pair printed by the copier, plus this archive copy):'
        foreach ($e in @((Join-Path $sFaceBin 'faceinfer.exe'), (Join-Path $sClipBin 'clipinfer.exe'))) {
            Write-Host ("    {0} = {1}" -f (Split-Path -Leaf $e), (Get-Sha256HexUpper -Path $e))
        }
        Invoke-ProvenanceChecks -PackageName 'face' -LicensePath (Join-Path $sFace 'LICENSE') `
            -NoticePath (Join-Path $sFace 'THIRD_PARTY-NOTICE.md') -NoticePatterns $faceNoticePatterns -ShaPairs $facePairs
        Invoke-ProvenanceChecks -PackageName 'clip' -LicensePath (Join-Path $sClip 'LICENSE') `
            -NoticePath (Join-Path $sClip 'THIRD_PARTY-NOTICE.md') -NoticePatterns $clipNoticePatterns -ShaPairs $clipPairs
        if (-not (Test-AiPackageLayout -PackageRoot $sFace -BinFiles $faceBinFiles)) { Write-Host 'fetch_ai FAIL: sandbox face layout negative'; exit 1 }
        if (-not (Test-AiPackageLayout -PackageRoot $sClip -BinFiles $clipBinFiles)) { Write-Host 'fetch_ai FAIL: sandbox clip layout negative'; exit 1 }
        Write-Host 'fetch_ai selftest: happy path OK (LICENSE + NOTICE entries + SHA + exe copy-integrity wired)'

        # (1) Inject bad SHA -- face track (corrupted expectation, same exit path).
        Wait-Throw -Label 'SHA-mismatch(face)' -Keyword 'SHA-256 mismatch' -Body {
            Invoke-ProvenanceChecks -PackageName 'face' -LicensePath (Join-Path $sFace 'LICENSE') `
                -NoticePath (Join-Path $sFace 'THIRD_PARTY-NOTICE.md') -NoticePatterns $faceNoticePatterns `
                -ShaPairs @(@{ Name = 'detection.onnx'; Path = (Join-Path $sFaceBin 'detection.onnx'); Sha = ('0' * 64) })
        }

        # (1b) Inject bad SHA -- clip track (both tracks share one criterion).
        Wait-Throw -Label 'SHA-mismatch(clip)' -Keyword 'SHA-256 mismatch' -Body {
            Invoke-ProvenanceChecks -PackageName 'clip' -LicensePath (Join-Path $sClip 'LICENSE') `
                -NoticePath (Join-Path $sClip 'THIRD_PARTY-NOTICE.md') -NoticePatterns $clipNoticePatterns `
                -ShaPairs @(@{ Name = 'clip_vit_b32_img.onnx'; Path = (Join-Path $sClipBin 'clip_vit_b32_img.onnx'); Sha = ('0' * 64) })
        }

        # (2) Inject LICENSE deletion -> must throw with the LICENSE message.
        Wait-Throw -Label 'LICENSE-missing' -Keyword 'LICENSE missing or empty' -Body {
            Remove-Item -LiteralPath (Join-Path $sFace 'LICENSE') -Force
            Invoke-ProvenanceChecks -PackageName 'face' -LicensePath (Join-Path $sFace 'LICENSE') `
                -NoticePath (Join-Path $sFace 'THIRD_PARTY-NOTICE.md') -NoticePatterns $faceNoticePatterns -ShaPairs $facePairs
        }
        Set-Content -LiteralPath (Join-Path $sFace 'LICENSE') -Value 'insightface research-use terms (selftest)'

        # (2b) Inject NOTICE deletion on the clip side -> hard check fires (R-54).
        Wait-Throw -Label 'NOTICE-missing' -Keyword 'THIRD_PARTY-NOTICE.md missing or empty' -Body {
            Remove-Item -LiteralPath (Join-Path $sClip 'THIRD_PARTY-NOTICE.md') -Force
            Invoke-ProvenanceChecks -PackageName 'clip' -LicensePath (Join-Path $sClip 'LICENSE') `
                -NoticePath (Join-Path $sClip 'THIRD_PARTY-NOTICE.md') -NoticePatterns $clipNoticePatterns -ShaPairs $clipPairs
        }
        Set-Content -LiteralPath (Join-Path $sClip 'THIRD_PARTY-NOTICE.md') -Value $clipNoticeTxt

        # (3a) Missing SOURCE, face flavour: remove BOTH byte-identical aliases from
        #      the sandbox -LocalSource (the real tree is never touched, R-110).
        $sDet = Join-Path $sSrc 'detection.onnx'
        $sDetAlias = Join-Path $sSrc 'det_10g.onnx'
        Remove-Item -LiteralPath $sDet -Force
        if (Test-Path -LiteralPath $sDetAlias) { Remove-Item -LiteralPath $sDetAlias -Force }
        Wait-Throw -Label 'source-missing(face)' -Keyword 'face source missing' -Body {
            Resolve-FaceModel -Name 'detection.onnx' -Sha $faceShaDetect `
                -Aliases @('detection.onnx', 'det_10g.onnx') -SourceDir $sSrc `
                -DestPath (Join-Path $sFaceBin 'detection-absent.onnx')
        }
        $faceMissing = $msgs[$msgs.Count - 1]
        Set-Content -LiteralPath $sDet -Value 'fake-det-bytes'

        # (3b) Missing SOURCE, clip flavour: tier 3 of R-56 -- no download, the
        #     throw must carry the export command. The two messages MUST differ;
        #     otherwise the three-tier order has no counter-proof.
        Wait-Throw -Label 'source-missing(clip)' -Keyword 'export_clip_onnx.py' -Body {
            Resolve-ClipArtifact -Name 'clip_vit_b32_img.onnx' -Sha $clipShaOnnx `
                -DestPath (Join-Path $sClipBin 'clip-absent.onnx') -SourcePath ''
        }
        $clipMissing = $msgs[$msgs.Count - 1]
        if ($faceMissing -eq $clipMissing) { Write-Host 'fetch_ai FAIL: face/clip missing-source throws are identical (R-56 tier 3 unproven)'; exit 1 }
        if ($faceMissing -match 'export_clip_onnx\.py') { Write-Host 'fetch_ai FAIL: face source-missing message leaked the clip export hint'; exit 1 }

        # (4) Drop the stb_image line from the clip NOTICE -> must go red. This is
        #     the counter-proof that the R-54 entry assertions are live (C10 only
        #     checks non-empty and would stay green).
        Wait-Throw -Label 'notice-entry-stb-dropped' -Keyword "required entry 'stb_image'" -Body {
            Set-Content -LiteralPath (Join-Path $sClip 'THIRD_PARTY-NOTICE.md') `
                -Value ($clipNoticeTxt -replace 'stb_image', 'third-party single-header loader')
            Invoke-ProvenanceChecks -PackageName 'clip' -LicensePath (Join-Path $sClip 'LICENSE') `
                -NoticePath (Join-Path $sClip 'THIRD_PARTY-NOTICE.md') -NoticePatterns $clipNoticePatterns -ShaPairs $clipPairs
        }
        Set-Content -LiteralPath (Join-Path $sClip 'THIRD_PARTY-NOTICE.md') -Value $clipNoticeTxt

        # (5) Exe missing in BOTH dest and build dirs -> fail-closed throw with the
        #     build command (fetch_lama.ps1:231 precedent, never a silent skip).
        Wait-Throw -Label 'exe-missing' -Keyword 'NEVER downloaded' -Body {
            Copy-BuiltExe -Name 'faceinfer.exe' -DestPath (Join-Path $sFaceBin 'absent-faceinfer.exe') `
                -SourceDirs @(Join-Path $sBuild 'nowhere') -Project 'faceinfer' `
                -CMakeExtra '-DONNXRUNTIME_ROOT=...' -ParamHint '-FaceInferDir'
        }

        if ($caught -ne 8) { Write-Host "fetch_ai FAIL caught=$caught expected=8"; exit 1 }
        Write-Host "fetch_ai OK contracts=8 caught=$caught"
        exit 0
    } finally {
        Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
    }
}

# --- Real provisioning (no network unless -AllowNetwork is passed) -------------

Write-Host 'fetch_ai.ps1: provisioning native\3rdparty\{face,clip}'
Write-Host "  -LocalSource=$LocalSource  -AllowNetwork=$AllowNetwork"

# Fail-closed attribution gate FIRST: LICENSE + NOTICE are version-controlled text
# and must be present before a single model byte is hashed or copied.
Invoke-ProvenanceChecks -PackageName 'face' -LicensePath $faceLicense -NoticePath $faceNotice `
    -NoticePatterns $faceNoticePatterns -ShaPairs @()
Invoke-ProvenanceChecks -PackageName 'clip' -LicensePath $clipLicense -NoticePath $clipNotice `
    -NoticePatterns $clipNoticePatterns -ShaPairs @()

New-Item -ItemType Directory -Path $faceBin -Force | Out-Null
New-Item -ItemType Directory -Path $clipBin -Force | Out-Null

# 1. face weights: verify-before-land from the local source (R-25 / R-26).
foreach ($m in $faceModels) {
    Resolve-FaceModel -Name $m.Name -Sha $m.Sha -Aliases $m.Aliases `
        -SourceDir $LocalSource -DestPath (Join-Path $faceBin $m.Name) | Out-Null
}

# 2. clip export products: R-56 three-tier order, verify-only is the default here
#    (the 351 MB tower is the only copy on the machine; gate #77 lives off tier 1).
Resolve-ClipArtifact -Name 'clip_vit_b32_img.onnx' `
    -Sha $clipArtifacts[0].Sha -DestPath (Join-Path $clipBin 'clip_vit_b32_img.onnx') `
    -SourcePath $ClipOnnx | Out-Null
Resolve-ClipArtifact -Name 'prompts.bin' `
    -Sha $clipArtifacts[1].Sha -DestPath (Join-Path $clipBin 'prompts.bin') `
    -SourcePath $ClipPrompts | Out-Null

# 3. ORT dll: three pinned copies (lama/bin already holds one; face and clip each
#    get the same verified bytes so each subprocess is cwd-isolated).
$lamaOrt = Join-Path $thirdRoot 'lama\bin\onnxruntime.dll'
Resolve-OrtDll -DestPath (Join-Path $faceBin 'onnxruntime.dll') -ExpectedSha $ortDllSha `
    -SourcePath $OrtSource -CrossCheckPath $lamaOrt -Network:$AllowNetwork | Out-Null
Resolve-OrtDll -DestPath (Join-Path $clipBin 'onnxruntime.dll') -ExpectedSha $ortDllSha `
    -SourcePath $OrtSource -CrossCheckPath $lamaOrt -Network:$AllowNetwork | Out-Null

# 4. the two exes: build products, never downloaded, pre/post copy SHA asserted.
$repoRoot = Split-Path -Parent $root
Copy-BuiltExe -Name 'faceinfer.exe' -DestPath (Join-Path $faceBin 'faceinfer.exe') `
    -SourceDirs @($FaceInferDir, (Join-Path $repoRoot 'faceinfer\build'), (Join-Path $root 'build')) `
    -Project 'faceinfer' -ParamHint '-FaceInferDir' `
    -CMakeExtra '-DONNXRUNTIME_ROOT="D:/la-tools/onnxruntime-win-x64-1.19.2" -DCMAKE_PREFIX_PATH="D:/qt/6.8.3/mingw_64"' | Out-Null
Copy-BuiltExe -Name 'clipinfer.exe' -DestPath (Join-Path $clipBin 'clipinfer.exe') `
    -SourceDirs @($ClipInferDir, (Join-Path $repoRoot 'clipinfer\build'), (Join-Path $root 'build')) `
    -Project 'clipinfer' -ParamHint '-ClipInferDir' `
    -CMakeExtra '-DONNXRUNTIME_ROOT="D:/la-tools/onnxruntime-win-x64-1.19.2"' | Out-Null

# 5. Provenance gates on the landed tree (same mechanism as -SelfTest), then layout.
$facePairs = @()
foreach ($m in $faceModels) {
    $facePairs += @{ Name = $m.Name; Path = (Join-Path $faceBin $m.Name); Sha = $m.Sha }
}
$facePairs += @{ Name = 'onnxruntime.dll'; Path = (Join-Path $faceBin 'onnxruntime.dll'); Sha = $ortDllSha }
$clipPairs = @()
foreach ($c in $clipArtifacts) {
    $clipPairs += @{ Name = $c.Name; Path = (Join-Path $clipBin $c.Name); Sha = $c.Sha }
}
$clipPairs += @{ Name = 'onnxruntime.dll'; Path = (Join-Path $clipBin 'onnxruntime.dll'); Sha = $ortDllSha }
Invoke-ProvenanceChecks -PackageName 'face' -LicensePath $faceLicense -NoticePath $faceNotice `
    -NoticePatterns $faceNoticePatterns -ShaPairs $facePairs
Invoke-ProvenanceChecks -PackageName 'clip' -LicensePath $clipLicense -NoticePath $clipNotice `
    -NoticePatterns $clipNoticePatterns -ShaPairs $clipPairs

if (-not (Test-AiPackageLayout -PackageRoot $faceRoot -BinFiles $faceBinFiles)) { throw "post-fetch layout check failed: $faceBin" }
if (-not (Test-AiPackageLayout -PackageRoot $clipRoot -BinFiles $clipBinFiles)) { throw "post-fetch layout check failed: $clipBin" }

Write-Host 'landed file table (byte size + SHA-256, measured from the deploy tree):'
foreach ($p in @($facePairs + $clipPairs)) {
    $item = Get-Item -LiteralPath $p.Path
    Write-Host ("  {0,-46} {1,12} B  {2}" -f $p.Path.Substring($root.Length + 1), $item.Length, (Get-Sha256HexUpper -Path $p.Path))
}
foreach ($p in @((Join-Path $faceBin 'faceinfer.exe'), (Join-Path $clipBin 'clipinfer.exe'))) {
    $item = Get-Item -LiteralPath $p
    Write-Host ("  {0,-46} {1,12} B  {2}  (build product, copy-self-checked, not SHA-pinned)" -f $p.Substring($root.Length + 1), $item.Length, (Get-Sha256HexUpper -Path $p))
}
Write-Host 'face package ready:' $faceRoot
Write-Host 'clip package ready:' $clipRoot
exit 0
