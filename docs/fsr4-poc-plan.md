# FSR 4.1 backend proof of concept: plan and status

Written 2026-11-22. Local only: EDPE is cloned to `work/EDPE` (git-ignored here), branch `fsr4-poc`, based on upstream `master` at `cb2f3b1`, one minute before the `v0.8.0-rc1` tag. No fork, no push. The aim is a working proof of function to support an upstream issue, per EDPE's CONTRIBUTING.md and PLAN.md (Phase 11 "Route B": official FidelityFX through a D3D11 to D3D12 interop layer).

## Status

| Step | State |
|---|---|
| Clone `master`, ImGui submodule | done |
| Configure (`cmake -S . -B build -A x64`); `tools/fetch_ffx_dx11.py` built the FSR 3.1.2 D3D11 port (commit `9b04fa4`) | done |
| `external/ffx-sdk` linked to our local FidelityFX SDK v2.4.0 (junction, so the FG DLLs and headers are found) | done |
| Build unmodified in Release, 26 of 26 `ctest` tests pass on the RX 7900 XTX (including bridge and AMD FG tests) | done |
| Harness check that FSR 4.1.2 accepts EDPE's 8-bit LDR color and output (`bridge_test.exe --ldr`) | done |
| Implement `AmdFsr4Backend` | not started |

## De-risking results so far

- **LDR color works.** EDPE feeds FSR an 8-bit `R8G8B8A8_UNORM` scene target (no HDR). In the harness, FSR 4.1.2 accepts 8-bit color in and an 8-bit UAV output with the create flag `FFX_UPSCALE_ENABLE_NON_LINEAR_COLORSPACE`, the dispatch flag `FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_SRGB` and the HDR flag off. A flat test color survived unchanged (77 102 128). GPU time 2.49 ms (1080p input, 3840x2160 output), bridge overhead 0.16 ms.
- **Agility SDK device** works through `ID3D12DeviceFactory` from a side-loaded `D3D12Core.dll` without any EXE export (earlier test).

## Integration points in EDPE (from the source)

- `src/temporal_inputs.h`: `enum class UpscalerBackend { None, NvidiaDlss, AmdFsr, Taa }`. Add `AmdFsr4 = 4` and a name.
- `src/upscaler_backend.h/.cpp`: `IUpscalerBackend` (initialize, evaluate(context, TemporalFrameInputs, destinationRtv, destinationTexture), resetHistory, isModeSupported). `UpscalerManager` owns one instance per backend, routes through `getBackendInstance`, `isBackendAvailable`, `getBackendUnavailableReason`, and sizes `backendTiming_` with `kBackendCount = 4` (must become 5). It already handles fail-open, sharpening (skipped for `AmdFsr`), GPU timing and diagnostics.
- `src/context_census.cpp`: builds `TemporalFrameInputs` (about line 11164), maps `cfg.upscalerBackend` to the enum (about line 13391), lists backends in the menu (about lines 9996 to 10026). `src/perf_overlay.cpp` also branches on the AMD backend. Config value: `Backend=` in `edpe.ini` (new value 4).
- `src/amd_fsr_fg_backend.cpp`: the pattern for loading `amd_fidelityfx_loader_dx12.dll` and the provider DLL beside `dxgi.dll` (`loadSiblingDll`, `ffxLoadFunctions`), creating a context with `ffxCreateBackendDX12Desc` plus a version descriptor, and querying the provider version.
- `src/d3d12_bridge.h/.cpp`: shared textures (`createSharedTexture`), a 4-slot allocator ring, `beginCommandList`, `submitAfterD3D11Signal`, `waitOnD3D11`. It creates the D3D12 device with the system `d3d12.dll` and `D3D12CreateDevice`, with no Agility SDK.
- `CMakeLists.txt` (about lines 50 to 160): the optional `external/ffx-sdk` block defines `EDPE_ENABLE_FFX_FG` and stages the loader and FG DLLs. FSR 4 needs `amd_fidelityfx_upscaler_dx12.dll` (57 MB) staged the same way.

## Input mapping (EDPE `TemporalFrameInputs` to the FSR 4 dispatch)

