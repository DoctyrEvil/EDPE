# PR 2 (compact): AMD FSR 4.1.2 as a second provider in the AMD FSR backend (open: https://github.com/filispeen/EDPE/pull/35)

Head: `DoctyrEvil:fsr4-pr2-on-feat` (`bcccabc`, pushed to the fork; CI green). Base: `filispeen/EDPE` `feat/FSR4` (`3584691`, includes #31). Open PR.

Lines marked **[CHECK]** are still open before this replaces the live body.

**Title:** `add AMD FSR 4.1.2 as a second provider in the AMD FSR backend`

**Body:**

## What changed

Adds FSR 4.1.2 to the AMD FSR backend, next to the D3D11 FSR 3.1.2 path. It runs AMD's signed FidelityFX loader and upscaler on a D3D12 queue behind the #31 bridge, with persistent shared textures and a shared fence.

**Behavior**
- **Provider:** uses the highest 4.x provider AMD's runtime offers. If none is offered, FSR 4 is unavailable and the reason is shown. A 3.x provider never runs under the FSR 4 name.
- **Fallback:** a failed frame uses FSR 3.1.2. After 120 failures in a row, FSR 4 is off until it is selected again. `HdrInput=1` also uses FSR 3.1.2 (FSR 4 has no HDR color support yet) and does not count as a failure.
- **History:** the provider that takes over starts from a clean history. FSR 3.1.2 is reset when it takes over. FSR 4 is reset before its first frame after FSR 3.1.2 rendered in between.
- **Config:** `Backend=4` selects FSR 4.1.2 and `Backend=2` selects FSR 3.1.2. There is no separate `Fsr4` key.

**Lifecycle**
- FSR 4 is probed at startup on AMD (or unidentified) adapters only. NVIDIA and Intel GPUs never load the signed DLLs or create a D3D12 device for it.
- After the probe, the engine and its secondary D3D12 device are released unless FSR 4 is selected. Selecting FSR 4.1.2 initializes it again; selecting anything else releases it.

**Menu and HUD**
- Separate `AMD FSR 3.1.2` and `AMD FSR 4.1.2` entries, with runtime switching.
- FSR 4.1.2 is greyed out with a reason when it cannot run. Each reason prints once.
- The active provider and its version show in the menu and HUD from the moment the provider is picked.

**Structure and build**
- Testable logic is in header-only `src/fsr4_logic.h`. D3D and FidelityFX code is in `src/amd_fsr4_engine.{h,cpp}`, owned by `AmdUpscalerBackend` behind its pimpl.
- FSR 4 is built when `external/ffx-sdk` has the frame generation and upscaler headers and the signed upscaler DLL. Otherwise an inert stub is built.
- CMake logs `FSR 4: enabled` or `FSR 4: disabled (missing: ...)`. The new `EDPE_REQUIRE_FSR4` option (off by default) makes missing inputs a configure error. The build action sets it, so CI cannot pass on the stub.
- Signed DLLs are not committed. Post-build stages the loader, the upscaler and an optional `external/agility/D3D12Core.dll` under `D3D12/`.

17 files, +2339/-32. The FSR 3.1.2 path and frame generation are unchanged. The only workflow change: the build action runs the fetch script and sets `EDPE_REQUIRE_FSR4=ON`.

## Review feedback

Items 1 to 6 are fixed in separate commits:

| # | Fix |
|---|---|
| 1 | Provider version is set in `selectProvider()`. |
| 2 | Each menu reason line prints once. |
| 3 | History resets on every provider change. |
| 4 | AMD-only probe; engine released when FSR 4 is not selected. |
| 5 | `Backend=4` is the only selector; `Fsr4` and `selectedByConfig()` are removed. |
| 6 | CMake status message and `EDPE_REQUIRE_FSR4`. |

Item 7 (the frame generation fix) stays in its own PR, described below.

## Testing

**Current head (`bcccabc`)**
- `ctest` locally: 26 of 27 pass. I skipped the five frame generation and presenter tests (`amd_fsr_fg`, `fg_backend`, `fg_integration`, `d3d12_presenter`, `nvidia_sl_fg`), which can reset the GPU. `upscaler_dejitter` also fails on unmodified `master` here (see #31).
- Fork CI passes on `bcccabc` (Build and Test, 33 of 33 tests). The configure log shows `-DEDPE_REQUIRE_FSR4=ON` and `FSR 4: enabled (FidelityFX upscaler headers and signed upscaler DLL found)`, so CI builds FSR 4 and not the stub. The runner has no GPU, so the engine test skips there.
- In game (clean build, no debug module, `Backend=4`, Performance mode, FG off), read from `edpe.log`:
  - Startup on `Backend=4` creates the D3D12 device once, picks provider 4.1.2 and upscales the first frame through the bridge. The saved `Backend=4` is applied before the startup probe, so the probed engine is kept (an earlier commit released it and initialized it again).
  - Three cycles of FSR 4.1.2 → Bypass → FSR 3.1.2 → TAA → FSR 4.1.2 release the engine and its D3D12 device on leaving FSR 4.1.2 and initialize it again on selecting it.
  - No `[ffx error]`, no evaluate failure, no fallback, no exception, and a clean exit with no GPU reset.
  - Menu behavior and reason lines checked on screen: normal.
  - `HdrInput=1` (a config-only setting, no menu toggle): the first in-game upscale logs `FSR 4 does not support HDR color input (HdrInput=1); using FSR 3.1.2 while it is on`, FSR 3.1.2 runs, and nothing else errors. Every mode except Bypass shows visual artifacts with `HdrInput=1`, including FSR 3.1.2, because that setting sends the linear HDR scene into the temporal path. It is not specific to FSR 4, and this PR does not change it. The return to FSR 4 after `HdrInput` is turned off cannot be tried at runtime (no toggle, no reload), so it is covered by `fsr4_logic_test` only.
  - There is no automated test for the startup selection; it was checked in game.

**New tests**
- `fsr4_logic_test` (CPU): now also covers the HDR transitions, the start of a failure streak and releasing the engine.
- `amd_fsr4_engine_test` (GPU; skips without the signed DLLs or FSR 4 hardware): checks the provider version after initialize and after shutdown.
- `atomic_file_test`: config round trips for `Backend=4` and `Backend=2`.

**Performance (previous head)**

Main menu, Performance mode, 1920x1080 to 3840x2160, windowed, FG off, two rounds in opposite order:

| Option | Median frame (ms) | GPU timer (ms) |
|---|---|---|
| Bypass | 2.5 | n/a |
| Native TAA | 3.1 | 0.31 |
| AMD FSR 3.1.2 | 3.4 | 0.61 |
| AMD FSR 4.1.2 | 6.0 to 6.1 | 3.2 |

- FSR 4.1.2 costs about 3.5 ms over bypass; FSR 3.1.2 about 0.9 ms. The two rounds agree within about 0.1 ms.
- 60 provider switches in 600 frames: longest frame 6 to 17 ms across runs.
- With AMD frame generation on, FG adds about 3.2 ms and starts with one frame of about 240 ms.
- `edpe.log` over those sessions: no `[ffx error]`, no evaluate failures, no fallbacks.
- The review changes touch startup, selection and fallback only, not the per-frame path of a running provider.

**Hardware:** Radeon RX 7900 XTX, Adrenalin 26.11.2, Windows 10 (19045). Not tested: other AMD GPUs (RDNA 2, RDNA 4), NVIDIA or Intel, Windows 11, VR, Proton, HDR output.

## Known limitations and questions

- **Judder after FG is turned off with FSR 4.1.2.** At about 165 fps on a 240 Hz display, about 31% of frames are never shown until the window is minimized and restored. A short window re-evaluation after the FG overlay is destroyed fixes it (3%, then 0%). It edits `fg_integration.cpp`, so it will be a separate small PR. That PR should land before `feat/FSR4` goes to `master`.
- **Crash when switching backend while FG runs.** On my machine, switching from FSR 4.1.2 to Bypass with AMD frame generation on made the FG provider throw from `ffxDestroyContext` while EDPE rebuilt the FG context. A GPU reset happened in the same second, and the game aborted. This code is not part of this PR. A small separate fix is ready on a branch: wait for the GPU before the teardown, and turn a provider exception into FG off.
- **Fetching (as agreed in #27).** New `tools/fetch_fsr4_runtime.py` fetches the FidelityFX SDK v2.4.0 (sparse, now including `Kits/FidelityFX/upscalers`) into `external/ffx-sdk`. It can also fetch the Agility `D3D12Core.dll` from NuGet (pinned SHA-256) into `external/agility`. The build action runs it for the SDK, with a new cache key. CI does not download `D3D12Core.dll`.
- **Windows 10.** On my machine the inbox D3D12 runtime did not offer FSR 4.1.2. With `D3D12Core.dll` beside `dxgi.dll` it did. Without it, FSR 4 is shown as unavailable, with the reason. Not tested on Windows 11.
- **Release packaging.** `build/stage` does not stage the upscaler DLL or `D3D12Core.dll`. I left that until the SDK license check you mentioned in #27. Say if you want them in test builds.

Part of #27. Builds on #31.

Written with Claude Code. I reviewed the diff, built it and ran the tests above.
