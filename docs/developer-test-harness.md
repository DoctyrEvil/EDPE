# Developer test harness

An in-game test module for EDPE developers, and a PowerShell driver that builds, deploys and collects it. It measures what unit tests and CI cannot: frame times per upscaler option inside the real game, switching between options, and frame generation behavior recorded with PresentMon.

It is a developer tool. It is not part of the EDPE runtime, CI or any pull request, and it adds no code to a branch under test.

## What it is, and what it is not

| | Unit tests (`tests/`, CI) | This harness |
|---|---|---|
| Needs the game | No | Yes |
| Runs in CI | Yes | No |
| Checks | Logic, math, hooks, standalone D3D11/D3D12 harnesses | Frame time per option, option switching, FG on and off, device errors, in the real game |
| Result | Pass or fail | Reports and numbers to compare between builds |

If a behavior can be checked without the game, write a test in `tests/` instead. The harness does not replace the developer panels behind `[Overlay] DEV=1` (comparison capture, jitter view, `EnableProfiler=1` sub-pass profiling). It adds unattended runs, per-option statistics, a switch stress test, scripted FG captures and DRED reporting, which those panels do not provide.

## How it works

1. `edpe-test.ps1 Build` checks out the commit under test in a throwaway, detached worktree (never the branch itself).
2. `apply_hook.py` copies the host-side hook into that worktree's `src/` and makes small anchored edits. If a file changed so that an anchor no longer matches, it stops with an error.
3. The proxy (`dxgi.dll`) and the module (`edpe_debug.dll`) are built and stored in a per-commit archive, so a build can be deployed again without rebuilding.
4. The hook loads `edpe_debug.dll` only when `[Overlay] DEV=1`. Without the module the hooked proxy does nothing extra.
5. `Deploy` copies the build into the game folder, and `Collect` copies the results out.

The module talks to the hook through a versioned C ABI (`tools/edpe_debug/include/edpe_debug_api.h`), so it never includes EDPE headers.

## Requirements

- Windows 10 or 11 x64 and the EDPE build requirements from `BUILD.md` (Visual Studio 2022 C++ tools, CMake, Git). Run from an x64 Developer PowerShell.
- Python 3, used only by `apply_hook.py`.
- The `external/` folder (SDKs and imgui) in your EDPE checkout, as for any EDPE build.
- Elite Dangerous installed. The scripted FG capture also needs AMD's PresentMon (`PresentMon-x64.exe`, installed with Adrenalin).

## Quick start

```powershell
cd <your EDPE checkout>            # on the tools/edpe-debug branch, or any checkout with tools\edpe_debug
.\tools\edpe_debug\edpe-test.ps1 Status

# build the branch under test and deploy it (game closed); runs the self-test by itself and exits the game
.\tools\edpe_debug\edpe-test.ps1 Deploy -Ref my-feature-branch -AutoRun -AutoExit

# start the game, load a static scene (the main menu with the same ship works well), wait for the game to exit
.\tools\edpe_debug\edpe-test.ps1 Collect -Scene "main menu"

# put the game folder back
.\tools\edpe_debug\edpe-test.ps1 Restore
```

`Get-Help .\tools\edpe_debug\edpe-test.ps1 -Full` lists every parameter.

## Commands

