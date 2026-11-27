"""Summarise AMD Adrenalin metrics logs (FPS.Latency.<stamp>.CSV + Hardware.<stamp>.CSV).

Usage:
  python -I tools/parse_adrenalin.py [--dir DIR] [--process NAME] [--skip SECONDS] [--labels labels.csv]

  --dir      folder with the logs (default: %LOCALAPPDATA%\\AMD\\CN)
  --process  only use rows for this process, e.g. EliteDangerous64.exe (default: every process, shown separately)
  --skip     seconds to drop at the start of each run (default 2)
  --labels   optional CSV with header  stamp,config,scene,run  (stamp = the 20261121-131550 part of the file name).
             When given, only labelled runs are reported and a per-config/scene comparison table is printed.

Notes on the data (from a real log):
  * FPS.Latency rows come every ~250 ms but the FPS columns update about once per second (values repeat),
    so every row is one sample of a rolling one-second window.
  * "99TH% FPS" is AMD's rolling 99th-percentile figure; its mean is reported as "p99 fps" (a 1% low proxy).
  * Hardware rows are about once per second. The first data row (timestamp N/A) is AMD's own session average.
  * "GPU MEM UTIL" is total dedicated VRAM in use, in MB (checked against the Windows
    "GPU Adapter Memory" counter: 12.85 GB there vs about 12,900 here), for all processes, not just the game.
  * Adrenalin labelled the game's frames "EDLaunch.exe" even though only EliteDangerous64.exe was running
    and using the GPU (verified with the Windows "GPU Engine" counter), so use --process EDLaunch.exe or omit --process.
"""
import argparse
import csv
import glob
import os
import re
import statistics
from datetime import datetime

TS = "%Y-%m-%d %H:%M:%S.%f"


def num(s):
    try:
        return float(s)
    except (TypeError, ValueError):
        return None


def ts(s):
    try:
        return datetime.strptime(s, TS)
    except (TypeError, ValueError):
        return None


def mean(v):
    v = [x for x in v if x is not None]
    return statistics.fmean(v) if v else None


def fmt(x, d=1):
    return "n/a" if x is None else f"{x:.{d}f}"


def read_fps(path):
    rows = []
    with open(path, newline="", encoding="utf-8-sig") as f:
        for r in csv.DictReader(f):
            t = ts(r.get("TIME STAMP"))
            if t is None:
                continue
            rows.append({
                "t": t, "proc": r.get("PROCESS", ""), "api": r.get("GRAPHICS API", ""),
                "fps": num(r.get("FPS")), "ft": num(r.get("AVG FRAME TIME")),
                "p99": num(r.get("99th% FPS")), "micro": num(r.get("MICRO STUTTER")),
                "heavy": num(r.get("HEAVY STUTTER RATE")), "fglag": num(r.get("FRAME GEN LAG")),
            })
    return rows


def read_hw(path):
    rows, session_avg = [], None
    if not path or not os.path.exists(path):
        return rows, session_avg
    with open(path, newline="", encoding="utf-8-sig") as f:
        for r in csv.DictReader(f):
            t = ts(r.get("TIME STAMP"))
            vals = {k: num(v) for k, v in r.items() if k != "TIME STAMP"}
            if t is None:
                session_avg = vals if session_avg is None else session_avg
                continue
            vals["t"] = t
            rows.append(vals)
    return rows, session_avg


