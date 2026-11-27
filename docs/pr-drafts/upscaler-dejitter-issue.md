# Issue draft (not filed)

**Title:** `upscaler_dejitter` fails for AMD_FSR_QualitySR on a real Radeon GPU, passes on WARP

**Labels (suggested):** bug, tests

## Summary

`upscaler_dejitter` fails on an AMD Radeon RX 7900 XTX. The same test passes under WARP and on the GitHub runner. The failure reproduces on unmodified `master` (`a5377c9` and `3c31694`), so it is not caused by any open PR.

## Environment

- GPU: Radeon RX 7900 XTX, Adrenalin 26.11.2
- OS: Windows 10 19045
- Build: Release, Visual Studio 2022 generator, `ctest -C Release`
- Test added in the #22 commits (2026-11-22)

## Observed

`upscaler_dejitter_test.exe` on the hardware device, case `AMD_FSR_QualitySR 128x128->192x192 phases=18`:

```
StepEdge(+jit): span=(0.3134, 0.2254) std=(0.0731, 0.0554) err=(0.0692, 0.0619) rms=(0.0258, 0.0254)
StaticThinLine: sub0.45px span=0.1049 rms=0.0422 meanPeak=0.3205
-> FAIL: StepEdge(+jit) span exceeded 0.25
-> FAIL: Static 0.45px thin line flickered (span=0.1049, rms=0.0422)
```

The span limit for SR cases is 0.25 (`maxEdgeSpan`, `tests/upscaler_dejitter_test.cpp`). The other cases (`AMD_FSR_NativeAA`, `AMD_FSR_RealisticSlowPan`, `AMD_FSR_PerfSR_0.5x_Walk`, `Native_D3D11_TAA`) report no failure.

With the device forced to WARP, the same case passes: StepEdge(+jit) span 0.1515.

## Expected

The test passes on the hardware device, or the limits account for hardware differences.

## Why it matters

The test creates a hardware D3D11 device first and falls back to WARP only if that fails (`D3D11CreateDevice`, `D3D_DRIVER_TYPE_HARDWARE`, then `D3D_DRIVER_TYPE_WARP`). CI runs without a GPU, so CI only exercises WARP. A contributor with a Radeon GPU sees a red `ctest` on a clean checkout.

## Possible causes (not confirmed)

- The 0.25 limit was tuned on WARP or on another vendor's GPU, and the FSR 3.1 compute path rounds differently on RDNA 3.
- A real difference in the AMD hardware path (driver, float precision, or a race in the temporal accumulation) that WARP does not reproduce.

I have not investigated further.

## Options

1. Run the test on WARP only, which makes the result deterministic. This hides hardware differences.
2. Keep the hardware run and widen the SR limit, or make it per-vendor.
3. Find the cause of the hardware difference, if the output is a real quality problem in-game.

## Reproduce

```
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
ctest -C Release -R upscaler_dejitter --output-on-failure
```