| Input | EDPE | FSR 4 dispatch |
|---|---|---|
| Color | `inputs.color`, 8-bit LDR (`colorIsHdr` false) in the logged case | shared copy, `R8G8B8A8_UNORM`; create flag non-linear colorspace, dispatch flag `NON_LINEAR_COLOR_SRGB`, HDR flag off. If `colorIsHdr` is true, use the HDR path. |
| Depth | `inputs.depth` = `motion_depth`, a `CopyResource` of the scene depth (`R32G8X24_TYPELESS`, view `R32_FLOAT_X8X24_TYPELESS`), reversed Z, infinite | needs a D3D12-readable copy. Option 1: shared copy in the same typeless format (the FFX D3D12 helper maps it to `R32_FLOAT`). Option 2: a small D3D11 pass writing `R32_FLOAT`. Create flags depth inverted and depth infinite; near `FLT_MAX`, far `depthB` as EDPE's FSR 3.1 path does. Option 1 is untested. |
| Motion vectors | `inputs.motionVectors`, `R16G16_FLOAT`, current to previous, render pixels, jitter excluded | shared copy; `motionVectorScale` 1.0 (times the EDPE scale), no jitter cancellation flag |
| Jitter | `inputs.jitterX/Y`, render pixels, Halton, EDPE conversion already matches the SDK recipe | `jitterOffset` unchanged |
| Reset | `inputs.reset`, history reset on source texture change | `reset` flag, plus reset when shared textures are recreated |
| Frame time | `inputs.frameTimeMs` | `frameTimeDelta` |
| Output | `destinationTexture` | shared UAV output, then `CopyResource` into the destination (same format and size) |

## Design

- New `src/amd_fsr4_backend.{h,cpp}` implementing `IUpscalerBackend`, compiled only when the FidelityFX SDK, the signed upscaler DLL and a CMake option are present; otherwise the backend reports unavailable (fail-open).
- Own `D3D11D3D12Bridge` instance (own D3D12 device, queue and fences) for the proof of concept, so frame generation is not affected. Share it with the presenter later if it proves worthwhile.
- Bridge change: an optional Agility SDK path in `initialize` through `ID3D12SDKConfiguration1::CreateDeviceFactory`, off by default. `D3D12Core.dll` staged under `stage/D3D12/`. This is a new dependency, so it needs an upstream issue before a pull request.
- Per frame: ring of N slots with shared color, depth, motion and output textures; D3D11 copies into the slot, signals; the D3D12 queue waits, dispatches FSR 4, signals; D3D11 waits on that value on the GPU and copies the output into the destination. CPU only waits when a slot's allocator is still in flight, and a timeout is checked, not ignored.
- Persistent resources, no per-frame allocation. Recreate only when size or format changes, then reset history. Log version, formats, sizes and fallback reasons.
- Add `AmdFsr4` to the enum, manager, config mapping, menu and overlay. Timing: reuse the manager's D3D11 timestamp ring around `evaluate`, and optionally D3D12 timestamps inside the backend.

## Verification plan

1. `ctest` stays green. Add a test for the backend's state transitions that does not need the game (as AGENTS.md asks).
2. In-game: `Backend=4`, mode Native AA, Performance (0.5), Quality (0.65); check `edpe.log` for provider version 4.1.2 and no fallback lines.
3. Same static planet scene and PresentMon procedure as the earlier configs (`docs/baseline-capture-steps.md`): frame time against D at the same scale, and screenshots for image quality.
4. Measure the bridge cost inside the game (CPU and GPU), as PLAN.md Phase 11 requires.

## First in-game result and follow-ups (2026-11-22)

FSR 4.1.2 runs in game through the bridge. Native 4K: 12.91 ms (77.5 fps), 3.1 ms more than the FSR 3.1.2 path; the cost matches the harness figure plus EDPE's common passes (see `baseline-results-A.md`). Follow-ups, in the order I would take them:

1. **Instrument first.** Add timing inside the engine (D3D12 timestamps around the dispatch, D3D11 timestamps around copy-in, depth convert and copy-out) so the 4.2 to 4.7 ms is split into copies, conversion, fence waits and the FSR dispatch instead of guessed.
2. **Remove the first-use hitch.** The first FSR 4 dispatch took about 1.9 s (136 ms context creation plus about 1.76 s first dispatch), and a mode or size change recreates the context (99 ms). Create the context once with `maxRenderSize` equal to the output size so mode changes do not recreate it, and warm it up with a dummy dispatch at startup.
3. **Cut copies.** The engine copies color and motion into shared textures and runs a depth conversion at 4K each frame. Create EDPE's color target and motion output as shared textures, and have the depth conversion write the shared `R32_FLOAT` directly, to save roughly 0.2 to 0.3 ms at 4K (estimate).
4. **Overlap with D3D11 work.** Today D3D11 waits for FSR in the same frame. Defer the wait and output copy to the point where the upscaled image is needed, so HUD mask replay and other D3D11 work can run while FSR executes on the D3D12 queue. The gain on a saturated GPU is unknown.
5. **Try the flush option.** `submitAfterD3D11Signal` flushes D3D11 each frame; the harness showed ±0.1 to 0.2 ms either way.
6. **Compare HDR input** (`HdrInput=1`) for quality and cost.

