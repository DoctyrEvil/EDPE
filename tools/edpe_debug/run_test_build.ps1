# Build any EDPE branch with the debug hook and test module in a throwaway worktree, and optionally deploy it.
# The branch itself is never modified, so nothing test-only can reach a pull request.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\edpe_debug\run_test_build.ps1 -Branch fsr4-pr2-backend
#   ... -Deploy                       # also deploy to the game folder (game must be closed), DEV=1, Mode=Performance, Fsr4=1
#   ... -Deploy -AutoRun -AutoExit    # the game runs the self-test by itself and exits; report: edpe-selftest-<date>-<time>-mode<N>.txt/.json
#   ... -Deploy -Remove               # restore the game folder (calls deploy_fsr4_poc.ps1 -Action Remove)
#
# Reports are written next to dxgi.dll in the game folder. Use -Collect [-Scene <label>] to copy them into baseline\selftest\<stamp>-<branch> and append them to baseline\results.csv.
param(
    [string]$Branch = 'fsr4-pr2-backend',
    [switch]$Deploy,
    [switch]$Remove,
    [switch]$Collect,
    [string]$Scene = 'main menu',   # scene label stored with -Collect results in baseline\results.csv
    [switch]$NoBuild,
    [string]$UseArchive,   # with -Deploy -ModuleOnly: swap in dxgi.dll and edpe_debug.dll from work\builds\<sha> instead of the worktree build
    [switch]$ModuleOnly,   # with -Deploy: only refresh edpe_debug.dll and edpe_debug.ini (keeps the original backup intact)
    [ValidateSet('NativeAA', 'Quality', 'Performance')][string]$Mode = 'Performance',
    [switch]$AutoRun,
    [switch]$AutoExit,
    [int]$StartDelay = 300,
    [int]$Warm = 120,
    [int]$Hold = 600,
    [int]$Stress = 600,
    [double]$Sharpness = 0.0
)
$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$main = Join-Path $root 'work\EDPE'
$wt = Join-Path $root 'work\EDPE-test'
$game = 'C:\Program Files (x86)\Steam\steamapps\common\Elite Dangerous\Products\elite-dangerous-odyssey-64'
$deployScript = Join-Path $root 'tools\deploy_fsr4_poc.ps1'

function Invoke-Native([string]$exe, [string[]]$arguments) {
    & $exe @arguments
    if ($LASTEXITCODE -ne 0) { throw "$exe $($arguments -join ' ') failed with exit code $LASTEXITCODE" }
}

if ($Remove) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File $deployScript -Action Remove
    return
}

if ($Collect) {
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $dest = Join-Path $root "baseline\selftest\$stamp-$Branch"
    New-Item -ItemType Directory -Force $dest | Out-Null
    # Only reports not collected before (matched by file name anywhere under baseline\selftest).
    $known = @(Get-ChildItem (Join-Path $root 'baseline\selftest') -Recurse -Filter 'edpe-selftest-*' -ErrorAction SilentlyContinue | ForEach-Object { $_.Name })
    foreach ($f in Get-ChildItem $game -Filter 'edpe-selftest-*') { if ($known -notcontains $f.Name) { Copy-Item $f.FullName $dest -Force } }
    foreach ($f in 'edpe.log', 'edpe.ini', 'edpe_debug.ini') {
        $p = Join-Path $game $f
        if (Test-Path $p) { Copy-Item $p $dest -Force }
    }
    "Collected into $dest"
    # Append the new reports to the running collection (baseline\results.csv); already imported files are skipped.
    $json = @(Get-ChildItem $dest -Filter 'edpe-selftest-*.json' | ForEach-Object { $_.FullName })   # @() so one file is not splatted as characters
    if ($json) { & python (Join-Path $root 'tools\results_add.py') import-selftest --scene $Scene @json }
    Get-ChildItem $dest -Filter 'edpe-selftest-*.txt' | ForEach-Object { Get-Content $_.FullName }
    return
}

if (-not $NoBuild -or $Deploy) {
    $sha = (& git -C $wt rev-parse --short HEAD).Trim()
}

