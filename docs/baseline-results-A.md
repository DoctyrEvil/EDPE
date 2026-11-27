# Baseline results, configuration A (vanilla game, no proxy)

Measured 2026-11-21 with AMD's bundled PresentMon (per-frame CSVs in `baseline/presentmon/`), parsed with `tools/parse_presentmon.py`. Setup in `baseline-config-A.md`: RX 7900 XTX, Adrenalin 26.11.2, Ryzen 7 5800X3D, 3840x2160 exclusive fullscreen, VSync off, no frame cap. Each run is about 59 seconds after a one-second skip. Scene names come from the file names.

## Scene 1: planet landed, static camera (3 runs)

| Run | Avg fps | 1% low | 0.1% low | Frame time mean (ms) | Frame time stdev (ms) |
|---|---|---|---|---|---|
| 1 | 133.9 | 114.9 | 103.5 | 7.47 | 0.44 |
| 2 | 135.6 | 115.0 | 96.7 | 7.37 | 0.45 |
| 3 | 136.7 | 116.1 | 83.8 | 7.32 | 0.53 |
| Mean | 135.4 | 115.3 | | 7.39 | |

- Run-to-run spread of average fps: 2.0%. Of 1% low: 1.1%. The 0.1% low is noisy (rests on about 8 frames; worst frame grew from 14 to 29 ms across runs).
- Average fps rose slightly in each successive run. Cause not determined (warm-up, temperature, or state).
- GPU time 7.4 ms of a 7.5 ms frame, GPU wait 0, GPU latency 17.8 ms (about two frames queued): GPU-bound.

## Scene 2: settlement, on foot (3 runs, moving, more variable)

| Run | Avg fps | 1% low | 0.1% low | Frame time mean (ms) | Frame time stdev (ms) | Worst frame (ms) |
|---|---|---|---|---|---|---|
| 1 | 101.3 | 71.7 | 67.3 | 9.87 | 2.45 | 16.7 |
| 2 | 94.6 | 68.6 | 60.4 | 10.57 | 2.62 | 20.8 |
| 3 | 98.5 | 70.2 | 59.8 | 10.15 | 2.47 | 21.2 |
| Mean | 98.2 | 70.1 | 62.5 | 10.20 | | |

- Run-to-run spread: average fps 6.8%, 1% low 4.4%, 0.1% low 12.0%. Frame time stdev is about 2.5 ms, against 0.44 ms in the static scene.
- Within a single run the ten-second average fps moves between about 75 and 128 fps. The camera path was not identical between runs, so part of the variation is the scene, not the system.
- GPU time 9.6 to 10.3 ms of a 9.9 to 10.6 ms frame (97 to 98% busy), GPU wait about 0.1 ms. GPU latency is about one frame (9 to 11 ms), against about 2.4 frames in scene 1: less work queued ahead, so the CPU is closer to the limit here than in scene 1. Overall still GPU-heavy.

## What this means for the AA decision (assumptions marked)

Costs from the bridge harness at 3840x2160 on synthetic frames: FSR 4.1.2 through the bridge about 4.2 ms at native resolution (AA only) and about 3.7 ms with Quality scaling (2560x1440 render). The existing FSR 3.1.2 path measured 9.4 ms once, cold, in the user's EDPE log.

| Scene | Baseline | Native-resolution FSR 4 AA (assumed baseline + about 4.2 ms) | Quality scaling (assumes GPU time scales with pixels, 44%, plus about 3.7 ms) |
|---|---|---|---|
| Planet landed | 7.4 ms, 135 fps | about 11.6 ms, about 86 fps | about 7.0 ms, about 143 fps |
| Settlement on foot | 10.2 ms, 98 fps | about 14.4 ms, about 69 fps | about 8.2 ms, about 122 fps |

These are estimates, not measurements. Real GPU time rarely scales exactly with pixel count (geometry, shadows and compute do not), so the Quality-scaling figures are probably optimistic.

Noise to keep in mind: differences under about 3% (static) or about 7% (on foot) between configurations cannot be separated from run-to-run variation without more runs or a repeatable camera path.

## Config E: planet landed static at 2x supersampling (3 runs, final)

Three runs confirm the single-run picture. `baseline/config-E/options_Graphics_Custom.4.4.fxcfg` was copied after the runs and records `SSAAMultiplier` 2.000000, `AAMode` 0, `UpscalingQuality` 0, checkerboard false, so the 2x setting is now on file.

