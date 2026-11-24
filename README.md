# EDPE FSR 4.1.2 test results (summary tables only)

Condensed results from the FSR 4.1.2 work, kept so the summary graphs can be reproduced for quick comparison.
Raw captures, logs and crash dumps are not stored here.

- `baseline/results.csv`: one row per self-test result (frame time, p99, GPU timer, build, scene).
- `docs/SUMMARY.md`: the written results summary.
- `docs/selftest_trend.svg`: the trend plot across self-test runs.
- `tools/plot_selftest_trend.py`: regenerates the plot from `baseline/results.csv` (no dependencies):
  `python tools/plot_selftest_trend.py`
