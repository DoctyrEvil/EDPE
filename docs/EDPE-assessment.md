# EDPE architecture assessment and FSR 4 bridge notes

Status: working notes, written 2026-11-21. Provisional.

## Basis and limits

This assessment rests on a subagent's read of the EDPE repository (github.com/filispeen/EDPE, branch master, v0.8.0 in progress) and on our own prototype in `bridge/`. It is not a review of the code by us. Files the subagent read in full include `src/upscaler_backend.h`, `src/upscaler_backend.cpp`, `src/temporal_inputs.h`, `src/dxgi_proxy.cpp`, `src/d3d11_proxy.cpp`, `src/d3d12_bridge.h`, `STATUS.md` and several design docs. Files not read include `src/context_census.cpp` (only the first 40K of 749K characters), `src/d3d12_bridge.cpp`, `src/motion_pass.cpp`, `src/ui_layer.cpp`, `src/cobra_camera_inject.h`, `src/elite_camera.h` and most other implementation files. Documentation in the repository is partly stale, so where docs and code disagree we treat the latest release as the reference and verify by reading code before relying on any detail.

## Why the AMD driver cannot upgrade this game

The driver's FSR 3.1 to FSR 4 override requires a game that loads AMD's signed FSR 3.1 DLL through DirectX 12 (per press reports; AMD does not publish the detection logic). Elite Dangerous is a Direct3D 11 title and does not load that DLL. EDPE and ED-UP both supply their own in-process FSR 3.1 built on a community D3D11 port, so the driver has nothing to detect. FSR 4 therefore has to be added by the mod itself, not enabled through Adrenalin.

## Prototype results (bridge/)

A standalone harness on an RX 7900 XTX (render 1706x960, display 2560x1440) showed that a D3D11 to D3D12 bridge feeding AMD's signed FSR 4.1.2 DLL is cheap when the GPU is otherwise idle.

| Measure | Result |
|---|---|
| Bridge overhead (no FSR), one frame in flight | about 0.15 ms per frame |
| FSR 4.1.2 GPU time (timestamps) | 1.27 to 1.37 ms |
| Bridge plus FSR 4.1.2, 1 to 3 frames in flight | 1.60 to 1.63 ms per frame |

Pipelining gave no measurable gain because the benchmark has no other work to overlap. Earlier runs showing about 20 ms were caused by another process loading the GPU. The Agility SDK (1.616.1) was required for the runtime to offer FSR 4.1.2, and in the harness this was done by exporting `D3D12SDKVersion` from the executable. Inside a game process the code lives in a DLL, so this has to be done through `ID3D12SDKConfiguration1` and `ID3D12DeviceFactory`. That route is untested.

## EDPE structure relevant to the bridge

EDPE is a `dxgi.dll` proxy. It patches `D3D11CreateDevice` imports, patches device and immediate or deferred context vtables, and observes `Present`. Its backend interface is `IUpscalerBackend`, with `evaluate(context, TemporalFrameInputs, destinationRtv, destinationTexture)`. Existing backends are the FSR 3.1.2 D3D11 port, NVIDIA DLSS/DLAA, native D3D11 TAA and a null backend. `UpscalerManager::evaluate` is the single entry point and runs on the game's render thread. Any failure returns the original frame.

`src/d3d12_bridge.h` already provides a secondary D3D12 device on the same adapter, a high-priority queue, four command allocators, shared NT handles, and shared D3D11/D3D12 fences with GPU-side waits. It currently serves frame generation. No D3D12 upscaler dispatch exists. The natural place for FSR 4 is a new backend class beside the existing ones, using this bridge, with a return leg where D3D12 signals completion and D3D11 waits on the GPU before copying to the destination.

Input conventions reported by the subagent: reversed-Z infinite depth stored as `R32G8X24_TYPELESS` (an `R32_FLOAT` copy is needed), motion vectors as `R16G16_FLOAT` in render-resolution pixels without jitter, jitter in render pixels from a Halton sequence, scene color captured before the HUD and the HUD composited after upscaling.

## Jitter injection

