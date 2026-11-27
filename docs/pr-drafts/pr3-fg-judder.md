# PR: re-evaluate the game window after frame generation teardown (draft, not opened)

Head: `DoctyrEvil:fsr4-pr3-fg-judder` (`377687c`, two commits, on the fork). Base: `filispeen/EDPE` `feat/FSR4` (`9573c1e`). This is the toggle-off fix the owner asked to land before `feat/FSR4` goes to `master`.

**Title:** `re-evaluate the game window after frame generation teardown`

**Body:**

Third of four follow-ups to #35, after the constant buffer fix (#39) and the FG teardown fix (#40). The performance PR comes last.

## What changed

When frame generation (FG) is turned off, the overlay child window of the FG presenter is destroyed. Afterwards the game window can stay in a state where a renderer below the display rate loses about a third of its frames. Minimizing and restoring the window clears it.

This PR adds `requestPresentationReevaluation(HWND)` in `src/fg_integration.cpp`. It makes a no-op frame change (`SetWindowPos` with `SWP_FRAMECHANGED`, then `RedrawWindow`) so DWM re-evaluates the window. It runs once per overlay teardown, at the two places the child window is destroyed, and does nothing if the window is gone. It logs one line per teardown: success, or failure with the error code of each call.

1 file, +24 lines. Nothing changes when FG never starts. I did not identify the DWM behavior behind this. The evidence is the captures below, and that minimize and restore has the same effect.

## Testing

**Captures.** PresentMon, scripted sequence (FG off, menu open, FG on, FG off, menu open), Performance, 1920x1080 to 3840x2160, windowed, 240 Hz. "Never displayed" is the share of rows with no displayed time.

| FSR 4.1.2 (about 165 fps) | Never displayed | Display time p99 | Cadence variation |
|---|---|---|---|
| After FG off, without the fix | 31 to 32% | 19 ms | 0.64 |
| First 20 s after FG off, with the fix | 3% | 16 ms | 0.26 (includes about 2 s of recovery) |
| Next 20 s, with the fix | 0% | 6.3 ms | 0.016 to 0.026 |
| Before any FG (reference) | 0% | 6.3 ms | 0.02 |

- FSR 3.1.2 renders above the display rate (about 290 fps into 240 Hz). About 19% is never displayed before FG and the same after, with or without the fix.
- The present mode label stays on the compositor copy path. The change is in pacing, not the mode.
- Display latency does not change after teardown (FSR 4.1.2: 40.5 ms before FG, 40.4 to 40.6 ms after).

**All-three build** (this PR on #40 and #39, plus a developer test module that is not part of this PR):
- The captures were repeated with the same FSR 4.1.2 result: 3% never displayed in the first 20 s after FG off, then 0%.
- 6 upscaler switches with FG on each logged one re-evaluation. FG resumed after every one. There were no failure lines, DRED report, driver event or crash. The log shows 10 re-evaluations in total.
- Self-test: 10 of 10 passed. Median frame time (Performance, sharpness 0.00, 2 rounds of 600 frames):

  | Option | Earlier runs | All-three build |
  |---|---|---|
  | Bypass | 2.51 to 2.56 ms | 2.38 ms |
  | AMD FSR 3.1.2 | 3.39 to 3.44 ms | 3.26 ms |
  | AMD FSR 4.1.2 | 5.80 to 6.00 ms | 6.01 ms |
  | Native TAA | 3.02 to 3.06 ms | 2.94 ms |

  No option is above the earlier range by more than 0.01 ms.
- Stalls, not investigated: FSR 3.1.2 capture, 252 ms after FG off in the first phase and 179 ms in the next. FG start costs one 235 ms frame (263 ms for FSR 4.1.2). The 60-switch stress had one 670 ms frame (limit 2000 ms, earlier runs peaked near 287 ms).

**Clean build** (`377687c` alone, no test module): one session with 4 FG teardowns. Each logged one re-evaluation. There were no failure lines, crash or driver event. This was checked from the log only, because the scripted capture needs the test module.

**Not run without #39 and #40:** upscaler switches with FG on crash or hang without them (a bluescreen on a build with #39 only).

**CI and tests**
- Fork CI passes on `377687c`: 33 of 33 tests, `FSR 4: enabled` in the configure log.
- Local Release build, `ctest` without the FG and presenter GPU tests (they can reset the GPU on my machine): 26 of 27. `upscaler_dejitter` also fails on unmodified `master`.
- `fg_integration_test` passed on the original base and was not re-run after the rebase.
- No unit test covers the change, because it acts on window and DWM state.

**Not tested:** other FG backends (the change is in the shared presenter teardown, so it applies to any backend that uses it), fullscreen, other refresh rates, Windows 11, other GPUs, VR, Proton.

Radeon RX 7900 XTX, Adrenalin 26.11.2, Windows 10 (19045), AMD frame generation.

Follow-up to #27 and #35.

Written with Claude Code. I reviewed the diff, built it and ran the tests and captures listed above.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
