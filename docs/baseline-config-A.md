# Configuration A: recorded game and system setup

Recorded 2026-11-21 from the user's installed files, before any baseline capture. A copy of every relevant file, with hashes, is in `baseline/config-A/` (made by `tools/snapshot_config.ps1 -Label A`). Re-run that script for each later configuration (B, C, D) and compare.

## System at capture time

| Item | Value |
|---|---|
| GPU | AMD Radeon RX 7900 XTX |
| Driver | Adrenalin 26.11.2, Windows driver store version 32.0.32029.2010 (dated 17 Nov 2026) |
| CPU | AMD Ryzen 7 5800X3D (8 cores, 16 threads) |
| OS | Windows 10 Home 10.0.19045 |
| Resizable BAR | Reported enabled by the user |
| Game folder | No `dxgi.dll`, no `d3d11.dll`, no `edpe.ini`. `edvr.ini` and `edvr-flat.ini` (leftovers from ED-UP) are present; they do nothing without the proxy DLL. |

## Display settings (`Options\Graphics\DisplaySettings.xml`)

These are the settings in force. The file was last written 13:13 on 21 Nov, during the current game launch (started 13:09), so it reflects the settings used in the first two Adrenalin logs (13:15 and 13:20).

| Setting | Value | Note |
|---|---|---|
| Resolution | 3840 x 2160 | |
| Mode | `FullScreen=1` (exclusive fullscreen) | Not borderless. Keep the same in every configuration. |
| VSync | false | Good for measurement. |
| PresentInterval | 1 | Ignored while VSync is off. |
| Refresh rate | 240 Hz (`DX11_RefreshRateNumerator=240`) | Observed 131 to 136 fps is below this, so the game is not frame capped. |
| LimitFrameRate | false | |

`AppConfig.xml` in the game folder shows 1280x720, windowed, VSync true. Those are shipped defaults and are overridden by `DisplaySettings.xml`. Do not use them as the setup.

## Graphics options (`Options\Graphics\Settings.xml`)

| Setting | Value | Why it matters |
|---|---|---|
| PresetName | Custom | The per-option values of the custom preset (textures, shadows, ambient occlusion, and so on) are **not stored in these files**. Record them by screenshotting the in-game graphics screens. |
| FOV / HumanoidFOV | 56.249 | Affects the camera projection that EDPE reads and jitters. |
| GammaOffset | -0.6 | Color setup. Affects image comparisons, not performance. |
| ShaderWarming | true | Shader preparation on startup is on. Affects first-launch time only. |
| TerrainCheckerboardRenderingEnabled | **RESOLVED: false.** The user confirmed it is off, and `Custom.4.4.fxcfg` (the real preset file, see the next section) says `false`. The `true` in `Settings.xml` is a stale or ignored key. The superseded note follows. |
| (superseded note) | true in `Settings.xml` | `Settings.xml` was last written at 12:25:43 on 21 Nov, before the 13:09 game launch used for all baseline runs, and still says `true` (checked again after the runs; the game was closed). If this key controls terrain rendering, the baseline runs used checkerboard terrain, which reduces GPU cost on planet surfaces and can interact with jitter and motion vectors for temporal upscalers. If the in-game setting is off but the file was not rewritten, the effective value is unknown. **Resolve before more runs:** check the in-game graphics options (the key may not be exposed there), and note that changing it would invalidate the planet baselines. Keep it identical across all configurations. |
| AMDCrashFix | false | |
| SSAA | not set, so default 1.0 | EDPE's log confirmed `SSAAMultiplier=1.000000` earlier. |
| HDR | not recorded here | Check whether HDR is enabled in the game and in Windows, and keep it the same. |

`GraphicsConfigurationOverride.xml` is empty (`<GraphicsConfig />`, last changed 2024), so there are no manual overrides.

## The real preset file: `Options\Graphics\Custom.4.4.fxcfg`

This file holds the per-option values that `Settings.xml` does not (found 2026-11-21 after the E_1 run). Copy in `baseline/config-A/options_Graphics_Custom.4.4.fxcfg`. It was last written 14:22:32, after the E_1 capture, with these values:

