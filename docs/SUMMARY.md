# FSR 4.1.2 in Elite Dangerous (D3D11) via EDPE: results summary

One-page reference for the upstream issue and review, updated 2026-11-22. Figures are copied from the detailed docs listed at the end, unchanged. Single runs are marked. Anything estimated or not measured is labeled as such. A condensed version for the feature request is in `docs/feature-request.md`.

## 1. Status and what was built

- **Problem.** Elite Dangerous is a Direct3D 11 game. The AMD driver's FSR 3.1 to FSR 4 override needs a game that loads AMD's signed FSR 3.1 DLL through D3D12 (press reports; AMD does not publish the detection logic). EDPE and ED-UP supply their own D3D11 port of FSR 3.1, so the driver has nothing to upgrade.
- **Implementation.** A D3D11 to D3D12 bridge that runs AMD's official signed FSR 4.1.2 DLLs inside EDPE (github.com/filispeen/EDPE). This is the "Route B" of `PLAN.md` (official current FidelityFX API through D3D11/D3D12 interop). The menu has separate "AMD FSR 3.1.2" and "AMD FSR 4.1.2" entries that switch at runtime; the active provider is shown in the menu and the performance HUD.
- **Branch and release.** Local branch `fsr4-poc`, 7 commits on upstream master `30ee13a` (rebased 2026-11-22), tag `fsr4-prototype-1` at `99d944d`; nothing pushed. Local prototype package `release/edpe-fsr4-prototype-1.zip` (35.2 MB, not for redistribution until AMD and Microsoft license terms are confirmed).
- **Method.** Color, depth and motion are copied into shared NT-handle textures. A D3D12 queue waits on a shared fence, dispatches FSR 4, signals back, and D3D11 waits on the GPU before copying the output to the destination. The D3D12 device is created through `ID3D12DeviceFactory` from a side-loaded Agility SDK 1.616.1 `D3D12Core.dll` (needed for the runtime to offer FSR 4.1.2 on this Windows 10 machine; falls back to the system runtime if absent).
- **Provider selection.** The engine asks AMD's runtime which providers it offers (here 4.1.2, 3.1.6, 2.3.4), picks the highest 4.x one, and requests it explicitly. If none is offered (RDNA 2, other vendors) the menu shows FSR 4.1.2 as unavailable with the reason, as DLSS is, and FSR 4 never runs a 3.x provider under its name. FSR 4 is probed once at startup when its DLLs are present.
- **Fail open.** Any FSR 4 failure falls back to the D3D11 FSR 3.1.2 path, and FSR 4 is disabled after 120 consecutive failures until selected again. HDR color input (`HdrInput=1`) is not supported by FSR 4: those frames use FSR 3.1.2 and the menu says so.
- **Input mapping.** Reversed-Z infinite depth (typeless depth converted to `R32_FLOAT`), motion vectors `R16G16_FLOAT` in render pixels without jitter, jitter in render pixels (Halton, conversion matches the SDK recipe `2*jx/renderW`, `-2*jy/renderH`), 8-bit LDR color with the non-linear color flag and sRGB dispatch flag, auto exposure (`HdrInput=0` for every measurement).
- **Tests.** 28 of 28 `ctest` targets pass on the rebased tree, including a CPU-only `fsr4_logic_test` (failure gate, provider state, version picking, input mapping), an engine test with 10 cases that skips cleanly without FSR 4 (basic, provider, resize, frame-order, depth-r24g8, reinit, fail-open, hdr-refused, agility-fallback, agility-staged), and a config round trip with the FSR 4 keys.

## 2. Test setup

RX 7900 XTX (24 GB), Ryzen 7 5800X3D, Windows 10 Home 19045, Adrenalin 26.11.2 (driver 32.0.32029.2010), Resizable BAR enabled (user-reported). 3840x2160 exclusive fullscreen, VSync off, no frame cap. Frame times from AMD's bundled PresentMon (about 59 s per run). Scene: planet landed, static camera (primary); settlement on foot (vanilla only, moving, more variable). Render scale set through Elite's `SSAAMultiplier` (0.5 is the lowest value in Elite's standard settings; EDPE's AutoSyncEliteConfig rewrites it per mode). In-game checkerboard rendering off, AA mode 0.

