# Feature matrix (owner request of 2026-11-07)

Machine-readable copy: `feature-matrix.json` (same rows). Status: Missing / Partial / Implemented (code + automated
tests in this branch) / Verified (also checked in a real host or by listening, with recorded evidence). Nothing below
is Verified: no DAW, listening or Windows/macOS interactive evidence exists in `docs/qa/` yet. Completion % = share of
the row's acceptance checks that pass in automated tests; it is not a release claim.

Branch: `claude/zealous-gauss-7b4omr` (based on #36 head `1c94ce4`). Per-ticket history of the earlier features:
`docs/dev/features/IMPLEMENTATION-STATUS.md`.

| Feature | Status | Completion | Priority | Owner | Tests | PR | Blocker | Next action |
|---|---|---|---|---|---|---|---|---|
| A. Core engine (init/shutdown, buffers, SR/block independence, mono/stereo, smoothing, transport, state, presets, automation, denormals, bypass) | Implemented (pre-existing) | 90 % | Critical | core | Passing locally (full suite) | #32/#36 | Real-host validation | FIN-8 human acceptance pack (doc 13) |
| A1. Cross-machine float determinism (FMA contraction) | Implemented | 100 % of the 4 CI failures | Critical | core | Repro + v3 build run (see SESSION-NOTES) | this PR | none | confirm on hosted CI |
| B. Glitch/stutter tab (GLITCH) | Implemented | 100 % of F-G01 checks 2, 4 | High | glitch | TestGlitchDsp | this PR | listening | host + listening test |
| C. Swing and timing | Implemented | 100 % | High | glitch | grid + gator swing tests | this PR | listening | host test with a groove |
| D. Microgator | Implemented | 100 % | High | glitch | 4 rates x 2 blocks, pattern/depth/stereo | this PR | listening | - |
| E. Phase and pan inversion | Implemented | 100 % | Medium | glitch | polarity, swap, mono fold, flip glide | this PR | - | - |
| F. HP/LP auto-sweep | Implemented | 100 % | High | glitch | responses at 4 rates, LFO range | this PR | listening | - |
| G. Phaser | Implemented | 100 % | Medium | glitch | notch position, feedback stability | this PR | listening | - |
| H. Reverse processing | Implemented | 100 % | High | glitch | sample-exact reversed segment, prob 0 identity | this PR | listening | - |
| I. STAB macro with infinite operation | Implemented | 100 % | High | glitch | finite/infinite/retrigger, zero allocations over 10 s | this PR | listening | MIDI-learn STAB in a host |
| J. GLITCH tab UI (labels, tooltips, layout, tab/menu reach) | Implemented | 80 % | Medium | gui | panel layout + editor reach tests | this PR | T-02 screenshots, screen reader | screenshots at 100/150/200 % |
| K. Previously tracked partial tickets (F-L20, F-M03, F-X05, F-X06, Q-01, T-01, T-02, W-03, W-01) | Partial / No | see status file | Medium | various | see status file | #36 | owner decisions (doc 13), hosts | per doc 13 / SESSION-NOTES P3 |
| Release artifacts 0.0.3 / 1.0 RC | Missing | 0 % | High | release | - | - | CI green on #36 + this PR; owner publish approval | FIN-7 packaging |