### Profiling build (step 1 done 2026-11-22)

The engine can now log a per-stage timing summary. Enable it with `[Upscaler] Fsr4Profile=1` in `edpe.ini` (deploy script switch `-Profile`), summary every `Fsr4ProfileInterval` frames (default 300). `Fsr4Flush=0` (script `-Flush 0`) skips the D3D11 flush after the bridge fence signal, for a direct A/B. The line in `edpe.log` looks like:

`EDPE FSR4 profile (avg/max ms, n=N): d11 copyIn+depth | d11 wait(d12+sync) | d11 copyOut | d11 total | d12 total (barriersIn, fsr, barriersOut) | sync gap | cpu evaluate | cpu beginCmdList (cpuWaits)`

What each number is and what it decides:

| Metric | Measured by | Reading it |
|---|---|---|
| `d12 fsr` | D3D12 GPU timestamps around `ffxDispatch` | The FSR 4 network itself. Expected about 3.0 to 3.8 ms at 4K output. Intrinsic: only render scale or the model can change it. |
| `d12 barriersIn/Out` | D3D12 timestamps | Resource transitions. Expected near zero. If large, merge or drop barriers. |
| `d12 total` | D3D12 timestamps, list start to end | GPU time actually spent on the D3D12 queue. |
| `d11 copyIn+depth` | D3D11 timestamps | Copy of color and motion into shared textures plus the depth conversion pass. This is the cost the "share EDPE textures directly" change would remove. |
| `d11 copyOut` | D3D11 timestamps | Copy of the shared output into the destination. |
| `d11 wait(d12+sync)` | D3D11 timestamps from signal to the end of the GPU wait | Time the D3D11 queue sits waiting for the D3D12 work. |
| `sync gap` | `d11 wait` minus `d12 total` | Pure cross-queue handoff latency (signal, wake, execute start, signal back, wake). The smoke test (small frame) showed about 1.6 ms. If this is large in game, it is the first thing to attack: defer the wait so D3D11 does useful work meanwhile, and test `Fsr4Flush=0`. |
| `cpu evaluate` | CPU clock | Render-thread cost of the whole call. |
| `cpu beginCmdList`, `cpuWaits` | CPU clock, bridge allocator wait | A non-zero wait count means the GPU is more than the ring depth (4 frames) behind, i.e. CPU stalls on the GPU. |

Decision table once game numbers exist:
- `sync gap` large: defer wait and copy-out (overlap with D3D11 HUD work); try `Fsr4Flush=0`; consider a dedicated compute-only D3D12 queue.
- `d11 copyIn+depth` plus `copyOut` large: share EDPE's textures directly, write depth conversion output straight into the shared texture.
- `d12 fsr` dominates and the rest is small: little left to gain except render scale; look at scale and HDR branch.
- `cpuWaits` frequent: deepen the ring or find why the GPU falls behind.

### First profiling results (2026-11-22, static planet scene, Performance mode, 1920x1080 to 3840x2160)

Run with `Fsr4Profile=1`, flush on, build `d20a6d5`. 113 summary lines (300 frames each); the last 39 blocks (11,699 frames, about 107 s) are the static scene and are the numbers below. Earlier blocks are boot, menus and transitions.

| Stage | Steady-state average | Share of the 9.14 ms frame |
|---|---|---|
| D3D12 FSR dispatch (`d12 fsr`) | **3.228 ms** (range 3.16 to 3.31 across blocks) | 35% |
| Cross-queue sync gap (`d11 wait` minus `d12 total`) | **0.285 ms** (0.20 to 0.38) | 3.1% |
| D3D11 copy-in plus depth conversion | **0.015 ms** (worst block max 0.038) | 0.2% |
| D3D11 copy-out | **0.033 ms** | 0.4% |
| D3D12 barriers (in plus out) | 0.003 ms | 0.03% |
| D3D12 total | 3.231 ms | |
| D3D11 total (copy-in to copy-out) | 3.565 ms | 39% |
| CPU time of `evaluate` | 0.311 ms (no block above 0.33) | |
| CPU waits for the GPU (`cpuWaits`) | 0 of 11,699 frames | |

