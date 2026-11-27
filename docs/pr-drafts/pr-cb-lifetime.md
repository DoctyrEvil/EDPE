# PR: stop stale constant buffer pointers from redirecting the wrong buffer (open as #39, body update draft)

Head: `DoctyrEvil:fsr4-cb-lifetime-fixes` (`a0cc416`, seven commits, on the fork). Base: `filispeen/EDPE` `feat/FSR4` (`9573c1e`).

**Title:** `hold a reference on the scene constant buffer and verify cached constant buffer widths`

**Body:**

## Series

First of four follow-ups to #35, in this order: this constant buffer fix, the FG teardown fix (#40), the FG toggle-off judder fix (#41, the one you asked to land before `feat/FSR4` goes to `master`), and a performance PR last. The two stability fixes go first because the game crashes or hangs without them in normal testing.

## What changed

EDPE keeps raw `ID3D11Buffer*` and `ID3D11Resource*` pointers to decide which `Map` and `UpdateSubresource` calls belong to the 5376-byte scene constant buffer and the small constant buffers it patches for jitter. The pointers hold no reference. After the game freed a buffer, a larger resource created at the same address matched, `observedMap` redirected its `Map` into a 64 KB map-bounce slot, and the game overran the pool and the static data after it. That crashed in `getRecommendedJitterPhaseCount` on a corrupt vtable.

One file, `src/context_census.cpp` (+104/-17), seven commits:

1. **Hold a reference on the watched scene constant buffer.** `scene_map_target` owns one reference. Once per Present, `releaseOrphanedSceneMapTarget` drops it when EDPE holds the last reference, so detection can pick up the game's new buffer.
2. **Check the real buffer before trusting a cached width.** `Map` and `UpdateSubresource` check the resource's description (`GetType`, `GetDesc`) for the same `ByteWidth`, the constant buffer bind flag, and dynamic usage for `Map`. A mismatch evicts the cache entry.
3. to 7. **Review fixes**, one per point of your review: no `bytes` store after eviction, release the camera delta source on the no-context return, `refs == 1` in the orphan release, return the old pointer from set and clear, and a real 5376-byte size check before the scene bounce.

Added cost: one `GetType` and one `GetDesc` per cached `Map` or `UpdateSubresource` (about 36 thousand per second in the long session below) and per scene `Map`. `vs_first_scene_buffer` is only compared and logged, never dereferenced, so I left it.

## Testing

- **Crash:** seen 2026-11-24 23:01 and 2026-11-25 14:44 in `getRecommendedJitterPhaseCount`. The second one occurred with commit 1 already in, which led to commit 2.
- **After commits 1 and 2:** a 55-minute session of about 592,000 frames on an integration build with unrelated code and a debug module. 119.6 million bounced maps, all flushed, no trip, no FFX or device error, no DRED report, no driver reset, clean exit.
- **Self-test before and after commit 2** (median frame time, Performance, 1920x1080 to 3840x2160, 2 rounds of 600 frames; sharpness 0.00 before and 0.50 after):

  | Option | Before (`92c54d1`) | After (`d1e3c53`) |
  |---|---|---|
  | Bypass | 2.55 ms | 2.40 ms |
  | AMD FSR 3.1.2 | 3.41 ms | 3.37 ms |
  | AMD FSR 4.1.2 | 5.99 ms | 6.03 ms |
  | Native TAA | 3.06 ms | 3.14 ms |

  Medians move by at most 0.15 ms in both directions. The second run had a noisier tail (FSR 4.1.2 p99 6.45 to 12.79 ms and one 138 ms stall); the same kind of stall appeared in earlier runs without this change, but I cannot rule out an effect.
- **After the review fixes (`ff20e4a` to `a0cc416`):**
  - Fork CI passes on `a0cc416` (33 of 33). Local `ctest` without the FG and presenter GPU tests: 26 of 27, and `upscaler_dejitter` also fails on unmodified `master`.
  - In game on a normal build of `a0cc416`: about 135,000 frames with switches among AMD FSR 3.1.2, TAA, Bypass and FSR 4.1.2, FG on and working on screen, no device, DRED or FFX errors in the log, clean exit.
  - Self-test on a build with the FG fixes and the debug module: round 2 medians were Bypass 2.30 ms, FSR 3.1.2 3.21 ms, FSR 4.1.2 6.30 ms and TAA 2.86 ms, within noise of the table above. Round 1 was unstable while the scene settled and is not used. The size check in commit 7 was not benchmarked on its own.
  - The D3D11 debug layer crashes inside `D3D11_3SDKLayers.dll` at device creation in the game process, so it was not run in game. In a standalone program on WARP and the GPU with the layer on, the cast, `GetDesc` and `AddRef`/`Release` assumptions held, a reused address was always rejected, and the layer reported no messages.
- **Limits:** no unit test covers the eviction or the reference handling, because `context_census.cpp` has no test seam. The crash was seen on test builds with extra code, and I expect the bug on `master` but have not reproduced it there. RenderDoc, DXVK and Proton are not tested.

Radeon RX 7900 XTX, Adrenalin 26.11.2, Windows 10 (19045), Elite Dangerous, windowed.

Follow-up to #27 and #35.

Written with Claude Code. I reviewed the diff, built it and ran the tests and in-game checks listed above.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
