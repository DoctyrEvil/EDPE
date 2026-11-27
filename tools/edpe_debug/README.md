# EDPE debug module (developer tests, never part of a pull request)

In-game test module and its PowerShell driver. Full documentation: [docs/developer-test-harness.md](../../docs/developer-test-harness.md).

| Path | What |
|---|---|
| `edpe-test.ps1` | Driver: `Status`, `Build`, `Deploy`, `Collect`, `Restore`. `Get-Help .\edpe-test.ps1 -Full` lists the parameters |
| `apply_hook.py` | Copies the hook into a throwaway worktree and makes anchored edits. Fails if an anchor moved |
| `hook/` | Host side (`debug_hook`, `debug_dred`). Loads `edpe_debug.dll` only when `[Overlay] DEV=1` |
| `include/edpe_debug_api.h` | Versioned C ABI between the hook and the module |
| `module/` | `edpe_debug.dll`: self-test, switch stress, scripted FG capture, report writer, "EDPE Developer Tests" menu |
| `analyze_capture.py` | Prints the per-phase table for a capture folder |

```powershell
.\tools\edpe_debug\edpe-test.ps1 Status
.\tools\edpe_debug\edpe-test.ps1 Deploy -Ref <branch> -AutoRun -AutoExit
.\tools\edpe_debug\edpe-test.ps1 Collect -Scene "main menu"
.\tools\edpe_debug\edpe-test.ps1 Restore
```
