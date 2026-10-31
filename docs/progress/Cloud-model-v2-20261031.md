# N-FX2 model 2 — 2026-10-31

Model 1 retains the original scheduler, Hann/Tukey choice, fixed overlap gain and RNG draw order. Saved ModelVersions.cloud selects 1/2; old states without metadata select 1. Density/Size and their registry defaults (20 grains/s, 60 ms) do not change.

Model 2 uses stratified ±5% onset jitter (same one random draw), 75%-plateau Tukey windows at every size, and actual squared-window/amp overlap normalization. Its window gain 0.5 / sqrt(max(0.0625, energy)) is bounded at 2. To handle sparse source material as well as grain-window holes, it estimates source-span RMS once per voice and the actual windowed grain RMS at each spawn, with 256 fixed stratified probes each. Stratification prevents periodic-tone aliasing on a uniform probe grid; no extra RNG is consumed. Grain source gain is clamped 0.25..4 separately from window gain. Nonfinite or silent grains (below max(1e-5, 1% of source RMS)) are skipped, never amplified. No output compressor is used. Size, position, pitch, pan and amplitude random draws retain their existing distributions.

The fixed 256-probe bound is independent of sample rate and grain length: 256 stereo probes at first use plus 256 per spawn, at most 80 spawns/s under registry limits. Processing remains bounded by 48 grains, with no allocation, locks or I/O. Reprepare/version metadata routing is owned by the processor; settings are latched at fill start.

Native Linux x86_64 GCC Release, pinned JUCE 8.0.15 objects reused. All relevant current project sources (including FillVoice, Cloud, voice FX, SpectralSmear, Formant and fixtures) were compiled together; no old project ABI objects were linked. Three committed component/musical tests pass:

- Canonical drum-loop source at 150 BPM/3 bars, 20 seeds × 4 anchors (0/100/200/300 ms offsets), identical registry macro defaults on both paths (Intensity 50, Complexity 40, Chaos 25), Cloud controls at actual defaults. Ten-ms stereo RMS envelope coefficient of variation: plain Granular 1.285701, model 2 Cloud 0.997722: **22.40% lower**, strict ≥20% assertion passes. No anchors or seeds are discarded.
- Historical single-seed white-noise fixture through actual FillVoice: plain 0.112850, model 2 Cloud 0.045479, satisfying the strict default-setting assertion. The previous dense-only assertion was replaced, not relaxed.
- Deterministic white-noise history across 20 seeds: legacy Cloud CV 0.560414, model 2 CV 0.048778. Both versions remain finite, deterministic, bounded in grain count and allocation-free, including first-use energy estimation.

An independent original-source comparison compiled CloudMode from commit 1432394 alongside final model 1 and compared 1,920,000 float samples exactly across 20 seeds, pitch 100/600, density 20/80, sizes 10/60/250 (including legacy Tukey), chaos 0.3: identical.

Retained failure history: window-only normalization initially reduced white-noise modulation but failed 20 seeds × 4 drum anchors (plain 1.285701 vs Cloud 1.307336, 1.68% worse). Bounded per-grain source levelling corrected that specific gap. Source energy is estimated, so this metric is not a promise of constant loudness or absence of silence on arbitrary source audio.

Whole-plugin state routing, all pre-existing Cloud regressions, cross-platform native results, CPU delta under a quiet reference workload and listening/GUI review still require the integrated candidate. Scratch driver/build logs are under /workspace/work/runtime-probes; all asserted musical scenarios live in committed tests.
