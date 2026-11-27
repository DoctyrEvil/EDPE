# Deploy or remove the locally built EDPE (FSR 4 proof of concept) in the Elite Dangerous game folder.
#
#   Deploy: backs up any existing proxy files, copies the build output and writes an edpe.ini.
#   Remove: deletes the deployed files and restores the backup (leaves the folder vanilla if there was none).
#
# Examples (run from anywhere; the game must be closed):
#   powershell -NoProfile -ExecutionPolicy Bypass -File <project>\tools\deploy_fsr4_poc.ps1 -Action Deploy -Mode Performance -Fsr4 1
#   powershell -NoProfile -ExecutionPolicy Bypass -File <project>\tools\deploy_fsr4_poc.ps1 -Action Deploy -Mode Performance -Fsr4 0   # same build, FSR 3.1 path (control)
#   powershell -NoProfile -ExecutionPolicy Bypass -File <project>\tools\deploy_fsr4_poc.ps1 -Action Remove
#
# Mode: NativeAA (1.0 scale), Quality (render scale 1/1.5), Performance (render scale 0.5).
# Note: EDPE's AutoSyncEliteConfig=1 rewrites SSAAMultiplier in Custom.4.4.fxcfg to match the mode, as it did before.
param(
    [ValidateSet('Deploy', 'Remove')][string]$Action = 'Deploy',
    [ValidateSet('NativeAA', 'Quality', 'Performance')][string]$Mode = 'Performance',
    [ValidateSet(0, 1)][int]$Fsr4 = 1,
    [switch]$HdrInput,
    [switch]$Profile,                       # log a per-stage FSR 4 timing summary to edpe.log (Fsr4Profile=1)
    [switch]$EdpeProfiler,                  # EDPE's own zone profiler: 'EDPE PROFILE (120f avg)' lines in edpe.log ([Overlay] EnableProfiler=1)
    [ValidateSet(0, 1)][int]$Flush = 1,     # Fsr4Flush: flush D3D11 after the bridge fence signal
    [ValidateRange(0.0, 1.0)][double]$Sharpness = 0.50,  # [Upscaler] Sharpness (FSR RCAS pass inside the upscaler; 0 = off)
    [string]$BuildDir,                      # build output folder (default work\EDPE\build\Release)
    [switch]$Dev                            # [Overlay] DEV=1: required for the edpe_debug.dll test module to load
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$game = 'C:\Program Files (x86)\Steam\steamapps\common\Elite Dangerous\Products\elite-dangerous-odyssey-64'
$build = if ($BuildDir) { $BuildDir } else { Join-Path $root 'work\EDPE\build\Release' }
$backupRoot = Join-Path $root 'baseline\game-backup'
$managed = @('dxgi.dll', 'edpe.ini', 'amd_fidelityfx_loader_dx12.dll', 'amd_fidelityfx_upscaler_dx12.dll', 'amd_fidelityfx_framegeneration_dx12.dll', 'edpe_debug.dll', 'edpe_debug.ini', 'edpe-selftest.txt', 'edpe-selftest.json')

if (Get-Process EliteDangerous64 -ErrorAction SilentlyContinue) { throw 'Close the game first (EliteDangerous64.exe is running).' }
if (-not (Test-Path $game)) { throw "Game folder not found: $game" }

$marker = Join-Path $backupRoot 'LATEST.txt'

if ($Action -eq 'Deploy') {
    foreach ($f in 'dxgi.dll', 'amd_fidelityfx_loader_dx12.dll', 'amd_fidelityfx_upscaler_dx12.dll', 'D3D12\D3D12Core.dll') {
        if (-not (Test-Path (Join-Path $build $f))) { throw "Build output missing: $f (build work\EDPE first)" }
    }
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $backup = Join-Path $backupRoot $stamp
    New-Item -ItemType Directory -Force $backup | Out-Null
    $backedUp = @()
    foreach ($f in $managed) {
        $p = Join-Path $game $f
        if (Test-Path $p) { Copy-Item $p (Join-Path $backup $f) -Force; $backedUp += $f }
    }
    if (Test-Path (Join-Path $game 'D3D12')) { Copy-Item (Join-Path $game 'D3D12') (Join-Path $backup 'D3D12') -Recurse -Force; $backedUp += 'D3D12\' }
    Set-Content (Join-Path $backup 'backed-up.txt') ($backedUp -join "`r`n") -Encoding utf8
    Set-Content $marker $stamp -Encoding utf8

    Copy-Item (Join-Path $build 'dxgi.dll') $game -Force
    Copy-Item (Join-Path $build 'amd_fidelityfx_loader_dx12.dll') $game -Force
    Copy-Item (Join-Path $build 'amd_fidelityfx_upscaler_dx12.dll') $game -Force
    # EDPE's own frame generation needs its provider DLL beside the loader; the build stages it when the SDK has it.
    if (Test-Path (Join-Path $build 'amd_fidelityfx_framegeneration_dx12.dll')) { Copy-Item (Join-Path $build 'amd_fidelityfx_framegeneration_dx12.dll') $game -Force }
    New-Item -ItemType Directory -Force (Join-Path $game 'D3D12') | Out-Null
    Copy-Item (Join-Path $build 'D3D12\D3D12Core.dll') (Join-Path $game 'D3D12') -Force

    $modeId = @{ NativeAA = 1; Quality = 2; Performance = 4 }[$Mode]
    $hdr = if ($HdrInput) { 1 } else { 0 }
    $prof = if ($Profile) { 1 } else { 0 }
    $edpeProf = if ($EdpeProfiler) { 1 } else { 0 }
    $devFlag = if ($Dev) { 1 } else { 0 }
    $backendId = if ($Fsr4 -eq 1) { 4 } else { 2 }
    $ini = @"
[Upscaler]
; Backend: 0 = Native (Bypass), 1 = NVIDIA DLSS/DLAA, 2 = AMD FSR 3.1.2, 3 = TAA, 4 = AMD FSR 4.1.2
Backend=$backendId
; Mode: 1 = Native AA, 2 = Quality, 3 = Balanced, 4 = Performance, 5 = Ultra Performance
Mode=$modeId
Sharpness=$($Sharpness.ToString('0.00', [System.Globalization.CultureInfo]::InvariantCulture))
Preset=0
AutoSyncEliteConfig=1
PreserveNativeUi=1
HdrInput=$hdr

[Jitter]
Enabled=1
InvertX=0
InvertY=0
SwapXY=0
Scale=1.00
PhaseCount=8
LogEnabled=0
LogInterval=10
LogFlushMs=1000
DebugView=0
DebugSensitivity=1.50
DebugThreshold=0.020
DebugMode=0

[Motion]
ZeroMotion=0
InvertX=0
InvertY=0
Scale=1.00

[Overlay]
ShowPerfOverlay=0
EnableProfiler=$edpeProf
Corner=1
Scale=1.50
UiScale=1.00
DEV=$devFlag
"@
    Set-Content (Join-Path $game 'edpe.ini') $ini -Encoding ascii
    "Deployed EDPE (FSR 4 PoC) to the game folder. Mode=$Mode (id $modeId), Fsr4=$Fsr4, HdrInput=$hdr, Profile=$prof, EdpeProfiler=$edpeProf, Flush=$Flush, Sharpness=$Sharpness."
    "Backup of previous files: $backup  (restore with -Action Remove)"
    "After a run check edpe.log for: 'EDPE FSR4: context ready provider=4.1.2' and 'first frame upscaled'."
    return
}

# Remove
foreach ($f in $managed) { $p = Join-Path $game $f; if (Test-Path $p) { Remove-Item $p -Force } }
if (Test-Path (Join-Path $game 'D3D12')) { Remove-Item (Join-Path $game 'D3D12') -Recurse -Force }
foreach ($log in 'edpe.log') { $p = Join-Path $game $log; if (Test-Path $p) { Remove-Item $p -Force } }
if (Test-Path $marker) {
    $stamp = (Get-Content $marker -Raw).Trim()
    $backup = Join-Path $backupRoot $stamp
    if (Test-Path $backup) {
        foreach ($f in $managed) { $b = Join-Path $backup $f; if (Test-Path $b) { Copy-Item $b $game -Force } }
        if (Test-Path (Join-Path $backup 'D3D12')) { Copy-Item (Join-Path $backup 'D3D12') $game -Recurse -Force }
        "Removed deployed files and restored the backup from $backup."
        return
    }
}
'Removed deployed files; the folder is vanilla again (no backup to restore).'