| Setting | Value | Meaning |
|---|---|---|
| AAMode | 0 | Anti-aliasing **off** |
| UpscalingQuality | 0 | In-game upscaling off |
| SSAAMultiplier | 1.0 | No supersampling |
| TerrainCheckerboardRenderingEnabled | false | Confirmed by the user |
| FFXCASIntensity | 1.0 | Contrast adaptive sharpening strength |
| DirectionalShadowQuality / SpotShadowQuality | 4 / 4 | |
| TextureQualityEx / TextureFilterQuality / SurfaceSamplerQuality | 2 / 4 / 3 | |
| AOQuality / BloomQuality / DOFEnabled / EnvmapQuality | 3 / 3 / 2 / 1 | |
| Material / Environment / FX / ParticleEffects quality | 3 / 3 / 3 / 3 | |
| TerrainQuality / TerrainLodBlendingQuality / SurfaceMaterialQuality | 3 / 2 / 3 | |
| VolumetricsQuality / JetConeQuality / GalaxyMapQuality | 3 / 3 / 2 | |
| LODDistanceScale | 1.0 | |

Provenance: the file was rewritten after E_1 (which ran at 2x supersampling), and it shows SSAA back at 1.0. The user confirmed that the only value changed for E_1 was supersampling (no AA, nothing else), so these values are the config A values and E_1 differs from A only in SSAA. The A-time file itself was not saved. Config A is therefore **AA off, no upscaling, no supersampling**, which is consistent with the user's statement that the game's AA is not working. `snapshot_config.ps1` now copies the newest `Custom.*.fxcfg` for future configurations.

## Threading (`AppConfig.xml`, shipped file)

`NumWorkerThreads="6"`, `OptimiseForPerformance="1"`, `PerformanceScaling="1"`, `MinSpareCores="0"`, `UseThreadPriorities="0"`. The game already runs 6 worker threads plus a render thread. This matters for the earlier idea of using more cores for the bridge: the game's own worker pool is already sized at 6 of the CPU's 8 cores.

## Compute calibration (`GpuWorkTable.xml`)

The game wrote this file at 13:11:08 on 21 Nov, shortly after launch, and it records `driverVersion="32.0.32029.2010"` for the RX 7900 XTX. It is a table of measured timings for terrain and planet compute tasks (terrain, atmosphere, scatter, texture compression). The game recalibrates it when the card or driver changes. Consequence: after any driver change, the first launch may behave differently, so do not change the driver between configurations, and check this file's driver version matches before a capture.

## Files that are not useful for this work

- `GraphicsConfiguration.xml` (2025): shipped tone mapping, HBAO and lighting presets, not user choices.
- `DeviceDefaults.xml` (2024): a machine-generated table of quality presets per GPU model. It has no entry for the RX 7900 XTX (device id 29772), so the game's automatic preset logic does not know this card.
- `AudioConfiguration.xml`: entirely commented out, defaults in use.
- `Options\Startup\Settings.xml`: only `RenderVersion=2`.

## Still to record by hand

1. Screenshots of the in-game graphics options screens (textures, shadows, ambient occlusion, bloom, and the rest of the Custom preset).
2. Whether HDR is on (game and Windows).
3. The save locations and scene definitions for S1 to S4.
4. Windows settings: power plan, Hardware-accelerated GPU scheduling.

## Capture helper found

The Adrenalin metrics logging uses AMD's bundled `PresentMon-x64.exe` (`C:\Program Files\AMD\CNext\CNext\`), launched with `-stop_existing_session -output_stdout -v1_metrics -qpc_time -session_name RSXTraceSession`. It started at 13:15:29, the same moment as the first Adrenalin log, and was still running afterward. This means a per-frame capture tool is already installed. We ran it ourselves with a different session name and a file output: it works without elevation and produces per-frame data (see `baseline-capture-steps.md`, section 4b). A 10-second test in a game scene gave 136.2 fps average, 1% low 113.2 fps, 0.1% low 104.1 fps, frame time standard deviation 0.44 ms, present mode "Hardware: Independent Flip", GPU busy about 100% and CPU busy about 99% of the frame time. The scene for that test was not recorded.
