# PR: turn frame generation off around upscaler switches and when its device is lost (draft, not opened)

Head: `DoctyrEvil:fsr4-fg-teardown-fix` (`4b12507`, two commits, on the fork). Base: `filispeen/EDPE` `feat/FSR4` (`9573c1e`). Independent of the constant buffer PR (#39): different files.

**Title:** `turn frame generation off around upscaler switches and when its device is lost`

**Body:**

## Series

One of four PRs on top of `feat/FSR4` after #35, in this order:

1. **Constant buffer** (#39): fixes a crash from stale constant buffer pointers.
2. **FG teardown** (this PR): fixes a GPU hang when the upscaler is switched with frame generation on, and an abort when the FG provider throws.
3. **FG toggle-off judder:** the fix you asked to land before `feat/FSR4` goes to `master`.
4. **Performance:** FSR 4 deferred sharpening and a compute queue option, opened last.

The two stability fixes (1 and 2) go ahead of the judder PR, which was planned next. Without them the game crashes or hangs in normal testing, which makes the judder fix hard to review. They do not depend on each other, and the judder PR still lands before `master`.

## What changed and why

Switching the upscaler backend while frame generation (FG) is on can hang the FG D3D12 device. A switch such as Performance to Bypass changes the render size. On its next frame, FG freed and recreated its shared depth and motion vector textures while the previous frame's work on its D3D12 queue still read them. DRED showed a GPU page fault and `DEVICE_HUNG` on the FG device. The driver reset, EDPE kept FG on against the dead device and reopened shared textures every frame, and the game stopped responding.

Two commits:

1. **Restart frame generation around upscaler switches and handle a lost FG device** (`src/fg_integration.{h,cpp}`, `src/ui.cpp`, +56)
   - `ensureBridgeResources` waits for the FG D3D12 queue once, before it frees the first existing shared texture. This covers every render-size change, not only backend switches.
   - The upscaler backend menu calls `FgRuntime::restartForUpscalerChange()` first. It turns FG off with a synchronous teardown that waits for its GPU work, and FG starts again on the next frame, as if turned off and on by hand.
   - Each frame, a removed FG D3D12 device turns FG off, logs the removal reason once, and latches FG off until it is selected again.
2. **Keep the game running when the frame generation provider throws** (`src/amd_fsr_fg_backend.cpp`, +55/-11)
   - The AMD FG provider threw a C++ exception from `ffxDestroyContext` when its device had been removed, and the exception aborted the game. The device-lost path in commit 1 shuts FG down on exactly such a device, so the provider calls are guarded.
   - Every provider call goes through `callFfxGuarded`, which logs a throw and returns an error code. After a throw the backend sets `faulted`: it never calls or destroys its contexts again (destroying them would throw again) and `evaluate` fails from then on, which the manager already treats as fail-open (the game keeps presenting real frames, the status reads "FG backend evaluate failed (fail-open)"). It resets the next time the backend is initialized (FG toggled off and on, or a resize).

Nothing changes while FG is off. While FG is on, the only added steady-state work is one `GetDeviceRemovedReason()` call per frame. The queue wait happens only when a shared texture is about to be freed, and on a hung device it lasts up to 5 seconds, once.

## Testing

- **Before:** on the integration build at `8060dcf`, switching to Bypass with FG on gave a GPU page fault and `DEVICE_HUNG` (DRED), then a hang (2026-11-25 14:21).
- **Commit 1** (integration build, debug hook and DRED on): 8 upscaler switches (Bypass x2, AMD FSR x3, TAA x1, FSR 3.1.2 and FSR 4.1.2 each way x3) with FG on for about 6.5 minutes. FG resumed after each switch. No `[ffx error]`, no device removed or hung, no DRED report, no provider exception, and a clean exit with no crash report.
- **Both commits** (this branch, clean build, no debug module): repeated switches among Bypass, AMD FSR, TAA and FSR 4.1.2 with FG on. 27 restart log lines, and FG came back every time (observed on screen). No `[ffx error]`, no exception and no `device lost` line. Exit code 0 and no display driver reset in the Windows event log.
- **Build and tests:** Release build, `ctest` without the FG and presenter GPU tests (those can reset the GPU on my machine): 26 of 27. `upscaler_dejitter` also fails on unmodified `master` here. Fork CI passes on `4b12507` (33 of 33).
- **Limits:**
  - **The guard and the device-lost path did not run in any test.** No provider call threw and no FG device was removed in the final runs. I did not force a GPU reset to test them.
  - No unit test covers the restart, the latch or the guard. They sit on a GPU device and the FG provider, and the test suite has no fake device.
  - The longest continuous FG run was about 6.5 minutes.
  - Not tested: NVIDIA frame generation (the restart and the device-lost check are backend independent, but the guard is in the AMD backend only), fullscreen, Windows 11, other GPUs.

Radeon RX 7900 XTX, Adrenalin 26.11.2, Windows 10 (19045), AMD frame generation, windowed, 240 Hz.

Follow-up to #27 and PR #35. Separate from the FG toggle-off judder fix (its own PR).

Written with Claude Code. I reviewed the diff, built it and ran the tests and in-game checks listed above.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