## 3. Baseline and EDPE configurations (planet landed, static)

| Config | Frame ms | fps | 1% low | GPU busy | Runs |
|---|---|---|---|---|---|
| A vanilla, native, no AA | 7.38 (7.39 mean of 3) | 135.4 | 115.3 | 100% | 3 |
| A vanilla, scale 0.5 | 6.95 | 143.9 | 118.6 | 65% (4.51 ms) | 1 |
| B EDPE installed, backend none | 7.52 | 133.0 | 109.4 | 98% | 1 |
| C EDPE native TAA | 8.46 | 118.2 | 100.2 | 100% | 1 |
| D EDPE FSR 3.1.2, native AA | 9.79 | 102.2 | 87.2 | 100% | 1 |
| D EDPE FSR 3.1.2, scale 0.65 (older build) | 8.36 | 119.7 | | 78% (6.54 ms) | 1 |
| D EDPE FSR 3.1.2, scale 0.5 (older build) | 8.31 | 120.4 | 100.2 | 69% (5.73 ms) | 1 |
| F3 EDPE FSR 3.1.2, scale 0.5 (current build, `Fsr4=0`, sharpness 0.50) | 8.79 | 113.7 | | 77% (6.81 ms) | 1 |
| E vanilla, 2x supersample | 21.37 | 46.8 | 43.1 | 100% | 3 |
| F4 EDPE FSR 4.1.2, native AA (sharpness 0.50) | 12.91 | 77.5 | 68.0 | 99% | 1 |
| F4 EDPE FSR 4.1.2, scale 0.5 (sharpness 0.50) | 9.14 | 109.4 | 94.6 | 99% (9.02 ms) | 1 |
| **F4 EDPE FSR 4.1.2, scale 0.5, sharpness 0** | **8.53** | **117.3** | | 98% (8.38 ms) | 1 |
| **F4 EDPE FSR 4.1.2, scale 0.5, sharpness 0 (rebased build)** | **8.47** | **118.0** | | 98% (8.33 ms) | 1 |

- Run-to-run spread, vanilla static: 2.0% average fps (A), 0.4% (E). Differences under about 3% (static) or 7% (on foot) cannot be separated from noise without more runs.
- Vanilla settlement on foot (3 runs): 98.2 fps mean, 10.20 ms, 1% low 70.1; spread 6.8% (camera path differed between runs).
- Hook layer cost (B): +0.13 ms, inside the noise. TAA (C): +1.08 ms. FSR 3.1.2 native AA (D): +2.40 ms in steady state. The 9.4 ms "first dispatch" figure in an earlier log was a cold dispatch and is not representative.
- FSR 4 native AA is 3.1 ms more than FSR 3.1.2 native AA (12.91 vs 9.79).
- The FSR 4 versus FSR 3.1.2 comparison at scale 0.5 is not like for like: the FSR 3.1.2 control ran at sharpness 0.50 and FSR 3.1.2 at sharpness 0 has not been measured. At equal sharpness 0.50 FSR 4 was 109.4 fps against 113.7 for the current-build FSR 3.1.2 control (about 4% lower), where FSR 3.1.2 is held by the CPU (GPU 77% busy) and FSR 4 is GPU-bound (99%).
- The older FSR 3.1.2 runs were held at about 8.3 ms by the CPU (identical frame times at 0.65 and 0.5, GPU 22 to 31% idle). That build predates upstream fix `c92b249` (HUD mask reallocation, 4,797 and 5,525 allocations in the old log, one in the new build), so that CPU floor is not firm.
- Stability: FSR 4 native 0.71 ms frame time stdev, 77.4 to 77.5 fps in every 10-second window; FSR 4 at 0.5 and sharpness 0.50 0.59 ms stdev, 109.0 to 110.0 fps, no frame over 20 ms.

## 4. Where the FSR 4 frame time goes (Performance, 1920x1080 to 3840x2160, static scene)

Cost model for the game itself (vanilla, native vs 2x): about 2.72 ms fixed plus 4.66 ms resolution-dependent at 4K native, assuming 2x is 2x per axis; with the 0.5 scale point the curve is slightly sublinear (slope about 3.8 ms per 4K of pixels between 0.25 and 1.0).

