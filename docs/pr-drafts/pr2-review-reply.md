# Reply to the PR 2 review on #35 (draft, not posted)

Post as a comment on https://github.com/filispeen/EDPE/pull/35 after the PR body is replaced. Head `bcccabc`.

---

Thanks for the review. All seven points are addressed, one commit each, and fork CI passes on the head (`bcccabc`).

1. **Provider version** (`4ec2345`). `providerVer` now starts empty and `selectProvider()` sets it from the picked version. Context creation still refreshes it, and `shutdown()` clears it. The menu and HUD show `FSR 4.1.2` before the first frame.
2. **Reason lines** (`dc1f423`). When the AMD backend is unavailable, one combined line covers both entries. Otherwise the menu prints the active provider and at most one `AMD FSR 4.1.2 reason:` line.
3. **History resets** (`2d74f84`). The provider that takes over now starts from a clean history, in both directions. This is covered in `fsr4_logic_test`. In game, I confirmed that `HdrInput=1` switches to FSR 3.1.2 with the logged message. I could not test the way back to FSR 4, because `HdrInput` is read only at startup, so that direction is covered by the unit test only.
4. **Probe and bridge** (`c31eab1`, `bcccabc`). The probe is skipped on known non-AMD adapters. After the probe, the engine and its D3D12 device are released unless FSR 4 is selected, and selecting it initializes them again. In game, three cycles through Bypass, FSR 3.1.2 and TAA released and re-created the engine without errors. `bcccabc` applies the saved `Backend=4` before the probe, so a start on `Backend=4` creates the device once.
5. **`Backend=4` only** (`3e56888`). `Fsr4`, `Config::fsr4` and `selectedByConfig()` are removed. `Backend=4` selects FSR 4.1.2 and `Backend=2` selects FSR 3.1.2.
6. **CMake and CI** (`92cc447`, `25a801a`). CMake logs the FSR 4 status, and `EDPE_REQUIRE_FSR4` turns missing inputs into a configure error. The build action sets it, and the CI log shows `FSR 4: enabled`. The README documents both.
7. **FG toggle-off fix.** It stays in its own PR and will land before `feat/FSR4` goes to `master`. I have not yet tested whether the change in point 5 affects the judder.
