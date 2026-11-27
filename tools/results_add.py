"""Running collection of every measurement: baseline/results.csv (append only, one row per data point).

    python tools/results_add.py import-selftest baseline/selftest/*/edpe-selftest-*.json
    python tools/results_add.py add --method presentmon --scene "planet landed" --build X --option "..." \
        --mode 4 --sharpness 0 --frame-ms 8.47 --fps 118.0 --gpu-busy-pct 98 --runs 1 --source-file docs/SUMMARY.md
    python tools/results_add.py list [--scene SUBSTRING]

Columns: date, method (presentmon | selftest), scene, build, option, mode, sharpness, render, frame_ms, fps, p99_ms,
low1pct_fps, gpu_busy_pct, gpu_timer_ms, runs, source_file, notes. Empty means not recorded. Rows are never edited by
this tool; imports skip rows already present (same source_file and option), so they can be re-run safely.
Frame times from "selftest" are present to present inside the process, not PresentMon, and are not comparable with
"presentmon" rows. The selftest GPU timer is not valid for Bypass.
"""
import argparse
import csv
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CSV = ROOT / "baseline" / "results.csv"
COLS = ["date", "method", "scene", "build", "option", "mode", "sharpness", "render", "frame_ms", "fps", "p99_ms",
        "low1pct_fps", "gpu_busy_pct", "gpu_timer_ms", "runs", "source_file", "notes"]


def read_rows():
    if not CSV.exists():
        return []
    with CSV.open(newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def append(rows):
    new = not CSV.exists()
    CSV.parent.mkdir(parents=True, exist_ok=True)
    with CSV.open("a", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=COLS, lineterminator="\n")
        if new:
            w.writeheader()
        for r in rows:
            w.writerow({c: r.get(c, "") for c in COLS})


def key(r):
    return (r.get("source_file", ""), r.get("option", ""))


def import_selftest(paths, scene):
    have = {key(r) for r in read_rows()}
    out = []
    for p in paths:
        p = Path(p)
        d = json.loads(p.read_text(encoding="utf-8"))
        full = p.resolve()
        rel = full.relative_to(ROOT).as_posix() if full.is_relative_to(ROOT) else p.as_posix()
        render = ""
        if d.get("renderWidth"):
            render = "%sx%s->%sx%s" % (d["renderWidth"], d["renderHeight"], d["outputWidth"], d["outputHeight"])
        base = dict(date=d.get("started", ""), method="selftest", scene=scene, build=d.get("build", ""),
                    mode=d.get("mode", ""), sharpness="%.2f" % d.get("sharpness", 0), render=render, source_file=rel)
        if "summary" in d:  # per-option summary over the rounds
            for s in d["summary"]:
                spread = s["maxMedianMs"] - s["minMedianMs"]
                notes = "mean of %d block medians; spread %.2f ms" % (s["blocks"], spread)
                if s["option"].startswith("Bypass"):
                    gpu = ""
                    notes += "; GPU timer not valid in bypass"
                else:
                    gpu = "%.2f" % s["meanGpuMs"]
                out.append(dict(base, option=s["option"], frame_ms="%.2f" % s["meanMedianMs"],
                                p99_ms="%.2f" % s["meanP99Ms"], gpu_timer_ms=gpu, runs=s["blocks"], notes=notes))
            for ph in d.get("phases", []):
                if ph["name"].startswith(("switch stress", "settle")):
                    out.append(dict(base, option=ph["name"], frame_ms="%.2f" % ph["medianMs"],
                                    p99_ms="%.2f" % ph["p99Ms"], gpu_timer_ms="%.2f" % ph["gpuMedianMs"], runs=1,
                                    notes="%s; max %.2f ms; switches %d" % (ph["outcome"], ph["maxMs"], ph["switches"])))
        else:  # first self-test format: one block per phase, no summary
            for ph in d.get("phases", []):
                out.append(dict(base, option=ph["name"], frame_ms="%.2f" % ph["medianMs"], p99_ms="%.2f" % ph["p99Ms"],
                                gpu_timer_ms="%.2f" % ph["gpuMedianMs"], runs=1,
                                notes="%s; max %.2f ms; blocks run in sequence, no rounds" % (ph["outcome"], ph["maxMs"])))
    fresh = []
    for r in out:
        if key(r) in have:
            continue
        have.add(key(r))
        fresh.append(r)
    append(fresh)
    print("added %d rows, skipped %d already present" % (len(fresh), len(out) - len(fresh)))


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("import-selftest")
    s.add_argument("files", nargs="+")
    s.add_argument("--scene", default="main menu")
    a = sub.add_parser("add")
    for c in COLS:
        a.add_argument("--" + c.replace("_", "-"), dest=c)
    li = sub.add_parser("list")
    li.add_argument("--scene")
    args = ap.parse_args()
    if args.cmd == "import-selftest":
        import_selftest(args.files, args.scene)
    elif args.cmd == "add":
        row = {c: (getattr(args, c) or "") for c in COLS}
        if key(row) in {key(r) for r in read_rows()}:
            sys.exit("row with this source_file and option already exists")
        append([row])
        print("added 1 row")
    else:
        for r in read_rows():
            if args.scene and args.scene.lower() not in r["scene"].lower():
                continue
            print("%-19s %-10s %-14s %-36s mode=%-2s sharp=%-4s %6s ms %6s fps  %s" % (
                r["date"][:19], r["method"], r["scene"][:14], r["option"][:36], r["mode"], r["sharpness"],
                r["frame_ms"], r["fps"], r["notes"][:50]))


if __name__ == "__main__":
    main()
