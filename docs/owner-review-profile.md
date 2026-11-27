# Owner review profile (filispeen/EDPE)

Purpose: review our own changes the way the repo owner (`filispeen`) does, before they see them. Built from the owner's comments on PR 1 (#31), PR 2 (#35) and the scoping comments on issue #27. Sources are quoted by paraphrase; the PR and issue links are at the end.

## Summary

The owner reviews as a careful maintainer of a long-lived, runtime-hostile codebase (a graphics hook injected into a game). The questions behind almost every comment are:

1. Can this state become wrong, stale, or inconsistent, even in an unusual configuration?
2. Does the code cost something at runtime when the feature is not in use?
3. Will CI catch it if this breaks?
4. Is there exactly one source of truth for this setting or fact?
5. Is the PR small and separable enough to review and revert?

Their reviews are detailed and numbered, point at exact files, functions and branches, and propose a concrete fix. They are approving in tone ("looks good", "clean"), but they do not merge until every item is addressed. They accept a reasoned "no" to a suggestion if it is justified and documented in code (PR 1, `FreeUnusedSDKs()`).

## What they care about, with the evidence

### 1. Explicit, observable state
- **Logs must be accurate.** PR 1 item 1: a success log named the wrong cause on Windows 11, so they asked for rewording and the real SDK version in `edpe.log`.
- **UI text must never show a placeholder.** PR 2 item 1: `"unavailable"` leaking into the menu and HUD before the first frame. Their fix: empty string as the default, set at the moment of selection, cleared on shutdown.
- **No duplicated messages.** PR 2 item 2: the same reason line printed twice, and a shared backend error printed for two entries.
- **Reasons are shown, not hidden.** Unavailable options are greyed out with a reason (also in #27, "as DLSS is").

### 2. State transitions must be handled in both directions
- PR 2 item 3: fallback to FSR 3.1.2 needed a history reset, and the return to FSR 4 needed one too. They name both directions explicitly.
- Expect questions of the form: what happens on the way back, on the second time, after a failure, after a restart?

### 3. No cost when a feature is not in use
- PR 2 item 4: an idle D3D12 device kept alive for the whole session is rejected. They also name side effects (RenderDoc captures, loading signed DLLs only to refuse them on non-AMD GPUs).
- Preferred shape: probe cheaply, gate by vendor, release resources until the user selects the feature, initialize lazily.
- They know the existing helpers (`src/gpu_vendor.h`) and expect them to be reused.

### 4. One source of truth for configuration
- PR 2 item 5: two settings (`Backend=4` and `Fsr4=1`) that can disagree is a defect. Reading `edpe.ini` outside `Config` is also a defect. Their rule: since the feature is new, remove the redundant path rather than reconcile it.
- Expect: no direct ini reads, no new keys that duplicate existing ones, menu choices must leave the ini consistent.

### 5. CI must be able to fail for the right reason
- PR 2 item 6: a silent fall back to a stub build while CI (no GPU, tests skipped) stays green is unacceptable. They want a status message and an explicit "require" switch for CI.
- Expect: any optional dependency needs a visible status line and a way for CI to make it mandatory.

### 6. Robustness over cleverness
- PR 1 item 2: a hardcoded SDK version is replaced by reading the exported version from the DLL, with a documented fallback.
- PR 1 item 4: a path-length limit that produces a misleading error is replaced by correct sizing and a separate error message.
- PR 1 note: a relative-path caveat is noted even though it does not block ("fine as long as callers pass an absolute directory").
- Failure messages must say which thing failed.

### 7. Tests assert the negative path too
- PR 1 item 5: assert the default (non-Agility) path and the state after `shutdown()`, not only the success path.
- They praised the CPU-testable `fsr4_logic.h` split and GPU tests that skip cleanly. Pure logic in a header, I/O in a separate file, is a pattern they like.

### 8. Small, separable PRs and branch discipline
- #27: split the D3D12 bridge and Agility SDK changes from the FSR 4 backend and tests, "makes review much easier".
- PR 1 merged into `feat/FSR4`, not `master`; follow-ups target `feat/FSR4`. Features accumulate there and merge to `master` together.
- PR 2 item 7: they accept a separate PR for the frame generation fix, with a hard ordering requirement: it lands before `feat/FSR4` merges to `master`.
- Do not mix in unrelated fixes. They noted `upscaler_dejitter` as a separate issue.

### 9. Dependencies, licensing and packaging
- Signed AMD DLLs are never committed; they are fetched at build time like `external/ffx-sdk`.
- `D3D12Core.dll` is optional, with a fallback to the system runtime.
- Release packaging waits for a license check of the FidelityFX SDK. Do not stage vendor binaries into release or test builds without asking.
- Labels are explicit: `AMD FSR 3.1.2` and `AMD FSR 4.1.2`.

### 10. Line-level correctness: concurrency, lifetime, hostile environments (PR #39 review)
First seen on #39, the first low-level change they reviewed. They read the diff for interleavings and ownership, and phrase points as questions that ask for evidence.
- **Windows between atomic operations.** A store after a compare-and-swap can overwrite a new owner's entry. A pointer set to null and restored later is briefly invisible to other threads. Each structure gets one invalidation mechanism (for the accepted constant buffer cache, `ways[w] == nullptr`), and writes must not clear data a concurrent writer may already own.
- **Two owners of one resource.** When two members hold a reference to the same COM object, they asked whether one is always released before the other's refcount check, and on every early return.
- **Do not trust third-party return values.** Refcounts and interface casts can differ under wrappers and overlays (RenderDoc, the D3D11 debug layer, DXVK). Prefer the safer comparison (`refs == 1`, not `<= 1`) and fall back to a watchdog. Say whether the wrapper cases were tested.
- **Validate at the point of damage.** Do not rely on an earlier classification being right. Put a hard bound where memory could be overrun, such as the real buffer size at the map-bounce site.
- **One atomic operation, log the exact thing.** A clear function returns what it cleared, instead of a separate load and exchange.

## Review style: what a good response looks like
- Address every numbered point, one commit per point, with the commit named in the reply.
- Say what was verified and how (in game, unit test, CI log). Be explicit about what was only unit tested.
- If declining a suggestion, give a reproducible reason and leave an explanatory comment in the code. This was accepted on PR 1.
- Keep the PR body in step with the final diff (file counts, config keys, behavior). They re-read the updated diff and confirm each point back.
- Keep replies short and factual. They do not ask for essays.

## Checklist for reviewing our own code as the owner would

Behavior and state
- [ ] Does any user-visible text show a placeholder, a stale value or a duplicate line in any reachable state (before first frame, after shutdown, unsupported GPU, missing DLL)?
- [ ] Are both directions of every fallback and switch handled (history, caches, resources)?
- [ ] Is a failure message specific about what failed, and logged once?

Cost when idle
- [ ] Does the feature allocate a device, thread, queue or DLL when it is not selected?
- [ ] Does it run on vendors it cannot support? Is `gpu_vendor.h` used to gate it?
- [ ] Is initialization lazy and release symmetric?

Configuration
- [ ] Is there a single setting for each choice? Is the ini read only through `Config`?
- [ ] After a menu choice, is the saved ini consistent with the runtime state?

CI and tests
- [ ] If a dependency is optional, is its status logged, and can CI require it?
- [ ] Are the negative and default paths asserted, not only the success path?
- [ ] Is pure logic separate from I/O so it can run without a GPU?

Concurrency, lifetime and environment
- [ ] For every atomic, is there a window between two operations where another thread sees a half-updated or null state? Is there one invalidation mechanism per structure?
- [ ] If two members hold the same COM object, is the release order known, and does every early return release what it holds?
- [ ] Are refcount and cast assumptions safe under wrappers (RenderDoc, debug layer, DXVK, Proton)? Are they listed as tested or in "Not tested"?
- [ ] Is there a hard bound where memory could be overrun, independent of earlier classification?
- [ ] Does each risky assumption in the PR body appear as tested, or in "Not tested"?

PR hygiene
- [ ] Is the PR one concern? Are unrelated fixes in their own PRs?
- [ ] Does the target branch follow the repo flow (`feat/FSR4`, then `master`)?
- [ ] Are the file count, keys and behavior in the PR body correct for the final diff?
- [ ] Do comments explain why, not what, and justify any deliberate omission?
- [ ] Are vendor binaries and licensing handled as agreed in #27?

## Known open preferences (not yet decided by the owner)
- Whether the FG teardown fix and the FG toggle-off (judder) fix should be one PR or two. The owner only asked that the toggle-off fix land before `master`.
- The FidelityFX SDK license decision for release packaging.

## Sources
- PR 1 (#31) review and approval, merge comment: https://github.com/filispeen/EDPE/pull/31
- PR 2 (#35) review and approval comments: https://github.com/filispeen/EDPE/pull/35
- PR #39 review (constant buffer lifetime): https://github.com/filispeen/EDPE/pull/39
- Issue #27 scoping comment: https://github.com/filispeen/EDPE/issues/27
