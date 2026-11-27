<#
.SYNOPSIS
Build, deploy and collect results for the EDPE in-game developer test module.

.DESCRIPTION
One driver for the whole workflow. The branch under test is never modified: it is built in a throwaway worktree
with the debug hook applied (apply_hook.py), so no test code can reach a pull request.

Commands:
  Status   Show the resolved paths, whether a test build is deployed, and the available builds.
  Build    Build -Ref (default: HEAD of -RepoDir) with the hook and the module, into a per-commit build archive.
  Deploy   Build if needed, back up the game folder files this script manages, copy the build, patch edpe.ini
           key by key, and write edpe_debug.ini. The game must be closed.
  Collect  Copy new self-test reports, capture folders and logs from the game folder into -OutDir.
  Restore  Remove the deployed files and put the backed-up originals back.

Nothing is read from or written to a fixed location: every path is a parameter, an environment variable or
auto-detected, and is printed by Status.

.PARAMETER Ref
Branch, tag or commit to test. Default: the commit checked out in -RepoDir.

.PARAMETER RepoDir
EDPE checkout to build from. Default: the repository that contains this script.

.PARAMETER GameDir
Folder with EliteDangerous64.exe. Default: $env:EDPE_GAME_DIR, then the Steam library (registry + libraryfolders.vdf).

.PARAMETER WorkDir
Throwaway worktree, build archive and game-file backups. Default: $env:LOCALAPPDATA\edpe-test.

.PARAMETER OutDir
Where Collect writes results. Default: .\edpe-test-results

.PARAMETER ExternalDir
The external\ folder (SDKs, imgui) shared with the worktree through a junction. Default: <RepoDir>\external.

.PARAMETER Backend
Upscaler selected at game start: Bypass, DLSS, FSR3, TAA or FSR4. Default FSR4.

.PARAMETER Rounds
Times the option list runs (module default 2). 0 keeps the default.

.PARAMETER RunStress
Add the provider switch stress test and a settle block after the measurements.

.PARAMETER DebugKey
Extra edpe_debug.ini [Debug] keys as 'Key=Value', for example 'CaptureSequence=off:20,on:20,off:40' or 'PresentMonPath=D:\tools\PresentMon.exe'.

.PARAMETER Forensics
Apply the extra crash-forensics edits from apply_hook.py (a guard in PerfTracker that logs and recovers from corrupt state).
Off by default, because it can hide the bug it is meant to find.

.EXAMPLE
.\edpe-test.ps1 Status

.EXAMPLE
.\edpe-test.ps1 Deploy -Ref my-feature-branch -AutoRun -AutoExit

.EXAMPLE
.\edpe-test.ps1 Deploy -Ref my-feature-branch -ModuleOnly      # swap in a newer module, keep the backup

.EXAMPLE
.\edpe-test.ps1 Collect -Scene "main menu"

.EXAMPLE
.\edpe-test.ps1 Restore
#>
[CmdletBinding()]
param(
    [Parameter(Position = 0)][ValidateSet('Status', 'Build', 'Deploy', 'Collect', 'Restore')][string]$Command = 'Status',
    [string]$Ref,
    [string]$RepoDir,
    [string]$GameDir,
    [string]$WorkDir,
    [string]$OutDir,
    [string]$ExternalDir,
    [ValidateSet('NativeAA', 'Quality', 'Performance')][string]$Mode = 'Performance',
    [ValidateSet('Bypass', 'DLSS', 'FSR3', 'TAA', 'FSR4')][string]$Backend = 'FSR4',
    [ValidateRange(0.0, 1.0)][double]$Sharpness = 0.0,
    [switch]$EdpeProfiler,
    [switch]$AutoRun,
    [switch]$AutoExit,
    [int]$StartDelay = 300,
    [int]$Warm = 120,
    [int]$Hold = 600,
    [int]$Stress = 600,
    [int]$Rounds = 0,
    [switch]$RunStress,
    [string[]]$DebugKey = @(),
    [switch]$NoBuild,
    [switch]$ModuleOnly,
    [switch]$Forensics,
    [string]$Scene = ''
)
$ErrorActionPreference = 'Stop'
$inv = [System.Globalization.CultureInfo]::InvariantCulture