Per-stage timings from the engine's own profiler (`Fsr4Profile=1`, sharpness 0.50, last 39 blocks, 11,699 frames, about 107 s, flush on, first profiling build):

| Stage | Average | Share of the 9.14 ms frame |
|---|---|---|
| D3D12 FSR dispatch | 3.228 ms | 35% |
| Cross-queue sync gap | 0.285 ms | 3.1% |
| D3D11 copy-in plus depth conversion | 0.015 ms | 0.2% |
| D3D11 copy-out | 0.033 ms | 0.4% |
| D3D12 barriers | 0.003 ms | |
| D3D11 total (copy-in to copy-out) | 3.565 ms | 39% |
| CPU time of `evaluate` | 0.311 ms | |
| CPU waits for the GPU | 0 of 11,699 frames | |

With sharpness 0 the dispatch averages 2.49 ms (89 blocks), 2.57 ms (9 blocks) and 2.58 ms (76 blocks) in the rebased session's log, with `cpuWaits` 0 throughout; the sharpness 0.50 sessions showed 3.1 to 3.25 ms.

EDPE zone profiler (`EnableProfiler=1`, sharpness 0.50, last 60 windows, approximate): depthCopy 0.001, motion 0.075, colorPrep 0.022, uiComposite 0.089, hudBloom 0 ms; EDPE passes together about 0.19 ms. EDPE hook CPU time 0.34 ms. About 5.4 ms of the 9.14 ms frame is the game's own rendering.

Harness (synthetic frames, game closed, idle GPU) for reference, output 3840x2160:

| Render input | FSR 4.1.2 GPU time | Bridge only | Bridge + FSR 4 |
|---|---|---|---|
| 1920x1080 (0.5) | 2.96 ms | 0.22 ms | 3.48 ms |
| 2496x1404 (0.65) | 3.19 ms | 0.33 ms | 3.91 ms |
| 3840x2160 (native AA) | about 3.7 to 3.8 ms | 0.45 ms | 4.17 to 4.26 ms |

AMD's published reference for the RX 7900 XTX (model "Full INT8", 4K target, Performance mode, no RCAS, peak clocks) is 3070 us. An earlier smaller harness test (render 1706x960, display 2560x1440) gave FSR 1.27 to 1.37 ms and bridge plus FSR 1.60 to 1.63 ms; pipelining gave no gain there because the harness has no other work to overlap.

**Conclusion.** The bridge itself is not the problem: copies, depth conversion and barriers total about 0.05 ms, the sync gap is 0.29 ms, and the FSR network plus its sharpening pass is about 91% of the engine's cost. The in-game total is consistent with harness cost plus EDPE's passes, so no hidden overhead was found.

## 4a. Self-test across upscaler options (main menu scene, 2026-11-22)

Measured with the developer test module (`tools/edpe_debug/`, not part of any pull request) on the main menu scene with the same ship, build `fsr4-pr2-backend@4f83d89` plus the module hook, Mode 4 (Performance), render 1920x1080 to output 3840x2160, sharpness 0. Each option ran two rounds of 600 measured frames (120 warm-up frames each), in forward then reverse order. Frame time is present to present measured inside the process, not PresentMon. Report files: `baseline/selftest/20261123-000409-fsr4-pr2-backend/` (`edpe-selftest-20261122-235907-mode4.txt` and `.json`).

| Option | Median frame (ms) | Cost over bypass (ms) | p99 (ms) | Spread between rounds (ms) | GPU timer (ms) |
|---|---|---|---|---|---|
| Bypass (no reconstruction) | 2.44 | n/a | 2.72 | 0.01 | not valid (see below) |
| Native TAA | 2.92 | 0.48 | 3.21 | 0.00 | 0.31 |
| AMD FSR 3.1.2 | 3.26 | 0.82 | 3.59 | 0.01 | 0.61 |
| AMD FSR 4.1.2 | 5.80 | 3.36 | 6.23 | 0.00 | 3.06 |
| NVIDIA DLSS | skipped: detected AMD GPU | | | | |

