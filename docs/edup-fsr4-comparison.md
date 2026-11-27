# ED-UP's FSR 4 implementation compared with ours

Written 2026-11-22. Source: ED Unofficial Patch release v0.23.0 (github.com/characterecho-sean/ed-unofficial-patch, published 2026-11-22), read through the GitHub API: the release notes, `src/d3d11/fsr4_engine.{h,cpp}`, `src/d3d11/fsr4_version.h`, `tools/fetch_fsr4.py`, `tools/fsr4_engine_test/` and `docs/fsr-upscaler-design-2026-09-16.md` (route 2 entries from 2026-10-23 onward). Their code was read, not built or run. Nothing was copied; any reuse would need its license checked first.

## Summary

ED-UP reaches FSR 4.1 the same way we do: AMD's signed `amd_fidelityfx_loader_dx12.dll` and `amd_fidelityfx_upscaler_dx12.dll` through the ffx-api, over a D3D11 to D3D12 bridge (a D3D12 device on the game's adapter, shared NT-handle textures, a shared fence, an allocator ring). They did not find a different route. Their own design notes list the same dead ends we found: no official AMD D3D11 backend, FSR 4 is D3D12-only, and the driver's FSR 3.1 to 4 override applies only to D3D12 ffx-api titles. They also name OptiScaler's "FSR4 w/Dx12" as the precedent for this bridge. Their release ships it (`fix.temporal_aa = fsr4`, flat and VR, Radeon RX 7000 or 9000 only), so our approach is independently confirmed as the practical one.

## What matches ours

| Topic | ED-UP | Ours |
|---|---|---|
| Bridge | D3D12 device by adapter, shared textures, shared fence, ring of command allocators | same |
| Input copies | Inputs copied into shared textures of exactly the render size, because FSR 4's auto exposure reads the whole texture (an OptiScaler finding) | same, shared textures are the render size |
| Color | LDR display-referred 8-bit declared non-linear sRGB (create flag plus dispatch flag); HDR route uses the HDR flag | same |
| Exposure | Auto exposure on both routes | same, `preExposure = 1` |
| Depth and camera | Reversed Z, `cameraNear = FLT_MAX` for an infinite projection | same values |
| D3D12 device | Feature level 12_0 (FSR 4 needs shader model 6.6) | same |
| Pure-logic header | `fsr4_version.h` holds the decisions (version pick, formats, flags, camera planes, frame time) so a test can pin them with no GPU | `fsr4_logic.h`, same idea |
| Sizing | One context per size, remade on size, format or mode change | same |
| Measured cost | GPU FSR 4 cost scales with pixel count (1.7 ms at 2560x720 to 5120x1440 on their Radeon 8060S) | consistent with our 3.1 ms at 4K output |

## What they do that we do not

