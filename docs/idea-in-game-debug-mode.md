# Idea: in-game debug and test mode for the FSR 4 backend

Status: idea only, written 2026-11-22, scope section revised the same day. The in-game mode is not implemented and not planned for the first submission. The small development-test seams described in the scope section are the part intended for the pull request.

## Scope decision (revised 2026-11-22)

Testing is for development before commit, not a shipped feature. The repository rules support small test seams: `CONTRIBUTING.md` requires that all tests pass before a pull request and that behavior changes add or update a test in `tests/`, and `AGENTS.md` asks for CPU-side tests of conventions and standalone harnesses that do not need the game. So it is acceptable to change the plugin in small, safe ways that make pre-commit testing possible, under these limits:

- The change must not touch the per-frame hot path in a way that costs performance. Preferred forms: moving existing logic into a small pure function or header-only struct that the hot path calls unchanged, or a value read once at initialization.
- Behavior must stay identical. Verify with the existing test suite and a before-and-after profiling check (the engine's `cpu evaluate` was 0.31 ms and the frame 9.14 ms at Performance scale).
- No new dependencies, no test-only switches that remain active in normal use, and no runtime test UI in the first submission (that part stays an idea, below).

| Gap from the test review | Handled by |
|---|---|
| Agility fallback, resize and format changes, fence ordering, depth format, init/shutdown cycles, profile sanity | Extend `amd_fsr4_engine_test`, no plugin change |
| 120-failure gate, provider state machine (select, switch, retry, label text) | Move the counters and flags into a small header-only struct used by `AmdUpscalerBackend`, unit tested on the CPU in `fsr4_logic_test` |
| Dispatch-parameter mapping (jitter sign, depth and color flags) | Move the existing mapping into a pure function the engine calls, unit tested in `fsr4_logic_test` |
| Config round trip for `Fsr4`, `Fsr4Profile`, `Fsr4Flush`, `Backend=4` | Test of the existing config code, no plugin change if it can run in a temp folder |
| Missing DLLs | A ctest entry that runs a copy of the test exe from an empty folder and expects a clean SKIP, no plugin change |
| No-SDK stub build | CI configure-and-build line, not a test |
| HDR input | Out of scope (see `baseline-results-A.md`) |

The two test files, the checklist and the CI line from the test-strategy discussion stay the plan for the pull request. The in-game debug mode below remains an optional later addition, not part of the first submission.

## The idea

A developer-only mode inside EDPE that runs the verification checklist and the A/B measurements from inside the running game, so a reviewer or the author can confirm the FSR 4 path on a real scene in minutes without external scripts or log parsing.

### Gating (must not affect normal users)

- Compiled only with a CMake option, for example `EDPE_DEV_TESTS=ON`, which is off in release builds, so no test code or fault-injection hooks exist in the shipped DLL.
- At runtime it additionally requires the existing developer flag (`[Overlay] DEV=1`), shown as a separate "Developer tests" menu section.

### Features, in priority order

1. **Self-test run.** One button runs a fixed sequence on the current scene: for each AMD provider (FSR 3.1.2, FSR 4.1.2) switch to it, wait for warm-up, hold for N frames while collecting GPU time per frame and the engine's profile counters, then switch back and restore the user's settings. It ends with a pass/fail table and writes `edpe-selftest.txt` next to `edpe.log`.
   - Checks: provider switch succeeds, no `evaluate failed` or fallback lines, `cpuWaits` stays 0 in steady state, no frame stall over a threshold, FSR 4 dispatch time within a sane range, settings restored.
2. **Fault injection.** Buttons that make FSR 4 fail on purpose: fail the next N evaluations (exercises the per-frame fallback to FSR 3.1 and the 120-failure disable), fail initialization (exercises "unavailable" and the retry on reselect), and simulate missing DLLs. Each reports whether EDPE failed open: the frame still presented, the provider label changed, and a retry recovers.
3. **Switch stress.** Rapid provider and mode cycling for a fixed number of frames to exercise history resets, context reuse and the slot ring across many frames.
4. **A/B capture.** Save a paired screenshot of the same frame through FSR 3.1.2 and FSR 4.1.2 (extending the existing sequential comparison capture, which today lists one AMD FSR entry) and a short per-provider frame-time summary, so image-quality evidence is produced the same way every time.
5. **Machine-readable result.** A small JSON summary alongside the text report, for pasting into an issue or attaching to a pull request.

### What it would replace or support

- The manual in-game checklist would become a button.
- The measurement scripts in this repo stay for full PresentMon captures, since PresentMon-level frame times (display latency, present mode) are not available from inside the process.

## Delivery option: a drop-in debug module (added 2026-11-22)

Instead of compile-time gating inside the main DLL, ship the debug and test functions as a separate, optional module (for example `edpe_debug.dll`) that is copied next to `dxgi.dll` only on a developer machine. The module both carries the functions (self-test, fault injection, switch stress, A/B capture) and enables them by being present. Release packages do not include it.

How it would work:
- The proxy gets one small, non-hot hook at startup: if `edpe_debug.dll` exists beside `dxgi.dll` and the developer flag is set, load it and call a single exported entry point, passing a table of narrow, versioned callbacks (read-only state such as the active provider and timing counters, plus the few controlled actions the tests need: select provider, request history reset, inject a failure through an explicit seam). Without the file, nothing happens.
- The module draws its own "Developer tests" menu section through the existing ImGui interface and writes its report next to `edpe.log`.

Advantages over compile-time gating:
- The release binary has no test or fault-injection code at all, only the loader hook, so there is nothing to ship by accident and the owner reviews a much smaller diff in the main plugin.
- The debug code can change, grow or be dropped without touching the plugin, and can stay in our fork or a separate repository.

Costs and open points:
- The proxy still needs the loader hook and the callback table. That is the part to propose upstream, and it is small, but it is a stable-ABI commitment: the table needs a version field and size check, and the proxy must refuse a module built for a different version.
- Anything that needs to read internal state (HUD, camera, pass timings) widens the callback table, so keep the first version to provider, timings and failure injection.
- Loading an arbitrary DLL beside the proxy is a surface to guard: require the developer flag, check the module's reported EDPE version, and never load it in a release configuration.
- The module must be built against the same EDPE headers as the proxy it is paired with, so it is built from the same tree.

This would replace the compile-time `EDPE_DEV_TESTS` gating described above if the owner prefers it.

## Costs and risks

- More code to maintain in the plugin, even if compiled out of release builds. The upstream owner may not want it; it can live in our fork or a separate branch.
- The self-test disturbs the player's session (switches provider, resets history, pauses on warm-up). It must be explicit, interruptible and restore state.
- In-process timing is GPU-timestamp based and does not include presentation or driver queue effects, so it cannot replace PresentMon for the headline numbers.
- Fault injection must be unable to ship by accident: the compile-time gate plus a build-time check that release builds contain no injection symbols.

## When to build it

After the first submission has been reviewed. If the owner wants a lighter touch, ship only the development tests and the written checklist, and keep this mode in a fork.

## Open questions

- Does the owner accept a developer-only, compile-time-gated section at all?
- Where should results be written, and should the report include hardware and driver details (the existing crash logger already collects them)?
- Should the self-test also run on NVIDIA/DLSS and TAA backends, or stay FSR-specific?