| Run | Avg fps | 1% low | 0.1% low | Frame time mean (ms) | Frame time stdev (ms) | Worst frame (ms) |
|---|---|---|---|---|---|---|
| E_1 | 46.7 | 42.5 | 22.0 | 21.40 | 2.14 | 67.5 |
| E_2 | 46.8 | 43.2 | 42.0 | 21.39 | 0.66 | 24.5 |
| E_3 | 46.9 | 43.7 | 42.0 | 21.31 | 0.75 | 39.8 |
| Mean | 46.8 | 43.1 | | 21.37 | | |

- Run-to-run spread of average fps is only **0.4%** (A was 2.0%). 1% low spread 2.8%.
- E_1 had a burst of hitches (13 frames over 30 ms, worst 67.5 ms), likely the first run after the setting change (shader or streaming warm-up; cause not determined). E_2 and E_3 are clean.
- With three runs the cost model is unchanged: **about 2.72 ms fixed + 4.66 ms resolution-dependent** at 4K native (63% of the config A frame). Assumes 2x means 2x per axis.
- Updated estimates from this model: FSR 4 at Quality scaling plus its cost about 8.5 ms (118 fps); at Performance scaling (1920x1080 render, about 3.5 ms for FSR 4 assumed) about 7.4 ms (135 fps, equal to A); native-resolution FSR 4 as AA about 11.6 ms (86 fps). The FSR 4 costs come from the synthetic bridge harness.

Details of the first E run, as first read:

`Planet_Landed_Static_E_1.csv`, same scene and display settings as scene 1. The user reports 2x supersampling for this run and confirms it was the only setting changed from A (AA stayed off, nothing else changed). The preset file was rewritten afterward showing 1.0, so the 2x value is the user's report, not a file record.

| Metric | A (3-run mean) | E_1 (2x supersample) | Ratio |
|---|---|---|---|
| Avg fps | 135.4 | 46.7 | 0.34 |
| Frame time mean | 7.39 ms | 21.40 ms | 2.90x |
| 1% low fps | 115.3 | 42.5 | |
| 0.1% low fps | about 94 | 22.0 (worst frame 67.5 ms) | |
| GPU busy / GPU wait | 100% / 0 | 100% / 0 | |
| GPU latency | 17.8 ms | 58.9 ms (about 2.8 frames queued) | |

Reading it (assumption: "2x" means 2x per axis, so 4x the pixels; this is the only reading consistent with the numbers, since 2x the pixel count would imply a negative fixed cost):
- A simple model `time = fixed + pixel part` gives about **2.7 ms fixed** and **4.7 ms pixel-dependent** per frame at 4K native in this scene. Only about 63% of the frame time scales with resolution.
- **Revised estimate for Quality-scaling upscaling** (render at 2560x1440, 44% of the pixels): about 2.7 + 2.05 = 4.8 ms of game time. Adding about 3.7 ms for FSR 4.1.2 through the bridge gives about 8.5 ms (118 fps), which is **slower than config A's 7.4 ms (135 fps)**. This replaces the earlier estimate of about 143 fps, which assumed the whole frame scaled with pixels.
- Native-resolution FSR 4 as AA only: about 7.4 + 4.2 = 11.6 ms (86 fps), unchanged.
- So in this scene, upscaling with FSR 4 does not buy frame rate; the value would be image quality (AA) at a frame rate cost. The single run and the model are rough; the 4x-pixels assumption is unverified.

## Configs B, C and D (EDPE) on the planet-landed static scene, one run each, no upscaling

EDPE installed (`dxgi.dll` 9,680,896 bytes, modified 14:33:16; SHA256 recorded in `baseline/config-D/environment.txt`). Render and output are both 3840x2160 in all three. In-game settings unchanged from A: `AAMode` 0, SSAA 1.0, checkerboard off. Order in the log (`edpe.log`, frame counters): B was the launch state (backend none), then TAA (switched at frame 102752), then None, then AMD FSR in Native AA mode (frame 121112). Backends were switched in the EDPE menu within one game session. `edpe.ini` at the time of the snapshot has `Backend=2`, `Sharpness=0.50`, `Jitter PhaseCount=8`, `Jitter Enabled=1`; B and C settings were not snapshotted separately.

