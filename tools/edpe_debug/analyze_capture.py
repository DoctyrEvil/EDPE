"""Join a debug-module capture (events.csv) to its PresentMon CSV and print one table per phase.

    python tools/analyze_capture.py <capture folder>              # a folder made by the module: edpe-capture-<stamp>-fsr4
    python tools/analyze_capture.py <capture folder> --timeline   # also print the present-mode run-length timeline

The module writes events.csv with the QPC time in ms (the same clock PresentMon prints with --qpc_time_ms), so each
PresentMon row lands in exactly one phase. No wall-clock guessing and no hand timing.

Per phase: frame generation state, present modes, rendered frames per second, frame time median / p99 / max, displayed
frame time median / p99, cadence (stdev of the displayed frame time over its mean: a judder indicator, 0 is perfectly
even), median display latency, and when inside the phase the present mode last changed.
"""
import csv
import statistics as st
import sys
from collections import Counter
from pathlib import Path


def num(x):
    try:
        return float(x)
    except (TypeError, ValueError):
        return None


def pct(values, p):
    if not values:
        return 0.0
    s = sorted(values)
    return s[max(0, min(len(s) - 1, int(p * len(s)) - 1))]


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if not args:
        sys.exit(__doc__)
    folder = Path(args[0])
    show_timeline = "--timeline" in sys.argv

    events = list(csv.DictReader(open(folder / "events.csv", encoding="utf-8-sig")))
    pm_rows = list(csv.DictReader(open(folder / "presentmon.csv", encoding="utf-8-sig")))
    if not pm_rows:
        sys.exit("presentmon.csv has no rows")
    qcol = next((c for c in pm_rows[0] if "QPC" in c.upper()), None)
    if not qcol:
        sys.exit("no QPC column in presentmon.csv; columns: %s" % list(pm_rows[0].keys()))

    starts = [(float(e["qpc_ms"]), e["detail"]) for e in events if e["event"] == "phase_start"]
    end = next((float(e["qpc_ms"]) for e in events if e["event"] == "capture_end"), None)
    if not starts or end is None:
        sys.exit("events.csv has no complete phase list (capture did not finish?)")
    markers = [e for e in events if e["event"] == "marker"]

    bounds = []
    for i, (t, detail) in enumerate(starts):
        t_end = starts[i + 1][0] if i + 1 < len(starts) else end
        name = detail.split("name=")[1].split()[0] if "name=" in detail else "?"
        bounds.append((t, t_end, name))

    print(folder.name)
    first = next((e for e in events if e["event"] == "capture_start"), None)
    if first:
        print("start:", first["local_time"], first["detail"])
    print()
    hdr = "%-3s %-9s %5s %-34s %6s %6s %6s %6s %7s %7s %6s %6s %8s %5s %7s %s" % (
        "ph", "fg", "secs", "present modes", "fps", "ft_med", "ft_p99", "ft_max", "dsp_med", "dsp_p99", "cadCV",
        "lat", "fgActive", "menu", "unshown", "last mode change")
    print(hdr)
    print("-" * len(hdr))

    for idx, (t0, t1, name) in enumerate(bounds):
        rs = [r for r in pm_rows if num(r[qcol]) is not None and t0 <= num(r[qcol]) < t1]
        secs = (t1 - t0) / 1000.0
        if not rs:
            print("%-3d %-9s %5.0f (no PresentMon rows in this phase)" % (idx, name, secs))
            continue
        ft = [num(r["FrameTime"]) for r in rs if num(r["FrameTime"])]
        dt = [num(r["DisplayedTime"]) for r in rs if num(r["DisplayedTime"])]
        unshown = sum(1 for r in rs if num(r["DisplayedTime"]) is None) / len(rs)  # frames DWM never put on screen
        lat = [num(r["DisplayLatency"]) for r in rs if num(r["DisplayLatency"])]
        modes = Counter(r["PresentMode"] for r in rs)
        modes_text = ", ".join("%s:%d%%" % (m.replace("Hardware: ", "HW ").replace("Composed: ", "Comp ")[:22],
                                            round(100 * c / len(rs))) for m, c in modes.most_common(2))
        cad = (st.pstdev(dt) / st.mean(dt)) if len(dt) > 5 else 0.0
        # when did the present mode last change inside this phase (seconds from phase start)
        last_change = ""
        prev = None
        for r in rs:
            if prev is not None and r["PresentMode"] != prev:
                last_change = "%+.1fs" % ((num(r[qcol]) - t0) / 1000.0)
            prev = r["PresentMode"]
        in_phase = [m for m in markers if t0 <= float(m["qpc_ms"]) < t1]
        fg_active = (sum(int(m["fg_active"]) for m in in_phase) / len(in_phase)) if in_phase else 0.0
        menu_open = (sum(int(m.get("menu_open", 0) or 0) for m in in_phase) / len(in_phase)) if in_phase else 0.0
        print("%-3d %-9s %5.0f %-34s %6.0f %6.2f %6.2f %6.1f %7.2f %7.2f %6.3f %6.1f %7.0f%% %4.0f%% %6.0f%% %s" % (
            idx, name, secs, modes_text, len(rs) / secs, st.median(ft) if ft else 0, pct(ft, 0.99),
            max(ft) if ft else 0, st.median(dt) if dt else 0, pct(dt, 0.99), cad, st.median(lat) if lat else 0,
            100 * fg_active, 100 * menu_open, 100 * unshown, last_change))

    if show_timeline:
        print("\npresent-mode timeline (seconds from capture start):")
        t_base = starts[0][0]
        run_mode, run_start, run_n = None, 0.0, 0
        for r in pm_rows:
            t = num(r[qcol])
            if t is None or t < t_base or t >= end:
                continue
            if r["PresentMode"] != run_mode:
                if run_mode is not None:
                    print("  %6.1fs  %-34s %d frames" % ((run_start - t_base) / 1000.0, run_mode, run_n))
                run_mode, run_start, run_n = r["PresentMode"], t, 0
            run_n += 1
        if run_mode is not None:
            print("  %6.1fs  %-34s %d frames" % ((run_start - t_base) / 1000.0, run_mode, run_n))


if __name__ == "__main__":
    main()
