# EDPE test results backup

Raw and summarised results from FSR 4.1.2, frame generation and constant buffer testing for filispeen/EDPE (PRs #35, #39, #40, #41), taken on a Radeon RX 7900 XTX, Windows 10.

- `baseline/results.csv`: one row per self-test block.
- `baseline/selftest/`: self-test reports, with the logs and ini files of each run.
- `baseline/fgcheck/`, `baseline/fgjudder/`, `baseline/fgcapture/`, `baseline/presentmon/`: frame generation checks and PresentMon captures.
- `baseline/crashes/`: crash and DRED logs. Minidumps are not included.
- `baseline/evidence/`, `baseline/config-*`: earlier baselines and configuration comparisons.

- `docs/`: PR drafts, owner review profile and analysis notes.
- `tools/`: capture, deploy and self-test scripts. The local user name in paths is replaced with `<user>`.

This branch is a backup. It has no EDPE source and an unrelated history.
