# FSR 4.1.2 input-color test: HDR (linear, pre-tonemap R11G11B10_FLOAT, HdrInput=1) or LDR (tonemapped 8-bit, HdrInput=0),
# same build, same landed-planet scene, so the two can be compared for cost, stability and image quality.
#
# What it does: deploys the build with the chosen input, waits while you start the game and settle in the scene, asks you
# to take a same-spot screenshot, records a 60 s PresentMon capture, waits for you to quit, snapshots the config, checks
# edpe.log (FSR 4 context created with the right hdr flag, no evaluate failures or fallbacks) and prints the engine's
# profiling averages plus a frame-time comparison against the earlier FSR 4 (LDR) and FSR 3.1 runs.
#
# Run from your own PowerShell window (it prompts). Do the HDR run first, then the LDR repeat for a fair A/B of the new build:
#   powershell -NoProfile -ExecutionPolicy Bypass -File C:\Users\<user>\Development\GPU_Performance_Analysis\tools\run_hdr_test.ps1 -Hdr 1
#   powershell -NoProfile -ExecutionPolicy Bypass -File C:\Users\<user>\Development\GPU_Performance_Analysis\tools\run_hdr_test.ps1 -Hdr 0
# Add -Run 2 to repeat; -Mode Quality or NativeAA to change the scale. Existing captures are never overwritten.
# While a run is going you can also switch FSR 3.1.2 / FSR 4.1.2 from the EDPE menu (Insert) for a quick visual check.
param(
    [Parameter(Mandatory = $true)][ValidateSet(0, 1)][int]$Hdr,
    [ValidateSet('NativeAA', 'Quality', 'Performance')][string]$Mode = 'Performance',
    [int]$Run = 1
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$game = 'C:\Program Files (x86)\Steam\steamapps\common\Elite Dangerous\Products\elite-dangerous-odyssey-64'
$py = 'C:\Users\<user>\AppData\Local\Programs\Python\Python312\python.exe'
$tag = if ($Hdr -eq 1) { 'HDR' } else { 'LDR' }
$name = "Planet_Landed_Static_F4_${Mode}_${tag}_$Run"
$label = "F4_${Mode}_$tag"
$csv = Join-Path $root "baseline\presentmon\$name.csv"

if (Get-Process EliteDangerous64 -ErrorAction SilentlyContinue) { throw 'Close the game first (EliteDangerous64.exe is running).' }
if (Test-Path $csv) { throw "Capture already exists: $csv. Use -Run $($Run + 1)." }

Write-Host "== 1. Deploy current build: FSR 4, Mode=$Mode, HdrInput=$Hdr ($tag), profiling on, flush on =="
$deployArgs = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'deploy_fsr4_poc.ps1'),
    '-Action', 'Deploy', '-Mode', $Mode, '-Fsr4', 1, '-Profile', '-Flush', 1)
if ($Hdr -eq 1) { $deployArgs += '-HdrInput' }
& powershell @deployArgs
if ($LASTEXITCODE -ne 0) { throw 'Deploy failed.' }
Remove-Item (Join-Path $game 'edpe.log') -ErrorAction SilentlyContinue

Write-Host ""
Write-Host "== 2. Start Elite from Steam and load the same landed-planet scene. Stop touching input. GPU otherwise idle. =="
Write-Host "   (HDR check: if the picture is much too bright or too dark, or tonemapping looks doubled, note it.)"
Read-Host 'Press Enter when the game is in the scene and settled'

Write-Host "== 3. Take a same-spot screenshot now (Steam F12 or Adrenalin). Name or note it as: $label =="
Read-Host 'Press Enter after the screenshot'

Write-Host "== 4. Capture (10 s delay, 60 s recording). Click into the game and do not touch anything. =="
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'capture_presentmon.ps1') -Name $name
if (-not (Test-Path $csv)) { throw 'No capture written.' }

Write-Host ""
Read-Host 'Capture done. Quit the game fully (menu, then exit), then press Enter'

Write-Host "== 5. Snapshot config as baseline\config-$label =="
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'snapshot_config.ps1') -Label $label

Write-Host "== 6. Check edpe.log =="
$log = Join-Path $game 'edpe.log'
if (-not (Test-Path $log)) { Write-Host 'edpe.log not found; cannot verify.'; }
else {
    $text = [System.Text.Encoding]::UTF8.GetString([System.IO.File]::ReadAllBytes($log)).Replace("`0", '')
    $lines = $text -split "`r?`n"
    $ready = $lines | Where-Object { $_ -match 'EDPE FSR4: context ready' }
    $failed = @($lines | Where-Object { $_ -match 'FSR 4 evaluate failed|FSR 4 disabled|requested but unavailable|FSR4: .*(failed|error)' })
    Write-Host "Context lines ($($ready.Count)):"
    $ready | ForEach-Object { Write-Host "  $_" }
    $wantHdr = "hdr=$Hdr"
    if (-not ($ready | Where-Object { $_ -match [regex]::Escape($wantHdr) })) {
        Write-Host "WARNING: no context line with $wantHdr. The run did not use the intended input." -ForegroundColor Red
    }
    if ($failed.Count -gt 0) {
        Write-Host "WARNING: $($failed.Count) FSR 4 failure or fallback line(s):" -ForegroundColor Red
        $failed | Select-Object -First 5 | ForEach-Object { Write-Host "  $_" }
    } else { Write-Host 'No FSR 4 failure or fallback lines.' }

    $prof = @($lines | Where-Object { $_ -match 'EDPE FSR4 profile' })
    $take = [Math]::Min(39, $prof.Count)
    if ($take -gt 0) {
        $last = $prof | Select-Object -Last $take
        $avg = { param($rx) ($last | ForEach-Object { if ($_ -match $rx) { [double]$Matches[1] } } | Measure-Object -Average).Average }
        Write-Host ("Engine profile, last {0} blocks (ms): d11 total {1:N3}, FSR dispatch {2:N3}, sync gap {3:N3}, cpu evaluate {4:N3}, cpuWaits max {5}" -f
            $take, (& $avg 'd11 total ([\d.]+)'), (& $avg 'fsr ([\d.]+),'), (& $avg 'sync gap ([\d.]+)'), (& $avg 'cpu evaluate ([\d.]+)'),
            (($last | ForEach-Object { if ($_ -match 'cpuWaits (\d+)') { [int]$Matches[1] } } | Measure-Object -Maximum).Maximum))
    }
    Copy-Item $log (Join-Path $root "baseline\config-$label\edpe.log") -Force
}

Write-Host ""
Write-Host "== 7. Frame-time comparison (first 60 frames skipped) =="
$p = Join-Path $root 'baseline\presentmon'
$cmp = @('-I', (Join-Path $root 'tools\compare_runs.py'), "$label=$csv")
$ldr = Join-Path $p "Planet_Landed_Static_F4_${Mode}_1.csv"
if (Test-Path $ldr) { $cmp += "F4_${Mode}_earlierLDR=$ldr" }
$f3 = Join-Path $p "Planet_Landed_Static_F3_${Mode}_2.csv"
if (Test-Path $f3) { $cmp += "F3_${Mode}=$f3" }
& $py @cmp
Write-Host ""
Write-Host "Saved: $csv"
Write-Host "Tell Claude the run is done: mode, -Hdr value, and the screenshot file names."
