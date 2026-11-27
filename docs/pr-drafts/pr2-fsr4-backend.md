# PR 2: AMD FSR 4.1.2 as a second provider in the AMD FSR backend (compact version opened as draft: https://github.com/filispeen/EDPE/pull/35)

Head: `DoctyrEvil:fsr4-pr2-on-feat` (`bd1179e`). Base: `filispeen/EDPE` `feat/FSR4` (`3584691`, which contains PR 1 / #31). Open as a draft.

**Title:** `add AMD FSR 4.1.2 as a second provider in the AMD FSR backend`

**Body:**

## What changed and why

The AMD FSR backend gets a second provider, FSR 4.1.2, next to the existing D3D11 FSR 3.1.2 path. FSR 4 runs through AMD's signed FidelityFX loader and upscaler DLLs on a D3D12 queue behind the D3D11-D3D12 bridge from #31: color and motion vectors are copied into shared textures, depth is converted to R32_FLOAT with a small compute pass, the dispatch runs on the bridge queue, and the result is copied back, synchronized with the shared fence. Resources are persistent; they are recreated only when size, format or depth flags change.

- **Provider choice.** The engine asks AMD's runtime which upscaler providers it offers and uses the highest 4.x one. If none is offered (for example a GPU without FSR 4 support) FSR 4 is unavailable and the reason is shown; it never runs a 3.x provider under the FSR 4 name.
- **Fallback.** Any FSR 4 failure falls back to the existing FSR 3.1.2 path for that frame. After 120 consecutive failures FSR 4 is disabled until it is selected again. HDR color input (`HdrInput=1`) is not supported by FSR 4 yet, so those frames use FSR 3.1.2 without counting as failures.
- **Menu and HUD.** Two entries, `AMD FSR 3.1.2 (Native AA / SR)` and `AMD FSR 4.1.2 (Native AA / SR, D3D12 bridge)`, with runtime switching. FSR 4.1.2 is greyed out with a reason when it cannot run, the way DLSS is. The active provider shows in the menu and the performance HUD.
- **Config.** `Backend=4` selects FSR 4.1.2. `Backend=2` with `Fsr4=1` also selects it. Existing configs behave as before.
- **Structure.** The pure logic (provider state, failure gate, version parsing and picking, input mapping, label text) is in the header-only `src/fsr4_logic.h` so it is unit tested on the CPU. The D3D/FidelityFX code is in `src/amd_fsr4_engine.{h,cpp}`. `AmdUpscalerBackend` owns the engine behind its existing pimpl.
- **Build.** FSR 4 is built when `external/ffx-sdk` holds `Kits/FidelityFX/upscalers` and the signed upscaler DLL (`EDPE_HAVE_FFX_FSR4`); otherwise an inert stub is built and the menu entry stays greyed out. The signed DLLs are not committed. The post-build step stages the loader and upscaler DLLs, and an optional `external/agility/D3D12Core.dll` under `D3D12/` (the optional runtime from #31).

Files: 18 files, +2229/-31. `src/amd_fsr4_engine.{h,cpp}`, `src/fsr4_logic.h`, `src/upscaler_backend.{h,cpp}`, `src/ui.cpp`, `src/perf_overlay.cpp`, `src/context_census.cpp`, `src/renderdoc_support.cpp`, `src/config.h`, `CMakeLists.txt`, `README.md`, `tools/fetch_fsr4_runtime.py`, the build action (`.github/actions/build-edpe/action.yml`), and three test files. `src/d3d12_bridge.h` has one comment reworded.

This PR does not change the per-frame behavior of the FSR 3.1.2 path, frame generation, or the CI workflows.

## How it was tested

- Build: Release, Visual Studio 2022 generator.
- `ctest`: 31 of 32 pass. The failing test, `upscaler_dejitter`, also fails on unmodified `master` on this machine (see the note in #31).
- New tests:
  - `fsr4_logic_test` (CPU only): failure gate, provider state and fallback transitions, version parsing and provider picking, refusal text, HDR fallback, context flags and dispatch plan, label text.
  - `amd_fsr4_engine_test` (GPU; skips cleanly without the signed DLLs or a GPU that offers FSR 4): flat color round trip through the bridge, provider report, resize, frame order, an R24G8 depth format, re-initialization, fail-open, HDR refused, Agility fallback and a staged `D3D12Core.dll`. 10 of 10 pass on the test machine.
  - Config round trip cases for `Backend=4` and `Fsr4` in `atomic_file_test`.
- CI on my fork passes on this commit (33 of 33), including a fresh SDK fetch through the new script. The runner has no GPU, so the engine test skips there.
- In game (Elite Dangerous, main menu scene, Performance mode, 1920x1080 to 3840x2160, windowed, frame generation off), with a developer self-test that runs each option twice in opposite order. Median frame time and the in-process GPU timer:

  | Option | Median frame (ms) | GPU timer (ms) |
  |---|---|---|
  | Bypass (game's own low-resolution render) | 2.5 | n/a |
  | Native TAA | 3.1 | 0.31 |
  | AMD FSR 3.1.2 | 3.4 | 0.61 |
  | AMD FSR 4.1.2 | 6.0 to 6.1 | 3.2 |

  So FSR 4.1.2 costs about 3.5 ms over bypass here, against about 0.9 ms for FSR 3.1.2. The two rounds agree within about 0.1 ms, and results were stable across several runs and builds. 60 provider switches between FSR 3.1.2 and FSR 4.1.2 in 600 frames left the longest frame at 6 to 17 ms across runs.
- Frame generation with FSR 4.1.2: with AMD frame generation on, FG adds about 3.2 ms to the rendered frame time (FSR 4.1.2 6.0 to 9.2 ms; FSR 3.1.2 3.4 to 6.8 ms) and starts with one frame of about 240 ms.
- No FSR errors in `edpe.log` over the test sessions: no `[ffx error]`, no evaluate failures, no fallbacks. The log shows `AMD's runtime offers FSR 4.1.2, 3.1.6, 2.3.4; using 4.1.2` and `first frame upscaled through the D3D12 bridge`.

Hardware and software: Radeon RX 7900 XTX, Adrenalin 26.11.2, Windows 10 (19045), flat mode. Not tested: other AMD GPUs (including RDNA 2 and RDNA 4), NVIDIA or Intel GPUs, Windows 11, VR, Proton, HDR output.

## Known limitations and open points

- **Presentation after frame generation with FSR 4.1.2.** After frame generation is turned off the window stays on the compositor's copy path, and with FSR 4.1.2 (about 165 fps on a 240 Hz display in my tests) about 31% of frames are never shown, which looks like judder, until the window is minimized and restored. A short window re-evaluation after the frame generation overlay is destroyed fixes it (3% of frames in the first 20 seconds, then 0%). It edits `fg_integration.cpp`, so I will send it as a separate small PR rather than add it here. FSR 3.1.2 is not affected in the same way in my captures.
- **Fetching (as agreed in #27).** The signed DLLs are fetched at build time and not committed. New `tools/fetch_fsr4_runtime.py` fetches the FidelityFX SDK v2.4.0 (shallow, sparse: `api`, `framegeneration`, `upscalers`, `signedbin`, license) into `external/ffx-sdk`, and optionally the Agility SDK `D3D12Core.dll` from the `Microsoft.Direct3D.D3D12` 1.616.1 NuGet package (checked against a pinned SHA-256) into `external/agility`. It skips what is already present, and `--dry-run`, `--ffx-only`, `--agility-only` and `--force` are available. The build action now runs it for the SDK, so CI and local builds share the same pins, with a new cache key so an older cache without the upscaler headers is not reused. CI therefore has the inputs CMake needs for FSR 4 (I have not confirmed from the CI log that it took the FSR 4 path rather than the stub). CI does not download `D3D12Core.dll`.
- **Windows 10.** On my machine the inbox D3D12 runtime did not offer FSR 4.1.2; with `D3D12Core.dll` beside `dxgi.dll` it did. Without it FSR 4 is shown as unavailable, with the reason. Not tested on Windows 11.
- **Release packaging.** `build/stage`, which feeds the test-build and release artifacts, does not stage the upscaler DLL or `D3D12Core.dll`. I left that as is until you have checked the SDK license terms, as you said in #27. Tell me if you want them staged for test builds.
- **HDR input** falls back to FSR 3.1.2; FSR 4 HDR support is not implemented.

## Linked issue

Part of #27. Builds on #31.

## AI assistance

Written with Claude Code. I reviewed the diff, built it and ran the tests listed above.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
