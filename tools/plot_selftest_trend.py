"""Plot the self-test results in baseline/results.csv across runs, as a dependency-free SVG.

    python tools/plot_selftest_trend.py [output.svg]      # default: docs/selftest_trend.svg

One x position per self-test run, in time order, labelled with the build. Three panels: median frame time, p99 frame
time and the in-process GPU timer, one line per upscaler option. Only main menu rows of the four measured options are
plotted (the landed scene is not comparable). Runs limited by the display rate (VSync) are flagged in CAPPED.
"""
import csv
import sys
from pathlib import Path

root = Path(__file__).resolve().parent.parent
out = Path(sys.argv[1]) if len(sys.argv) > 1 else root / "docs" / "selftest_trend.svg"

OPTIONS = [
    ("Bypass (no reconstruction)", "Bypass", "#7f7f7f"),
    ("Native TAA", "Native TAA", "#2ca02c"),
    ("AMD FSR 3.1.2", "AMD FSR 3.1.2", "#1f77b4"),
    ("AMD FSR 4.1.2", "AMD FSR 4.1.2", "#d62728"),
]
# Run timestamps (results.csv "date") whose frame times are capped by the display rate, not by the renderer.
CAPPED = {"2026-11-24 11:53:22": "full screen, VSync on"}
PANELS = [("frame_ms", "Median frame time (ms)", False), ("p99_ms", "p99 frame time (ms)", False),
          ("gpu_timer_ms", "In-process GPU timer (ms)", True)]


def num(x):
    try:
        return float(x)
    except (TypeError, ValueError):
        return None


rows = [r for r in csv.DictReader(open(root / "baseline" / "results.csv", newline=""))
        if r["method"] == "selftest" and r["scene"] == "main menu"]
runs = sorted({(r["date"], r["build"]) for r in rows})
data = {(r["date"], r["option"]): r for r in rows}

W, H = 980, 1010
left, right, top = 70, 20, 60
pw = W - left - right
ph = 230
gap = 70
parts = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" font-family="Segoe UI, Arial, sans-serif" font-size="12">',
         f'<rect width="{W}" height="{H}" fill="#ffffff"/>',
         f'<text x="{left}" y="24" font-size="16" font-weight="600" fill="#222">Self-test trend by run (main menu, Performance, 1920x1080 to 3840x2160)</text>']
lx = left
for _, label, color in OPTIONS:
    parts.append(f'<line x1="{lx}" y1="42" x2="{lx + 22}" y2="42" stroke="{color}" stroke-width="2.5"/>'
                 f'<circle cx="{lx + 11}" cy="42" r="3.5" fill="{color}"/>'
                 f'<text x="{lx + 28}" y="46" fill="#333">{label}</text>')
    lx += 28 + 8 * len(label) + 24

n = len(runs)
xs = [left + (pw * (i + 0.5) / n) for i in range(n)]
for p, (col, title, gpu) in enumerate(PANELS):
    y0 = top + 20 + p * (ph + gap)
    vals = [num(data[(d, o)][col]) for d, _ in runs for o, _, _ in OPTIONS if (d, o) in data and num(data[(d, o)][col]) is not None]
    vmax = max(vals) * 1.1
    vmax = max(1.0, round(vmax + 0.49))
    sy = lambda v: y0 + ph - ph * v / vmax
    parts.append(f'<text x="{left}" y="{y0 - 8}" font-weight="600" fill="#222">{title}</text>')
    for t in range(int(vmax) + 1):
        parts.append(f'<line x1="{left}" y1="{sy(t):.1f}" x2="{W - right}" y2="{sy(t):.1f}" stroke="#e5e5e5"/>'
                     f'<text x="{left - 8}" y="{sy(t) + 4:.1f}" text-anchor="end" fill="#555">{t}</text>')
    for i, (d, _) in enumerate(runs):
        if d in CAPPED:
            w = pw / n
            parts.append(f'<rect x="{xs[i] - w / 2:.1f}" y="{y0}" width="{w:.1f}" height="{ph}" fill="#f4e3b0" opacity="0.55"/>')
            if p == 0:
                parts.append(f'<text x="{xs[i]:.1f}" y="{y0 + 14}" text-anchor="middle" fill="#7a5b00" font-size="11">{CAPPED[d]}</text>')
    for o, _, color in OPTIONS:
        pts = [(xs[i], sy(num(data[(d, o)][col]))) for i, (d, _) in enumerate(runs)
               if (d, o) in data and num(data[(d, o)][col]) is not None and not (gpu and o.startswith("Bypass"))]
        if len(pts) > 1:
            parts.append('<polyline fill="none" stroke="%s" stroke-width="2" points="%s"/>' % (color, " ".join("%.1f,%.1f" % q for q in pts)))
        for x, y in pts:
            parts.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="3.5" fill="{color}"/>')
    parts.append(f'<rect x="{left}" y="{y0}" width="{pw}" height="{ph}" fill="none" stroke="#999"/>')
    for i, (d, b) in enumerate(runs):
        label = b.split("@", 1)[1] if "@" in b else b
        parts.append(f'<text transform="translate({xs[i]:.1f},{y0 + ph + 12}) rotate(35)" fill="#444" font-size="11">{d[5:16]}  {label}</text>')

parts.append("</svg>")
out.write_text("\n".join(parts), encoding="utf-8")
print("wrote", out, f"({n} runs)")
