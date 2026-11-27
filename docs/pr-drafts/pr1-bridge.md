# PR 1: bridge Agility SDK support (opened as draft: https://github.com/filispeen/EDPE/pull/31)

Head: `DoctyrEvil:fsr4-pr1-bridge` (`c57c51d`). Base: `filispeen/EDPE` `master` (`a5377c9`). Open as a draft.

**Title:** `add optional Agility SDK runtime to the D3D11-D3D12 bridge`

**Body:**

## What changed and why

`D3D11D3D12Bridge::initialize` takes an optional second argument, a directory that holds `D3D12Core.dll`. When it is set and the file loads, the bridge creates its D3D12 device through `ID3D12SDKConfiguration1::CreateDeviceFactory` and `ID3D12DeviceFactory::CreateDevice` from that Agility SDK runtime (Microsoft.Direct3D.D3D12 1.616.1). `usingAgilitySdk()` reports which runtime is in use.

Every failing step (missing interface, factory creation, device creation) is logged and falls through to the existing system `D3D12CreateDevice` path, so behavior without the file is unchanged. The default argument is `nullptr`, so existing callers (frame generation, `ngx_streamline_interop_test` and others) compile and behave as before.

Why: on Windows 10 the inbox D3D12 runtime did not offer FSR 4.1.2 through AMD's FidelityFX loader; the Agility runtime did. The FSR 4 backend (a follow-up PR, discussed in #27) needs this. The maintainer agreed in #27 that an optional `D3D12Core.dll` with fallback to the system runtime is fine, and asked for the bridge change to be split from the FSR 4 backend.

This PR adds no caller and does not stage or ship `D3D12Core.dll`.

Files: `src/d3d12_bridge.{h,cpp}` and `tests/d3d11_d3d12_bridge_test.cpp` (3 files, +67/-7).

## How it was tested

- Build: Release, Visual Studio 2022 generator.
- `ctest`: 29 of 30 pass. The failing test, `upscaler_dejitter`, also fails on unmodified `master` (`a5377c9` and `3c31694`) on this machine; see the note below.
- New test case in `d3d11_d3d12_bridge_test`: an Agility directory without `D3D12Core.dll` still initializes (system runtime fallback) and `usingAgilitySdk()` is false.
- In game, to check that the shared bridge did not regress: AMD FSR frame generation (the bridge's only runtime user without the FSR 4 backend) on upstream `master` against this branch. Same settings (FSR 3.1.2, Performance, AMD frame generation, borderless windowed). Neither `edpe.log` has an `AmdFsrFg ... failed` line or a `[EDPE][FG] Proxy present failed` line. Frame generation's success path does not log, so this shows no logged failure, not a measurement of generated frames.
- The Agility success path (device created from a real `D3D12Core.dll`) is not covered by this PR's tests. The FSR 4 follow-up PR covers it (engine test with a staged `D3D12Core.dll`), and it ran in game there.

Hardware and software: Radeon RX 7900 XTX, Adrenalin 26.11.2, Windows 10 (19045), flat mode. Not tested: NVIDIA or Intel GPUs, Windows 11, VR, Proton.

**Existing test failure:** `upscaler_dejitter` (added in the #22 commits) uses the hardware D3D11 device and falls back to WARP only if that fails. On the RX 7900 XTX the `AMD_FSR_QualitySR 128x128->192x192` case reports a step-edge span of 0.3134 against its 0.25 limit, plus thin-line flicker. With the device forced to WARP the case passes (span 0.1515). It is unrelated to this change; I can open a separate issue if useful.

## Linked issue

Part of #27.

## AI assistance

Written with Claude Code. I reviewed the diff, built it and ran the tests listed above.