1. **Explicit provider selection (their decision D7).** They query the versions AMD's runtime offers (`ffxQueryDescGetVersions`), pick the highest 4.x, and chain `ffxOverrideVersion` to select it. If the runtime offers no 4.x provider (RDNA 2, RDNA 3.5 without the upgrade path, every non-AMD GPU) FSR 4 refuses, naming what was offered, and never runs FSR 3.1.5 in FSR 4's name. Ours takes the default provider and only reports the version afterward, so on a non-capable GPU an "FSR 4.1.2" menu choice could silently run FSR 3.1.5 under that label. This is a real gap and cheap to close.
2. **No Agility SDK.** Their code creates the D3D12 device with the system `D3D12CreateDevice` and has no reference to the Agility SDK, and they pin SDK v2.3.0 (upscaler provider 4.1.1). We needed Agility SDK 1.616.1 for the runtime to offer provider 4.1.2 from SDK v2.4.0 on this Windows 10 machine. Untested: whether the 4.1.1 provider is offered by the inbox runtime here. If it is, pinning SDK v2.3.0 would remove the new-dependency risk we flagged as needing an upstream issue.
3. **Pinned, verified fetch.** `fetch_fsr4.py` downloads the headers and the two DLLs from AMD's official FidelityFX-SDK repository at one pinned commit and checks every file against a pinned SHA-256 before installing. EDPE already has a fetch tool for the FSR 3.1 port; the same pattern would fit.
4. **Provider and size handling:** a failure latch that remembers one failed context key and does not retry until the key or mode changes, three strikes before a failed dispatch is latched, and a bounded GPU wait with a log line. Our 120-failure gate plays a similar role.
5. **Frame-time clamp.** `frameTimeDelta` is clamped to 0.1 to 100 ms when real and a nominal value when unknown, so a hitch or debugger pause does not reach AMD's history weighting. Ours passes the frame time through unclamped.
6. **Zero-copy direct views.** For steady-state frames they hand the caller D3D11 views directly over the shared textures, so the game or their passes write into the bridge's textures. They need this at VR resolutions. Our profiling found the copies cost about 0.05 ms at 4K, so it is not worth it for us.
7. **Keyed mutexes and Proton support.** They use `SHARED_KEYEDMUTEX` and keep the keys held, because DXVK's `ReleaseSync` waits on the CPU for the GPU and was costing 5 to 7 ms of render-thread time under Proton (measured, then fixed by holding the keys). Our bridge uses a shared fence and no keyed mutex, so this stall does not apply, but Proton behavior is untested for us.
8. **Release packaging.** They decided (2026-11-21) to ship AMD's loader and upscaler DLLs unmodified, with AMD's license text, in their release. The DLLs keep AMD's own signature; a build that lacks them is refused. Their notes call the SDK license ambiguous (a binary-only redistribution grant, then a file list under MIT) and record that the maintainer decided to ship anyway. This is a precedent, not an answer: our redistribution question is still open.

## What they report that is worth knowing

- **HDR route outputs RGBA16F.** On their flat HDR route the input is R11G11B10_FLOAT scene radiance and the output texture is R16G16B16A16_FLOAT, and their output-format table (`fsr4TypedFormat`) deliberately has no R11G11B10 entry: output is written through a typed UAV, "so only the formats every D3D12 GPU stores to" (RGBA8, R10G10B10A2, RGBA16F). Our engine used the destination's own format for the output, which in the HDR test was R11G11B10_FLOAT. That is a plausible cause of our strange colors, though it would not obviously explain the HUD failures or the cursor trails. HDR stays out of scope for now; if it is reopened, try RGBA16F output with a conversion into the destination first.
- **Jitter phases.** AMD suggests 8 x (output / render)^2 phases (32 at 2x); EDPE's configured count is 8 (our deploy script writes `PhaseCount=8`). They tested 32 phases at 2x for a flicker and it did not fix it: the flicker got faster, and they traced it to their own refusal of stale motion history, not to FSR 4. So more phases is not a known lever.
- **Motion vectors are the bigger quality lever.** Their engine carries per-object and world-camera motion and a refusal census; ours uses camera and depth only. Moving objects and thin geometry are where that shows, and it is independent of which FSR provider runs.
- **Context cost.** They report context creation of 130 ms the first time and 2.5 ms for a rebuild; ours took 99 to 138 ms every time it was recreated. Possibly a different cache behavior, not investigated.

## Suggested actions for our work

1. Add provider selection (item 1 above): enumerate versions, override to the highest 4.x, refuse with the offered list otherwise. Small and testable in `fsr4_logic.h` (pick, parse, refusal text). Also clamp the frame time (item 5).
2. Test whether SDK v2.3.0 (provider 4.1.1) runs without the Agility SDK on this machine. If yes, it removes the Agility dependency for the submission. This needs a download of AMD's SDK v2.3.0 DLLs (their pinned commit `60f4ea81909200d8542eca14dccb2628b763a9a3`); ask before downloading.
3. Note ED-UP in the issue as independent confirmation of the approach, and as precedent for shipping AMD's signed DLLs with AMD's license.
4. Leave HDR, zero-copy views and keyed mutexes for later.