if (-not $NoBuild) {
    # 1. Worktree on the branch under test (detached, so the branch ref is never touched).
    if (-not (Test-Path $wt)) {
        Invoke-Native git @('-C', $main, 'worktree', 'add', '--detach', $wt, $Branch)
    } else {
        Invoke-Native git @('-C', $wt, 'reset', '--hard', '-q')
        Invoke-Native git @('-C', $wt, 'clean', '-fdq', '--', 'src', 'tests')
        Invoke-Native git @('-C', $wt, 'checkout', '--detach', '-q', $Branch)
    }
    $sha = (& git -C $wt rev-parse --short HEAD).Trim()

    # 2. external/ is untracked in the main checkout (SDKs, imgui): share it with a junction.
    $ext = Join-Path $wt 'external'
    $isJunction = (Test-Path $ext) -and ((Get-Item $ext -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)
    if (-not $isJunction) {
        if (Test-Path $ext) { Remove-Item $ext -Recurse -Force }
        New-Item -ItemType Junction -Path $ext -Target (Join-Path $main 'external') | Out-Null
    }

    # 3. Hook (copied sources + three anchored edits). Fails loudly if the tree drifted.
    Invoke-Native python @('-I', (Join-Path $PSScriptRoot 'apply_hook.py'), $wt)

    # 4. Build the proxy and the module.
    Invoke-Native cmake @('-S', $wt, '-B', (Join-Path $wt 'build'), '-A', 'x64')
    Invoke-Native cmake @('--build', (Join-Path $wt 'build'), '--config', 'Release', '--target', 'EDPE_DXGI', '--parallel')
    Invoke-Native cmake @('-S', (Join-Path $PSScriptRoot 'module'), '-B', (Join-Path $wt 'build-debug'), '-A', 'x64', "-DEDPE_TREE=$wt")
    Invoke-Native cmake @('--build', (Join-Path $wt 'build-debug'), '--config', 'Release', '--parallel')
    "Built branch $Branch ($sha) with the debug hook in $wt"
    # Keep the built proxy and module so builds can be swapped without rebuilding (-UseArchive <sha>).
    $arch = Join-Path $root "work\builds\$sha"
    New-Item -ItemType Directory -Force $arch | Out-Null
    Copy-Item (Join-Path $wt 'build\Release\dxgi.dll') $arch -Force
    Copy-Item (Join-Path $wt 'build-debug\Release\edpe_debug.dll') $arch -Force
}

if ($Deploy) {
    $build = Join-Path $wt 'build\Release'
    if (-not $ModuleOnly) {
        & powershell -NoProfile -ExecutionPolicy Bypass -File $deployScript -Action Deploy -Mode $Mode -Fsr4 1 `
            -Sharpness $Sharpness -Dev -BuildDir $build
        if ($LASTEXITCODE -ne 0) { throw 'deploy failed' }
    } else {
        if ($UseArchive) { $build = Join-Path $root "work\builds\$UseArchive" }
        Copy-Item (Join-Path $build 'dxgi.dll') $game -Force
    }
    $moduleSrc = if ($ModuleOnly -and $UseArchive) { Join-Path $root "work\builds\$UseArchive\edpe_debug.dll" } else { Join-Path $wt 'build-debug\Release\edpe_debug.dll' }
    Copy-Item $moduleSrc $game -Force
    $ini = @"
[Debug]
AutoRun=$(if ($AutoRun) { 1 } else { 0 })
AutoExit=$(if ($AutoExit) { 1 } else { 0 })
StartDelay=$StartDelay
WarmFrames=$Warm
HoldFrames=$Hold
StressFrames=$Stress
Build=$Branch@$(if ($UseArchive) { $UseArchive } else { $sha })
"@
    Set-Content (Join-Path $game 'edpe_debug.ini') $ini -Encoding ascii
    'Deployed with edpe_debug.dll. Start the game, load a static scene, open the EDPE menu (F5) -> "EDPE Developer Tests".'
    if ($AutoRun) { 'AutoRun is on: the self-test starts by itself once the upscaler has run for StartDelay frames.' }
}