| Config | What it is | Avg fps | Frame time (ms) | Delta vs A (ms) | 1% low | 0.1% low | Stdev (ms) |
|---|---|---|---|---|---|---|---|
| A | vanilla game (3-run mean) | 135.4 | 7.38 | | 115.3 | 94.2 | 0.47 |
| B | EDPE installed, backend none (hooks only) | 133.0 | 7.52 | +0.13 | 109.4 | 92.2 | 0.55 |
| C | EDPE native TAA, native AA mode | 118.2 | 8.46 | +1.08 | 100.2 | 83.0 | 0.53 |
| D | EDPE FSR 3.1.2 (D3D11 port), Native AA mode | 102.2 | 9.79 | +2.40 | 87.2 | 63.1 | 0.57 |

- **Hook layer is cheap:** B costs +0.13 ms (1.8%), within the 2% run-to-run noise measured for A. Its lower 1% low is driven by a single 31.7 ms frame in a 59 s run.
- **Native TAA (C) costs about 1.1 ms** (-13% fps). EDPE's own timer reports the TAA pass alone as 0.28 ms on its first call, so most of the cost is elsewhere (injected jitter, motion pass, extra copies).
- **FSR 3.1.2 at native 4K (D) costs about 2.4 ms** (-24% fps) in steady state. The single 9.4 ms figure in the user's earlier `edpe.log` was the cold first dispatch and was not representative.
- All three are single runs, but the deltas for C and D (1.1 and 2.4 ms) are well beyond the 2% (about 0.15 ms) run-to-run noise measured for A. B is not distinguishable from A.
- Consequence for FSR 4: the harness cost of FSR 4.1.2 through the bridge at 4K native is about 4.2 ms, so it is roughly **1.8 ms more expensive than the existing FSR 3.1.2 path**, not cheaper.

## EDPE FSR 3.1.2 with upscaling, planet landed static, one run per scale

