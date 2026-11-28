# Owner review profile (filispeen/EDPE)

Purpose: review our own changes the way the repo owner (`filispeen`) does, before they see them. Built from the owner's comments on #31 (PR 1), #35 (PR 2), #39 (two rounds) and the scoping comments on issue #27. Sources are paraphrased; links are at the end.

## How the owner reviews

- They are a careful maintainer of a long-lived, runtime-hostile codebase: a graphics hook injected into a game.
- Reviews are numbered, name exact files and functions, and propose a concrete fix. The tone is approving ("looks good", "ready to merge"), but they do not merge until every blocking point is addressed.
- Each round reads deeper than the last. PR 1 and PR 2 drew comments on config, UI text and CI. #39 drew line-level comments on atomics, ownership and memory ordering. Expect that depth on any low-level change.
- They verify a fix against every site it touches ("checks out across every write site"), so a fix applied in one place and not its siblings will be found.
- They read a helper as if it could be called from anywhere, not only from today's callers. A precondition that every caller happens to satisfy is still checked inside the helper (#39 nit: a null resource).
- They separate blocking points from "non-blocking nits, fine to leave". They accept a reasoned "no" if it is justified and documented in code (PR 1, `FreeUnusedSDKs()`; #39, the ownership `exchange`).
- They ask for evidence as questions: "is it always reset?", "have you tested it with…?". Answer with what was verified, or say it was not tested.

## Principles and checks

Each principle has the owner evidence in brief and the questions to ask of our own diff.

### 1. State is explicit, accurate and observable
- Evidence: a log naming the wrong cause (PR 1); `"unavailable"` leaking into the menu before the first frame, the same reason printed twice (PR 2); unavailable options greyed out with a reason (#27).
- [ ] Can any user-visible text show a placeholder, a stale value or a duplicate line in any reachable state (before first frame, after shutdown, unsupported GPU, missing DLL)?
- [ ] Does each failure message say which thing failed, and is it logged once?

### 2. Every transition is handled in both directions, on every path
- Evidence: fallback to FSR 3.1.2 and the return to FSR 4 both needed a history reset (PR 2). The swapchain-switch path did not clear what shutdown clears (#39 nit).
- [ ] What happens on the way back, the second time, after a failure and after a restart?
- [ ] Do all paths that reset or release the same state do the same thing: shutdown, device switch, early return, error exit? If one differs on purpose, is there a comment?

### 3. One source of truth, one rule, one atomic step
- Evidence: `Backend=4` and `Fsr4=1` could disagree; the owner removed the redundant path instead of reconciling it (PR 2). A cache slot needs one invalidation mechanism, not a flag plus a store (#39). A check followed by a separate clear can act on a changed target; use a compare-and-swap helper (#39 nit). The same predicate should apply everywhere a thing is classified, for example `requireDynamic` matching the discovery paths (#39 nit).
- [ ] Is there one setting per choice, read only through `Config`, with the ini consistent after a menu choice?
- [ ] Does each structure have one way to mark an entry invalid, and does no writer clear data another thread may already own?
- [ ] Is any check-then-act on shared state done as one atomic operation, with the old value returned for logging?
- [ ] Does every path that classifies the same object use the same test?
- [ ] Does an insert into a set-associative cache loop over all ways for a match before it claims an empty way, as the sibling insert helper does? (#39 nit: an evicted way 0 made a resource already in way 1 get inserted again.)

### 4. No cost when a feature is idle
- Evidence: an idle D3D12 device kept alive all session was rejected, including side effects such as RenderDoc captures and loading signed DLLs only to refuse them on non-AMD GPUs (PR 2).
- [ ] Does the feature allocate a device, thread, queue or DLL when it is not selected?
- [ ] Is it gated with `src/gpu_vendor.h`, initialized lazily and released symmetrically?

### 5. Defend at boundaries; prefer the robust form over the clever one
- Evidence: a hardcoded SDK version replaced by reading it from the DLL, with a fallback (PR 1). A misleading path-length error replaced by correct sizing (PR 1). `refs == 1` instead of `<= 1`, with a watchdog as the fallback if a wrapper reports another count (#39). A hard size check where memory could be overrun, independent of earlier classification (#39). A helper that called `Release` on null when a null resource met an empty target, although its only caller already checked, and the same gap in its sibling cache helpers (#39 nit).
- [ ] Do we rely on a third-party return value, refcount or cast? Is it safe under wrappers and overlays (RenderDoc, the debug layer, DXVK, Proton), and is that tested or listed as not tested?
- [ ] Is there a hard bound at the point of damage, not only an upstream check?
- [ ] Is every helper safe for its whole input domain (null, empty, zero, stale), even when all current callers check first? Does it start with the same early return as its sibling helpers?
- [ ] Can an empty or default slot compare equal to a null or default input? What does the helper then read or release? Did our own change (for example no longer clearing a field) make a stale value reachable for such an input?

### 6. Concurrency, ownership and comments that match the code
- Evidence: a store after a CAS overwrote a new owner's entry; a pointer set to null and restored was briefly invisible (#39). Two members held the same COM object, and the question was whether one is released before the other's refcount check and on every early return (#39). A comment promised "written before published" while the code used relaxed ordering (#39 nit).
- [ ] For every atomic, is there a window between two operations where another thread sees a half-updated or null state? If it stays, does a comment say why it is harmless?
- [ ] If two members hold one object, is the release order known and does every exit release what it holds?
- [ ] Does the memory ordering give the guarantee the comment claims (release and acquire, not relaxed, when one write must be seen before another)?

### 7. CI and tests can fail for the right reason, and results are stated honestly
- Evidence: a silent fall back to a stub build kept CI green, so they wanted a status line and a `require` switch (PR 2). They asked for negative and default paths to be asserted (PR 1) and praised pure logic split from I/O, such as `fsr4_logic.h`.
- [ ] If a dependency is optional, is its status logged and can CI make it mandatory?
- [ ] Are the default and negative paths asserted, with pure logic separate from GPU and I/O code?
- [ ] Does each risky assumption in the PR body appear as tested (how: game, unit test, CI) or under "Not tested"?

### 8. Small, separable PRs, honest bodies, no vendor binaries
- Evidence: the bridge and the backend were split because it "makes review much easier" (#27). Follow-ups target `feat/FSR4`, then `master` together. The FG fix may be a separate PR, but it must land before `feat/FSR4` merges (PR 2). Signed AMD DLLs are fetched at build time and never committed; release packaging waits for the SDK license check (#27).
- [ ] Is the PR one concern, with unrelated fixes (such as `upscaler_dejitter`) left out?
- [ ] Is the target branch right, and are file counts, keys and behavior in the PR body correct for the final diff?
- [ ] Do comments explain why, not what, and justify any deliberate omission?
- [ ] Are vendor binaries, packaging and labels (`AMD FSR 3.1.2`, `AMD FSR 4.1.2`) as agreed in #27?

## Replying to a review
- Address every numbered point, one commit per point, with the commit named in the reply.
- Say what was verified and how, and what was not tested and why.
- If declining, give a reproducible reason and leave an explanatory comment in the code.
- For optional nits, either take them or say so. We take nits when they are small and make the code more consistent.
- Keep the PR body in step with the final diff. Keep replies short and factual.

## Open preferences (not yet decided by the owner)
- Whether the FG teardown fix and the toggle-off judder fix should be one PR or two. The owner asked only that the toggle-off fix land before `master`.
- The FidelityFX SDK license decision for release packaging.

## Sources
- #31 (PR 1) review, approval and merge comment: https://github.com/filispeen/EDPE/pull/31
- #35 (PR 2) review and approval comments: https://github.com/filispeen/EDPE/pull/35
- #39 (constant buffer lifetime), two review rounds: https://github.com/filispeen/EDPE/pull/39
- Issue #27 scoping comment: https://github.com/filispeen/EDPE/issues/27
