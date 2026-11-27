# Feature request draft: AMD FSR 4.1.2 upscaler option

Condensed from `docs/SUMMARY.md` for the repository's feature request form (title, problem, proposed solution, area, alternatives). Paste each section into the matching field.

**Title:** `[Feature]: AMD FSR 4.1.2 option through a D3D11 to D3D12 bridge (PLAN.md Phase 11, Route B)`

**Area:** Upscaler backend (DLSS, FSR, XeSS, TAA)

## Problem

EDPE's AMD backend runs FSR 3.1.2 through the D3D11 port. AMD's newer ML upscaler, FSR 4, is D3D12-only and ships as signed DLLs, and the driver's FSR 3.1 to FSR 4 override works only for D3D12 titles, so Elite Dangerous (D3D11) cannot get FSR 4 from the driver. Radeon RX 7000 and 9000 owners have no way to use it. PLAN.md already lists this as Route B ("official current FidelityFX API" through D3D11/D3D12 interop).

## Proposed solution

Add FSR 4.1.2 as a second provider inside the existing AMD FSR backend, using the bridge in `d3d12_bridge`:

- **Menu:** the Upscaler Backend list gets "AMD FSR 3.1.2" and "AMD FSR 4.1.2" entries. The active provider shows in the menu and the performance HUD. FSR 4.1.2 is greyed out with a reason when it cannot run, as DLSS is.
- **Config:** `Backend=4` selects FSR 4.1.2 (`Backend=2` stays FSR 3.1.2). Optional keys: `Fsr4=1` (start on FSR 4), `Fsr4Profile=1` (log per-stage timings), `Fsr4Flush`.
- **How it works:** color, depth and motion are copied into shared textures; a D3D12 queue dispatches FSR 4 through AMD's signed loader and upscaler DLLs; D3D11 waits on a shared fence and copies the result back. The engine picks the highest 4.x provider AMD's runtime offers and refuses otherwise, so it never runs a 3.x provider under the FSR 4 name. Any failure falls back to the existing FSR 3.1.2 path.
- **Optional dependency:** the Agility SDK `D3D12Core.dll` (1.616.1) beside the proxy. On the test machine (Windows 10) the inbox D3D12 runtime did not offer FSR 4.1.2 without it; the bridge falls back to the system runtime when it is absent.
- **Tests:** a CPU-only logic test, an engine test that skips cleanly without FSR 4, and a config round trip; 28 of 28 `ctest` targets pass.

A working prototype exists (local branch on upstream master `30ee13a`, 7 commits). I would split it into pull requests per CONTRIBUTING (Agility bridge support, engine and tests, menu entries) once the dependency questions below are settled.

## Evidence (RX 7900 XTX, Windows 10, Adrenalin 26.11.2, 3840x2160, planet landed, static; single runs unless noted)

| Config | Frame ms | fps | GPU busy |
|---|---|---|---|
| Vanilla, native, no AA (3 runs) | 7.39 | 135.4 | 100% |
| EDPE FSR 3.1.2, native AA | 9.79 | 102.2 | 100% |
| EDPE FSR 4.1.2, native AA (sharpness 0.50) | 12.91 | 77.5 | 99% |
| EDPE FSR 3.1.2, scale 0.5 (sharpness 0.50) | 8.79 | 113.7 | 77% (CPU-limited) |
| EDPE FSR 4.1.2, scale 0.5 (sharpness 0.50) | 9.14 | 109.4 | 99% |
| EDPE FSR 4.1.2, scale 0.5, sharpness 0 | 8.47 to 8.53 | 117.3 to 118.0 | 98% |

- **Cost breakdown at scale 0.5:** the FSR 4 dispatch is 2.5 to 3.25 ms depending on sharpening; the bridge itself (copies, depth conversion, barriers) is about 0.05 ms, the cross-queue sync gap about 0.3 ms, EDPE's own passes about 0.19 ms. AMD's published figure for this GPU is 3.07 ms with sharpening off; ours is 2.5 to 2.6 ms with it off. The sharpening pass costs about 0.65 ms in one session (inferred).
- **Stability:** no failures, device-lost messages or CPU waits in the profiled runs; frame time stdev 0.59 ms at scale 0.5.
- **Image quality:** reported better than FSR 3.1.2 by the tester; no screenshots or metrics yet.
- **Not comparable yet:** FSR 4 against FSR 3.1.2 at equal sharpness 0 (FSR 3.1.2 was only measured at 0.50).

## Scope and limits

- Radeon RX 7000 or 9000 series only; tested on one RX 7900 XTX, flat mode. Not tested: RX 9000, Windows 11, VR, Proton, NVIDIA or Intel GPUs (the unavailable-menu path is covered by CPU tests only).
- HDR input (`HdrInput=1`) is not supported by FSR 4: a test showed corrupted HUD parts, wrong colors and cursor trails. FSR 4 falls back to FSR 3.1.2 while it is on.
- Frame generation was not tested with this build; one attempt failed to start and its cause is undetermined (FSR 4's startup probe loads the Agility runtime into the process, which has not been ruled out as a factor).
- AMD's signed DLLs are used unmodified; ED Unofficial Patch v0.23.0 independently shipped FSR 4.1 the same way (D3D11 to D3D12 bridge, AMD's signed DLLs, AMD's license text included).

## Alternatives

- **Driver FSR 3.1 to 4 override:** applies only to D3D12 titles that load AMD's signed FSR 3.1 DLL, so it cannot apply to Elite.
- **Community D3D11 FSR 3.1 port only (current state):** no FSR 4 for Radeon users.
- **Another D3D11 FSR 4 implementation:** none exists; AMD's FSR 4 is D3D12-only and has no official D3D11 backend.

## Questions for the maintainer

1. Is a new optional dependency on the Agility SDK `D3D12Core.dll` acceptable, or should the feature use the system D3D12 runtime only (SDK v2.3.0, provider 4.1.1, may not need it; untested here)?
2. Redistribution: AMD's SDK license is ambiguous about shipping the signed DLLs. Should they be fetched by a script at build time (as the FSR 3.1 port is) and stay out of releases?
3. Preferred split into pull requests, and whether the explicit FSR 3.1.2 / FSR 4.1.2 menu labels are acceptable.

## Checks

I searched existing issues for "FSR 4", "FSR4", "Agility" and "D3D12 upscaler": no open issue covers this (results were unrelated FSR 3.1 bugs). PLAN.md Phase 11 and the "Route B" section describe the same architecture.