Captures `Planet_Landed_Static_D_Quality_1.csv` (scale 0.65, 2496x1404) and `Planet_Landed_Static_D_Performance_1.csv` (scale 0.5, 1920x1080), output 3840x2160. The render sizes are confirmed by `edpe.log` lines ("temporal color input created 2496x1404" and "1920x1080"). After the Quality run the user also briefly tried 0.75 (2880x1620) and returned to 0.65 (the log's last state is 2496x1404), so the Quality capture is assumed to be at 0.65. `Custom.4.4.fxcfg` now holds `SSAAMultiplier` 0.65.

`tools/compare_runs.py` output (all files, first 60 frames skipped):

| label | frames | frame ms | fps | GPU busy ms | GPU busy % | GPU wait ms | GPU latency ms | limit |
|---|---|---|---|---|---|---|---|---|
| A vanilla | 24180 | 7.38 | 135.4 | 7.36 | 100% | 0.002 | 17.4 | GPU |
| B EDPE, no backend | 7913 | 7.52 | 133.0 | 7.33 | 98% | 0.167 | 5.7 | GPU |
| C EDPE TAA native | 7025 | 8.46 | 118.2 | 8.43 | 100% | 0.002 | 19.2 | GPU |
| D EDPE FSR native AA | 6067 | 9.79 | 102.2 | 9.76 | 100% | 0.000 | 23.6 | GPU |
| D at 0.65 (1404p) | 7115 | 8.36 | 119.7 | 6.54 | 78% | 1.450 | 4.3 | CPU/other |
| D at 0.5 (1080p) | 7160 | 8.31 | 120.4 | 5.73 | 69% | 1.547 | 4.2 | CPU/other |
| E vanilla, 2x supersample | 8230 | 21.37 | 46.8 | 21.33 | 100% | 0.002 | 44.7 | GPU |

Findings:
1. **Upscaling hits a floor at about 8.3 ms (about 120 fps), and that floor is not the GPU.** Scales 0.65 and 0.5 give the same frame time (8.36 and 8.31 ms) although the GPU works 6.5 and 5.7 ms; the GPU idles 22 to 31% of each frame and the GPU queue empties (latency about 4 ms against 17 to 24 ms in the GPU-bound runs). Two different GPU loads with identical frame times point to a fixed per-frame CPU-side cost of about 8.3 ms with EDPE in FSR mode. It is **slower than vanilla A (7.38 ms, no AA)** because vanilla was GPU-bound at 7.4 ms and EDPE adds CPU work on the render thread (hooks, injected jitter, motion pass, HUD handling; which of these dominates is not measured). The user's earlier belief that the game is not CPU-bound holds for vanilla at 4K, but not for EDPE in this mode.
2. **The cost model holds on the GPU side.** Predicted game GPU time is 2.72 + 4.66 x (pixel share): 4.69 ms at 0.65 and 3.89 ms at 0.5. Measured GPU busy is 6.54 and 5.73 ms, i.e. a constant EDPE plus FSR GPU overhead of about **1.85 ms** at both scales (2.4 ms at native 4K, where the game's own pixels are also counted differently). Consistent across two points, from single runs.
3. **Upscaling versus native FSR AA:** 120 fps against 102 fps (+18%), at the price of lower render resolution. Versus vanilla (no AA) it is about 11% slower.
4. **What this means for an FSR 4 backend:** the GPU overhead of FSR 4.1.2 through the bridge is about 3.7 ms at Quality against about 1.85 ms for EDPE's FSR 3.1.2. In this scene that would put the GPU time at about 4.7 + 3.7 = 8.4 ms, right at the CPU floor, so roughly 8.5 to 9 ms (about 115 fps) against 120 fps today. In GPU-bound scenes (the settlement scene, higher resolution) FSR 3.1.2 is about 1.85 ms cheaper per frame. FSR 4 is therefore a quality play, not a performance play, and EDPE's CPU floor would cap any gain.
5. **Single runs** (one per scale). The floor evidence is strong (identical frame times, GPU busy well below frame time, empty queue), the exact numbers are not.

Suggested next measurements to locate the CPU floor: vanilla (no EDPE) at SSAA 0.5 to find the game's own CPU limit, and EDPE with no backend (B) at the same low scale. If vanilla at 0.5 also stops near 8 ms, the floor belongs to the game; if it goes much lower, EDPE's render-thread work is the cause.

## Vanilla at scale 0.5 (locating the CPU floor), one run

`Planet_Landed_Static_A_Scale050_1.csv`: no EDPE (`dxgi.dll` and `edpe.ini` absent), `SSAAMultiplier` 0.5 (1920x1080 render, presented at 3840x2160), AA off, nothing else changed. Snapshot in `baseline/config-A_Scale050/`.

| label | frame ms | fps | GPU busy ms | GPU busy % | GPU wait ms | GPU latency ms | limit |
|---|---|---|---|---|---|---|---|
| A vanilla, native | 7.38 | 135.4 | 7.36 | 100% | 0.002 | 17.4 | GPU |
| **A vanilla, scale 0.5** | **6.95** | **143.9** | **4.51** | **65%** | 0.858 | 3.8 | CPU/other |
| D EDPE FSR 3.1.2, scale 0.5 | 8.31 | 120.4 | 5.73 | 69% | 1.547 | 4.2 | CPU/other |
| D EDPE FSR 3.1.2, scale 0.65 | 8.36 | 119.7 | 6.54 | 78% | 1.450 | 4.3 | CPU/other |

- **The game itself has a CPU floor of about 6.9 ms (about 144 fps).** At scale 0.5 the GPU works only 4.5 ms of a 6.95 ms frame and the queue is empty. Median 6.82 ms. One 157 ms hitch in the run (frame 5627) raises the mean slightly; excluding it the mean is 6.93 ms (144.3 fps).
- **EDPE in FSR mode raises that floor to about 8.3 ms**, i.e. EDPE adds roughly **1.4 ms of CPU-side time per frame** (about 20%) in this scene, and costs about 16% of fps at the same render scale (120.4 against 143.9).
- The earlier belief that the game is not CPU-bound holds at native 4K (GPU 7.4 ms against a CPU floor of about 6.9 ms), but at 0.5 to 0.65 scale it is CPU-bound even without EDPE.
- Refit of the game's GPU cost with three points (0.25, 1.0 and 4.0 of the 4K pixel count: 4.51, 7.36, 21.33 ms): the curve is slightly sublinear at low resolution (slope about 3.8 ms per 4K of pixels between 0.25 and 1.0, 4.7 between 1.0 and 4.0). The earlier two-point model (2.7 ms fixed) slightly underestimated the 0.5 case (3.9 predicted against 4.5 measured).
- EDPE plus FSR 3.1.2 GPU overhead, recomputed with measured game GPU time: about 1.2 ms at scale 0.5 (5.73 - 4.51) and about 1.4 ms at 0.65 (6.54 against about 5.2 estimated).

**Implication for FSR 4 (estimate, not measured):** at scale 0.5 the GPU has about 2.6 ms of idle time per frame under EDPE's 8.3 ms floor. Replacing the 1.2 to 1.4 ms of FSR 3.1.2 and EDPE passes with FSR 4.1.2 at roughly 3.5 ms (not measured at 1080p input; 3.7 ms was measured at 1440p input) would raise GPU time to about 8 to 8.5 ms, about equal to the CPU floor. So in a CPU-limited scene FSR 4 might cost almost no frame rate, while in GPU-bound scenes it would cost about 2 ms more than FSR 3.1.2. The real limiter is then EDPE's roughly 1.4 ms of render-thread work.

## Bridge harness at the upscaled input sizes (game closed, GPU idle, driver 26.11.2)

Synthetic frames, FSR 4.1.2 through the D3D11 to D3D12 bridge, output 3840x2160. `bridge_test.exe` timestamps and `pipelined_test.exe` frame times:

| Render input | Scale | FSR 4.1.2 GPU time | Bridge only | Bridge + FSR 4 (1 slot) | (2 to 3 slots) |
|---|---|---|---|---|---|
| 1920x1080 | 0.5 | 2.96 ms | 0.22 ms | 3.48 ms | 3.56 to 3.80 ms |
| 2496x1404 | 0.65 | 3.19 ms | 0.33 ms | 3.91 ms | 3.90 ms |
| 3840x2160 (native AA, earlier) | 1.0 | about 3.7 to 3.8 ms (3.7 at 1440p input) | 0.45 ms | 4.17 to 4.26 ms | |

Estimate for FSR 4 in EDPE's place (assumes about 0.8 to 1.0 ms of the 1.2 to 1.4 ms EDPE-plus-FSR 3.1.2 GPU overhead is the FSR 3.1.2 pass itself and about 0.3 ms is EDPE's other passes; these two shares are assumptions, not measured):

| Scale | Now (EDPE FSR 3.1.2) | GPU time with FSR 4 (game + 0.3 + bridge+FSR 4) | CPU floor | Likely frame time and fps |
|---|---|---|---|---|
| 0.5 | 8.31 ms, 120 fps (CPU-bound, GPU 5.7 ms) | about 4.5 + 0.3 + 3.5 = 8.3 ms | about 8.3 ms | about 8.3 to 8.6 ms, **about 116 to 120 fps (about break-even)** |
| 0.65 | 8.36 ms, 120 fps (CPU-bound, GPU 6.5 ms) | about 5.2 + 0.3 + 3.9 = 9.4 ms | about 8.3 ms | about 9.4 ms, **about 106 fps (about 11% slower)** |

Reading: at scale 0.5 the CPU floor hides FSR 4's extra GPU cost, so frame rate would stay about the same as today. At 0.65 the GPU becomes the limit again. Image quality is not measured: if FSR 4 at 0.5 looks as good as FSR 3.1.2 at 0.65, it would deliver the same fps (about 120) from a lower render cost, which is the case for FSR 4; that remains to be shown by comparing screenshots.

## First in-game FSR 4.1.2 capture: native 4K (config F4 Native), one run

`Planet_Landed_Static_F4_Native_1.csv`; local EDPE build `80d4ffd` (upstream master `cb2f3b1` plus the FSR 4 engine), `[Upscaler] Fsr4=1`, FSR Native AA (render = output = 3840x2160), `HdrInput=0` (8-bit LDR input), driver 26.11.2. Snapshot: `baseline/config-F4_Native/`. `edpe.log` confirms `context ready provider=4.1.2 render=3840x2160 output=3840x2160 colorFmt=28 hdr=0 agility=1` and no fallback lines, so FSR 4 ran for the whole capture.

| label | frame ms | fps | 1% low | GPU busy % | GPU latency ms |
|---|---|---|---|---|---|
| A vanilla, native | 7.38 | 135.4 | 115.3 | 100% | 17.4 |
| C EDPE TAA, native | 8.46 | 118.2 | 100.2 | 100% | 19.2 |
| D EDPE FSR 3.1.2, native AA | 9.79 | 102.2 | 87.2 | 100% | 23.6 |
| **F4 EDPE FSR 4.1.2, native AA** | **12.91** | **77.5** | **68.0** | **99%** | **32.9** |

- Stable: frame time stdev 0.71 ms, 10-second windows all 77.4 to 77.5 fps, no frame over 25 ms. GPU-bound.
- FSR 4 costs **3.1 ms more than FSR 3.1.2** at native 4K (12.91 against 9.79 ms), and 5.5 ms more than vanilla. fps loss against the FSR 3.1 path: 24%.
- The cost adds up from known pieces, so no hidden in-game bug is indicated: vanilla 7.4 + EDPE hooks and common passes about 0.8 to 1.1 (C minus A) + FSR 4 through the bridge about 4.2 to 4.7 (harness at 3840x2160: 4.17 to 4.26 ms) is 12.4 to 13.2 ms against 12.91 measured. Roughly 0.5 ms more than the harness figure at most.
- `edpe.log` line: "AMD FSR reconstructed 1920x1080 -> 3840x2160 in 1899.482 ms (#1)". The first FSR 4 dispatch in the session took about 1.9 s (context creation about 136 ms plus about 1.76 s for the first dispatch). A one-time hitch at first use, and the context is recreated (99 ms) when the mode or size changes.
- Native AA is the worst case for FSR 4; the case for it is lower render scales. Performance and Quality captures, plus an FSR 3.1 control from the same build (`-Fsr4 0`), are still to do.

## FSR 4.1.2 at Performance scale (config F4 Performance), one run

`Planet_Landed_Static_F4_Performance_1.csv`: render 1920x1080, output 3840x2160, `Fsr4=1`, `HdrInput=0`, local build `80d4ffd`; `SSAAMultiplier` 0.5 in the preset file, AA mode 0. Snapshot `baseline/config-F4_Performance/`. `edpe.log`: `context ready provider=4.1.2 render=1920x1080 output=3840x2160 ... (99 ms)`, no fallback lines.

| label | frame ms | fps | 1% low | GPU busy ms | GPU busy % | GPU latency ms | limit |
|---|---|---|---|---|---|---|---|
| A vanilla, native (no AA) | 7.38 | 135.4 | 115.3 | 7.36 | 100% | 17.4 | GPU |
| A vanilla, scale 0.5 | 6.95 | 143.9 | 118.6 | 4.51 | 65% | 3.8 | CPU |
| D EDPE FSR 3.1.2, scale 0.5 (older build) | 8.31 | 120.4 | 100.2 | 5.73 | 69% | 4.2 | CPU/other |
| D EDPE FSR 3.1.2, native AA | 9.79 | 102.2 | 87.2 | 9.76 | 100% | 23.6 | GPU |
| **F4 EDPE FSR 4.1.2, scale 0.5** | **9.14** | **109.4** | **94.6** | **9.02** | **99%** | 12.4 | GPU |
| F4 EDPE FSR 4.1.2, native AA | 12.91 | 77.5 | 68.0 | 12.77 | 99% | 32.9 | GPU |

- Stable: frame time stdev 0.59 ms, ten-second windows all 109.0 to 110.0 fps, no frame over 20 ms.
- FSR 4 at scale 0.5 is **GPU-bound at 9.0 ms**. FSR 3.1.2 at the same scale was held at 8.31 ms by the CPU (its GPU time was only 5.7 ms), so FSR 4 adds about 3.3 ms of GPU time but only 0.8 ms of frame time. Against that run FSR 4 is 9% lower in fps (109.4 against 120.4); against vanilla at the same scale it is 24% lower; against vanilla native with no AA it is 19% lower (109.4 against 135.4).
- The GPU time adds up: game at scale 0.5 about 4.5 ms + EDPE common passes about 0.8 to 1.1 + FSR 4 through the bridge about 3.5 (harness: 2.96 ms FSR plus 0.22 bridge at this size) is 8.8 to 9.1 ms against 9.02 measured. No hidden overhead in game.
- Ordering by fps on this scene: vanilla no AA 135, FSR 3.1 at 0.5 (older build) 120, FSR 3.1 at 0.65 (older build) 120, TAA native 118, **FSR 4 at 0.5: 109**, FSR 3.1 native AA 102, FSR 4 native AA 77.5. FSR 4 at scale 0.5 is faster than FSR 3.1 native AA and slower than FSR 3.1 at 0.65, so its worth depends on whether its image at 0.5 beats those two; that needs a same-spot screenshot comparison.
- The FSR 3.1 numbers are from the older build that had the HUD mask reallocation. A control from the current build (`-Fsr4 0`, same mode) is still needed before the FSR 4 versus FSR 3.1 gap can be stated firmly.

## HUD mask reallocation: fixed upstream before these builds

The mask reallocation seen in the earlier `edpe.log` (4,797 and 5,525 allocations in Performance and Quality) does not occur in the newer source. The `edpe.log` of the new build holds exactly one "allocated HUD mask target" line across a session that included Performance and Native AA modes. Upstream commit `c92b249` (2026-11-19, "prevent flare stabilization from resizing HUD mask to render resolution") fixes it, and it is contained in `v0.8.0-beta`, `exp-build-50` and later, `v0.8.0-rc1` included. The older build used for configs B, C, D and the scale runs evidently predates it. Consequence: the 8.3 ms CPU floor measured with EDPE FSR at scales 0.5 and 0.65 may have been partly caused by that reallocation; repeat those two runs with the current build (control `-Fsr4 0`) before drawing conclusions about EDPE's CPU floor.

## Notes

- Files named `...csv.csv`: the capture script appends `.csv` to the name, so pass `-Name` without the extension next time.
- No Adrenalin logs were taken for these runs.

## FSR 3.1.2 control on the current build, Performance scale (config F3 Performance), one run

`Planet_Landed_Static_F3_Performance_2.csv`: same local build as the FSR 4 runs, `Fsr4=0` (FSR 3.1.2 D3D11 path), Mode Performance (render 1920x1080, output 3840x2160), `Fsr4Flush=1`. Snapshot `baseline/config-F3_Performance/`. `edpe.log` has zero "FSR4: context ready" lines, one "AMD FSR 3.1 D3D11 backend initialized", "upscaler mode changed to Performance", and one "allocated HUD mask target" line (no reallocation). Run 1 was started too early (38,393 frames at 640.9 fps, a menu or loading capture) and is scrapped; its file is renamed `...F3_Performance_1.SCRAPPED_early.csv`.

| label | frame ms | fps | GPU busy ms | GPU busy % | GPU wait ms | CPU busy ms | GPU latency ms | limit |
|---|---|---|---|---|---|---|---|---|
| A vanilla, scale 0.5 | 6.95 | 143.9 | 4.51 | 65% | 0.858 | 6.89 | 3.8 | CPU/other |
| D FSR 3.1.2, scale 0.5 (older build) | 8.31 | 120.4 | 5.73 | 69% | 1.547 | 8.24 | 4.2 | CPU/other |
| **F3 FSR 3.1.2, scale 0.5 (current build)** | **8.79** | **113.7** | **6.81** | 77% | 1.489 | 8.72 | 4.5 | CPU/other |
| **F4 FSR 4.1.2, scale 0.5** | **9.14** | **109.4** | **9.02** | 99% | 0.121 | 9.07 | 12.4 | GPU |

- FSR 4 costs **0.35 ms of frame time** (4%, 113.7 to 109.4 fps) against FSR 3.1.2 on the same build, but **2.21 ms more GPU time** (9.02 against 6.81). FSR 3.1.2 is held by the CPU (GPU 77% busy), so FSR 4's extra GPU work mostly fills idle GPU time. In a GPU-bound case (native AA: 12.91 against 9.79 ms) the full 3.1 ms shows.
- Single runs; the 4% difference is only about twice the 2% run-to-run spread measured for vanilla, so treat it as approximate.
- The current build is **slower** for FSR 3.1.2 than the older build at the same scale: 8.79 against 8.31 ms frame, GPU busy 6.81 against 5.73 ms. FSR 3.1.2 GPU time above vanilla at scale 0.5 is now 2.30 ms (6.81 - 4.51) against 1.22 ms. Cause not determined; the builds differ by upstream changes up to `cb2f3b1` and by our FSR 4 code (not initialized when `Fsr4=0`), and each is one run.

## FSR 4.1.2 with HDR input (config F4 Performance HDR), one run, visually broken

`Planet_Landed_Static_F4_Performance_HDR_1.csv`: same build as the menu build, Performance (1920x1080 to 3840x2160), `Fsr4=1`, `HdrInput=1`, flush on. Snapshot `baseline/config-F4_Performance_HDR/`. The user reports the image was extremely buggy: rendering failures on certain HUD objects, strange colors, and persistent trails left by the cursor on the start menu. The timing below is therefore not a usable result.

**Decision (2026-11-22): HDR input is out of scope for the proof of concept.** All measurements and the submission use `HdrInput=0` (LDR). The cause is not investigated. Candidates, untested: EDPE's own `HdrInput=1` path (off by default, never part of our measurements, which could affect any upscaler), exposure mismatch (`preExposure=1` with FSR auto exposure against the game's tonemapper), and `R11G11B10_FLOAT` precision or missing alpha. A cheap first test if it is ever reopened: with `HdrInput=1`, toggle FSR 3.1.2 and FSR 4.1.2 in the menu in the same spot to see whether the defect is in EDPE's HDR path or in our engine.

