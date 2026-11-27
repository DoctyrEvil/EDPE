# One 60 s per-frame capture of Elite Dangerous with AMD's bundled PresentMon.
# Usage (from any folder):
#   powershell -NoProfile -ExecutionPolicy Bypass -File C:\Users\<user>\Development\GPU_Performance_Analysis\tools\capture_presentmon.ps1 -Name A_S1_run1
# Output always goes to <project>\baseline\presentmon\<Name>.csv (absolute path, independent of the current folder).
param(
    [Parameter(Mandatory = $true)][string]$Name,
    [int]$DelaySeconds = 10,
    [int]$DurationSeconds = 60
)
$root = Split-Path $PSScriptRoot -Parent
$dir = Join-Path $root 'baseline\presentmon'
New-Item -ItemType Directory -Force $dir | Out-Null
$out = Join-Path $dir "$Name.csv"
if (Test-Path $out) { Write-Host "Refusing to overwrite existing $out. Pick a new -Name."; exit 1 }
$pm = 'C:\Program Files\AMD\CNext\CNext\PresentMon-x64.exe'
if (-not (Test-Path $pm)) { Write-Host "PresentMon not found at $pm"; exit 1 }
Write-Host "Output: $out"
Write-Host "Click into the game now. Recording starts in $DelaySeconds s and lasts $DurationSeconds s. Do not touch anything while it records."
& $pm --process_name EliteDangerous64.exe --output_file $out --session_name EDBaseline --delay $DelaySeconds --timed $DurationSeconds --terminate_after_timed --no_console_stats --date_time 2>$null
if (Test-Path $out) {
    $rows = (Get-Content $out | Measure-Object -Line).Lines - 1
    Write-Host "Done. $rows frames written to $out"
} else {
    Write-Host "No file was written. Is the game running and presenting frames?"
}