Findings:
1. **The engine's cost is 91% the FSR network itself** (3.23 of 3.57 ms). It is intrinsic: only render scale, or an FSR 4 model option, changes it.
2. **The copies and the depth conversion are negligible** (about 0.05 ms in total). The "share EDPE's textures directly" idea is not worth doing for speed; drop it unless it simplifies the code.
3. **The only avoidable engine overhead is the sync gap, about 0.29 ms (3% of the frame).** That is the ceiling for overlap or flush experiments: overlapping the wait with D3D11 work or dropping the flush can save at most about 0.3 ms.
4. **The rest of the earlier estimated overhead is outside the engine.** The in-game frame is about 9.1 ms: game about 4.5 + FSR 4 engine 3.57 + EDPE common passes (jitter injection, motion pass, color prep, HUD handling) about 1.0. EDPE's own GPU zone profiler (`[Overlay] EnableProfiler=1`, with `ShowPerfOverlay=1`) times `DepthCopy`, `MotionPass`, `ColorPrep`, `UpscaleEval`, `UiComposite` and `HudBloom`; that is the way to split the last 1.0 ms.
5. **Stalls outside the static scene.** Single-frame D3D11 waits over 20 ms occur in 21 blocks, all during boot, menus and transitions (the worst, 266 and 605 ms, at lines 1565 to 1566 of the log; also 92, 84 and 114 ms near the start and 94 ms at line 1662). None occur in the static scene (largest single-frame wait there 7.6 ms). Blocks at lines 1570 to 1581 show the render thread blocking on the GPU in about 290 of every 300 frames (`cpu evaluate` 3.3 ms), i.e. a GPU-bound section with a full queue, not a bug in itself. Which game state those lines correspond to is not known.
6. **First-use hitch is much shorter on a warm cache.** The first FSR 4 dispatch took 154 ms this session (log: "reconstructed ... in 154.261 ms (#1)"), against 1.9 s the first time ever, so the long delay was a one-time driver compile.

Revised gain strategy, ranked by what the data supports:
- **Biggest lever: EDPE's own common passes (about 1.0 ms), outside the engine.** Measure them with EDPE's profiler first.
- **Next: render-scale choice.** FSR 4 costs 3.2 ms at 4K output from 1080p, 3.4 ms at Quality. Image quality is reported better than FSR 3.1, so the useful question is the lowest scale with acceptable quality (for example Ultra Performance).
- **Small: the 0.29 ms sync gap.** Test `Fsr4Flush=0` (one deploy, same capture) and a deferred wait. Ceiling about 3%.
- **Not worth it for speed:** shared-texture copy removal, barrier changes, ring depth.

Also requested (feasibility to evaluate later): explicit **FSR 3.1** and **FSR 4.1.2** entries in the backend dropdown so it is clear which is running. Today FSR 4 is a hidden option (`Fsr4=1`) inside the AMD FSR backend and the log says only "AMD FSR". A clean solution is a new `UpscalerBackend::AmdFsr4` enum value that reuses the engine: touch points are `temporal_inputs.h` (enum and names), `UpscalerManager` (instance, availability, `kBackendCount` 4 to 5), the config mapping in `context_census.cpp` and `config.h`, the menu in `ui.cpp`, the overlay in `perf_overlay.cpp`, and the comparison list. A cheaper interim step: show the active provider ("FSR 4.1.2" or "FSR 3.1.2 (D3D11)") in the menu status line and in the one-time log line.

## HDR versus SDR input (review requested 2026-11-22)

What the official FSR 4 documentation says (`Kits/FidelityFX/docs/techniques/super-resolution-ml.md`, SDK 2.4.0):
- The color buffer "should always be in Linear Colorspace". If that is not possible, set `FFX_UPSCALE_ENABLE_NON_LINEAR_COLORSPACE` at context creation, and optionally the dispatch flag `FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_SRGB` or `..._PQ` so FSR 4 knows the encoding. The doc says image quality "might be improved" with these, i.e. non-linear input is supported but is the fallback, not the recommended path.
- High dynamic range input is supported with `FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE`, "in linear color space" for best results.
- Placement: tonemapping, bloom, depth of field, motion blur, film grain, chromatic aberration and vignette are listed as "Post processing B", recommended to run after upscaling; SSAO, SSR and denoisers run before it. No separate TAA pass is wanted.
- Exposure: it should match what the application uses in tonemapping; with `FFX_UPSCALE_ENABLE_AUTO_EXPOSURE` the exposure texture can be omitted. FSR returns output in the same domain as the input.

