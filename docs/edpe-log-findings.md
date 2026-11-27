# Findings from the user's edpe.log

Source: `Elite Dangerous\Products\elite-dangerous-odyssey-64\edpe.log` (12,232 lines, one game session, process 13836). Read 2026-11-21 in portions: lines 1 to 120, lines 2118 to 2157, pattern searches, and a count of message kinds. The version of EDPE that produced it is not stated in the log. The file ends with the swap chain being destroyed, so the game had closed.

## Setup the log reveals

- GPU: AMD Radeon RX 7900 XTX, D3D11 feature level 0xC000 (12_0).
- Display and render size: 3840x2160 (4K). `SSAAMultiplier=1.000000` was synced from the game's config.
- NVIDIA path: `NVSDK_NGX_D3D11_Init failed: 0xBAD00001`, expected on an AMD card. The AMD backend initialised ("AMD FSR 3.1 D3D11 backend initialized successfully").
- The backend was switched to AMD FSR in mode Native AA during the session (render = output = 3840x2160), at frame 163225.

## Resolved: jitter is applied

After the switch, the log shows "projection jitter applied #1 ... pixel=(0.2500, -0.3889) ndc=(0.000130208, 0.000360082)", then "injected projection jitter into 192/208-byte CB0 transpose block", and three repairs: the cascade shadow mask CB2 InvProj jitter phase, the clustered lighting compute CB0 UvToView jitter phase, and the deferred lighting/shadow CB2 camera block. This contradicts the repository docs that say runtime jitter is disabled, which are stale, as suspected. It also shows the jitter reaches several consumers beyond the main projection block, not just one buffer.

The logged ndc values equal `2*pixel/3840` and `-2*pixel/2160` (0.25 gives 0.000130208, -0.3889 gives 0.000360082 after the Y sign flip), matching the FSR SDK recipe.

## Useful numbers

- "AMD FSR reconstructed 3840x2160 -> 3840x2160 in 9.411 ms (#1)". This is the only timing line in the log. It is the first dispatch, so it may include warm-up, and the log gives no steady-state figure. Native AA at 4K is also the most expensive mode.
- Our bridge harness at the same size (3840x2160 to 3840x2160, FSR 4.1.2, idle GPU) measures about 4.1 to 4.2 ms per frame including about 0.44 ms of bridge overhead. At 4K Quality scaling (2560x1440 to 3840x2160) it measures about 3.8 to 4.1 ms, with 0.32 to 0.33 ms of bridge overhead. If the 9.4 ms figure is representative, FSR 4.1.2 through the bridge could be faster than the existing D3D11 FSR 3.1 port at 4K. One cold sample is not enough to claim that.
- Resources seen: depth textures use format 19 (`R32G8X24_TYPELESS`) with view format 20, and also format 44 (`R24G8_TYPELESS`) with view 45. The scene pass uses 8 render targets of formats 24 and 28 at 4K. This confirms the depth-copy requirement noted earlier: FSR needs an `R32_FLOAT` copy, and a second depth format exists to handle.

**Correction (later measurement):** the 9.4 ms figure was the first, cold dispatch. PresentMon frame times with EDPE in FSR Native AA mode at 4K show a steady-state cost of about 2.4 ms over the vanilla frame (9.79 vs 7.38 ms). So FSR 4.1.2 through the bridge (about 4.2 ms in the harness) would be about 1.8 ms more expensive than the existing FSR 3.1.2 path, not faster. See `baseline-results-A.md`.

## Things the log flags for an FSR 4 backend

