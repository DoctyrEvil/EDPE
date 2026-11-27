# Reply to the PR 1 review (draft, not posted)

Thanks for the review. Changes are in `1f27bf3` (not pushed yet).

1. **Success log.** Fixed. It now reads `D3D12 device created through ID3D12DeviceFactory (requested SDK version 616, runtime reports 616)`. The reported value comes from `ID3D12DeviceConfiguration::GetDesc().SDKVersion` (0 if that interface is unavailable). Seen in `edpe.log` on Windows 10; not tested on Windows 11.

2. **Hardcoded 616.** Fixed. The bridge loads the staged `D3D12Core.dll`, reads its `D3D12SDKVersion` export, frees it and requests that version. If the load or export fails it logs why and requests 616 (`kDefaultAgilitySdkVersion`). No new parameter, so callers are unchanged.

3. **`FreeUnusedSDKs()` in `shutdown()`.** Not done, because it crashes. I implemented it as described: after resetting `device12_` and `deviceFactory_`, fetch `ID3D12SDKConfiguration1` from the loaded `d3d12Module_` and call it when `usingAgilitySdk_` was true. The call returns normally. The next `initialize()` in the same process gets a factory from `CreateDeviceFactory`, then `ID3D12DeviceFactory::CreateDevice` crashes with an access violation inside `d3d12.dll`. It reproduced on every run of `amd_fsr4_engine_test` (follow-up PR), with and without a 1 second sleep before the call, and with and without the version read from point 2. Without the call the test passes. I did not find the root cause. The bridge is re-initialized in normal use, so `D3D12Core.dll` stays loaded for the process lifetime, with a comment in `shutdown()`. Windows 10 19045, Adrenalin 26.11.2, RX 7900 XTX. Holding an extra reference to the factory across re-inits did not help: it still crashes with the call and passes without it, so the crash follows `FreeUnusedSDKs()` itself.

4. **Path conversion.** Fixed. Two-pass `WideCharToMultiByte` into a `std::string`, so no `MAX_PATH * 3` limit. A conversion failure logs `Agility SDK path conversion to UTF-8 failed; using system D3D12`, separate from `Agility SDK configuration interface unavailable`.

5. **Test assertions.** Done in `d3d11_d3d12_bridge_test`: `usingAgilitySdk() == false` after the default `initialize(d3d11Dev)` and after `shutdown()` (default and fallback bridges), plus a re-initialize after `shutdown()` on the fallback bridge.

`ctest` passes except `upscaler_dejitter`, which also fails on unmodified `master` on this machine.
