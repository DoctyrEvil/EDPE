# In-game regression check for the D3D11-D3D12 bridge change (fsr4-pr1-bridge): AMD frame generation on upstream
# master against the same on fsr4-pr1-bridge. PR 1 has no FSR 4 caller, so frame generation is the only runtime user
# of the bridge. Both proxies are prebuilt in work\fgcheck\<master|pr1>\ (dxgi.dll, FidelityFX loader, frame
# generation provider).
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\pr1_fg_check.ps1 -Build master     # deploy upstream master
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\pr1_fg_check.ps1 -Build pr1        # deploy fsr4-pr1-bridge
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\pr1_fg_check.ps1 -Build fgfix      # deploy master + window re-evaluation after frame generation shutdown (branch fg-restore-flip)
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\pr1_fg_check.ps1 -Collect <label>  # save edpe.log to baseline\fgcheck\
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\pr1_fg_check.ps1 -Restore          # put the game folder back as it was
#
# Deploy removes the FSR 4 pieces (upscaler DLL, D3D12 folder, edpe_debug files) after backing everything up, so the
# game folder holds only what the branch under test ships. The game must be closed.
param(
    [ValidateSet('master', 'pr1', 'fgfix')][string]$Build,
    [string]$Collect,
    [switch]$Restore
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$game = 'C:\Program Files (x86)\Steam\steamapps\common\Elite Dangerous\Products\elite-dangerous-odyssey-64'
$backupRoot = Join-Path $root 'baseline\game-backup'
$marker = Join-Path $backupRoot 'LATEST-fgcheck.txt'
$touched = @('dxgi.dll', 'edpe.ini', 'edpe.log', 'amd_fidelityfx_loader_dx12.dll', 'amd_fidelityfx_upscaler_dx12.dll',
    'amd_fidelityfx_framegeneration_dx12.dll', 'edpe_debug.dll', 'edpe_debug.ini', 'D3D12')

if ($Collect) {
    $dest = Join-Path $root "baseline\fgcheck\$(Get-Date -Format 'yyyyMMdd-HHmmss')-$Collect"
    New-Item -ItemType Directory -Force $dest | Out-Null
    foreach ($f in 'edpe.log', 'edpe.ini') { if (Test-Path (Join-Path $game $f)) { Copy-Item (Join-Path $game $f) $dest } }
    "Collected into $dest"
    Select-String -Path (Join-Path $dest 'edpe.log') -Pattern 'FG|frame gen|FrameGen|ffxCreateContext|D3D12Bridge|SwapChainWrap' -ErrorAction SilentlyContinue |
        Select-Object -First 40 | ForEach-Object { $_.Line }
    return
}

if (Get-Process EliteDangerous64 -ErrorAction SilentlyContinue) { throw 'Close the game first (EliteDangerous64.exe is running).' }

if ($Restore) {
    if (-not (Test-Path $marker)) { throw 'No frame generation check backup found.' }
    $backup = Join-Path $backupRoot ((Get-Content $marker -Raw).Trim())
    foreach ($f in $touched) {
        $p = Join-Path $game $f
        if (Test-Path $p) { Remove-Item $p -Recurse -Force }
        $b = Join-Path $backup $f
        if (Test-Path $b) { Copy-Item $b $game -Recurse -Force }
    }
    "Restored the game folder from $backup"
    return
}

if (-not $Build) { throw 'Specify -Build master|pr1, -Collect <label> or -Restore.' }
$src = Join-Path $root "work\fgcheck\$Build"
foreach ($f in 'dxgi.dll', 'amd_fidelityfx_loader_dx12.dll', 'amd_fidelityfx_framegeneration_dx12.dll') {
    if (-not (Test-Path (Join-Path $src $f))) { throw "Missing $src\$f" }
}

# Back up once per check session (a second deploy keeps the first backup, so -Restore returns to the original state).
if (-not (Test-Path $marker)) {
    $stamp = "$(Get-Date -Format 'yyyyMMdd-HHmmss')-prefgcheck"
    $backup = Join-Path $backupRoot $stamp
    New-Item -ItemType Directory -Force $backup | Out-Null
    foreach ($f in $touched) { $p = Join-Path $game $f; if (Test-Path $p) { Copy-Item $p $backup -Recurse -Force } }
    Set-Content $marker $stamp -Encoding utf8
    "Backed up the game folder files to $backup"
}

foreach ($f in $touched) { $p = Join-Path $game $f; if (Test-Path $p) { Remove-Item $p -Recurse -Force } }
foreach ($f in 'dxgi.dll', 'amd_fidelityfx_loader_dx12.dll', 'amd_fidelityfx_framegeneration_dx12.dll') { Copy-Item (Join-Path $src $f) $game -Force }

$ini = @"
[Upscaler]
; AMD FSR 3.1.2 (D3D11 path) so frame generation has a working upscaler under it
Backend=2
Mode=4
Sharpness=0.00
AutoSyncEliteConfig=1
PreserveNativeUi=1
HdrInput=0

[FrameGeneration]
; 0 = off, 1 = AMD FSR frame generation, 2 = NVIDIA DLSS-G
Backend=1
Multiplier=2

[Overlay]
ShowPerfOverlay=1
DEV=0
"@
Set-Content (Join-Path $game 'edpe.ini') $ini -Encoding ascii
"Deployed the '$Build' proxy. Start the game in borderless windowed mode, load a scene, open the EDPE menu (F5),"
"turn on frame generation if it is not already on, wait about 30 seconds, then quit. Then run -Collect $Build."
