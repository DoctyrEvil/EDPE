# Quick check of whether the current Elite Dangerous scene is CPU-bound: a short PresentMon capture, then the share
# of each frame the GPU was busy. Run it, click into the game, keep still until it prints the result.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File <project>\tools\cpu_bound_probe.ps1 -Label concourse_fsr4
#
# GPU busy well below 100% of the frame time (under about 85%) means the GPU waits on the CPU: a CPU-bound scene.
# Captures go to <project>\baseline\presentmon\probe\<label>-<time>.csv.
param(
    [Parameter(Mandatory = $true)][string]$Label,
    [int]$DelaySeconds = 5,
    [int]$DurationSeconds = 15
)
$root = Split-Path $PSScriptRoot -Parent
$dir = Join-Path $root 'baseline\presentmon\probe'
New-Item -ItemType Directory -Force $dir | Out-Null
$out = Join-Path $dir ("{0}-{1}.csv" -f $Label, (Get-Date -Format 'HHmmss'))
$pm = 'C:\Program Files\AMD\CNext\CNext\PresentMon-x64.exe'
if (-not (Test-Path $pm)) { Write-Host "PresentMon not found at $pm"; exit 1 }
Write-Host "Click into the game. Recording starts in $DelaySeconds s and lasts $DurationSeconds s. Keep still."
& $pm --process_name EliteDangerous64.exe --output_file $out --session_name EDProbe --delay $DelaySeconds --timed $DurationSeconds --terminate_after_timed --no_console_stats 2>$null
if (-not (Test-Path $out)) { Write-Host 'No file was written. Is the game running and presenting frames?'; exit 1 }

$script = @'
import csv, statistics, sys
rows = [r for r in csv.DictReader(open(sys.argv[1], encoding='utf-8-sig')) if r.get('Application', '').lower().startswith('elite')]
def med(c):
    v = []
    for r in rows:
        try: v.append(float(r[c]))
        except (KeyError, TypeError, ValueError): pass
    return statistics.median(v) if v else 0.0
ft, gb = med('FrameTime'), med('GPUBusy')
share = 100.0 * gb / ft if ft else 0.0
verdict = 'CPU-bound' if share < 85 else ('borderline' if share < 95 else 'GPU-bound')
print(f"{len(rows)} frames  fps {1000 / ft:.1f}  frame {ft:.2f} ms  GPU busy {gb:.2f} ms ({share:.0f}%)  -> {verdict}")
'@
$py = Join-Path $env:TEMP 'edpe_cpu_probe.py'
Set-Content $py $script -Encoding ascii
& python -I $py $out
Write-Host "Saved: $out"
