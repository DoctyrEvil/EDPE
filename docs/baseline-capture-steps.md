# Baseline capture steps (Adrenalin overlay)

Companion to `ingame-baseline-plan.md`. Covers configuration A (vanilla game, no proxy, no mods). Repeat the same procedure unchanged for B, C and D later.

Some Adrenalin menu names and hotkeys vary by driver version. Where this document is not certain, it says "check". Confirm those items once in the UI before the first real run.

## Quick reference card (keep this visible while you capture)

Setup facts: Adrenalin 26.11.2 (driver 32.0.32029.2010), Resizable BAR reported enabled, 4K, Solo mode, config A = no `dxgi.dll` or other EDPE files in the game folder.

| Key | Action |
|---|---|
| **Ctrl+Shift+O** | Show or hide the metrics overlay |
| **Ctrl+Shift+L** | Start logging. Press again to stop. Same key for both. |
| **Alt+R** | Open the Adrenalin window (use before the session, not during a run) |

Hotkeys are AMD's defaults (support article DH3-038). If one does nothing, open Alt+R > Performance > Metrics and check or reassign the hotkey tile.

**Per-run sequence (60 seconds of data):**
1. Game focused, camera on the saved spot, no input. Overlay visible (Ctrl+Shift+O if hidden). Optional check of which process is using the GPU (run in a second window or before starting; it takes about 2 seconds):
   ```powershell
   $s=(Get-Counter '\GPU Engine(*engtype_3D)\Utilization Percentage' -SampleInterval 1 -MaxSamples 2).CounterSamples | ? CookedValue -gt 0.5; $s | % { [pscustomobject]@{ PID=[int]($_.InstanceName -replace '^pid_(\d+)_.*','$1'); Util=$_.CookedValue } } | Group-Object PID | % { [pscustomobject]@{ PID=$_.Name; Util=[math]::Round(($_.Group | Measure-Object Util -Sum).Sum/2,1); Proc=(Get-Process -Id $_.Name -ErrorAction SilentlyContinue).ProcessName } } | Sort-Object Util -Descending | Select -First 8 | ft -Auto
   ```
2. Wait for the 60-second settle timer (first run of a scene only).
3. Press **Ctrl+Shift+L**. Look for the logging indicator on the overlay. If it is not shown, press again and re-check.
4. Do nothing for **60 seconds**. Do not Alt-Tab, do not open menus, do not move the mouse.
5. Press **Ctrl+Shift+L**. Check the indicator is gone.
6. Do **not** rename the files. Each run makes two files in `%LOCALAPPDATA%\AMD\CN`: `FPS.Latency.<stamp>.CSV` and `Hardware.<stamp>.CSV`, where `<stamp>` looks like `20261121-131550`. Write the stamp in the run sheet next to the config, scene and run number (or append a line to `labels.csv`, see below). The parser pairs the two files by stamp.
   - **Process label caveat:** Adrenalin labels the game's frames `EDLaunch.exe` (the launcher), not `EliteDangerous64.exe`. Verified twice: at the time of the second log only `EliteDangerous64.exe` was running and the Windows GPU counter showed it at 99% of the 3D engine, and `EDLaunch.exe` was not running. So the label is Adrenalin's, and the data is the game's. Run the parser with `--process EDLaunch.exe` (or without `--process`). If the label ever changes to `EliteDangerous64.exe`, that is worth noting.
7. Repeat steps 3 to 6 for runs 2 and 3. Then move to the next scene.

If something goes wrong in a run (popup, accidental input, indicator missing): discard that run, wait 30 seconds, redo it. Write "redo" in the notes column.

Logs are in `C:\Users\<user>\AppData\Local\AMD\CN`. Newest files, in PowerShell:
```powershell
Get-ChildItem "$env:LOCALAPPDATA\AMD\CN" -File -Filter *.CSV | Sort-Object LastWriteTime -Descending | Select-Object -First 6 Name, Length, LastWriteTime
```

Summarise every run found (run from the project folder):
```powershell
python -I tools/parse_adrenalin.py --process EliteDangerous64.exe
```