`edpe.log`: `context ready provider=4.1.2 render=1920x1080 output=3840x2160 colorFmt=26 hdr=1`, "first frame upscaled", path lines `fmt=26 hdr=1 eval=1 comp=1 stage=1` (colorFmt 26 is `R11G11B10_FLOAT`), no `evaluate failed`, no fallback, no "unavailable". So the HDR path ran without any API error and the defect is in the image content, not in initialization.

| label | frame ms | fps | GPU busy ms | GPU busy % | GPU latency ms |
|---|---|---|---|---|---|
| F4 FSR 4.1.2, LDR input | 9.14 | 109.4 | 9.02 | 99% | 12.4 |
| F4 FSR 4.1.2, HDR input (broken image) | 8.91 | 112.2 | 8.77 | 98% | 20.9 |

Engine profile, last 39 blocks: d11 total 3.436 ms, FSR dispatch 3.016 ms, copy-in plus depth 0.016 ms, copy-out 0.034 ms, sync gap 0.366 ms. The HDR run has a 0.23 ms shorter frame and a 0.2 ms shorter dispatch than the LDR run, within run-to-run variation of a single run. Log counts of "zero jitter writes" (1001) and "reconstruction skipped" (227) are similar in kind to the FSR 3.1 control log (710 and 168), so they are not specific to HDR.

