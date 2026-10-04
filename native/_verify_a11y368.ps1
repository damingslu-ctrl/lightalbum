param([switch]$Broken)
# F3 a11y keyboard gate verification (MapView + GalleryTile).
# Mirrors qa_runner.ps1 launch pattern: offscreen, redirected streams, self-exit.
$env:QT_QPA_PLATFORM = "offscreen"
$env:LA_GATE = "LA_A11Y_KEYBOARD_QA"
$env:LA_A11Y_KEYBOARD_QA = "1"
if ($Broken) { $env:LA_A11Y_KEYBOARD_QA_FORCE_BROKEN = "1" }
$exe = Join-Path $PSScriptRoot "build/LightAlbum.exe"
$p = New-Object System.Diagnostics.ProcessStartInfo
$p.FileName = $exe
$p.UseShellExecute = $false
$p.RedirectStandardOutput = $true
$p.RedirectStandardError = $true
$p.RedirectStandardInput = $true
$p.CreateNoWindow = $true
$pr = New-Object System.Diagnostics.Process
$pr.StartInfo = $p
$pr.Start() | Out-Null
$ok = $pr.WaitForExit(25000)
$raw = ""
try { $raw = $pr.StandardOutput.ReadToEnd() + $pr.StandardError.ReadToEnd() } catch {}
Write-Host ("EXIT=" + $pr.ExitCode + " KILLED=" + (-not $ok))
$raw -split "`r?`n" | Where-Object { $_ -match 'LA_A11Y_KEYBOARD_QA' } | ForEach-Object { Write-Host $_ }