- All 10 measured blocks passed (right backend active at the end of each block, no frame stall). The switch stress test (60 provider switches between FSR 3.1.2 and FSR 4.1.2, 600 frames) had a longest frame of 6.4 ms, and FSR 4.1.2 measured 5.83 ms after it (5.80 ms before). `edpe.log` shows no FSR 4 failures.
- The two rounds, run in opposite order, agree within 0.01 ms for every option, so this scene shows no drift or order effect.
- FSR 4.1.2 costs about 2.5 ms more than FSR 3.1.2 here (3.36 against 0.82 ms over bypass). The 3.36 ms agrees with the 3.06 ms GPU timer plus roughly the 0.3 ms sync gap in section 4. In the landed scene (sections 3 and 4) the FSR 4 to FSR 3.1.2 gap measured only 0.35 ms; the FSR 3.1.2 run there was CPU-limited (GPU busy 77%), which probably hid its cost. Treat this section as the cleaner cost comparison; that reading is an interpretation, not a separate measurement.
- Wall-clock cost over bypass exceeds the GPU timer by 0.17 ms (TAA), 0.21 ms (FSR 3.1.2) and 0.30 ms (FSR 4.1.2). The gap is not traced.
- The bypass GPU timer is not a measurement: it read 2.89 ms in round 1 and 0.61 ms in round 2, apparently keeping the last value of the previous backend. The FSR 3.1.2 timer may not cover all of its passes. Compare options by frame time.
- Bypass here is the game's own low-resolution render at Performance scale (stretched by the game), not vanilla at native resolution.
- An earlier self-test from the landed scene (`edpe-selftest-20261122-232529-mode4`, FSR 3.1.2 9.58 ms, FSR 4.1.2 9.93 ms, blocks run in sequence) is in the same folder. The scene differed, so it is not comparable with this table.
- The render scale is fixed when the game starts (EDPE overrides the runtime mode each frame from the actual render-to-output ratio), so one run covers one mode. Other modes need `Mode` changed in `edpe.ini` and a game restart; they have not been run.

Second main menu run on the rebased build (`fsr4-pr2-backend@36ff4f2`, upstream `a5377c9` plus PR 1, 2026-11-23 11:50, same settings, frame generation off; reports in `baseline/selftest/20261123-115143-fsr4-pr2-backend/`):

| Option | Median frame (ms) | Cost over bypass (ms) | p99 (ms) | First run median / p99 (ms) |
|---|---|---|---|---|
| Bypass | 2.49 | n/a | 2.99 | 2.44 / 2.72 |
| Native TAA | 2.99 | 0.50 | 5.72 | 2.92 / 3.21 |
| AMD FSR 3.1.2 | 3.36 | 0.87 | 5.92 | 3.26 / 3.59 |
| AMD FSR 4.1.2 | 6.00 | 3.51 | 11.56 | 5.80 / 6.23 |

- Medians moved up 0.05 to 0.20 ms (2 to 3%) against the first main menu run, and the ordering and the roughly 2.6 ms FSR 4 over FSR 3.1.2 gap are unchanged. Rounds still agree within 0.04 ms. All blocks and the stress test passed again (60 switches, longest frame 16.8 ms; settle block 6.01 ms, max 6.73 ms), and `edpe.log` shows no FSR 4 failures.
- The tails are worse: p99 rose for every option (FSR 4.1.2 from 6.23 to 11.56 ms, TAA 3.21 to 5.72 ms, FSR 3.1.2 3.59 to 5.92 ms, bypass 2.72 to 2.99 ms), and block maxima reached 24.68 ms (FSR 4.1.2). The cause is not known. Two things differ from the first run: the build now includes upstream's newer commits (game FPS limiter, HUD layer fix), and the run was taken at a different time with the game open for longer. A same-session comparison of the two builds has not been done.
- Frame generation was off in both runs. The report does not yet record the frame generation state.

Third run (same build, 2026-11-23 11:56, a new game launch with background apps closed; reports in `baseline/selftest/20261123-115808-fsr4-pr2-backend/`): medians repeat the second run (bypass 2.48, TAA 3.01, FSR 3.1.2 3.38, FSR 4.1.2 6.02 ms; cost over bypass 0.53, 0.90 and 3.54 ms), so the FSR 4 cost of about 3.5 ms is stable on this build. The tails got worse again: p99 bypass 3.36, TAA 8.94, FSR 3.1.2 8.88, FSR 4.1.2 15.18 ms, and block maxima up to 82 ms. One block failed the stall check: native TAA, round 1, one frame of 621.07 ms (limit 100 ms). Closing background apps did not improve the tails. The p99 of bypass, where EDPE does no reconstruction, also rose across the three runs (2.72, 2.99, 3.36 ms), so the cause does not look specific to FSR 4. The first run is the only one on the older build; the cause (the newer upstream code, the system, or the scene) is still undetermined.