Temporal upscalers such as FSR, DLSS and TAA work by combining many frames. To recover detail beyond what one low-resolution frame contains, the camera is shifted by a tiny sub-pixel amount each frame, so each frame samples the scene at a slightly different position inside every pixel. Over several frames the accumulated samples cover the pixel area much more densely than a single frame could. The shifts follow a low-discrepancy sequence, usually Halton with bases 2 and 3, repeated after a fixed number of phases. If the game renders without this shift, the upscaler has no new information to accumulate and the result looks like a blurry spatial upscale.

A game normally applies the shift in its own renderer by adding a small offset to the projection matrix. EDPE cannot edit the game's source, so it intercepts the camera constant buffer instead. According to the first subagent report (from `STATUS.md`), it hooks `Map` and `Unmap`, identifies the main camera buffer (5376 bytes) and overwrites words 1080 to 1095 with jittered values.

Correction from a second, deeper read (docs/ and `src/temporal_math.h`, `src/cobra_camera_inject.h`, `src/elite_camera.h`): words 1080 to 1095 are `cb1[270..273]` of the 5376-byte scene buffer bound at vertex-shader slot 1. They are not "CB0". In EDPE's docs, "CB0" means `cb0[4..7]`, a separate 208-byte slot-0 buffer used by one shader, which the docs describe as the transpose of `cb1[270..273]`. The 16 words form one 4x4 block of projection coefficients, and the W column (words 1083, 1087, 1091) is the multiplier. The jitter formula is `out[r*4+0] += ndcX * source[r*4+3]` and `out[r*4+1] += ndcY * source[r*4+3]`, which amounts to `clip.x += ndcX * clip.w`.

Status conflict, RESOLVED by the user's own `edpe.log` (see docs/edpe-log-findings.md): jitter is applied in the user's installed build. The log shows "projection jitter applied", "injected projection jitter into 192/208-byte CB0 transpose block", and repairs of three other jitter consumers. The docs below are stale on this point. Original note: the same docs say "Runtime jitter is disabled" and "no jitter is applied in Elite", and the code read so far is CPU math plus a diagnostic view. The earlier `STATUS.md` read says jitter is injected and FSR 3.1.2 runs in game. The files that would settle it (`context_census.cpp` call sites, the upscaler backend call paths, the latest release behavior) were not read for this question. Do not assume jitter is applied until verified in game or in code.

Jitter convention check against the FSR SDK documentation (checked 2026-11-21): the SDK says jitter offsets from `ffxQueryDescUpscaleGetJitterOffset` are in unit pixel space and shows the projection conversion as `2*jitterX/renderWidth` for X and `-2*jitterY/renderHeight` for Y, applied as a translation after projection. The same offsets must be set in the dispatch `jitterOffset` field. EDPE's `projectionJitter` in `src/temporal_math.h` uses Halton(2,3) in pixels with range [-0.5, 0.5) and the same conversion (`ndcX = 2*x/width`, `ndcY = -2*y/height`). So units and signs match the SDK recipe. Two caveats remain: EDPE's own docs call the sign at the upscaler boundary unresolved, and the SDK says jitter must be applied to all rendering at render resolution.

Several things must stay consistent with the injected shift, or the image shimmers or ghosts:

1. **Motion vectors must exclude jitter.** EDPE's motion pass writes current-to-previous displacement in render pixels, +Y down, without the jitter component. That matches the FSR contract wording and the SDK default (jitter cancellation flag off), with a motion vector scale of 1.
2. **The upscaler needs the same offset the game used.** The offset in pixels must be passed to FSR as `jitterOffset` using the same values that went into the projection. No code read so far actually passes it (see the status conflict above).
3. **Geometry drawn without the shift must be handled.** HUD draws replayed into the HUD mask are drawn with a viewport offset that cancels the jitter. The on-foot helmet visor pass is drawn unjittered by the game, so temporal unjittering makes it shimmer. A shader patch for this was proposed in the repository's notes.
4. **The phase count should match the scale ratio.** The sample config says 8 phases and the code default is 32. FSR's API can report the recommended phase count and offsets for a given render and display size, which removes the guesswork.

