# GapFill build, bug and DSP optimization audit — 2026-10-12

This is a static source review with small local standalone C++/numeric probes, NOT a verified JUCE or Windows build. Local Git clone failed because github.com DNS was unavailable. GitHub Actions preflight has failed with no runner assigned and zero executed steps.

## Confirmed build and correctness issues

1. **Actions runner scheduling**: run 38209858712 failed with no runner or executed steps; Linux/Windows jobs were skipped. Investigate GitHub account/repository Actions policies, annotations, and billing. Issue #11.
2. **Windows packager path**: the VS multi-config standalone executable was read from the wrong path. Corrected in PR #9; its archive/checksum fixture must run before release.
3. **False-positive local tests**: the test runner accepted a filter that selected zero tests, so the draft PR #8 local script's '-Combinatorial' invocation looked green despite running nothing. Fixed in this branch, with exclusion semantics and zero-test failure.
4. **Variable host block transport**: PPQ comparison advanced by the current processing chunk, rather than the preceding complete host callback, causing false jumps. Fixed in this branch with regression.
5. **Preset numeric validation**: partial numeric strings such as '1e', '1-2', and '++2' were accepted by a whitelist and permissive conversion. Fixed with locale-independent from_chars full consumption and regression.
6. **Windows validation still unverified**: the current source has not passed a native Windows compile, pluginval, clap-validator or host QA. Historical PR #7 cross-build statements are not current build evidence.

## DSP optimization inventory (priorities estimated, not benchmarked)

| Priority | Hot path | Opportunity | Status |
|---|---|---|---|
| High | FillVoice::genFilter, filter modifier | Stereo SVFs recalculate identical tan-based coefficients per sample. Copy coefficients only, retaining independent channel state. | Implemented with DSP regression; JUCE run pending |
| High | FillVoice::genDistort and distortion modifier | tanh(drive) is constant per fill, but was recalculated every sample. Cache during voice setup. | Implemented; JUCE run pending |
| High | FillVoice::genTapeStop | pow, exp and sin every sample. Consider accurate bounded recurrences or a coefficient ramp. | Candidate; needs benchmarks and golden audio checks |
| High | FillVoice::genPitch and filter sweeps | Repeated pow, tan and crossfade math in per-sample code. | Candidate; benchmark first |
| Medium | AnalysisEngine::processBlock | Main spectral bands process even outside Spectral detection mode. Assess gating versus mode-switch warmup. | Candidate; regression tests required |
| Medium | FillVoice::genTexture | Exponential envelope calculated every sample. | Candidate; preserve transient fidelity |
| Medium | FillVoice looped algorithms | Repeated fmod for monotonic positions. | Candidate; guard reverse/negative rates |
| Medium | SpectralSmear spectral processing | FFT-bin sqrt, sin, cos and smoothing. | Candidate; profile per-hop |

## Scan conclusions

The examined per-sample DSP hot paths use fixed-size buffers and do not show a direct identified heap allocation or mutex acquisition, but no real-time allocation tracer or sanitizer has yet been run. Reported speedups would be speculative until measured.
Build configuration pins JUCE 8.0.15 and clap-juce-extensions commit 55525c9858d4b25687be7759a5e0f70eccef218e. Preserve pins while reproducing the baseline.
Known functional follow-ups: non-host tempo falls back to 120 BPM; test tempo ramps, seeks/loops, unusual MIDI sequences and all parameter-state formats.

## Release gate

Restore runners; run repository preflight and packaging fixture; compile Linux/Windows tests and all plugin formats; run complete DSP/unit/combinatorial tests, strictness-10 pluginval, clap-validator, ASan/UBSan, native Windows installer, DAW and audio/MIDI host QA. Benchmark CPU by algorithm at 48, 96 and 192 kHz against audio goldens before adopting further DSP changes. Publish real Windows beta artifacts with SHA-256 checksums only after the gates pass.