1. **The FSR color input is LDR.** The log says "temporal color input created 3840x2160 format=28 (offscreen LDR scene target (no UI))" and the path lines show `hdr=0`. FSR works best on linear HDR before tone mapping. For an LDR sRGB-encoded input, the FSR 4 dispatch needs the non-linear color flag (`FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_SRGB`) and the HDR creation flag off. This is a quality-relevant detail and needs checking.
2. **The upscaler input changes between contexts.** "temporal path change" lines show the source texture alternating between two textures and formats (27 and 28) with stage 1 and stage 2 as the game moves between scenes (about 20 such transitions in this session). A backend must handle changing input textures and reset history on those changes without recreating shared resources each time.
3. **Camera tracking sometimes falls back.** Many lines read "commitMainCamera fallback" with `parseFail` and `filterFail` counts, and streaks up to 640 frames at the end of the session. "reconstruction skipped (no scene MRT pass this frame)" appears when there is no scene. These are probably menus or loading, but we cannot tell from the log. They show the main-camera detection is heuristic and can lose the scene camera.
4. **HUD is composited after upscaling.** Several HUD pass lines (cockpit holographic HUD shader pairs, HUD mask target allocated at 3840x2160 R16G16B16A16_FLOAT) confirm the pre-HUD upscale design.

## Not in the log

- No frame time, GPU utilization, VRAM use or fps, so this log cannot replace the baseline plan.
- No per-frame upscale timing after the first. EDPE's F7 HUD likely shows more, but that is not logged here.
- No EDPE version string or settings dump.

## Later log (15:13 session, EDPE installed, FSR/TAA/None modes)

Analysed with `tools/analyze_edpe_log.py` (segments the log at each "upscaler backend/mode changed" line). 15,995 lines, no errors, no warnings, no device-removed messages; NGX init failed once (expected on AMD).

**Finding: the HUD mask target is reallocated repeatedly in the upscaling modes.**

| Segment | HUD mask allocations | Sizes |
|---|---|---|
| TAA / Native AA | 1 | 3840x2160 |
| AMD FSR / Native AA | 1 | 1920x1080 (carried over from an earlier switch) |
| AMD FSR / Performance (0.5) | 4,797 | 3840x2160 x 2,398 and 1920x1080 x 2,398, alternating |
| AMD FSR / Quality (0.65) | 5,525 | 3840x2160 x 2,762 and 2496x1404 x 2,762, alternating |

- The allocation lines come in long unbroken runs of alternating sizes (runs of 2 to 22 lines and longer between other log lines).
- Source cause (subagent read of `src/context_census.cpp` and `src/ui_layer.cpp`, partly summarized): `ensureHudMaskResources(device, width, height)` caches a **single** size and returns early only if the requested size equals the stored size. Native AA and TAA only ever request one size, so the cache holds. With upscaling, two callers apparently request the output size and the render size alternately, and each call destroys and recreates the mask, its previous-frame copy, tonemapped copy, far linear depth, a depth-stencil texture, and the three UI layer textures. The subagent computed about 40 bytes per pixel per reallocation: about 332 MB of texture creation at 3840x2160 and about 83 MB at 1920x1080. The two call sites were not found, so how often this runs per frame is unconfirmed.
- No timestamps in the log, so the allocation rate per frame cannot be derived from it (the frame counters in the segment spans are mixed and unreliable).

**Possible link to the CPU floor (hypothesis, unproven):** the floor at about 8.3 ms identical at scales 0.5 and 0.65 would fit a fixed per-frame cost that depends on the output size, such as recreating 4K textures. Counter-evidence: the whole 0.5-scale segment has only 2,398 allocation pairs while one 60-second capture is about 7,100 frames, so the thrash is not once per frame for the whole capture, and the capture frame times show no large spikes (worst frame 12.6 ms). A decisive test: count new "allocated HUD mask target" lines in `edpe.log` during one capture. About 2 per frame (about 14,000) would explain the floor; a handful would not.

**Camera tracking:** many "commitMainCamera fallback" streaks (up to 16 frames) with high `parseFail` counts around frames 179872 and 225004, probably menus or camera changes, as before.

## Effect on the plan

- Target resolution is 4K, not the 1440p used in the earlier estimates. Re-estimate at 4K: FSR 4.1.2 costs roughly 3 to 4 ms there on this GPU, so the benefit depends strongly on how GPU-bound Elite is at 4K, which is likely more than at 1440p.
- Jitter is already in place, so the first job of an FSR 4 backend is not enabling jitter but feeding FSR correctly: the same jitter offset, the LDR color flag, pixel-space motion vectors and the `R32_FLOAT` depth copy.
