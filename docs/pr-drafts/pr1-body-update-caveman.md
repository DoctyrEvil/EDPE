## What and why

Extend existing D3D11-D3D12 bridge (`bef3669`) with optional Agility SDK runtime.

- `initialize(device, agilitySdkDir = nullptr)`: if dir holds `D3D12Core.dll`, read SDK version from its `D3D12SDKVersion` export (616 if unreadable), create device via `CreateDeviceFactory` + `ID3D12DeviceFactory::CreateDevice`.
- Any failing step (path conversion, interface, factory, device) logs and falls back to system `D3D12CreateDevice`. No file, no change.
- `usingAgilitySdk()` true when device came from the factory. Factory also succeeds when OS runtime already has that version, so success log names the factory and prints requested and runtime-reported version.
- `nullptr` default: existing callers (frame generation, `ngx_streamline_interop_test`, others) unchanged. Only `initialize()` and `shutdown()` touched.
- `shutdown()` does not call `FreeUnusedSDKs()`: `D3D12Core.dll` stays loaded. With the call, next `initialize()` in same process crashes in `CreateDevice` (see comment in `shutdown()`).

Why: on Windows 10 inbox D3D12 did not offer FSR 4.1.2 through AMD's FidelityFX loader; Agility runtime did. FSR 4 backend (follow-up PR, #27) needs it. Maintainer OK'd optional `D3D12Core.dll` with system fallback and the split in #27.

No caller added. `D3D12Core.dll` not staged or shipped.

Files: `src/d3d12_bridge.{h,cpp}`, `tests/d3d11_d3d12_bridge_test.cpp` (3 files, +124/-7).

## Tested

RX 7900 XTX, Adrenalin 26.11.2, Windows 10 19045, Release, VS 2022.

- `ctest`: 29 of 30. `upscaler_dejitter` fails on unmodified `master` (`a5377c9`, `3c31694`) too. See below.
- `d3d11_d3d12_bridge_test`: Agility dir without `D3D12Core.dll` initializes via system fallback, shuts down, initializes again. `usingAgilitySdk()` false after default `initialize`, after `shutdown()`, after fallback `shutdown()`. Not asserted after fallback `initialize()`: system runtime with that version can satisfy `CreateDeviceFactory` without the file (GitHub runner).
- In game, AMD FSR frame generation, `master` vs this branch at `c57c51d` (before review changes): FSR 3.1.2, Performance, borderless windowed. No `AmdFsrFg ... failed` or `[EDPE][FG] Proxy present failed` in either `edpe.log`. Success path does not log, so this shows no logged failure only. FG performance not measured. `1f27bf3` not re-run in game.
- Agility success path (real `D3D12Core.dll`) not covered here. FSR 4 follow-up covers it (engine test with staged DLL, plus in game). Passes there with review changes, logs `requested SDK version 616, runtime reports 616`.

Not tested: NVIDIA, Intel, Windows 11, VR, Proton.

**Existing failure, `upscaler_dejitter`** (#22): uses hardware D3D11 device, WARP only if that fails. On RX 7900 XTX `AMD_FSR_QualitySR 128x128->192x192` step-edge span 0.3134 vs 0.25 limit, plus thin-line flicker. Forced WARP passes (0.1515). Unrelated to this change. Can open separate issue.

## Linked issue

Part of #27.

## AI assistance

Written with Claude Code. Diff reviewed, built, tests run by me.
