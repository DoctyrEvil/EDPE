# PR: stop stale constant buffer pointers from redirecting the wrong buffer (open as #39, body update draft)

Head: `DoctyrEvil:fsr4-cb-lifetime-fixes` (`31b86c3`, ten commits, on the fork). Base: `filispeen/EDPE` `feat/FSR4` (`9573c1e`).

**Title:** `hold a reference on the scene constant buffer and verify cached constant buffer widths`

**Body:**

First of four follow-ups to #35, in this order: this constant buffer fix, the FG teardown fix (#40), the FG toggle-off judder fix (#41, the one you asked to land before `feat/FSR4` goes to `master`), and a performance PR last. The two stability fixes go first because the game crashes or hangs without them in normal testing.

## The crash

EDPE keeps raw `ID3D11Buffer*` and `ID3D11Resource*` pointers to decide which `Map` and `UpdateSubresource` calls belong to the 5376-byte scene constant buffer and the small constant buffers it patches for jitter. The pointers hold no reference. After the game freed a buffer, a larger resource created at the same address matched, `observedMap` redirected its `Map` into a 64 KB map-bounce slot, and the game overran the pool and the static data after it. The game then crashed in `getRecommendedJitterPhaseCount` on a corrupt vtable. It was seen on 2026-11-24 and 2026-11-25.

## What changed

All changes are in `src/context_census.cpp` (+120/-20), in ten commits:

The fix:
1. `23fc9a2`: hold a reference on the watched scene constant buffer, so its address cannot be reused. `releaseOrphanedSceneMapTarget` drops it once per Present when EDPE holds the last reference.
2. `8866c27`: check the resource's real description (`GetType`, `GetDesc`) before trusting a cached constant buffer width. A mismatch evicts the cache entry.

Review fixes:

3. `ff20e4a`: stop clearing the cached byte width on eviction, and document the cache rule.
4. `2c73265`: release the camera delta source on the no-context return.
5. `902a2a9`: release the orphaned target only on `refs == 1`.
6. `cf33159`: return the previous pointer from set and clear.
7. `a0cc416`: check the real 5376-byte size before the scene `Map` bounce.

Review nits:

8. `e1dba36`: clear the target with a compare-and-swap, and require dynamic usage in the size check.
9. `d6f8883`: release/acquire ordering for the cached byte width.
10. `31b86c3`: clear the target when the observed swap chain switches to the scene device.

Added cost: one `GetType` and one `GetDesc` per cached or scene `Map` and per cached `UpdateSubresource`, about 36 thousand per second in the long session below. `vs_first_scene_buffer` is only compared and logged, never dereferenced, so it is left as is.

## Testing

The fix worked: no crash and no errors in the runs below, and the functions affected were checked.
- **Sessions:** a 55-minute session (about 592,000 frames) after commits 1 and 2, and normal in-game runs on `a0cc416` and `31b86c3` with switches among AMD FSR 3.1.2, FSR 4.1.2, TAA and Bypass, FG on and off, and a 60-switch stress. No device, DRED, FFX or exception errors, and map-bounce `tripped=0` throughout.
- **CI and tests:** fork CI passes on `31b86c3` (33 of 33). Local `ctest` without the FG and presenter GPU tests: 26 of 27, and `upscaler_dejitter` also fails on unmodified `master`.
- **Performance:** self-test median frame time before and after commit 2 (Performance, 1920x1080 to 3840x2160, 2 rounds of 600 frames):

  | Option | Before (`92c54d1`) | After (`d1e3c53`) |
  |---|---|---|
  | Bypass | 2.55 ms | 2.40 ms |
  | AMD FSR 3.1.2 | 3.41 ms | 3.37 ms |
  | AMD FSR 4.1.2 | 5.99 ms | 6.03 ms |
  | Native TAA | 3.06 ms | 3.14 ms |

  Medians move by at most 0.15 ms in both directions. The final branch (`31b86c3`, sharpness 0.00) measures 2.56, 3.44, 5.80 and 3.02 ms, in the same range.
- **Limits:** no unit test covers the eviction or the reference handling, because `context_census.cpp` has no test seam. The crash was seen on test builds with extra code, and I expect the bug on `master` but have not reproduced it there. The D3D11 debug layer crashes at device creation in the game on this machine, so it was checked only in a standalone program. RenderDoc, DXVK and Proton are not tested.

Radeon RX 7900 XTX, Adrenalin 26.11.2, Windows 10 (19045), Elite Dangerous, windowed.

Follow-up to #27 and #35.

Written with Claude Code. I reviewed the diff, built it and ran the tests and in-game checks listed above.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