Coverage gaps reported in EDPE's docs: in one captured frame, 9 of 12 shaders read `cb1[270..273]`, 2 of those adjust only Z, and 3 draws use the `cb0[4..7]` path. Changing only `cb1` leaves those 3 draws unjittered. A second, separate mechanism, `applyTransientCameraBoundJitter`, offsets camera-bound values at camera+0x28C and +0x290 (or +0x228 and +0x22C) with no factor of 2, and it is unclear whether it is active in any release. A likely bug to verify: the transposed (`cb0`) jitter variant appears to use `cb1[273]` constants as W where the docs' own algebra implies `cb0[7]`. This is a reading of unrun tests.

Fragility: the injection depends on a game-build-specific buffer layout and offsets, so a game update can silently break it. Identification of the main camera relies on a heuristic (a depth-B range of 0.01 to 0.25), which could misfire on other views. Near and far planes are never passed by EDPE: the only depth parameter is `depthB` (measured 0.025), with `viewZ = depthB / rawDepth`. FSR's far-plane behavior for reversed infinite depth is listed as unverified in EDPE's contract doc.

## Strengths

- Fail-open design: failures restore the original frame.
- Non-blocking GPU timing with a ring of four timestamp queries, and readbacks that do not wait.
- Shared NT handles with shared fences and GPU-side waits instead of keyed mutexes or CPU stalls.
- A single evaluation entry point on one thread, which keeps ordering simple.
- Mip bias adjustment when rendering below native resolution, as FSR guidance recommends.

## Performance concerns

| Concern | Why it matters | Confidence |
|---|---|---|
| Hooks on `Draw`, `DrawIndexed`, `Map`, `Unmap`, `OMSetRenderTargets` and deferred-context paths | Per-draw overhead on the render thread. Not measured. | Medium |
| Several mutexes plus a recursive scene mutex around those hooks | Contention if the game submits from more than one thread. | Medium |
| HUD draws replayed into a separate mask | Roughly doubles HUD draw cost. | Medium |
| Per-frame extra passes: motion pass, custom reactive mask, full HDR copy of the previous frame, sharpen | Real GPU cost. FSR 4 needs no reactive mask and keeps its own history, so two of these are likely unnecessary on that path. | Medium |
| Constant `frameTimeDelta` of 16.6 ms | FSR should receive the real frame time. | Medium |

## Correctness and fragility concerns

- Documentation and code disagree on the HUD layer format and on the evaluation point (before the tone-mapping pass or at `Present`).
- The sampler hook adds mip bias to all linear-mip samplers, which may also affect UI or post-processing samplers. Not checked.
- IAT patching misses modules loaded after hook installation. Vtable patching and overlays or other injectors can conflict.
- `docs/shared-temporal-inputs.md` is stale (it says the FSR backend is unavailable, while code and status notes show it working).

## Project risks

- The repository appears to have a single author. Version 0.8.0-beta has an open crash report with DLSS (issue 19) and a general performance issue (issue 10).
- One 749K-character source file and inconsistent documentation make changes risky.
- Modifying the game process is unsupported by the publisher. We have not checked whether the game has anti-cheat or what its terms say.
- The frame generation presenter uses an overlay child window with its own flip-model swapchain. Latency, HDR, variable refresh rate and fullscreen behavior are unverified.

## Risks specific to adding FSR 4

1. **Mixed D3D12 runtimes.** EDPE's bridge uses the inbox runtime. FSR 4 needs the Agility SDK through the device factory. Sharing handles across two runtimes in one process is untested.
2. **Render-thread stalls.** A CPU wait on a busy allocator would block the game's render thread. The slot ring must be deep enough to avoid it.
3. **Mutex discipline.** The dispatch must run inside the existing scene mutex without deadlocking against hook paths.
4. **Licensing.** Distributing AMD's FSR DLLs is governed by AMD's license, which we have not checked.

## Suggested order

1. Extend `bridge/` to create the D3D12 device through `ID3D12DeviceFactory` and confirm FSR 4.1.2 is still offered.
2. Align the harness with EDPE's input conventions (reversed-Z infinite depth, pixel-space motion vectors, jitter units and sign), using FSR's jitter query.
3. Read `src/d3d12_bridge.cpp`, the latest release's evaluation call site and the per-frame paths in `context_census.cpp`.
4. Measure EDPE's baseline in-game frame time with and without the proxy before building anything.
5. Only then add an FSR 4 backend, either in a fork or as a patch to the upstream repository after opening an issue.