# Files this script may replace in the game folder. Everything else is left alone.
$managedFiles = @('dxgi.dll', 'edpe.ini', 'amd_fidelityfx_loader_dx12.dll', 'amd_fidelityfx_upscaler_dx12.dll',
    'amd_fidelityfx_framegeneration_dx12.dll', 'nvngx_dlss.dll', 'edpe_debug.dll', 'edpe_debug.ini')
$backendIds = @{ Bypass = 0; DLSS = 1; FSR3 = 2; TAA = 3; FSR4 = 4 }
$modeIds = @{ NativeAA = 1; Quality = 2; Performance = 4 }

# ---------------------------------------------------------------- helpers

function Invoke-Native([string]$exe, [string[]]$arguments) {
    & $exe @arguments
    if ($LASTEXITCODE -ne 0) { throw "$exe $($arguments -join ' ') failed with exit code $LASTEXITCODE" }
}

function Assert-Tool([string]$name, [string]$hint) {
    if (-not (Get-Command $name -ErrorAction SilentlyContinue)) { throw "'$name' was not found on PATH. $hint" }
}

function Resolve-RepoDir {
    if ($RepoDir) { return (Resolve-Path $RepoDir).Path }
    $top = (& git -C $PSScriptRoot rev-parse --show-toplevel 2>$null)
    if ($LASTEXITCODE -ne 0 -or -not $top) { throw 'Not inside a git checkout. Pass -RepoDir <EDPE checkout>.' }
    return (Resolve-Path $top.Trim()).Path
}

