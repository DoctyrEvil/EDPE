"""Compare PresentMon runs: mean frame, GPU and CPU timing columns (ms) per file, with a GPU-bound or CPU-bound hint.

Usage: python -I tools/compare_runs.py LABEL=FILE.csv [LABEL=FILE.csv ...]   (several files per label: LABEL=a.csv,b.csv)
Skips the first 60 frames of each file.

Columns (PresentMon 2.x): FrameTime, GPUBusy (GPU actually working), GPUWait, CPUBusy, CPUWait, GPULatency.
Hint: if GPUBusy is well below FrameTime the GPU is idle part of each frame, so the frame is limited by the CPU side
(or by a present or driver wait); if GPUBusy is about FrameTime the GPU is the limit.
"""
import csv
import statistics as s
import sys

COLS = ["FrameTime", "GPUBusy", "GPUWait", "CPUBusy", "CPUWait", "GPULatency"]


def load(path):
    rows = list(csv.DictReader(open(path, encoding="utf-8-sig")))[60:]
    out = {}
    for c in COLS:
        v = [float(r[c]) for r in rows if r.get(c) not in (None, "", "NA")]
        out[c] = v
    return out


def main():
    print("| label | frames | frame ms | fps | GPU busy ms | GPU busy % | GPU wait ms | CPU busy ms | CPU wait ms | GPU latency ms | limit |")
    print("|---|---|---|---|---|---|---|---|---|---|---|")
    for arg in sys.argv[1:]:
        label, files = arg.split("=", 1)
        acc = {c: [] for c in COLS}
        for p in files.split(","):
            d = load(p)
            for c in COLS:
                acc[c] += d[c]
        m = {c: s.fmean(acc[c]) for c in COLS}
        pct = 100 * m["GPUBusy"] / m["FrameTime"]
        limit = "GPU" if pct >= 95 else "CPU/other" if pct < 85 else "mixed"
        print(f"| {label} | {len(acc['FrameTime'])} | {m['FrameTime']:.2f} | {1000 / m['FrameTime']:.1f} | {m['GPUBusy']:.2f} | {pct:.0f}% "
              f"| {m['GPUWait']:.3f} | {m['CPUBusy']:.2f} | {m['CPUWait']:.3f} | {m['GPULatency']:.1f} | {limit} |")


if __name__ == "__main__":
    main()