## Sharpening is a large part of the FSR 4 dispatch cost (2026-11-22, inferred, one session)

Every FSR 4 and FSR 3.1 measurement before this point ran with `Sharpness=0.50` (the deploy script's default), so the upscaler's sharpening pass (RCAS inside the dispatch, enabled whenever sharpness is above 0) was on. In the provider-selection session the user turned the sharpening slider to 0 (the saved `edpe.ini` ends at `Sharpness=0.00`) and reported a clear performance gain. The engine log agrees:

| Stretch of the same session | Blocks | FSR dispatch average |
|---|---|---|
| Before the slider change, sharpness 0.50 | 99 | 3.25 ms (blocks range 3.0 to 3.5) |
| After the slider change and the provider switches, sharpness 0 | 66 | 2.60 ms |

The capture taken afterwards (`Planet_Landed_Static_F4_Performance_2.csv`) shows 8.53 ms (117.3 fps) with 8.38 ms of GPU time, against 9.14 ms (109.4 fps) and 9.02 ms for the earlier FSR 4 run: 0.61 ms of frame time and 0.64 ms of GPU time, the same size as the dispatch change. So the sharpening pass costs about 0.65 ms at Performance scale (1920x1080 to 3840x2160), about 20% of the FSR 4 dispatch and 7% of the frame. The time of the slider change is inferred from the log order and the user's report; the log records no sharpness changes, so a clean A/B in one session is still needed.

Consequences:
- The statement that the FSR 4 dispatch is about 3.1 ms and "intrinsic" held only with sharpening on; without it the dispatch is about 2.6 ms.
- The FSR 3.1 control ran at `Sharpness=0.50` as well, so the FSR 4 versus FSR 3.1 comparison is not like for like: the new FSR 4 run (sharpness 0) is 3% faster than the FSR 3.1 control (sharpness 0.50), but FSR 3.1 at sharpness 0 has not been measured, and FSR 4 sharpness 0.50 is the earlier 9.14 ms run.
- Image-quality judgments made so far were at sharpness 0.50. FSR 4 may need less or no sharpening; that has not been compared.
- The deploy script now takes `-Sharpness` (default 0.50, unchanged behavior) so a run can set it explicitly.