function Find-GameDir {
    if ($GameDir) { return $GameDir }
    if ($env:EDPE_GAME_DIR) { return $env:EDPE_GAME_DIR }
    $libraries = @()
    $steam = (Get-ItemProperty 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).SteamPath
    if ($steam) {
        $steam = $steam -replace '/', '\'
        $libraries += $steam
        $vdf = Join-Path $steam 'steamapps\libraryfolders.vdf'
        if (Test-Path $vdf) {
            foreach ($m in [regex]::Matches((Get-Content $vdf -Raw), '"path"\s+"([^"]+)"')) { $libraries += ($m.Groups[1].Value -replace '\\\\', '\') }
        }
    }
    foreach ($lib in $libraries) {
        foreach ($product in 'elite-dangerous-odyssey-64', 'elite-dangerous-64') {
            $p = Join-Path $lib "steamapps\common\Elite Dangerous\Products\$product"
            if (Test-Path (Join-Path $p 'EliteDangerous64.exe')) { return $p }
        }
    }
    return $null
}

function Assert-GameDir([string]$dir) {
    if (-not $dir) { throw 'Game folder not found. Pass -GameDir or set $env:EDPE_GAME_DIR (the folder with EliteDangerous64.exe).' }
    if (-not (Test-Path (Join-Path $dir 'EliteDangerous64.exe'))) { throw "EliteDangerous64.exe not found in: $dir" }
}

function Assert-GameClosed {
    if (Get-Process EliteDangerous64 -ErrorAction SilentlyContinue) { throw 'Close the game first (EliteDangerous64.exe is running).' }
}

# Sets [section] key=value in an ini file, keeping every other line. Adds the key or the section when missing.
function Set-IniValue([string]$path, [string]$section, [string]$key, [string]$value) {
    $lines = New-Object System.Collections.Generic.List[string]
    if (Test-Path $path) { foreach ($l in [IO.File]::ReadAllLines($path)) { $lines.Add($l) } }
    $inSection = $false; $sectionEnd = -1; $done = $false
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $t = $lines[$i].Trim()
        if ($t -match '^\[(.+)\]$') {
            if ($inSection) { $sectionEnd = $i; break }
            $inSection = ($Matches[1] -ieq $section)
            continue
        }
        if ($inSection -and $t -match '^([^;=\s][^=]*?)\s*=') {
            if ($Matches[1] -ieq $key) { $lines[$i] = "$key=$value"; $done = $true; break }
        }
    }
    if (-not $done) {
        if ($inSection -and $sectionEnd -lt 0) { $lines.Add("$key=$value") }
        elseif ($sectionEnd -ge 0) { $lines.Insert($sectionEnd, "$key=$value") }
        else { $lines.Add("[$section]"); $lines.Add("$key=$value") }
    }
    [IO.File]::WriteAllLines($path, $lines, (New-Object System.Text.UTF8Encoding($false)))
}

function Resolve-Sha([string]$repo, [string]$ref) {
    $target = if ($ref) { $ref } else { 'HEAD' }
    $sha = (& git -C $repo rev-parse --short=7 "$target^{commit}" 2>$null)
    if ($LASTEXITCODE -ne 0 -or -not $sha) { throw "Cannot resolve '$target' in $repo." }
    return $sha.Trim()
}

function Get-ArchiveDir([string]$sha) {
    $suffix = if ($Forensics) { '-forensics' } else { '' }
    return Join-Path $work "builds\$sha$suffix"
}

# ---------------------------------------------------------------- build

function Invoke-Build([string]$repo, [string]$sha) {
    Assert-Tool git 'Install Git.'
    Assert-Tool cmake 'Install CMake (see BUILD.md) and run from an x64 Developer PowerShell.'
    Assert-Tool python 'Install Python 3 (used only by apply_hook.py).'
    $archive = Get-ArchiveDir $sha
    $wt = Join-Path $work 'worktree'
    New-Item -ItemType Directory -Force $work | Out-Null

    # 1. Throwaway worktree, detached so no branch ref is ever touched.
    if (-not (Test-Path (Join-Path $wt '.git'))) {
        Invoke-Native git @('-C', $repo, 'worktree', 'add', '--detach', $wt, $sha)
    } else {
        Invoke-Native git @('-C', $wt, 'reset', '--hard', '-q')
        Invoke-Native git @('-C', $wt, 'clean', '-fdq', '--', 'src', 'tests')
        Invoke-Native git @('-C', $wt, 'checkout', '--detach', '-q', $sha)
    }

    # 2. external\ holds the SDKs and imgui and is not tracked: share the checkout's copy with a junction.
    $ext = if ($ExternalDir) { $ExternalDir } else { Join-Path $repo 'external' }
    if (-not (Test-Path $ext)) { throw "external folder not found: $ext. Run 'git submodule update --init --recursive' and fetch the SDKs as BUILD.md describes, or pass -ExternalDir." }
    $link = Join-Path $wt 'external'
    $isJunction = (Test-Path $link) -and ((Get-Item $link -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)
    if (-not $isJunction) {
        if (Test-Path $link) { Remove-Item $link -Recurse -Force }
        New-Item -ItemType Junction -Path $link -Target $ext | Out-Null
    }

    # 3. Hook. Fails loudly if the tree drifted from the anchors.
    $hookArgs = @('-I', (Join-Path $PSScriptRoot 'apply_hook.py'), $wt)
    if ($Forensics) { $hookArgs += '--forensics' }
    Invoke-Native python $hookArgs

    # 4. Proxy and module.
    Invoke-Native cmake @('-S', $wt, '-B', (Join-Path $wt 'build'), '-A', 'x64')
    Invoke-Native cmake @('--build', (Join-Path $wt 'build'), '--config', 'Release', '--target', 'EDPE_DXGI', '--parallel')
    Invoke-Native cmake @('-S', (Join-Path $PSScriptRoot 'module'), '-B', (Join-Path $wt 'build-debug'), '-A', 'x64', "-DEDPE_TREE=$wt")
    Invoke-Native cmake @('--build', (Join-Path $wt 'build-debug'), '--config', 'Release', '--parallel')

    # 5. Archive what Deploy needs, so a build can be redeployed without rebuilding.
    New-Item -ItemType Directory -Force $archive | Out-Null
    $out = Join-Path $wt 'build\Release'
    Copy-Item (Join-Path $out 'dxgi.dll') $archive -Force
    Copy-Item (Join-Path $wt 'build-debug\Release\edpe_debug.dll') $archive -Force
    foreach ($f in 'amd_fidelityfx_loader_dx12.dll', 'amd_fidelityfx_upscaler_dx12.dll', 'amd_fidelityfx_framegeneration_dx12.dll', 'nvngx_dlss.dll') {
        if (Test-Path (Join-Path $out $f)) { Copy-Item (Join-Path $out $f) $archive -Force }
    }
    if (Test-Path (Join-Path $out 'D3D12\D3D12Core.dll')) {
        New-Item -ItemType Directory -Force (Join-Path $archive 'D3D12') | Out-Null
        Copy-Item (Join-Path $out 'D3D12\D3D12Core.dll') (Join-Path $archive 'D3D12') -Force
    }
    if (Test-Path (Join-Path $wt 'edpe.ini')) { Copy-Item (Join-Path $wt 'edpe.ini') (Join-Path $archive 'edpe.ini.default') -Force }
    $branch = (& git -C $repo name-rev --name-only --no-undefined $sha 2>$null)
    Set-Content (Join-Path $archive 'build.txt') @("commit=$sha", "name=$branch", "hook=$(if ($Forensics) { 'forensics' } else { 'standard' })", "built=$(Get-Date -Format s)") -Encoding ascii
    "Built $sha with the debug hook: $archive"
}

# ---------------------------------------------------------------- deploy

function Invoke-Deploy([string]$repo, [string]$sha, [string]$game) {
    Assert-GameDir $game
    Assert-GameClosed
    $archive = Get-ArchiveDir $sha
    if (-not (Test-Path (Join-Path $archive 'dxgi.dll'))) {
        if ($NoBuild) { throw "No build archive for $sha in $archive and -NoBuild was given." }
        Invoke-Build $repo $sha
    }

    $backupRoot = Join-Path $work 'game-backup'
    $marker = Join-Path $backupRoot 'LATEST.txt'
    $alreadyDeployed = Test-Path (Join-Path $game 'edpe_debug.dll')
    if (-not $alreadyDeployed) {
        # First deploy since the last Restore: save the originals. A second deploy must not overwrite this backup
        # with files this script put there, so it is skipped while a test build is in place.
        $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
        $backup = Join-Path $backupRoot $stamp
        New-Item -ItemType Directory -Force $backup | Out-Null
        $saved = @()
        foreach ($f in $managedFiles) {
            $p = Join-Path $game $f
            if (Test-Path $p) { Copy-Item $p (Join-Path $backup $f) -Force; $saved += $f }
        }
        if (Test-Path (Join-Path $game 'D3D12')) { Copy-Item (Join-Path $game 'D3D12') (Join-Path $backup 'D3D12') -Recurse -Force; $saved += 'D3D12\' }
        Set-Content (Join-Path $backup 'backed-up.txt') $saved -Encoding ascii
        Set-Content $marker $stamp -Encoding ascii
        "Backed up the original files to $backup"
    } elseif (-not (Test-Path $marker)) {
        throw 'A test build is already in the game folder but there is no backup record. Restore it by hand (verify the files), then deploy again.'
    } else {
        'A test build is already deployed: keeping the existing backup.'
    }

    $files = if ($ModuleOnly) { @('dxgi.dll', 'edpe_debug.dll') } else { $managedFiles | Where-Object { $_ -notin 'edpe.ini', 'edpe_debug.ini' } }
    foreach ($f in $files) {
        $src = Join-Path $archive $f
        if (Test-Path $src) { Copy-Item $src $game -Force }
    }
    if (-not $ModuleOnly -and (Test-Path (Join-Path $archive 'D3D12\D3D12Core.dll'))) {
        New-Item -ItemType Directory -Force (Join-Path $game 'D3D12') | Out-Null
        Copy-Item (Join-Path $archive 'D3D12\D3D12Core.dll') (Join-Path $game 'D3D12') -Force
    }

    # edpe.ini: patch only what the test needs. The user's other settings stay as they are.
    $ini = Join-Path $game 'edpe.ini'
    if (-not (Test-Path $ini) -and (Test-Path (Join-Path $archive 'edpe.ini.default'))) { Copy-Item (Join-Path $archive 'edpe.ini.default') $ini }
    if (-not $ModuleOnly) {
        Set-IniValue $ini 'Upscaler' 'Backend' $backendIds[$Backend]
        Set-IniValue $ini 'Upscaler' 'Mode' $modeIds[$Mode]
        Set-IniValue $ini 'Upscaler' 'Sharpness' $Sharpness.ToString('0.00', $inv)
        Set-IniValue $ini 'Overlay' 'EnableProfiler' $(if ($EdpeProfiler) { 1 } else { 0 })
    }
    Set-IniValue $ini 'Overlay' 'DEV' 1    # the module loads only when DEV=1

    $name = (Get-Content (Join-Path $archive 'build.txt') | Where-Object { $_ -like 'name=*' } | Select-Object -First 1) -replace '^name=', ''
    $build = if ($name -and $name -ne 'undefined') { "$name@$sha" } else { $sha }
    $debugIni = @("[Debug]", "AutoRun=$(if ($AutoRun) { 1 } else { 0 })", "AutoExit=$(if ($AutoExit) { 1 } else { 0 })",
        "StartDelay=$StartDelay", "WarmFrames=$Warm", "HoldFrames=$Hold", "RunStress=$(if ($RunStress) { 1 } else { 0 })",
        "StressFrames=$Stress", "Build=$build")
    if ($Rounds -gt 0) { $debugIni += "Rounds=$Rounds" }
    $debugIni += $DebugKey
    Set-Content (Join-Path $game 'edpe_debug.ini') $debugIni -Encoding ascii

    "Deployed $build to $game"
    "Backend=$Backend Mode=$Mode Sharpness=$($Sharpness.ToString('0.00', $inv)) AutoRun=$([int]$AutoRun.IsPresent) AutoExit=$([int]$AutoExit.IsPresent)"
    'Start the game and load a static scene. Menu: F5, then "EDPE Developer Tests". Then run: edpe-test.ps1 Collect'
}

# ---------------------------------------------------------------- collect

function Invoke-Collect([string]$game) {
    Assert-GameDir $game
    $out = if ($OutDir) { $OutDir } else { Join-Path (Get-Location).Path 'edpe-test-results' }
    New-Item -ItemType Directory -Force $out | Out-Null
    $indexPath = Join-Path $out 'collected.txt'
    $known = if (Test-Path $indexPath) { @(Get-Content $indexPath) } else { @() }

    $new = @(Get-ChildItem $game -Filter 'edpe-selftest-*' -File) + @(Get-ChildItem $game -Filter 'edpe-capture-*' -Directory)
    $new = @($new | Where-Object { $known -notcontains $_.Name })
    if ($new.Count -eq 0) { 'No new self-test reports or capture folders in the game folder.'; return }

    $buildId = 'unknown'
    $dbgIni = Join-Path $game 'edpe_debug.ini'
    if (Test-Path $dbgIni) { $m = Select-String -Path $dbgIni -Pattern '^Build=(.+)$' | Select-Object -First 1; if ($m) { $buildId = $m.Matches[0].Groups[1].Value.Trim() } }
    $label = ($buildId -replace '[^\w.@-]', '_')
    $dest = Join-Path $out "$(Get-Date -Format 'yyyyMMdd-HHmmss')-$label"
    New-Item -ItemType Directory -Force $dest | Out-Null
    foreach ($item in $new) { Copy-Item $item.FullName $dest -Recurse -Force }
    foreach ($f in 'edpe.log', 'edpe.ini', 'edpe_debug.ini') {
        if (Test-Path (Join-Path $game $f)) { Copy-Item (Join-Path $game $f) $dest -Force }
    }
    Add-Content $indexPath ($new | ForEach-Object { $_.Name })
    "Collected $($new.Count) item(s) into $dest"

    # One row per option from each self-test JSON: frame time is present to present inside the process.
    $rows = @()
    foreach ($j in Get-ChildItem $dest -Filter 'edpe-selftest-*.json' -File) {
        $d = Get-Content $j.FullName -Raw | ConvertFrom-Json
        if (-not $d.summary) { continue }
        foreach ($s in $d.summary) {
            $rows += [pscustomobject]@{
                report = $j.Name; build = $d.build; scene = $Scene; mode = $d.mode; sharpness = ('{0:0.00}' -f $d.sharpness)
                option = $s.option; blocks = $s.blocks; median_ms = ('{0:0.00}' -f $s.meanMedianMs); p99_ms = ('{0:0.00}' -f $s.meanP99Ms)
                spread_ms = ('{0:0.00}' -f ($s.maxMedianMs - $s.minMedianMs)); gpu_timer_ms = ('{0:0.00}' -f $s.meanGpuMs)
            }
        }
    }
    if ($rows.Count -gt 0) {
        $rows | Export-Csv (Join-Path $dest 'summary.csv') -NoTypeInformation -Encoding ascii
        $rows | Format-Table option, blocks, median_ms, p99_ms, spread_ms, gpu_timer_ms -AutoSize | Out-String | Write-Host
    }
    foreach ($t in Get-ChildItem $dest -Filter 'edpe-selftest-*.txt' -File) { "--- $($t.Name)"; Get-Content $t.FullName | Select-Object -First 40 }
}

# ---------------------------------------------------------------- restore

function Invoke-Restore([string]$game) {
    Assert-GameDir $game
    Assert-GameClosed
    $backupRoot = Join-Path $work 'game-backup'
    $marker = Join-Path $backupRoot 'LATEST.txt'
    if (-not (Test-Path $marker)) { 'Nothing to restore: no test build was deployed from this work folder.'; return }
    $backup = Join-Path $backupRoot ((Get-Content $marker -Raw).Trim())
    if (-not (Test-Path $backup)) { throw "Backup folder missing: $backup. Not removing anything." }

    foreach ($f in $managedFiles) { $p = Join-Path $game $f; if (Test-Path $p) { Remove-Item $p -Force } }
    $d3d12 = Join-Path $game 'D3D12'
    if (Test-Path (Join-Path $d3d12 'D3D12Core.dll')) { Remove-Item (Join-Path $d3d12 'D3D12Core.dll') -Force }
    if ((Test-Path $d3d12) -and -not (Get-ChildItem $d3d12 -Force)) { Remove-Item $d3d12 -Force }

    foreach ($f in $managedFiles) { $b = Join-Path $backup $f; if (Test-Path $b) { Copy-Item $b $game -Force } }
    if (Test-Path (Join-Path $backup 'D3D12')) { Copy-Item (Join-Path $backup 'D3D12') $game -Recurse -Force }
    Remove-Item $marker -Force
    "Removed the test build and restored the originals from $backup (edpe.log and reports in the game folder were left in place)."
}

# ---------------------------------------------------------------- status

function Show-Status([string]$repo, [string]$game) {
    "RepoDir : $repo"
    "WorkDir : $work"
    "GameDir : $(if ($game) { $game } else { '(not found: pass -GameDir or set EDPE_GAME_DIR)' })"
    if ($game -and (Test-Path (Join-Path $game 'EliteDangerous64.exe'))) {
        "Game    : $(if (Get-Process EliteDangerous64 -ErrorAction SilentlyContinue) { 'running' } else { 'closed' })"
        $deployed = Test-Path (Join-Path $game 'edpe_debug.dll')
        $b = ''
        if ($deployed -and (Test-Path (Join-Path $game 'edpe_debug.ini'))) { $m = Select-String -Path (Join-Path $game 'edpe_debug.ini') -Pattern '^Build=(.+)$' | Select-Object -First 1; if ($m) { $b = " ($($m.Matches[0].Groups[1].Value.Trim()))" } }
        "Deployed: $(if ($deployed) { "test build$b" } else { 'no' })"
    }
    $builds = Join-Path $work 'builds'
    if (Test-Path $builds) { "Builds  : $((Get-ChildItem $builds -Directory | ForEach-Object { $_.Name }) -join ', ')" } else { 'Builds  : none' }
    foreach ($tool in 'git', 'cmake', 'python') { "$($tool.PadRight(8)): $(if (Get-Command $tool -ErrorAction SilentlyContinue) { 'found' } else { 'missing' })" }
}

# ---------------------------------------------------------------- main

$repoPath = Resolve-RepoDir
$work = if ($WorkDir) { $WorkDir } else { Join-Path $env:LOCALAPPDATA 'edpe-test' }
$gamePath = Find-GameDir

switch ($Command) {
    'Status'  { Show-Status $repoPath $gamePath }
    'Build'   { Invoke-Build $repoPath (Resolve-Sha $repoPath $Ref) }
    'Deploy'  { Invoke-Deploy $repoPath (Resolve-Sha $repoPath $Ref) $gamePath }
    'Collect' { Invoke-Collect $gamePath }
    'Restore' { Invoke-Restore $gamePath }
}