Run on the window nudge build with forensic probes (`fsr4-pr2-backend@4bc53c3-fgfix-probe`, 2026-11-24 11:53, main menu, Performance, frame generation off; reports in `baseline/selftest/20261124-121512-fsr4-pr2-backend/`, captures in `baseline/fgcapture/edpe-capture-20261124-*`):

| Option | Median frame (ms) | p99 (ms) | GPU timer (ms) | GPU timer in the 2026-11-23 20:08 run (ms) |
|---|---|---|---|---|
| Bypass | 4.16 | 4.45 | not valid | not valid |
| Native TAA | 4.16 | 4.49 | 0.34 | 0.31 |
| AMD FSR 3.1.2 | 4.16 | 4.67 | 0.62 | 0.61 |
| AMD FSR 4.1.2 | 5.74 | 6.25 | 3.01 | 3.18 |

- Bypass, TAA and FSR 3.1.2 all sit at 4.16 ms, which is exactly 240 fps. This run was made in full screen with VSync on by mistake (earlier runs ran uncapped at 2.4 to 3.4 ms), so the display rate capped the frame time and wall-clock cost over bypass is hidden for everything except FSR 4.1.2 (+1.58 ms, only because it exceeds the cap). The frame limiter was off (`GameFpsLimit=0.0`). For this run, compare options by the GPU timer: FSR 4.1.2 3.01 ms (3.18 before), FSR 3.1.2 0.62 ms, TAA 0.34 ms, all within 0.2 ms of the previous run.
- Spread between rounds was 0.00 to 0.03 ms. All blocks passed. Switch stress (60 provider switches): longest frame 6.33 ms (previous run 10.30 ms). Tails are clean: no block maximum above 6.5 ms.
- The earlier 2026-11-23 runs that had not been collected yet (rebased build, the 4bc53c3 build, and the 20:08 window nudge build) were added to `baseline/results.csv` in the same pass, so the file now holds all of them.

Frame generation capture, FSR 3.1.2 against FSR 4.1.2 (7 phases each: off, off with menu, off, off with menu, on, off, off with menu):

| Provider | Rendered fps | Frame time median / p99 (ms) | Cadence CV | Display latency (ms) | Frames never shown |
|---|---|---|---|---|---|
| FSR 3.1.2 | 240 | 4.17 / 4.20 | 0.003 | 16.6 | 0% |
| FSR 4.1.2 | 172 | 5.80 / 6.19 | 0.030 | 23.2 | 0% |

- Frame generation never became active in either capture. Phase 4 had it configured (`fg_configured=1`) but `fg_active` stayed 0 in every phase, and the present mode stayed Hardware Composed Independent Flip throughout, with no present mode change. The cause was not determined: the log and `events.csv` hold no frame generation message or error, and the run was in full screen with VSync on, which is the likely reason but is not confirmed. So this run does not exercise frame generation, and it cannot confirm or refute the window nudge fix after frame generation teardown. The earlier capture that did (`edpe-capture-20261123-200418-fsr4`) remains the evidence for that fix.
- With frame generation off, both providers are steady across all seven phases (medians within 0.06 ms, unshown 0%, no change when the menu opens). FSR 4.1.2 shows a cadence CV of 0.03 against 0.003 for FSR 3.1.2, which is expected when 5.8 ms frames land on a 4.17 ms display grid. It shows no dropped frames.
- Display latency is 6.6 ms higher with FSR 4.1.2 (23.2 against 16.6 ms), and both values are about four frame times (16.6 / 4.17 and 23.2 / 5.80), so the extra latency follows the frame time.

Rerun with the right settings (same build, 2026-11-24 12:22, windowed, not full screen (user reconfigured after the mistaken run), frame generation test included; reports in `baseline/selftest/20261124-125741-fsr4-pr2-backend/`, captures `baseline/fgcapture/edpe-capture-20261124-122322-fsr31` and `-125358-fsr4`):

