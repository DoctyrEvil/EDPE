# Bridge cleanup: split the Agility SDK device creation out of initialize (draft, not opened)

Head: `DoctyrEvil:fsr4-bridge-cleanup` (`8008f06`). Base: `filispeen/EDPE` `feat/FSR4` (`3584691`, includes #31). Independent of the FSR 4 backend PR: the two merge cleanly in either order. Open as a draft.

**Title:** `split the Agility SDK device creation out of the bridge initialize`

**Body:**

## What changed and why

Follow-up cleanup of code I added in #31. No behavior change.

- **`createAgilitySdkDevice()`.** `D3D11D3D12Bridge::initialize` nested the Agility SDK path four levels deep inside an already long function. The path is now a private member, `createAgilitySdkDevice(adapter, agilitySdkDir)`, with early returns. It logs the reason and returns false on any failure, leaving `device12_` and `deviceFactory_` empty, so `initialize` falls back to the system `D3D12CreateDevice` exactly as before. The steps, their order, and every log message are unchanged, including the success log with the requested and runtime-reported SDK version.
- **`INITGUID` removed.** `d3d12_bridge.cpp` defined `INITGUID` so that test targets that do not link `dxguid` still resolved `CLSID_D3D12SDKConfiguration`. That is a workaround in a production source file. The define is gone, and `dxguid` is linked in the test targets that compile `d3d12_bridge.cpp`: `d3d11_d3d12_bridge_test`, `d3d12_presenter_test`, `fg_backend_test`, `amd_fsr_fg_test`, `nvidia_sl_fg_test`, `fg_integration_test` and `ngx_streamline_interop_test`. `EDPE_DXGI` already linked it.

Files: `src/d3d12_bridge.{h,cpp}` and `CMakeLists.txt` (3 files, +56/-53).

## How it was tested

- Release build; `ctest` 29 of 30 on this branch. The failing test, `upscaler_dejitter`, also fails on unmodified `master` here (see #31). The bridge, presenter and frame generation tests pass, including the Agility fallback and re-initialize cases in `d3d11_d3d12_bridge_test`.
- CI on my fork passes (build and tests). CI builds `ngx_streamline_interop_test`, which I could not build locally because it needs the NGX and Streamline SDKs, so the CI run is what checks its new link line.
- The Agility success path (device created from a real `D3D12Core.dll`) is covered by the engine test in the FSR 4 backend PR, which stages a `D3D12Core.dll`. I ran that test locally on a merge of this branch and the FSR 4 branch and it passes. I have not run this branch alone in game, and the in-game FSR 4 testing so far predates this refactor.

Hardware and software: Radeon RX 7900 XTX, Adrenalin 26.11.2, Windows 10 (19045), flat mode. Not tested: NVIDIA or Intel GPUs, Windows 11, VR, Proton.

## Linked issue

Follow-up to #31, part of #27.

## AI assistance

Written with Claude Code. I reviewed the diff, built it and ran the tests listed above.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
