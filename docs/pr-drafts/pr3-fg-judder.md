# PR: re-evaluate the game window after frame generation teardown (draft, not opened)

Head: `DoctyrEvil:fsr4-pr3-fg-judder` (`2dded4c`, one commit, on the fork). Base: `filispeen/EDPE` `feat/FSR4` (`9573c1e`). This is the toggle-off fix the owner asked to land before `feat/FSR4` goes to `master`.

**Title:** `re-evaluate the game window after frame generation teardown`

**Body:**

Third of four follow-ups to #35, after the constant buffer fix (#39) and the FG teardown fix (#40). The performance PR comes last.

## What changed

When frame generation (FG) is turned off, the overlay child window that the FG presenter uses is destroyed. Afterwards the game window can stay in a state where a renderer running below the display rate loses about a third of its frames, until the window changes (minimizing and restoring it clears it).

This PR adds `requestPresentationReevaluation(HWND)` in `src/fg_integration.cpp`: a no-op frame change (`SetWindowPos` with `SWP_FRAMECHANGED`, then `RedrawWindow`) that makes DWM re-evaluate the window. It runs once per overlay teardown, in the two places the child window is destroyed, and does nothing if the window is gone. 1 file, +13 lines. Nothing changes when FG never starts.

I did not identify the exact DWM behavior behind this. The evidence is the before and after captures below, and that minimize and restore had the same effect.

## Testing

**Captures** (PresentMon, scripted sequence: FG off, menu open, FG on, FG off, menu open; RX 7900 XTX, 1920x1080 to 3840x2160, Performance, windowed, 240 Hz). "Never displayed" is the share of rows whose displayed time is NA.

| FSR 4.1.2 (about 165 fps) | Never displayed | Display time p99 | Cadence variation |
|---|---|---|---|
| After FG off, without the fix | 31 to 32% | 19 ms | 0.64 |
| First 20 s after FG off, with the fix | 3% | 16 ms | 0.26 (includes about 2 s of recovery) |
| Next 20 s, with the fix | 0% | 6.3 ms | 0.016 to 0.026 |
| Before any FG (reference) | 0% | 6.3 ms | 0.02 |

- FSR 3.1.2 renders above the display rate here (about 290 fps into 240 Hz). Its never-displayed share is about 18% before FG and the same after, with or without the fix. The only effect seen was a one-time stall of up to 187 ms at the first teardown.
- The present mode label stays on the compositor copy path in both cases. The change is in pacing, not the mode.
- Display latency did not change after teardown within a capture (FSR 4.1.2: 40.5 ms before FG, 40.4 to 40.6 ms after).
- The captures were repeated on two builds with this exact code. Those builds also carried a developer test module that is not part of this PR.
- A clean build (no test module) of this commit on top of #39 and #40 ran in game for one short session: 11 upscaler switches with FG on, FG resumed after each, no visual problems, clean exit, no DRED report or driver reset. This commit was not run without #39 and #40, because without them the game crashes or hangs in normal testing.

**Build and CI**
- Local Release build, `ctest` without the FG and presenter GPU tests (those can reset the GPU on my machine): 26 of 27. `upscaler_dejitter` also fails on unmodified `master` here.
- Fork CI passes on `2dded4c` (33 of 33 tests, `FSR 4: enabled` in the configure log).
- `fg_integration_test` passed on the original base and was not re-run after the rebase.
- No unit test covers the change, because it acts on window and DWM state.

**Not tested:** other FG backends (the change is in the shared presenter teardown, so it applies to any backend that uses it), fullscreen, other refresh rates, Windows 11, other GPUs, VR, Proton.

Radeon RX 7900 XTX, Adrenalin 26.11.2, Windows 10 (19045), AMD frame generation.

Follow-up to #27 and #35.

Written with Claude Code. I reviewed the diff, built it and ran the tests and captures listed above.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