Optional `labels.csv` (one line per run, stamp from the file name) gives a comparison table by config and scene:
```
stamp,config,scene,run
20261121-140112,A,S1,1
20261121-140301,A,S1,2
```
```powershell
python -I tools/parse_adrenalin.py --process EliteDangerous64.exe --labels labels.csv
```

The parser reports fps average and minimum, "p99 fps" (AMD's rolling 99th-percentile figure, a 1% low proxy), frame time, stutter rates, GPU utilization, clock, power, hotspot temperature, GPU memory and CPU utilization, and drops the first 2 seconds of each run.

## 0. Record the configuration first

Run once per configuration, with the game and settings as they will be during the test (copies the config files and records driver, OS and proxy files):
```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\snapshot_config.ps1 -Label A
```
The recorded setup for config A is in `docs/baseline-config-A.md`. Notes: display is 3840x2160 exclusive fullscreen at 240 Hz, VSync off, and `TerrainCheckerboardRenderingEnabled` is on. Items still to record by hand are listed in that file.

## 1. Pre-flight (once)

System and driver
- [ ] Close other GPU or overlay software: MSI Afterburner and RTSS, Discord overlay, Steam overlay, browser with video, recording tools.
- [ ] Windows power plan set to High performance or Ultimate. Plugged in. No other heavy background process (check Task Manager).
- [ ] Note and record: Adrenalin driver version, Windows version (Windows 10 19045), whether Hardware-accelerated GPU scheduling is on (Settings > System > Display > Graphics), and whether Resizable BAR / Smart Access Memory is on (Adrenalin > Performance > Tuning).

Adrenalin settings for this game profile (disable anything that changes frame pacing or resolution)
- [ ] Radeon Anti-Lag: off. Radeon Boost: off. Radeon Chill: off. Enhanced Sync: off. FSR / Radeon Super Resolution: off. Image Sharpening: off.
- [ ] VSync: Off (also off in game).
- [ ] Frame rate target control: off or set above the display refresh rate.

Game settings (write them down and do not change between configurations)
- [ ] 3840x2160, the same display mode (fullscreen or borderless) every time, HDR on or off the same every time.
- [ ] Same graphics preset. `SSAAMultiplier` / supersampling the same. Any in-game upscaler off for config A.
- [ ] Play in **Solo** mode to avoid other players changing the scene.

Overlay and logging
- [ ] Adrenalin > Performance > Metrics: enable the overlay and add FPS, frame time (graph if offered), GPU utilization, GPU clock, VRAM used, CPU utilization. Turn on the overlay in game (check the hotkey; commonly Ctrl+Shift+O).
- [ ] Logging hotkeys (per AMD's support article DH3-038, defaults): **Ctrl+Shift+L** starts and stops performance logging (one key toggles both), **Ctrl+Shift+O** toggles the metrics overlay. Confirm them in Adrenalin > Performance > Metrics (the hotkey tile next to each option; click it to reassign). Default log folder is under `%LOCALAPPDATA%\AMD` (confirm in the UI). Polling interval is configurable from 0.25 s to 5 s: use the shortest. Enable the option that shows an indicator on the overlay while logging, so you can see that a log is running.
- [ ] Press the logging hotkey **with the game focused**, and do not Alt-Tab during a run. Alt-Tab moving focus off the game is the likely reason capture stopped; AMD's docs do not say so, so treat it as a working theory. Start the log, wait 1 s, check the indicator, run the 60 s, press the hotkey again, check the indicator is gone.
- [ ] Check which columns the log records and the file name.
- [ ] Make a short 15-second test log and open the file. Send me the first lines of the CSV (or the file) so I can write a parser and confirm which columns exist.

## 2. Scenes (fixed positions, stationary)

Save each location so you can return to exactly the same spot and view. Use the same system, time of day and camera orientation every time.

| Scene | Where | Why |
|---|---|---|
| S1 | Deep space, ship stationary, planet or star in view, cockpit view | GPU-bound, simple geometry |
| S2 | Station concourse on foot, or ship in hangar, looking at a busy area | Draw-call and CPU heavy |
| S3 | Planetary settlement on foot, facing a fixed landmark | Heaviest Odyssey scene |
| S4 (optional) | Supercruise at fixed throttle toward a star | Motion and temporal load |

For S4, exact repeatability is hard; treat it as a rough comparison only.

## 3. Run protocol (per scene, per configuration)

1. Quit the game fully and relaunch (so shader cache and streaming state match across configurations).
2. Go to the saved location. Wait 60 seconds without input so streaming and shaders settle (warm-up, not recorded).
3. Open the overlay. Note the on-screen FPS and GPU utilization at idle camera.
4. Start the Adrenalin metrics log. Hold the camera still and give no input for **60 seconds**.
5. Stop the log. Write down the file name and the visible averages from the overlay as a cross-check.
6. Repeat steps 2 to 5 two more times without restarting the game (three runs per scene). Restart only when you change configuration.
7. Leave the log files as they are and record each run's `<stamp>` (from the file name) in the run sheet or `labels.csv`. Renaming breaks the pairing of the FPS and Hardware files.

Do the scenes in the order S1, S2, S3 in one game session, and S4 last.

## 4. Run sheet

| Config | Scene | Run | Log file | Overlay avg fps | GPU util % | Notes |
|---|---|---|---|---|---|---|
| A | S1 | 1 | | | | |
| A | S1 | 2 | | | | |
| A | S1 | 3 | | | | |
| A | S2 | 1 | | | | |
| A | S2 | 2 | | | | |
| A | S2 | 3 | | | | |
| A | S3 | 1 | | | | |
| A | S3 | 2 | | | | |
| A | S3 | 3 | | | | |

## 4b. Per-frame capture with AMD's bundled PresentMon (recommended addition)

Tested 2026-11-21: AMD's own `PresentMon-x64.exe` works without elevation (your account is in Performance Log Users), uses its own session name so it does not disturb Adrenalin's logging, labels the process correctly as `EliteDangerous64.exe`, and writes one CSV row per frame (about 136 per second here). That gives true 1% and 0.1% lows, which the Adrenalin log cannot.

60-second capture. Run in a PowerShell window; the 10-second delay is time to click back into the game (PresentMon does not care about window focus, so Alt-Tab does not stop it):
```powershell
& "C:\Program Files\AMD\CNext\CNext\PresentMon-x64.exe" --process_name EliteDangerous64.exe --output_file baseline\presentmon\A_S1_run1.csv --session_name EDBaseline --delay 10 --timed 60 --terminate_after_timed --no_console_stats --date_time
```
Change the file name for each run (`A_S1_run2.csv`, and so on). It prints "Started recording." and "Stopped recording." (plus a harmless warning about elevation). Summarise with:
```powershell
python -I tools/parse_presentmon.py baseline\presentmon\A_S1_run1.csv
```
You can run the Adrenalin log at the same time for a cross-check. The first test agreed: PresentMon averaged 136.2 fps with a 1% low of 113.2, and Adrenalin's 99th-percentile fps for the same kind of scene was 110 to 114.

## 5. Known limits of this method

- The Adrenalin metrics log is a sampled log (interval depends on the driver setting), not a per-frame capture. Averages and GPU utilization are reliable. True 1% and 0.1% lows and frame-time spikes may not be recoverable from it. If the log lacks frame time, we report average fps and GPU/CPU utilization only, and note that clearly.
- The overlay itself costs a little performance. Keep it enabled for every configuration so the comparison stays fair.
- If per-frame data is needed later, PresentMon or a similar capture tool would be the next step. Installing it needs your approval.

## 6. What to send back

- The three CSVs per scene (or the folder), the filled run sheet, and the pre-flight notes (driver, HAGS, Resizable BAR, in-game settings).
- Anything odd: stutters, a menu popup during a run, a scene that drifted.

With one sample CSV I will write a small parser that outputs average fps, utilization and VRAM per run and a comparison table. That comes before the full set.
