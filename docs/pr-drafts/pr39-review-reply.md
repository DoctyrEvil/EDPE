# Reply to the owner's review of #39 (draft, not posted)

Head after the fixes: `DoctyrEvil:fsr4-cb-lifetime-fixes` (`a0cc416`, five new commits on top of `8866c27`).

**Comment:**

Thanks for the careful read. Each point has its own commit, and fork CI passes on `a0cc416` (33 of 33).

1. **Eviction** (`ff20e4a`): you are right, and the `bytes[w].store(0)` is gone. I removed the same store from `clearRejectedResourceCache`.
2. **`camera_delta_source`** (`2c73265`): it is only taken in developer mode, and it is moved into a local before `Present`. One path could keep it: the early return when there is no context. It now resets there.
3. **Orphan release** (`902a2a9`): it is now `refs == 1`, and any other count keeps the target for the 120-frame watchdog. I kept the `exchange` that takes ownership first, because a `load` followed by `AddRef` could touch a buffer another thread has just released. The target reads as null for a moment, but that runs inside the Present scope guard, where `observedMap` and `observedUnmap` already return early.
4. **Wrappers:** I could not test this in the game. The D3D11 debug layer crashes inside `D3D11_3SDKLayers.dll` during device creation in the game process, though it works in a standalone program on the same machine. In that program, on WARP and on the GPU with the debug layer on, `GetType`, the cast and `GetDesc` behave as expected, the `AddRef`/`Release` probe returns 1 or 2 as the logic needs, and a larger buffer reusing a freed address is always rejected. The debug layer reported no messages. That program copies the function, because `context_census.cpp` has no test seam. RenderDoc, DXVK and Proton are not tested.
5. **`endDrawProbe`** (`cf33159`): `setSceneMapTarget` and `clearSceneMapTarget` return the previous pointer, so there is one exchange and the log shows what was cleared.
6. **Map-bounce size** (`a0cc416`): you are right that `onMap` only checks the width the caller passes. `observedMap` now checks the real 5376-byte buffer before the bounce, and drops the target on a mismatch. `UpdateSubresource` has no scene branch.

In game on a normal build of `a0cc416` I switched between AMD FSR 3.1.2, TAA, Bypass and FSR 4.1.2 for about 135,000 frames with FG on, with no errors in the log and a clean exit. A self-test on a build with the FG fixes showed round 2 medians within noise of the earlier baseline. I did not run a before-and-after test of the size check in point 6. It adds one `GetType` and one `GetDesc` per scene `Map`.