| Option | Median frame (ms) | p99 (ms) | GPU timer (ms) |
|---|---|---|---|
| Bypass | 2.54 | 2.94 | not valid |
| Native TAA | 3.05 | 3.45 | 0.31 |
| AMD FSR 3.1.2 | 3.42 | 4.45 | 0.61 |
| AMD FSR 4.1.2 | 6.08 | 6.51 | 3.22 |

- Back in line with every uncapped run: medians within 0.1 ms of the 2026-11-23 20:08 run, GPU timers identical (3.22 against 3.18 ms for FSR 4.1.2). Rounds agree within 0.06 ms (FSR 4.1.2 0.01 ms). Switch stress passed (60 switches, longest frame 7.02 ms) and the tails are clean (largest block p99 6.62 ms), like the last three runs before this one.
- Frame generation now engaged in both captures (`fg_active` 100% in phase 4):

| | FSR 3.1.2 | FSR 4.1.2 |
|---|---|---|
| Frame time, FG off (ms) | 3.40 | 6.08 |
| In-process frame time, FG on (ms) | 6.73 | 9.34 |
| Cost of frame generation (ms) | +3.3 | +3.3 |
| Display latency, FG off, windowed / FG on (ms) | 25.1 / 4.4 | 34.5 / 5.5 |
| Longest frame when FG starts (ms) | 262 | 240 |

  Frame generation costs the same 3.3 ms on both providers. The window uses a copied present path while FG is off (`Comp Copy with GPU GDI`) and Independent Flip while FG is on, which is why latency reads much lower with FG on. Turning FG on causes a hitch of about a quarter second in both captures.
- After frame generation teardown (phase 5) the present mode returns to the copied path. The window nudge fix limits the damage to a short transient: FSR 4.1.2 shows 3% of frames never shown in phase 5 (31% before the fix) and 0% in phase 6, with cadence CV back to 0.022. For FSR 3.1.2 the 18% never shown in phase 5 equals the 18% it shows before FG, because it renders at 290 fps into a 240 Hz display through the desktop compositor; it is not a post-FG effect.
- Display latency with FSR 4.1.2 read 6 ms higher after FG teardown (40.9 and 40.8 ms against 34.5 ms before) in this run. The next run disproved it: see the correction below.

Run on the cleaned PR 2 (`fsr4-pr2-on-feat@b23fee7-fgfix`: PR 2 after the code and comment cleanup, profiling code removed, plus the debug hook and the window nudge fix; 2026-11-24 13:42, windowed, same settings as the rerun above; reports in `baseline/selftest/20261124-134826-fsr4-pr2-on-feat/`, captures `baseline/fgcapture/edpe-capture-20261124-134335-fsr4` and `-134604-fsr31`):

| Option | Median frame (ms), round 1 / round 2 | GPU timer (ms) |
|---|---|---|
| Bypass | 2.44 / 2.49 | not valid |
| Native TAA | 3.07 / 3.05 | 0.31 |
| AMD FSR 3.1.2 | 3.36 / 3.42 | 0.61 |
| AMD FSR 4.1.2 | 6.07 / 6.09 | 3.22 |

- Same medians and GPU timers as every earlier uncapped run (FSR 4.1.2 3.22 ms, FSR 3.1.2 0.61 ms, TAA 0.31 ms). The cleanup and removing the profiling code did not change cost. Switch stress passed (60 switches, longest frame 11.7 ms) and so did the settle block (6.06 ms).
- The run reports 2 failed blocks out of 10, both from the harness limit of 100 ms for one frame: round 1 FSR 4.1.2 (one frame of 119.7 ms) and round 2 TAA (one frame of 641.5 ms). Medians in those blocks are normal (6.07 and 3.05 ms). The FSR 3.1.2 capture also has a single 476 ms frame in phase 2, with FG off and the menu closed. The stalls hit three different options, one of them TAA, which does not use the FSR code, so they do not point at FSR 4. Earlier runs on the same settings (12:22) had no such frames. Cause not found; the p99 values (up to 19 ms) are also higher than in the 12:22 run, so something else was using the machine during this run.
- Frame generation engaged in both captures. In-process frame time with FG on: FSR 3.1.2 6.75 ms (3.42 off), FSR 4.1.2 9.16 ms (6.04 off), so FG costs 3.3 ms on FSR 3.1.2 and 3.1 ms on FSR 4.1.2, as before. Starting FG causes one frame of about 235 to 240 ms.
- The window nudge fix still works after FG teardown: FSR 4.1.2 shows 3% of frames never shown in phase 5 and 0% in phase 6 (cadence CV 0.026), and FSR 3.1.2 stays at its normal 18% (it renders at 290 fps into a 240 Hz display).
- Correction to the rerun section above: display latency with FSR 4.1.2 is not raised after FG teardown. This run reads 40.7 ms before FG and 40.8 and 40.7 ms after it. The earlier 34.5 ms before FG came from a different run of the same configuration, so the baseline moves between runs by about 6 ms. Compare latency only within one capture.

