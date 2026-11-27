# PR (compact): turn frame generation off around upscaler switches and when its device is lost (draft, not opened)

Head: `DoctyrEvil:fsr4-fg-teardown-fix` (`1151105`, three commits, on the fork). Base: `filispeen/EDPE` `feat/FSR4` (`9573c1e`).

**Title:** `turn frame generation off around upscaler switches and when its device is lost`

**Body:**

## Series

One of four PRs on top of `feat/FSR4` after #35, in this order:

1. **Constant buffer** (#39): fixes a crash from stale constant buffer pointers.
2. **FG teardown** (this PR): fixes a GPU hang when the upscaler is switched with frame generation on.
3. **FG toggle-off judder:** the fix you asked to land before `feat/FSR4` goes to `master`.
4. **Performance:** FSR 4 deferred sharpening and a compute queue option, opened last.

The two stability fixes go ahead of the judder PR because without them the game crashes or hangs in normal testing. The PRs do not depend on each other, and the judder PR still lands before `master`.

## The bug

Switching the upscaler with frame generation (FG) on changes the render size. On its next frame, FG freed and recreated its shared depth and motion vector textures while the previous frame's work on its D3D12 queue still read them. DRED showed a GPU page fault and `DEVICE_HUNG` on the FG device. The driver reset, EDPE kept FG on against the dead device, and the game stopped responding. In a separate case, the AMD FG provider threw a C++ exception from `ffxDestroyContext` on a removed device, and the exception aborted the game.

## The fix

Commit 1, `Restart frame generation around upscaler switches and handle a lost FG device` (`fg_integration.{h,cpp}`, `ui.cpp`, +56):
- `ensureBridgeResources` waits for the FG D3D12 queue once before it frees the first existing shared texture, which covers every render-size change.
- The upscaler backend menu turns FG off first, with a teardown that waits for its GPU work. FG starts again on the next frame.
- A removed FG D3D12 device turns FG off, logs the reason once, and keeps FG off until it is selected again.

Commit 2, `Keep the game running when the frame generation provider throws` (`amd_fsr_fg_backend.cpp`, +55/-11):
- Every provider call goes through `callFfxGuarded`, which logs a throw and returns an error code.
- After a throw the backend never calls or destroys its contexts again, and FG generates no frames. The game keeps presenting real frames through the existing fail-open path, and the backend resets the next time it is initialized.

Commit 3, `Make the frame generation provider fault flag atomic` (`amd_fsr_fg_backend.cpp`, +5/-4): `fgDispatchCallback` can run on the swap chain's present thread, so the `faulted` flag is now a `std::atomic<bool>` instead of a plain `bool`.

Nothing changes while FG is off. While it is on, the only added steady-state work is one `GetDeviceRemovedReason()` call per frame. The queue wait runs only when a shared texture is about to be freed, and on a hung device it lasts up to 5 seconds, once.

## Testing

- **Before:** switching to Bypass with FG on gave a GPU page fault and `DEVICE_HUNG`, then a hang (integration build at `8060dcf`, 2026-11-25 14:21).
- **Commit 1:** 8 upscaler switches with FG on for about 6.5 minutes. FG resumed after each one, with no `[ffx error]`, no device removal, no DRED report and a clean exit.
- **Both commits (this branch, clean build):** repeated switches among Bypass, AMD FSR, TAA and FSR 4.1.2 with FG on. There were 27 restart log lines, FG came back every time, and nothing threw. Exit code 0, with no driver reset.
- **Build:** `ctest` without the FG and presenter GPU tests: 26 of 27, where `upscaler_dejitter` also fails on `master` here. Fork CI passes on `4b12507` (33 of 33). Commit 3 only changes the flag's type; it builds locally and its CI runs on this PR.

Limits:
- The guard and the device-lost path never ran in any test, because nothing threw and no device was removed. I did not force a GPU reset.
- No unit test covers the restart, the latch or the guard.
- The longest continuous FG run was about 6.5 minutes.
- Not tested: NVIDIA frame generation (the guard is in the AMD backend only), fullscreen, Windows 11, other GPUs.

Radeon RX 7900 XTX, Adrenalin 26.11.2, Windows 10 (19045), AMD frame generation, windowed, 240 Hz.

Follow-up to #27 and PR #35.

Written with Claude Code. I reviewed the diff, built it and ran the tests and in-game checks listed above.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
