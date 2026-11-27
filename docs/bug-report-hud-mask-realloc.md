> **Status update 2026-11-22: probably already fixed upstream.** Upstream commit `c92b249` (2026-11-19, "prevent flare stabilization from resizing HUD mask to render resolution") is contained in `v0.8.0-beta` and every later build, and a session with the current source (`cb2f3b1`) logs only one HUD mask allocation across Performance and Native AA modes. The log in this draft came from an older build. If you filed it, add a comment saying it no longer reproduces on `v0.8.0-rc1` and can be closed.

# GitHub issue draft: HUD mask target reallocated repeatedly when upscaling below native scale

Fill in before filing: EDPE release tag or commit, and the date you downloaded it. Attach `baseline/evidence/edpe-log-excerpt.txt` (and, if you like, the full `edpe.log`).

---

**Title:** HUD mask (and companion textures) reallocated over and over in FSR Performance/Quality modes, not in Native AA / TAA

## Summary

With the AMD FSR backend at a render scale below 1.0, `edpe.log` shows thousands of `EDPE: allocated HUD mask target WxH R16G16B16A16_FLOAT` lines alternating between the output size and the render size. In Native AA and TAA mode the same message appears once. Reading `ensureHudMaskResources` in `src/context_census.cpp`, a single cached size is compared to the requested size, so if it is called with two different sizes, every call destroys and recreates the mask and several related textures. I suspect this adds render-thread CPU time, but I have not proven that.

## Environment

- EDPE: [release tag or commit], `dxgi.dll` 9,680,896 bytes (SHA256 `10956A0A09A7CEC851704A161A1E663669A744D081096458519543DFD5079268`)
- Elite Dangerous Odyssey (Steam), D3D11, 3840x2160 exclusive fullscreen, VSync off, AA mode off in game
- AMD Radeon RX 7900 XTX, Adrenalin 26.11.2 (driver 32.0.32029.2010), Ryzen 7 5800X3D, Windows 10 19045
- `edpe.ini`: `Backend=2` (AMD FSR), `Sharpness=0.50`, `PhaseCount=8`, `HdrInput=0`, `PreserveNativeUi=1`

## Steps to reproduce

1. Start the game with EDPE, land on a planet and stay still (the scene does not matter for the allocation pattern; a static scene makes it easy to compare).
2. In the EDPE menu choose backend AMD FSR, mode Native AA. Look at `edpe.log`: one allocation line.
3. Change the mode to Performance (render 1920x1080) or Quality (render 2496x1404).
4. Look at `edpe.log` again: long unbroken runs of `allocated HUD mask target 3840x2160 ...` and `... 1920x1080 ...` (or `2496x1404`) alternating.

## Observed (counts per backend/mode segment of one 15,995-line log)

| Segment | HUD mask allocations | Sizes |
|---|---|---|
| TAA, Native AA | 1 | 3840x2160 |
| AMD FSR, Native AA | 1 | 1920x1080 (leftover from a previous switch) |
| AMD FSR, Performance (0.5) | 4,797 | 3840x2160 x 2,398 and 1920x1080 x 2,398 |
| AMD FSR, Quality (0.65) | 5,525 | 3840x2160 x 2,762 and 2496x1404 x 2,762 |

Sample of the repeating pattern (log lines 4000 to 4005):

```
EDPE: allocated HUD mask target 3840x2160 R16G16B16A16_FLOAT
EDPE: allocated HUD mask target 1920x1080 R16G16B16A16_FLOAT
EDPE: allocated HUD mask target 3840x2160 R16G16B16A16_FLOAT
EDPE: allocated HUD mask target 1920x1080 R16G16B16A16_FLOAT
```

The runs between other log lines are 2 to 22 allocations long and longer. The log has no timestamps, so I cannot tell how many happen per frame.

## Suspected cause (from reading the source; parts were summarized, not verified)

`ensureHudMaskResources(device, width, height)` returns early only when `s_hud_mask_texture` exists and `s_hud_mask_width == width && s_hud_mask_height == height`. When the width or height differs, it resets the mask, the previous-frame copy, a tonemapped copy (`R8G8B8A8_UNORM`), a far linear depth texture (`R32_FLOAT`), a depth-stencil texture (`R32G8X24_TYPELESS`), and calls `UiLayer::resize`, which recreates three UI layer textures. `replayHudDraw` scales by `s_hud_mask_width / srcW`, which suggests the mask is meant to differ from the scene size when upscaling, and `UiCompositePass::composite` expects an output-sized mask. So one caller appears to request the output size and another the render size, and they evict each other. I did not find the two call sites.

By my arithmetic from those formats, one reallocation creates roughly 330 MB of textures at 3840x2160 and roughly 80 MB at 1920x1080.

## Why it may matter (hypothesis, not proven)

I measured frame times with PresentMon on a static planet scene (EDPE FSR 3.1.2, output 3840x2160):

| Mode | Frame time | GPU busy |
|---|---|---|
| FSR scale 0.65 | 8.36 ms | 6.5 ms (78%) |
| FSR scale 0.5 | 8.31 ms | 5.7 ms (69%) |
| vanilla game, scale 0.5, no EDPE | 6.95 ms | 4.5 ms (65%) |

Frame time is the same at two render scales while the GPU does less work, so something on the CPU side sets a floor of about 8.3 ms with EDPE in upscaling mode, about 1.4 ms above the game's own floor. Texture reallocation on the render thread could be part of that, but the whole 0.5-scale segment contains only 2,398 allocation pairs, and one 60 second capture is about 7,100 frames, so it is not once per frame for the entire capture. I could not tie the allocations to the floor.

## Suggested fix

Keep a separate cached HUD mask (and UI layer) set per requested size, or allocate once for the larger size and use a viewport or sub-rectangle for the smaller one, so alternating requests do not reallocate. A debug counter of allocations per second in the F7 overlay would let users check it.

## Open questions for the maintainer

- Which two call sites request different sizes in Performance/Quality mode?
- Is the allocation rate per frame, per draw, or per context? A count of new allocation lines in `edpe.log` over a 60 second capture would answer it (I can provide this if useful).
- Is the alternation intended?

## Attachments

- `edpe-log-excerpt.txt`: verbatim log excerpts around the mode switches, plus per-segment counts.
- Optional: the full `edpe.log` (1.6 MB).