What Elite actually produces (from EDPE's `docs/render-passes.md` and `src/context_census.cpp`):
- The game is not SDR internally. The deferred lighting pass writes the scene into an `R11G11B10_FLOAT` render target, which is linear HDR, before the tonemapping pass (pass 54). EDPE keeps a clean pre-HUD copy of it (`s_clean_hdr_scene_texture`). The final backbuffer is `R8G8B8A8_UNORM`, which is SDR, after tonemapping and HUD composition.
- EDPE chooses the FSR input in `tryTemporalUpscale` by the config key `[Upscaler] HdrInput`: `HdrInput=0` (the default, and what every measured config used) feeds the tonemapped LDR scene target (`R8G8B8A8_UNORM`, `hdr=0`); `HdrInput=1` feeds the linear `R11G11B10_FLOAT` scene before tonemapping (`colorIsHdr = true`).
- So the first-choice input for FSR 4 per the documentation is the `HdrInput=1` branch, and the LDR branch is the documented fallback. The engine implements both: `colorIsHdr` selects `FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE` or `NON_LINEAR_COLORSPACE` plus the sRGB dispatch flag.

**Status 2026-11-22: HDR input is out of scope for now.** The HDR test (`HdrInput=1`) ran without API errors but rendered badly (HUD object failures, strange colors, persistent cursor trails on the start menu); see `baseline-results-A.md`. The items below stay open and are not planned.

Still to verify in game, after the first LDR test: (1) that `HdrInput=1` works with FSR 4 (shared `R11G11B10_FLOAT` color and output, typed UAV store support on D3D12), (2) the image quality of the HDR branch against the LDR branch, (3) that EDPE's output in the HDR branch re-enters the pipeline before the game's tonemapper (the existing FSR 3.1 path already does this at `evalStage=1`), and (4) whether auto exposure or a real exposure value gives better results than `preExposure = 1`.

## Risks

- Typeless depth sharing between D3D11 and D3D12 may fail (fallback: R32_FLOAT pass).
- EDPE's roughly 8.3 ms CPU floor may cap any gain; FSR 4 may be break-even at scale 0.5 and slower at 0.65 (see `baseline-results-A.md`).
- Source texture changes between scenes (observed in `edpe.log`) need history resets without recreating shared resources each time.
- The upscaler DLL is 57 MB; redistribution terms are unchecked, so keep it local.

## EDPE zone profiler results (EnableProfiler=1, Performance, FSR 4, landed static scene)

Source: `EDPE PROFILE (120f avg)` lines in edpe.log, last 60 windows (7,200 frames; the tail includes the menu and quit, so treat as approximate). Frame counter reached 129,960.

| Zone (GPU, ms) | Average |
|---|---|
| depthCopy | 0.001 |
| motion | 0.075 |
| colorPrep | 0.022 |
| eval (D3D11 side: FSR 4 dispatch + sync gap + copies) | 3.479 |
| uiComposite | 0.089 |
| hudBloom | 0.000 |
| total | 3.666 |

FSR4 engine blocks, last 37: FSR dispatch 3.13 ms, sync gap 0.29 ms, d11 total 3.48 ms, cpu evaluate 0.31 ms, cpuWaits 0. EDPE `upscaleHook` CPU time 0.34 ms.

Finding: EDPE's own passes (depthCopy + motion + colorPrep + uiComposite) cost about 0.19 ms. The earlier estimate of about 1.0 ms of "EDPE common passes" was a residual from subtraction and was wrong. About 5.4 ms of the 9.14 ms frame is the game's own rendering, which EDPE cannot touch. Offloading EDPE's passes to another queue could save at most about 0.2 ms.

Remaining levers: FSR 4 dispatch (3.13 ms, only render scale changes it; 0.5 is the lowest scale in Elite's settings), sync gap (0.29 ms, `Fsr4Flush=0` or deferred wait), and overlap of FSR with next-frame scene work (needs HUD separation and adds a frame of latency, estimated at most 0.3 to 0.5 ms).

## Fsr4Flush=0 A/B (Performance, FSR 4, landed static scene, no EDPE profiler)

Source: last 39 `EDPE FSR4 profile` blocks of each run (log confirms `flush=0`). No PresentMon capture, so no frame-time number.

| Metric (ms) | Flush=1 (first run) | Flush=1 (zone-profiler run, last 37) | Flush=0 |
|---|---|---|---|
| sync gap | 0.285 | 0.294 | 0.203 |
| d11 total | 3.565 | 3.477 | 3.366 |
| FSR dispatch (d12) | 3.228 | 3.132 | 3.112 |
| cpu evaluate | 0.311 | 0.312 | 0.253 |
| cpuWaits | 0 | 0 | 0 |

Result: Flush=0 lowers the sync gap by about 0.08 to 0.09 ms (about 1% of the 9.14 ms frame) with no CPU waits and no sign of a stall. The FSR dispatch itself varies by about 0.1 ms between runs, which is the same size as the gain, so the effect is real but near the noise floor and not yet confirmed in frame time.