def summarise(stamp, proc, fps_rows, hw_rows, skip):
    rows = [r for r in fps_rows if r["fps"] is not None]
    if not rows:
        return None
    t0 = rows[0]["t"].timestamp() + skip
    rows = [r for r in rows if r["t"].timestamp() >= t0]
    if not rows:
        return None
    tmin, tmax = rows[0]["t"], rows[-1]["t"]
    hw = [h for h in hw_rows if tmin <= h["t"] <= tmax]
    fps = [r["fps"] for r in rows]
    s = {
        "stamp": stamp, "proc": proc, "api": rows[0]["api"],
        "secs": (tmax - tmin).total_seconds(), "n": len(rows),
        "fps": mean(fps), "fps_min": min(fps), "ft": mean([r["ft"] for r in rows]),
        "p99": mean([r["p99"] for r in rows]),
        "micro": mean([r["micro"] for r in rows]), "heavy": mean([r["heavy"] for r in rows]),
        "gpu": mean([h.get("GPU UTIL") for h in hw]), "sclk": mean([h.get("GPU SCLK") for h in hw]),
        "pwr": mean([h.get("GPU BRD PWR") for h in hw]), "mem": mean([h.get("GPU MEM UTIL") for h in hw]),
        "cpu": mean([h.get("CPU UTIL") for h in hw]), "hotspot": mean([h.get("GPU HOTSPOT TEMP") for h in hw]),
    }
    return s


def print_run(s):
    print(f"{s['stamp']}  {s['proc']} ({s['api']})  {s['secs']:.0f}s, {s['n']} samples")
    print(f"   fps avg {fmt(s['fps'])}  min {fmt(s['fps_min'])}  p99 fps {fmt(s['p99'])}  frame time {fmt(s['ft'], 2)} ms")
    print(f"   micro-stutter {fmt(s['micro'], 2)}  heavy-stutter {fmt(s['heavy'], 2)}")
    print(f"   GPU util {fmt(s['gpu'])}%  clock {fmt(s['sclk'], 0)} MHz  power {fmt(s['pwr'], 0)} W  hotspot {fmt(s['hotspot'], 0)} C  mem {fmt(s['mem'], 0)}  CPU util {fmt(s['cpu'])}%")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", default=os.path.join(os.environ.get("LOCALAPPDATA", "."), "AMD", "CN"))
    ap.add_argument("--process")
    ap.add_argument("--skip", type=float, default=2.0)
    ap.add_argument("--labels")
    a = ap.parse_args()

    labels = {}
    if a.labels:
        with open(a.labels, newline="", encoding="utf-8-sig") as f:
            for r in csv.DictReader(f):
                labels[r["stamp"].strip()] = (r["config"].strip(), r["scene"].strip(), r["run"].strip())

    runs = []
    for p in sorted(glob.glob(os.path.join(a.dir, "FPS.Latency.*.CSV"))):
        m = re.search(r"FPS\.Latency\.(\d{8}-\d{6})\.CSV$", p, re.I)
        if not m:
            continue
        stamp = m.group(1)
        if labels and stamp not in labels:
            continue
        hw_path = os.path.join(a.dir, f"Hardware.{stamp}.CSV")
        fps_rows = read_fps(p)
        hw_rows, avg = read_hw(hw_path)
        procs = sorted({r["proc"] for r in fps_rows})
        if a.process:
            procs = [x for x in procs if x.lower() == a.process.lower()]
        for proc in procs:
            s = summarise(stamp, proc, [r for r in fps_rows if r["proc"] == proc], hw_rows, a.skip)
            if s:
                s["label"] = labels.get(stamp)
                runs.append(s)

    if not runs:
        print("no matching runs found")
        return
    for s in runs:
        print_run(s)
        if s["label"]:
            print(f"   label: config {s['label'][0]}, scene {s['label'][1]}, run {s['label'][2]}")

    if labels:
        groups = {}
        for s in runs:
            groups.setdefault((s["label"][0], s["label"][1]), []).append(s)
        print("\n| config | scene | runs | fps avg | p99 fps | frame ms | GPU % | power W | CPU % |")
        print("|---|---|---|---|---|---|---|---|---|")
        for (c, sc), g in sorted(groups.items(), key=lambda kv: (kv[0][1], kv[0][0])):
            print(f"| {c} | {sc} | {len(g)} | {fmt(mean([x['fps'] for x in g]))} | {fmt(mean([x['p99'] for x in g]))} "
                  f"| {fmt(mean([x['ft'] for x in g]), 2)} | {fmt(mean([x['gpu'] for x in g]))} "
                  f"| {fmt(mean([x['pwr'] for x in g]), 0)} | {fmt(mean([x['cpu'] for x in g]))} |")


if __name__ == "__main__":
    main()
