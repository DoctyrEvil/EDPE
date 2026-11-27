# PR (compact): turn frame generation off around upscaler switches and when its device is lost (open as #40, body update draft)

Head: `DoctyrEvil:fsr4-fg-teardown-fix` (`efa4087`, seven commits, on the fork). Base: `filispeen/EDPE` `feat/FSR4` (`9573c1e`).

**Title:** `turn frame generation off around upscaler switches and when its device is lost`

**Body:**

Second of three follow-ups to #35, after the constant buffer fix (#39). The FG toggle-off judder fix (#41, the one you asked to land before `feat/FSR4` goes to `master`) follows, and a performance PR is planned after it. The two stability fixes go first because the game crashes or hangs without them in normal testing.

## The bug

Switching the upscaler with frame generation (FG) on changes the render size. On its next frame, FG freed and recreated its shared depth and motion vector textures while the previous frame's work on its D3D12 queue still read them. DRED showed a GPU page fault and `DEVICE_HUNG` on the FG device. The driver reset, EDPE kept FG on against the dead device, and the game stopped responding. Separately, the AMD FG provider threw a C++ exception from `ffxDestroyContext` on a removed device, and the exception aborted the game.

## What changed

Seven commits, 7 files (+254/-19):

The fix:
1. `6592b12`: restart FG around upscaler switches and handle a lost FG device (`fg_integration.{h,cpp}`, `ui.cpp`).
   - `ensureBridgeResources` waits once for the FG D3D12 queue before it frees the first existing shared texture, which covers every render-size change.
   - The upscaler backend menu turns FG off first, with a teardown that waits for its GPU work. FG starts again on the next frame.
   - A removed FG D3D12 device turns FG off, logs the reason once, and keeps FG off until FG is selected again.
2. `4b12507`: keep the game running when the provider throws (`amd_fsr_fg_backend.cpp`). Provider calls go through a guard that logs a throw and returns an error code. After a throw the backend never calls or destroys its contexts again, FG generates no frames, and the game keeps presenting real frames through the existing fail-open path. The backend resets when it is initialized again.
3. `1151105`: make the fault flag atomic, because `fgDispatchCallback` can run on the swap chain's present thread.

Review pass on our own diff:

4. `9c75672`: guard the remaining provider calls (version query, swap chain create and its destroy on the attach-failure path, two memory-usage queries). The first guard missed them.
5. `7b1cfea`: return an error from `fgDispatchCallback` after a fault.
6. `600f1d1`: clear the fault only when no context is left, so a throw on a live context is not discarded.
7. `efa4087`: move the guard into `guarded_call.h` and add `guarded_call_test`, so CI fails if a guard is removed.

Nothing changes while FG is off. While it is on, the added steady-state work is one `GetDeviceRemovedReason()` call per frame. The queue wait runs only when a shared texture is about to be freed, and on a hung device it lasts up to 5 seconds, once.

## Testing

The fix worked: FG resumed after every switch, and no run had a hang, device error or exception.
- **Switching:** the clean branch at `4b12507` (no test module) ran repeated switches among Bypass, AMD FSR, TAA and FSR 4.1.2 with FG on: 27 restarts, FG back every time, exit code 0, no driver reset. The final tree, merged with #39 and with the test module, ran 15 more restarts across two sessions (7 and 8), with FG resuming each time (observed on screen).
- **Size change:** in that session, three mode changes from Native AA to Quality with FG on (render size change, shared textures freed). FG stayed on, with no page fault, device error or hang.
- **Self-test and captures** on the final tree (Performance, 1920x1080 to 3840x2160): 10 of 10 passed, including a 60-switch stress, and FG on and off captures with FSR 4.1.2 and FSR 3.1.2 ran cleanly. Median frame times are in the earlier range, so FG teardown adds no steady-state cost:

  | Option | Earlier runs | Final tree |
  |---|---|---|
  | Bypass | 2.51 to 2.56 ms | 2.51 ms |
  | AMD FSR 3.1.2 | 3.39 to 3.44 ms | 3.38 ms |
  | AMD FSR 4.1.2 | 5.80 to 6.00 ms | 5.95 ms |
  | Native TAA | 3.02 to 3.06 ms | 3.00 ms |
- **CI and tests:** fork CI passes on `efa4087` (34 of 34, with `FSR 4: enabled`). Local `ctest` without the FG and presenter GPU tests: 27 of 28, and `upscaler_dejitter` also fails on unmodified `master`. The new `guarded_call` test passes, and fails if the flag-set line is removed.
- **Before:** switching to Bypass with FG on gave a GPU page fault and `DEVICE_HUNG`, then a hang (integration build at `8060dcf`).
- **Limits:** the guard, the lost-device path and the 5-second wait on a hung device never ran in any test, because nothing threw and no device was removed. No test covers the restart or the device-lost latch. The final-tree runs used a build merged with #39. The longest continuous FG run was about 6.5 minutes. Not tested: NVIDIA frame generation (the guard is in the AMD backend only), fullscreen, Windows 11, other GPUs.

Radeon RX 7900 XTX, Adrenalin 26.11.2, Windows 10 (19045), AMD frame generation, windowed, 240 Hz.

Follow-up to #27 and #35.

Written with Claude Code. I reviewed the diff, built it and ran the tests and in-game checks listed above.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
