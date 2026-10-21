# Golden test tolerances and cross-machine evidence

The golden scenarios (`Tests/features/TestGolden.cpp`, baselines in `Tests/golden/<platform>/`) are rendered by `renderGoldenScenario` in `Tests/support/TestRig.cpp`.

## Evidence that the Phase 0 branch equals master

`GapFillTests --dump-golden <dir>` renders scenarios A..F. Built from master `83447a7` (with this branch's render rig overlaid) and from the branch, on the same machine, the two renders are bit-identical for all six scenarios (max abs difference 0.0, identical fill counts A5 B5 C10 D6 E16 F1). `tools/ci_golden_vs_master.sh` repeats the check on a CI runner; its result is recorded below.

CI run 39576753922 (runner AMD EPYC 7763, branch `e25f5fa` vs master `83447a7`): all six scenarios max abs 0.0, RMS 0.0, identical fill counts. The temporary CI job that ran it was removed before merge; the script stays for future use.

## Cross-machine error

The checked-in Linux baselines were produced on an Intel Xeon (AVX-512). The CI runner (AMD EPYC 9V74) renders the same code with these differences from the baselines:

| Scenario | max abs | RMS |
|---|---|---|
| A drum_loop_default | 1.8e-7 | 1.6e-8 |
| B spectral_mode | 3.3e-3 | 7.9e-6 |
| C sidechain_click_track | 6.9e-7 | 2.9e-8 |
| D forced_tape_stop | 8.2e-6 | 4.5e-7 |
| E complexity_chaos_stress | 1.7e-4 | 2.6e-6 |
| F bypass_to_active | 1.2e-7 | 8.5e-9 |

B (spectral analysis) and E (chaotic selection at maximum complexity) amplify last-ulp libm/FMA differences into small waveform differences. They get max abs 1e-2 and RMS 1e-3 (about 3x to 60x margin, and room for Windows MSVC and macOS arm64). A, C, D, F keep max abs 1e-5 and RMS 1e-6.

## Machine-independent invariants (all scenarios)

Checked against the baseline in addition to the sample tolerance, so the loose scenarios still catch real regressions: overall RMS within 2 %, peak within 5 %, DC offset within 1e-3, and per-512-sample-block energy within 1 dB (where either side is above -60 dBFS), which pins where fills start and stop. Fill count must be greater than zero.

## Rule

Baselines are regenerated (`--write-golden`, PR title with `[golden]`) only when a same-runner comparison against master shows an intentional sound change, never to absorb a machine difference.
