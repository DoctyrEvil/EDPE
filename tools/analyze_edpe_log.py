"""Segment an edpe.log by upscaler backend/mode and count repeated allocations per segment.

Usage: python -I tools/analyze_edpe_log.py [PATH_TO_edpe.log]

The log has no timestamps, so rates use the frame counters found in lines with "frame=N". Segments start at each
"upscaler backend changed to X" or "upscaler mode changed to Y" line. For every segment it reports line count, the
number of "allocated HUD mask target WxH" lines by size, the frame span seen in that segment, and allocations per frame.
"""
import collections
import re
import sys

DEFAULT = (r"C:\Program Files (x86)\Steam\steamapps\common\Elite Dangerous\Products"
           r"\elite-dangerous-odyssey-64\edpe.log")


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT
    segs = collections.OrderedDict()
    backend, mode = "(launch state)", ""
    cur = backend

    def seg(k):
        return segs.setdefault(k, dict(lines=0, alloc=collections.Counter(), f0=None, f1=None))

    seg(cur)
    for ln in open(path, encoding="utf-8", errors="replace"):
        m = re.search(r"upscaler backend changed to (.+)", ln)
        if m:
            backend, mode = m.group(1).strip(), ""
            cur = backend
            seg(cur)
            continue
        m = re.search(r"upscaler mode changed to (.+)", ln)
        if m:
            mode = m.group(1).strip()
            cur = f"{backend} / {mode}"
            seg(cur)
            continue
        s = seg(cur)
        s["lines"] += 1
        m = re.search(r"allocated HUD mask target (\d+x\d+)", ln)
        if m:
            s["alloc"][m.group(1)] += 1
        m = re.search(r"\bframe=(\d+)", ln)
        if m:
            fr = int(m.group(1))
            if fr < 10 ** 9:
                s["f0"] = fr if s["f0"] is None else min(s["f0"], fr)
                s["f1"] = fr if s["f1"] is None else max(s["f1"], fr)
    print(f"{'segment':34s} {'lines':>6s} {'HUD allocs':>10s}  sizes{'':28s} frames           allocs/frame")
    for k, v in segs.items():
        tot = sum(v["alloc"].values())
        fr = (v["f1"] - v["f0"]) if v["f0"] is not None else 0
        rate = f"{tot / fr:.3f}" if fr else "n/a"
        span = f"{v['f0']}..{v['f1']}" if v["f0"] is not None else "n/a"
        print(f"{k:34s} {v['lines']:6d} {tot:10d}  {dict(v['alloc'])!s:34s} {span:17s} {rate}")


if __name__ == "__main__":
    main()
