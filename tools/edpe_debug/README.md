# EDPE debug module (developer tests, never part of a pull request)

Runs the FSR 3.1.2 / FSR 4.1.2 checks from inside the game and writes `edpe-selftest-<date>-<time>-mode<N>.txt` and `.json` (a new pair per run, never overwritten; the report header has the build `branch@commit`, mode, sharpness and FSR 4 availability).
Nothing here touches the EDPE branches: `run_test_build.ps1` builds any branch in a throwaway worktree
(`work/EDPE-test`, detached) after applying the hook with `apply_hook.py`.

| Path | What |
|---|---|
| `include/edpe_debug_api.h` | Versioned C ABI between the proxy hook and the module (host callbacks, module entry table) |
| `hook/debug_hook.{h,cpp}` | Host side. Copied into the worktree's `src/`; loads `edpe_debug.dll` only when `[Overlay] DEV=1` |
| `apply_hook.py` | Copies the hook files and makes three anchored edits (CMake source list, `ui.cpp` include and frame call). Fails if an anchor moved |
| `module/` | `edpe_debug.dll`: self-test, switch stress, report writer, "EDPE Developer Tests" menu window |
| `run_test_build.ps1` | Build and deploy driver (`-Branch`, `-Deploy`, `-AutoRun`, `-AutoExit`, `-Collect`, `-Remove`) |

## Run

```powershell
# build a branch with the hook and deploy it (game closed)
powershell -NoProfile -ExecutionPolicy Bypass -File tools\edpe_debug\run_test_build.ps1 -Branch fsr4-pr2-backend -Deploy
# unattended: self-test starts once the upscaler has run for StartDelay frames, then the game exits
powershell -NoProfile -ExecutionPolicy Bypass -File tools\edpe_debug\run_test_build.ps1 -Branch fsr4-pr2-backend -Deploy -AutoRun -AutoExit
# after the run: copy reports into baseline\selftest\
powershell -NoProfile -ExecutionPolicy Bypass -File tools\edpe_debug\run_test_build.ps1 -Branch fsr4-pr2-backend -Collect
# restore the game folder
powershell -NoProfile -ExecutionPolicy Bypass -File tools\edpe_debug\run_test_build.ps1 -Remove
```

Load a static scene before the run (the frame-stall checks assume steady frames).

## Checks

Default run: every available option (bypass, AMD FSR 3.1.2, AMD FSR 4.1.2, NVIDIA DLSS, native TAA) gets a block of `WarmFrames` plus `HoldFrames`, and the list runs `Rounds` times (alternating forward and reverse order, so drift shows as a difference between rounds). Unavailable options are listed as SKIP with the reason. Per block: the right backend or provider is active at the end, FSR 4 reports a GPU time, and no frame exceeds max(100 ms, 8x the median). The report starts with a per-option summary (mean of block medians, spread across rounds, p99, GPU timer).

Optional at the end (`RunStress=1` or the menu checkbox): provider stress (FSR 3.1.2 and FSR 4.1.2 alternate every 10 frames, no frame over 2 s) and a settle block back on FSR 4.1.2.

The render scale (mode) is fixed at game start, so one run covers one mode. Change `Mode` in `edpe.ini` and restart for the others. Frame time is present to present inside the process (not PresentMon). The GPU timer is not valid for bypass and may be incomplete for FSR 3.1.2; compare options by frame time.

Settings are restored at the end (or on Abort). Not yet implemented: fault injection, A/B screenshot capture. Best test scene so far: the main menu with the same ship (rounds agree within 0.01 ms).

## Scripted frame generation capture (replaces hand-timed PresentMon runs)

The module can run the whole frame generation experiment by itself and record it: no timing by hand, and the menu can be
closed (a beep marks each phase). From the F5 menu, "EDPE Developer Tests", click "Capture: FSR 4.1.2" or "Capture: FSR 3.1.2".

What it does:
1. Selects the provider, turns frame generation off, and starts AMD's PresentMon as a child process attached to the game
   (`--process_id`, `--qpc_time_ms`, `--timed <schedule + 8 s>`).
2. Walks `CaptureSequence` (default `off:20,on:20,off:40,on:20,off:40`; the first `CaptureSettle` seconds are always off),
   calling the same frame generation switch as the menu combo, and logs every transition and a marker each second to
   `events.csv` with the QPC time in ms (the clock PresentMon writes).
3. Restores frame generation, the backend and the provider, then waits for PresentMon to finish.

Output goes to `edpe-capture-<stamp>-fsr4|fsr31\` beside `dxgi.dll`: `events.csv`, `presentmon.csv`, `summary.txt` (in-process frame
time per phase). Analyze it with:

```powershell
python toolsnalyze_capture.py "<game folder>\edpe-capture-<stamp>-fsr4" --timeline
```

The table has, per phase: frame generation state, present modes (Independent Flip, Composed ...), fps, frame time p99/max,
displayed frame time, a cadence figure (stdev of the displayed frame time over its mean), display latency, and when inside the phase
the present mode last changed. Optional keys in `edpe_debug.ini` `[Debug]`: `CaptureProvider`, `CaptureSequence`, `CaptureSettle`,
`PresentMonPath` (default `C:\Program Files\AMD\CNext\CNext\PresentMon-x64.exe`).

The hook now has an always-on `tick` (called every presented frame before the overlay early-return), so the self-test and captures run
with the menu closed. The self-test refuses to start, and a capture refuses to start, while the other is running; the self-test also
refuses while frame generation is on (switching backends with it on crashed the game once).
