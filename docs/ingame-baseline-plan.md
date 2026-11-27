# In-game baseline measurement plan

Status: plan only, nothing measured in game yet. Written 2026-11-21.

## Purpose

The bridge prototype shows FSR 4.1.2 plus the D3D11 to D3D12 bridge costs about 1.65 to 1.83 ms per frame on synthetic frames. Before adding an FSR 4 backend to EDPE, we need to know three things from the real game: what the proxy itself costs, whether jitter is actually applied in the shipped build, and how GPU-bound Elite is in each scene. The frame rate estimate (roughly 85 to 100 fps from 60 fps at 1440p) only holds if the game is GPU-bound.

## Configurations to compare

| Id | Setup | What it isolates |
|---|---|---|
| A | Vanilla game, no `dxgi.dll` proxy, no mods | True baseline |
| B | EDPE installed, backend Native (`Backend=0`) | Cost of the hook layer alone (vtable and import hooks, per-draw census) |
| C | EDPE, native D3D11 TAA (`Backend=3`) | Cost of jitter plus TAA, if jitter is active |
| D | EDPE, AMD FSR 3.1.2 (`Backend=2`), Quality scale | Current best upscaling path |
| E | In-game supersampling at 1.0 (native) with FSR as AA, if available | Upper bound on image-quality cost |

ED-UP can be added as a second tool column later. Keep every other setting identical across runs, including resolution, HDR, VSync off, frame cap off, and the same graphics preset. Record the display resolution and refresh rate.

## Scenes

Use fixed, repeatable positions held for 60 seconds each, three runs per configuration and scene. Suggested scenes:

1. Deep space, stationary, looking at a planet or star (GPU-bound, simple scene).
2. Station concourse or hangar (CPU and draw-call heavy).
3. Planetary surface at a settlement on foot (Odyssey, heaviest scene).
4. Supercruise or a ship-in-motion fly-by to exercise motion vectors.

Fixed camera, no input during the capture, same time of day and system. Restart the game between configurations so shader caches and streaming state are comparable.

## Metrics

- Average frame time and fps, 1% and 0.1% lows, frame time standard deviation.
- GPU utilization and clock, CPU utilization of the render thread, VRAM in use. A GPU utilization well below 100% means the scene is CPU-bound and an upscaler will not help.
- EDPE's own GPU timing ring for the upscale pass (F7 HUD, `edpe.log`), where it reports one.

Tools: a frame-time capture tool such as PresentMon, or the Adrenalin performance overlay, or MSI Afterburner with RTSS. Choose one and use it for every run. Note that RTSS and other overlay hooks can conflict with EDPE's own hooks, so prefer a capture tool that does not inject. Any new tool that needs downloading should be approved before it is installed.

## Jitter verification (answers the status conflict)

EDPE's docs say runtime jitter is disabled, while earlier notes say it is injected. Settle it before building on it:

1. Run configuration C or D with `DEV=1` in the `[Overlay]` section of `edpe.ini` to produce `jitter.log` and `jitter_view.log` next to the executable, then inspect them for non-zero offsets.
2. Check `edpe.log` for the backend, the active mode, and whether jitter is reported as active or stood down.
3. Flip through the camera in a still scene: a jittered render with no upscaler shows a visible sub-pixel shimmer on edges, while an unjittered render is stable.
4. Optionally capture a frame with a graphics debugger and compare the projection block in the scene constant buffer across consecutive frames. EDPE logs a warning when `renderdoc.dll` is present, so check how it behaves first.

If jitter turns out to be disabled in the release, the FSR 3.1.2 path would be running without sub-pixel offsets and its image quality will be below what FSR can deliver. That would also mean the first job of an FSR 4 backend is enabling jitter correctly.

## Decision gate

- If configuration B is within about 2% of A in frame time, the hook layer is cheap and the added FSR 4 cost (about 1.7 ms) is the main new cost.
- If the scene is GPU-bound (GPU utilization near 100%), compute `0.5 to 0.6 x native + 1.7 ms` for the expected gain and compare with configuration D.
- If B costs noticeably more than A, find out which hooks cost what (per-draw census, mutexes) before adding more work on the render thread.
- If Elite is CPU-bound in the scenes that matter to the user, an upscaler gives little frame rate gain and the value of FSR 4 is image quality only.

## Reframing (user, 2026-11-21): the real problem is anti-aliasing

The game's own spatial upscaling (FSR 1.0) works well, but its anti-aliasing does not. If the goal is "good AA at native 4K", the question changes from "can we upscale" to "which temporal AA is best and cheap enough". The candidates are the same techniques the plan already measures, run at 1.0 render scale:

| Candidate | What it is | Cost known so far | Needs |
|---|---|---|---|
| In-game AA setting (FXAA, SMAA, MSAA as offered) | Spatial or multisample AA in the game | Not measured | Nothing (record the settings in `baseline-config-A.md`) |
| EDPE native D3D11 TAA (`Backend=3`) | EDPE's own temporal AA on injected jitter and motion vectors | Not measured (config C) | EDPE reinstalled |
| EDPE FSR 3.1.2, Native AA mode | FSR 3.1 at 1.0 scale as an AA pass | 9.4 ms at 4K from one cold sample in the user's `edpe.log` | EDPE reinstalled |
| FSR 4.1.2 via the D3D12 bridge, native AA | Machine-learning temporal AA at 1.0 scale | 4.1 to 4.3 ms at 4K in the harness (bridge 0.45 ms + FSR), synthetic frames | New backend in EDPE (the bridge work) |
| In-game supersampling (SSAA multiplier above 1.0) | Brute-force | Roughly proportional to pixel count; at 4K the game is already GPU-bound at about 134 fps | Settings.xml `<SSAA>` tag |

Reasoning:
- Shimmering from specular highlights, thin geometry and shader aliasing is what temporal methods fix. FXAA and SMAA mostly do not.
- The scene baseline (about 7.45 ms of GPU time at 4K native, GPU-bound) leaves room for roughly 4 ms of AA pass only if the cost fits the budget. A 4 ms pass takes about 134 fps down to about 87 fps at native resolution. The existing 9.4 ms FSR 3.1.2 sample would take it to about 60 fps. That is a strong argument for measuring EDPE's own TAA first: if it is good enough and cheap, the FSR 4 bridge becomes optional.
- The bridge and FSR 4 remain the likely quality leader among the temporal options, but that is expected, not yet shown on Elite's own frames.

Revised order: (1) finish the baseline (config A, several scenes); (2) install EDPE and measure configs B, C and D in the same scenes; (3) judge image quality against the same camera positions before deciding whether FSR 4 is worth building; (4) only then extend the bridge work.

Image quality needs its own method, because fps does not capture it: fixed camera, same scene, same time of day, screenshots or a short video per configuration, looking at distant structures, cockpit glass and terrain edges. A cheap objective check for temporal stability is to compare consecutive frames from a static camera and measure the difference (less shimmer gives smaller differences). That is not set up yet.

## What this plan does not cover

Image quality comparison (needs side-by-side captures and a fixed method), frame generation, VR, and other GPUs.
