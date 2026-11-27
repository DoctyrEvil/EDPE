"""Summarise PresentMon per-frame CSV files (one row per presented frame).

Usage: python -I tools/parse_presentmon.py FILE.csv [FILE2.csv ...] [--skip SECONDS] [--process NAME]

Reports average fps, percentile frame times, 1% and 0.1% low fps, how much of the frame the GPU and CPU were busy,
present mode and tearing/sync flags. Columns come from PresentMon 2.x console output (as bundled with Adrenalin).

Definitions
  avg fps        1000 / mean FrameTime
  1% low fps     1000 / (99th percentile FrameTime)       0.1% low fps: same with the 99.9th percentile
  GPU busy       mean(GPUBusy) / mean(FrameTime)           close to 100% means GPU-bound
  CPU busy       mean(CPUBusy) / mean(FrameTime)           close to 100% means CPU-bound (render thread)
"""
import argparse
import csv
import statistics
from datetime import datetime


def pct(sorted_vals, p):
    if not sorted_vals:
        return None
    k = (len(sorted_vals) - 1) * p / 100.0
    lo = int(k)
    hi = min(lo + 1, len(sorted_vals) - 1)
    return sorted_vals[lo] + (sorted_vals[hi] - sorted_vals[lo]) * (k - lo)


def f(x):
    try:
        return float(x)
    except (TypeError, ValueError):
        return None


def parse_dt(s):
    # "2026-11-21 5:29:42.429996700" -> trim nanoseconds to microseconds
    try:
        head, frac = s.rsplit(".", 1)
        return datetime.strptime(f"{head}.{frac[:6]}", "%Y-%m-%d %H:%M:%S.%f")
    except Exception:
        return None


def summarise(path, skip, process):
    rows = []
    with open(path, newline="", encoding="utf-8-sig") as fh:
        for r in csv.DictReader(fh):
            if process and r.get("Application", "").lower() != process.lower():
                continue
            ft = f(r.get("FrameTime"))
            if ft is None:
                continue
            rows.append((parse_dt(r.get("CPUStartDateTime", "")), ft, r))
    if not rows:
        print(f"{path}: no rows")
        return
    t0 = rows[0][0]
    if t0 is not None and skip > 0:
        rows = [x for x in rows if x[0] is None or (x[0] - t0).total_seconds() >= skip]
    fts = [x[1] for x in rows]
    s = sorted(fts)
    dur = (rows[-1][0] - rows[0][0]).total_seconds() if rows[0][0] and rows[-1][0] else sum(fts) / 1000.0
    mean = statistics.fmean(fts)

    def col(name):
        v = [f(x[2].get(name)) for x in rows]
        return [a for a in v if a is not None]

    gpu_busy, cpu_busy = col("GPUBusy"), col("CPUBusy")
    modes = {}
    for x in rows:
        modes[x[2].get("PresentMode", "?")] = modes.get(x[2].get("PresentMode", "?"), 0) + 1
    app = rows[0][2].get("Application", "?")
    print(f"{path}")
    print(f"  {app}  frames {len(rows)}  duration {dur:.1f}s")
    print(f"  avg fps {1000 / mean:.1f}   frame time mean {mean:.2f} ms  p50 {pct(s, 50):.2f}  p95 {pct(s, 95):.2f}  "
          f"p99 {pct(s, 99):.2f}  p99.9 {pct(s, 99.9):.2f}  max {s[-1]:.2f}")
    print(f"  1% low fps {1000 / pct(s, 99):.1f}   0.1% low fps {1000 / pct(s, 99.9):.1f}   frame time stdev {statistics.pstdev(fts):.2f} ms")
    if gpu_busy and cpu_busy:
        print(f"  GPU busy {100 * statistics.fmean(gpu_busy) / mean:.0f}%   CPU busy {100 * statistics.fmean(cpu_busy) / mean:.0f}%")
    print(f"  present modes: {modes}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="+")
    ap.add_argument("--skip", type=float, default=1.0)
    ap.add_argument("--process")
    a = ap.parse_args()
    for p in a.files:
        summarise(p, a.skip, a.process)


if __name__ == "__main__":
    main()
