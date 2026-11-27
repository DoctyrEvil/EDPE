# Copies the Elite Dangerous config files that affect rendering into baseline\config-<label>\ with hashes,
# plus driver version and game process info, so every measured configuration has a recorded setup.
# Usage: powershell -File tools\snapshot_config.ps1 -Label A
param([Parameter(Mandatory = $true)][string]$Label)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$dest = Join-Path $root "baseline\config-$Label"
New-Item -ItemType Directory -Force $dest | Out-Null

$game = "C:\Program Files (x86)\Steam\steamapps\common\Elite Dangerous\Products\elite-dangerous-odyssey-64"
$opts = Join-Path $env:LOCALAPPDATA 'Frontier Developments\Elite Dangerous\Options'
$files = @(
    "$game\AppConfig.xml", "$game\GraphicsConfiguration.xml", "$game\GpuWorkTable.xml",
    "$opts\Graphics\DisplaySettings.xml", "$opts\Graphics\Settings.xml",
    "$opts\Graphics\GraphicsConfigurationOverride.xml", "$opts\Startup\Settings.xml"
)
if (Test-Path "$game\edpe.ini") { $files += "$game\edpe.ini" }
# The Custom preset file holds the real per-option values (AA mode, SSAA, texture/shadow quality, checkerboard).
$files += (Get-ChildItem "$opts\Graphics\Custom.*.fxcfg" -ErrorAction SilentlyContinue | Sort-Object Name | Select-Object -Last 1 -ExpandProperty FullName)
$rows = foreach ($f in $files) {
    if (Test-Path $f) {
        $name = ($f.Replace($game, 'game').Replace($opts, 'options') -replace '[\\:]', '_')
        Copy-Item $f (Join-Path $dest $name) -Force
        $i = Get-Item $f
        [pscustomobject]@{ File = $name; Bytes = $i.Length; Modified = $i.LastWriteTime; SHA256 = (Get-FileHash $f -Algorithm SHA256).Hash }
    } else {
        [pscustomobject]@{ File = $f; Bytes = 'MISSING'; Modified = ''; SHA256 = '' }
    }
}
$rows | Export-Csv (Join-Path $dest 'files.csv') -NoTypeInformation

$gpu = Get-CimInstance Win32_VideoController | Where-Object Name -match 'Radeon'
$procs = Get-CimInstance Win32_Process | Where-Object { $_.Name -match 'EDLaunch|EliteDangerous|dxgi|RTSS|MSIAfterburner|PresentMon' } |
    Select-Object ProcessId, ParentProcessId, Name, CreationDate
$os = Get-CimInstance Win32_OperatingSystem
$proxyList = (@('dxgi.dll', 'd3d11.dll', 'edpe.ini', 'edvr.ini') | ForEach-Object { "$_=" + (Test-Path "$game\$_") }) -join ', '
$lines = @()
$lines += "label: $Label"
$lines += "captured: $(Get-Date -Format s)"
$lines += "gpu: $($gpu.Name)"
$lines += "driver: $($gpu.DriverVersion) ($($gpu.DriverDate))"
$lines += "os: $($os.Caption) $($os.Version)"
$lines += "cpu: $((Get-CimInstance Win32_Processor).Name.Trim())"
$lines += "game folder files present: $proxyList"
if (Test-Path "$game\dxgi.dll") {
    $dx = Get-Item "$game\dxgi.dll"
    $lines += "dxgi.dll proxy: $($dx.Length) bytes, modified $($dx.LastWriteTime), SHA256 $((Get-FileHash $dx.FullName -Algorithm SHA256).Hash)"
}
$lines += "relevant processes:"
$lines += ($procs | Format-Table -AutoSize | Out-String).TrimEnd()
$lines | Set-Content (Join-Path $dest 'environment.txt') -Encoding utf8
"Saved snapshot to $dest"
$rows | Format-Table File, Bytes, Modified -AutoSize