## 5. Optimization experiments

- **Sharpening (inferred from one session).** In one session turning the sharpness slider to 0 took the FSR 4 dispatch from about 3.25 to 2.60 ms (about 0.65 ms, 20% of the dispatch, 7% of the frame) and the captured frame from 9.14 to 8.53 ms. The slider change time is inferred from log order and the user's report; a clean same-session A/B is not yet measured. FSR 4 may need less or no sharpening; image quality at 0 versus 0.50 has not been compared.
- **`Fsr4Flush=0`** (skip the D3D11 flush after the fence signal), last 39 blocks each, no PresentMon capture:

| ms | Flush=1 (first run) | Flush=1 (profiler run) | Flush=0 |
|---|---|---|---|
| sync gap | 0.285 | 0.294 | 0.203 |
| d11 total | 3.565 | 3.477 | 3.366 |
| FSR dispatch | 3.228 | 3.132 | 3.112 |
| cpu evaluate | 0.311 | 0.312 | 0.253 |
| cpuWaits | 0 | 0 | 0 |

  Saves about 0.08 to 0.09 ms (about 1% of the frame), near the 0.1 ms run-to-run variation. `Fsr4Flush=1` stays the default (the saving is small and may depend on driver submission behavior).
- **Model/preset.** The SDK offers three upscaler versions on this GPU (4.1.2, 3.1.6, 2.3.4); only 4.1.2 is ML. Model selection is automatic. No cheaper ML option exists through the API.
- **Parallelization.** CPU threading cannot help (frame is GPU-bound, `cpuWaits` 0). Overlapping FSR with next-frame scene work is estimated at most 0.3 to 0.5 ms, needs HUD separation, and adds a frame of latency; not built. Offloading EDPE's passes to another queue could save at most about 0.2 ms.
- **Render scale.** FSR 4 cost follows output resolution more than render scale (2.96, 3.19, about 3.7 ms for 1080p, 1404p, native input at 4K output), so going below 0.5 would save little.

## 6. Image quality

Reported by the user as superior to FSR 3.1 in game. No screenshots or numeric metrics saved; a same-spot comparison has not been done. The user also reports that changing Elite's AA method has no effect when Elite's own FSR 1 is on.

## 7. Independent cross-check: ED-UP v0.23.0

ED Unofficial Patch v0.23.0 (published 2026-11-22) also ships FSR 4.1 for Elite, through the same route: AMD's signed D3D12 loader and upscaler DLLs over a D3D11 to D3D12 bridge. Its notes rule out the same alternatives (no official AMD D3D11 backend, FSR 4 is D3D12-only, the driver override applies only to D3D12 titles). Details in `docs/edup-fsr4-comparison.md`. Differences worth noting: it selects a 4.x provider explicitly and refuses otherwise (we now do the same), it uses the system D3D12 runtime without the Agility SDK and pins SDK v2.3.0 (provider 4.1.1; whether that avoids our Agility dependency is untested here), and it ships AMD's DLLs unmodified with AMD's license text in its release.

## 8. Caveats

