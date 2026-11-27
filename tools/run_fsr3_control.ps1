# FSR 3.1.2 control run on the CURRENT EDPE build (same build as the FSR 4 runs, Fsr4=0), static landed scene.
#
# Steps it performs: deploys the build with Fsr4=0, waits while you start the game and settle in the scene,
# records a 60 s PresentMon capture, waits for you to quit the game, snapshots the config, checks edpe.log
# to prove FSR 3.1 (not FSR 4) was active, and prints a comparison against the vanilla and FSR 4 runs.
#
# Run one mode at a time, from your own PowerShell window (it prompts):
#   powershell -NoProfile -ExecutionPolicy Bypass -File C:\Users\<user>\Development\GPU_Performance_Analysis\tools\run_fsr3_control.ps1 -Mode Performance
#   powershell -NoProfile -ExecutionPolicy Bypass -File C:\Users\<user>\Development\GPU_Performance_Analysis\tools\run_fsr3_control.ps1 -Mode Quality
# Add -Run 2 (and so on) to repeat a mode; existing capture files are never overwritten.
param(
    [ValidateSet('NativeAA', 'Quality', 'Performance')][string]$Mode = 'Performance',
    [int]$Run = 1
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$game = 'C:\Program Files (x86)\Steam\steamapps\common\Elite Dangerous\Products\elite-dangerous-odyssey-64'
$py = 'C:\Users\<user>\AppData\Local\Programs\Python\Python312\python.exe'
$name = "Planet_Landed_Static_F3_${Mode}_$Run"
$label = "F3_$Mode"
$csv = Join-Path $root "baseline\presentmon\$name.csv"

if (Get-Process EliteDangerous64 -ErrorAction SilentlyContinue) { throw 'Close the game first (EliteDangerous64.exe is running).' }
if (Test-Path $csv) { throw "Capture already exists: $csv. Use -Run $($Run + 1)." }

Write-Host "== 1. Deploy current build, Fsr4=0 (FSR 3.1.2 D3D11 path), Mode=$Mode =="
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'deploy_fsr4_poc.ps1') -Action Deploy -Mode $Mode -Fsr4 0 -Flush 1
if ($LASTEXITCODE -ne 0) { throw 'Deploy failed.' }
Remove-Item (Join-Path $game 'edpe.log') -ErrorAction SilentlyContinue

Write-Host ""
Write-Host "== 2. Start Elite from Steam, load the same landed-planet scene, then stop touching input. GPU should be otherwise idle. =="
Read-Host 'Press Enter when the game is in the scene and settled'

Write-Host "== 3. Capture (10 s delay, 60 s recording). Click into the game now and do not touch anything. =="
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'capture_presentmon.ps1') -Name $name
if (-not (Test-Path $csv)) { throw 'No capture written.' }

Write-Host ""
Read-Host 'Capture done. Quit the game fully (menu, then exit), then press Enter'

Write-Host "== 4. Snapshot config as baseline\config-$label =="
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'snapshot_config.ps1') -Label $label

Write-Host "== 5. Verify FSR 3.1 was the active path =="
$log = Join-Path $game 'edpe.log'
if (Test-Path $log) {
    $bytes = [System.IO.File]::ReadAllBytes($log)
    $text = [System.Text.Encoding]::UTF8.GetString($bytes).Replace("`0", '')
    $fsr4 = ([regex]::Matches($text, 'EDPE FSR4: context ready')).Count
    $fsr31 = ([regex]::Matches($text, 'AMD FSR 3\.1 D3D11 backend initialized')).Count
    $fsr31Run = ([regex]::Matches($text, 'AMD FSR reconstructed')).Count
    Write-Host "FSR4 context-ready lines: $fsr4 (must be 0); FSR 3.1 backend init lines: $fsr31; 'AMD FSR reconstructed' lines: $fsr31Run"
    if ($fsr4 -ne 0) { Write-Host 'WARNING: FSR 4 ran during this capture. The run is NOT an FSR 3.1 control.' -ForegroundColor Red }
    Copy-Item $log (Join-Path $root "baseline\config-$label\edpe.log") -Force
} else {
    Write-Host 'edpe.log not found; cannot verify the active path.'
}

Write-Host ""
Write-Host "== 6. Comparison (first 60 frames skipped) =="
$p = Join-Path $root 'baseline\presentmon'
$args = @('-I', (Join-Path $root 'tools\compare_runs.py'), "$label=$csv")
$f4 = Join-Path $p "Planet_Landed_Static_F4_$Mode`_1.csv"
if ($Mode -eq 'NativeAA') { $f4 = Join-Path $p 'Planet_Landed_Static_F4_Native_1.csv' }
if (Test-Path $f4) { $args += "F4_$Mode=$f4" }
$a = Get-ChildItem $p -Filter 'Planet_Landed_Static_A_?.csv*' -ErrorAction SilentlyContinue | Where-Object { $_.Name -notmatch 'Scale' } | ForEach-Object { $_.FullName }
if ($a) { $args += ('A_vanilla=' + ($a -join ',')) }
& $py @args
Write-Host ""
Write-Host "Saved: $csv"
Write-Host "Tell Claude the run is done and which -Mode it was."
