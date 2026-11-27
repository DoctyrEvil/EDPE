# Reply to the owner's nits on #39 (draft, not posted)

Head: `DoctyrEvil:fsr4-cb-lifetime-fixes` (`31b86c3`, three new commits on top of `a0cc416`). Fork CI passes on `31b86c3` (33 of 33).

**Comment:**

Thanks for the review. I took all three nits, one commit each.

1. **`observedMap`** (`e1dba36`): the size check now passes `requireDynamic = true`, because every path that sets the target requires dynamic usage. It also clears the target through a new `clearSceneMapTargetIf(resource)`, a compare-and-swap, so a target installed by another thread in between is kept.
2. **`DotCbResourceSet`** (`d6f8883`): `cacheDotCbResource` now stores `ways` with `release`, and `getCachedDotCbByteWidth` loads it with `acquire`, so the comment about `bytes[w]` being written first holds for the compiler as well.
3. **Swapchain switch** (`31b86c3`): `clearSceneMapTarget()` now runs when the observed swap chain switches to the scene device, so a target found on the previous device is detected again.

`ctest` without the FG and presenter GPU tests passes except for `upscaler_dejitter`, which also fails on `master`.

I ran these commits in game in a self-test build of `31b86c3` at sharpness 0.00. Median frame times were 2.56 ms for Bypass, 3.44 ms for FSR 3.1.2, 5.80 ms for FSR 4.1.2 and 3.02 ms for TAA, in or just below the range of the earlier runs. The 60-switch stress passed, and the log has no device, FFX or exception errors. A scripted capture with FG on and off ran without problems.

One correction to my earlier comment: the self-test I described as within noise of the earlier baseline was at sharpness 0.50, and the earlier baseline had deferred sharpening that this branch does not, so that comparison was not valid. The numbers above are at sharpness 0.00.