- The earlier "about 1.0 ms of EDPE common passes" estimate was a subtraction residual; measured value is about 0.19 ms.
- The earlier statements that the dispatch is "intrinsic" at 3.1 ms and that "only render scale changes it" are superseded: it is about 2.6 ms without sharpening, and output resolution dominates its cost.
- Zone profiler and Flush results come from the last blocks of logs that include menu and quit, so treat them as approximate.
- **HDR input (`HdrInput=1`) is out of scope.** One in-game test ran without API errors but the image was badly broken: rendering failures on certain HUD objects, strange colors, and persistent cursor trails on the start menu. All reported results use the default LDR input (`HdrInput=0`). FSR 4 now refuses HDR input and the backend falls back to FSR 3.1.2 while `HdrInput=1` is on (logged once, shown in the menu); covered by tests, not tried in game. The AMD documentation prefers linear HDR input, so this may leave some FSR 4 image quality unused. A plausible cause is that the output used the destination's R11G11B10 format, while ED-UP writes RGBA16F; untested.
- **Frame generation was tried once in the rebased build and failed** (`ffxCreateContext(SwapChainWrapDX12)` returned error code 2; the frame generation DLL was not in the game folder at that time, and the cause is otherwise undetermined). Whether FSR 4's startup probe, which loads the Agility D3D12 runtime into the process, interferes with frame generation was not tested; frame generation is out of scope for this work.
- First FSR 4 dispatch hitch: about 1.9 s the first time ever (136 ms context creation plus about 1.76 s dispatch), 154 ms on a warm cache; 99 to 110 ms in later sessions; context recreation on mode or size change about 99 ms. Loading screens render fast frames and are capped near 290 fps by the FSR 4 dispatch (`cpuWaits` there is the ring throttling, not a bug).
- `edpe.log` grows without limit during a session (about 1.3 MB per hour in one session, mostly upstream diagnostics) and restarts on each launch; this is upstream behavior.

## 9. Not yet measured

FSR 4 sharpness 0 against 0.50 in one session; FSR 3.1.2 at sharpness 0; FSR 4 and FSR 3.1.2 at Quality scale; the self-test (section 4a) at Modes 1, 2, 3 and 5 (needs a game restart per mode); on-foot settlement with FSR 4; PresentMon comparison of Flush=1 and Flush=0; VRAM use; image-quality screenshots; RX 9000, other GPUs, Windows 11, VR and Proton; whether SDK v2.3.0 (provider 4.1.1) works without the Agility SDK, and what its dispatch time is against 4.1.2 (parked 2026-11-23: needs the older SDK DLLs; the public FSR 4 API has no model or preset selector, the provider version is the only one).

## 10. Points for the submission

- **Dependencies and licensing.** Needs AMD's signed `amd_fidelityfx_loader_dx12.dll` and `amd_fidelityfx_upscaler_dx12.dll` (57 MB) and the Agility SDK `D3D12Core.dll`. Redistribution terms are unchecked, so keep local until they are. The Agility SDK is a new dependency and needs an upstream issue before a pull request.
- **Hardware.** FSR 4 requires an AMD 7000-series discrete GPU or newer.
- **Pull requests.** CONTRIBUTING asks for one logical change per pull request; the branch would split into Agility bridge support, the engine with tests, and the menu entries.
- **Upstream risks noted in review.** Single-author project, game-build-specific camera buffer layout, heuristic main-camera detection, modifying the game process is unsupported by the publisher (terms and anti-cheat not checked).

## 11. Detail docs and data

`docs/baseline-results-A.md` (all in-game tables), `docs/fsr4-poc-plan.md` (design, input mapping, HDR review, profiling; early sections predate the final design), `docs/EDPE-assessment.md` (architecture and risks), `docs/edup-fsr4-comparison.md`, `docs/idea-in-game-debug-mode.md`, `tools/edpe_debug/README.md` (developer test module), `docs/edpe-log-findings.md`, `docs/baseline-config-A.md`, `docs/ingame-baseline-plan.md`, `docs/baseline-capture-steps.md`, `docs/bug-report-hud-mask-realloc.md`. Config snapshots in `baseline/config-*`; self-test reports in `baseline/selftest/`. **`baseline/results.csv` is the running collection of every data point** (PresentMon rows backfilled from section 3, self-test rows imported; append only, columns described in `tools/results_add.py`). Tools in `tools/` (PresentMon and Adrenalin parsers, `compare_runs.py`, `analyze_edpe_log.py`, `deploy_fsr4_poc.ps1`, `edpe_debug/` (test module, hook and `run_test_build.ps1`), `run_fsr3_control.ps1`, `run_hdr_test.ps1`). Harness in `bridge/`.