| Command | What it does |
|---|---|
| `Status` | Prints the resolved paths, whether the game is running, whether a test build is deployed, the available builds and which tools are found. Changes nothing. |
| `Build` | Builds `-Ref` (default: the commit checked out in `-RepoDir`) with the hook and module into `<WorkDir>\builds\<commit>`. |
| `Deploy` | Builds if no archive exists (unless `-NoBuild`), backs up the files it manages, copies the build, patches `edpe.ini` and writes `edpe_debug.ini`. The game must be closed. |
| `Collect` | Copies self-test reports, capture folders, `edpe.log`, `edpe.ini` and `edpe_debug.ini` that were not collected before into `<OutDir>\<stamp>-<build>\`, and writes `summary.csv`. |
| `Restore` | Removes the files `Deploy` put in the game folder and puts the backed-up originals back. |

### Where things go

| Item | Default | Override |
|---|---|---|
| EDPE checkout | The repository that contains the script | `-RepoDir` |
| Game folder | `$env:EDPE_GAME_DIR`, then the Steam library | `-GameDir` |
| Worktree, build archive, backups | `%LOCALAPPDATA%\edpe-test` | `-WorkDir` |
| Results | `.\edpe-test-results` | `-OutDir` |
| `external/` | `<RepoDir>\external` | `-ExternalDir` |

### What Deploy changes

- **Files it may replace in the game folder:** `dxgi.dll`, `edpe.ini`, the AMD FidelityFX DLLs, `nvngx_dlss.dll`, `edpe_debug.dll`, `edpe_debug.ini`, and `D3D12\D3D12Core.dll`. It does not touch anything else.
- **Backup:** the first deploy copies those files to `<WorkDir>\game-backup\<stamp>`. A second deploy while a test build is in place keeps that backup, so the backup always holds your original files.
- **`edpe.ini`:** only these keys are set: `[Upscaler] Backend`, `Mode`, `Sharpness`, `[Overlay] EnableProfiler` and `DEV=1`. Every other line stays as it was. `Collect` stores the `edpe.ini` of the run next to its results.
- **`-ModuleOnly`:** swaps in `dxgi.dll` and `edpe_debug.dll` and rewrites `edpe_debug.ini`, and leaves `edpe.ini` and the backup alone.
- **Elite settings:** EDPE's own `AutoSyncEliteConfig` setting decides whether EDPE edits the game's `Custom.*.fxcfg` for the render scale. The harness does not change that setting. Check it in your `edpe.ini` if you compare runs.

## Self-test

Each available option (bypass, AMD FSR 3.1.2, AMD FSR 4.1.2, NVIDIA DLSS, native TAA) gets a block of `WarmFrames` ignored frames and `HoldFrames` measured frames. The list runs `Rounds` times, in alternating order, so scene drift shows as a difference between rounds. Options that are not available are listed as SKIP with the reason.

Per block the module checks that the expected backend or provider is active at the end, that FSR 4 reports a GPU time, and that no frame is longer than the larger of 100 ms and 8 times the median. The report starts with a per-option summary: mean of block medians, spread across rounds, p99 and GPU timer.

With `-RunStress` the run ends with a provider stress test (FSR 3.1.2 and FSR 4.1.2 alternate every 10 frames, no frame over 2 s) and a settle block on FSR 4.1.2.

Reading the numbers:
- Frame time is present to present inside the process. It is not PresentMon and is not comparable with PresentMon figures.
- The render scale (`-Mode`) is fixed at game start, so one run covers one mode. Run again with another `-Mode`.
- The GPU timer is not valid for bypass and may be incomplete for FSR 3.1.2. Compare options by frame time.
- Compare builds at the same `-Sharpness`. The sharpen pass can add close to 1 ms on a branch without deferred sharpening, so a different sharpness is a different measurement.
- Load a static scene before the run. The stall check assumes steady frames.

Settings changed during the run are restored at the end or on Abort. The self-test refuses to start while FG is on or while a capture is running.

Output files, beside `dxgi.dll`: `edpe-selftest-<date>-<time>-mode<N>.txt` and `.json`. The header records the build (`branch@commit`), mode, sharpness and FSR 4 availability. A new pair is written per run and nothing is overwritten.

## Scripted frame generation capture

The module runs a whole FG experiment and records it with PresentMon, so no timing is done by hand. Start it from the F5 menu: "EDPE Developer Tests", then "Capture: FSR 4.1.2" or "Capture: FSR 3.1.2". The menu can be closed afterwards. A beep marks each phase.

1. It selects the provider and turns FG off, then starts PresentMon attached to the game process with `--qpc_time_ms`.
2. It walks `CaptureSequence`, switching FG with the same call as the menu, and logs every transition and a marker each second to `events.csv` with the QPC time in milliseconds (the clock PresentMon writes).
3. It restores FG, the backend and the provider, and waits for PresentMon to finish.

Keys in `edpe_debug.ini`, section `[Debug]`, set with `-DebugKey 'Key=Value'`:

| Key | Meaning |
|---|---|
| `CaptureProvider` | `4` = AMD FSR 4.1.2, `31` = AMD FSR 3.1.2 |
| `CaptureSequence` | Phases as `state:seconds[:menu]`, for example `off:15:m1,on:15:m0`. `m1` opens the F5 menu for the phase and `m0` closes it. Without a suffix the menu is left alone. Default: `off:15:m1,off:20:m0,off:20:m1,on:15:m0,off:20:m0,off:20:m1` |
| `CaptureSettle` | Seconds with FG off before the schedule. Default 3 |
| `PresentMonPath` | Default `C:\Program Files\AMD\CNext\CNext\PresentMon-x64.exe` |

Output goes to `edpe-capture-<stamp>-fsr4` or `-fsr31` beside `dxgi.dll`: `events.csv`, `presentmon.csv` and `summary.txt` (in-process frame time per phase). Analyze a capture with:

```powershell
python tools\edpe_debug\analyze_capture.py "<capture folder>" --timeline
```

The table lists, per phase: FG state, present modes, fps, frame time p99 and max, displayed frame time, a cadence figure (standard deviation of the displayed frame time over its mean), display latency, and when in the phase the present mode last changed. The share of frames that were never displayed is reported as well.

## DRED

The module forces D3D12 Device Removed Extended Data on for the devices EDPE creates for the bridge and frame generation, and polls them once per frame. After a device removal, the breadcrumbs and page fault data are written to `edpe.log` lines that start with `EDPE DRED`.

## Developer builds only: safety rules

- Never run `apply_hook.py` on a branch you intend to push. `Build` does this in its own worktree.
- Never open a pull request that contains hook or module code.
- `-Forensics` adds a guard in `PerfTracker` that recovers from corrupt history indices. It can hide the memory corruption it was written to find, so use it only to keep a log alive while chasing one crash.
- Do not switch the upscaler with FG on in a build that lacks the FG teardown fix. That sequence caused a GPU hang and a bluescreen on one test machine.

## Limits

- Tested on Windows 10 with an AMD Radeon RX 7900 XTX, Adrenalin 26.11.2, windowed, 240 Hz. Not tested on NVIDIA, Windows 11, Linux/Proton, fullscreen or VR.
- The D3D11 debug layer is not supported inside the game on the test machine (it crashed at device creation).
- The scripted capture is AMD-specific, because it uses AMD's PresentMon and the AMD FG provider.
- Fault injection and screenshot comparison are not implemented.
- The driver (build, deploy, collect, restore) was verified against a stand-in game folder, with a real build of an EDPE branch. The deployed build has not yet been started in the game through this driver. The in-game module itself is the one used for earlier test runs.
