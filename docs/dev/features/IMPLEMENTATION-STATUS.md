# Implementation status of docs/dev/features tickets

Branch: PR #25 (Phase 0 foundations F0-1..F0-10, WP3 explain/assist, WP8 voice effects) merged with master. `yes` = implemented and tested in this branch; `partial` = implemented with a stated gap; `no` = not started (spec only). Ticket IDs not in `docs/dev/features` (E-3, N-USE-b, N-EASY, N-FX1, N-FX2) are listed under the table.

| Ticket | Status | Key source files | Tests / notes |
|---|---|---|---|
| D-01-compliance-and-docs-refresh | partial | docs/COMPLIANCE.md, docs/TEST-TRACEABILITY.md, docs/architecture/MODULES.md updated per package | docs only; full refresh not done |
| F-FX0-effect-module-seam | yes | Source/features/fx/{FxChain,VoiceFxChain,EffectCard,FxHooks}.*, Source/gui/slots/EffectsSection.* | Tests/features/TestFx.cpp, TestVoiceFxCards.cpp, TestFxSection.cpp |
| F-L01-fill-lock | no | - | - |
| F-L02-fill-history-save-recipe | no | - | - |
| F-L03-more-like-that | no | - | - |
| F-L04-fill-export-daw-drag | no | - | - |
| F-L05-auto-calibrate | no | - | - |
| F-L06-performance-macros | no | - | - |
| F-L07-performance-scenes | no | - | - |
| F-L08-phrase-aware-scheduling | no | - | - |
| F-L09-groove-inheritance | no | - | - |
| F-L10-call-and-response | no | - | - |
| F-L11-midi-output | no | - | - |
| F-L12-wet-only-output-bus | no | - | - |
| F-L13-enhanced-sidechain-fills | no | - | - |
| F-L14-multiband-routing | yes | Source/features/fx/FxChain.*, EngineBlockIO.h, Source/params/bandfx.* | Tests/features/TestFx.cpp |
| F-L15-dub-feedback-network | no | - | - |
| F-L16-controlled-evolve | no | - | - |
| F-L17-mutation-lab | no | - | - |
| F-L18-manual-scale-key | no | - | - |
| F-L19-frequency-shifted-delays | no | - | - |
| F-L20-formant-transformations | no | - | - |
| F-L21-transient-convolution-reverb | no | - | - |
| F-L22-musical-energy-tracking | no | - | - |
| F-L23-recipe-ab-comparison | no | - | - |
| F-L24-controller-integration-feedback | no | - | - |
| F-L25-live-safety-recovery | no | - | - |
| F-M01-fill-audition | no | - | - |
| F-M02-why-didnt-it-fill | no | - | - |
| F-M03-visual-gap-timeline | partial | Source/features/timeline/*, Source/features/reasons/* | Tests/features/TestTimeline.cpp, TestReasons.cpp. Height growth deferred to UI-0 |
| F-M04-simple-advanced-view | no | - | - |
| F-M05-fill-intensity-envelope | no | - | - |
| F-M06-fill-personalities | no | - | - |
| F-M07-density-breathing | no | - | - |
| F-M08-capture-v2-recapture | no | - | - |
| F-M09-per-band-fill-type | no | - | - |
| F-M10-momentary-effects-and-latch | no | - | - |
| F-M11-quantized-scene-preset-changes | no | - | - |
| F-M12-preset-browser | no | - | - |
| F-M13-controlled-mistakes | no | - | - |
| F-M14-rhythmic-bitcrushing | yes | Source/features/fx/RcrushCard.*, Source/params/rcrush.* | Tests/features/TestVoiceFxCards.cpp |
| F-M15-comb-filter-resonator | no | - | - |
| F-M16-rhythmic-ratcheting | yes | Source/features/fx/RatchetCard.*, Source/params/ratchet.* | Tests/features/TestVoiceFxCards.cpp |
| F-M17-bit-perfect-wet-only-output | no | - | - |
| F-M18-beat-battles | no | - | - |
| F-M19-freeze-and-scrub | partial | Source/features/scrub/ScrubVoice.*, Source/gui/slots/ScrubStrip.cpp, Source/params/scrub.* | Tests/features/TestScrub.cpp. Standalone voice; C-10 / F-M10 integration pending |
| F-M20-tape-destruction-expansion | yes | Source/features/fx/TapeDestrCard.*, Source/params/tapedestr.* | Tests/features/TestVoiceFxCards.cpp |
| F-M21-tension-release-curves | no | - | - |
| F-M22-smart-preset-suggestions | no | - | - |
| F-M23-advanced-modulation-macro-targets | no | - | - |
| F-S01-cpu-latency-display | partial | Source/features/telemetry/*, Source/features/health/* | Tests/features/TestTelemetry.cpp, TestHealth.cpp. Options-page embedding deferred to UI-0 |
| F-S02-parameter-info-status-line | no | - | - |
| F-S03-ui-scale | no | - | - |
| F-S04-midi-note-trigger-audition | no | - | - |
| F-S05-effective-value-feedback | no | - | - |
| F-S06-ab-compare-level-matching | no | - | - |
| F-S07-stutter-storm | no | - | - |
| F-S08-glitch-roulette | no | - | - |
| F-S09-first-run-guide | no | - | - |
| F-S10-genre-performance-presets | no | - | - |
| F-S11-preset-favorites-next-previous | no | - | - |
| F-S12-safe-return-panic | partial | PluginProcessor.h panicWet(), FxChain fadeOut, Command slots | Tests/features/TestFx.cpp (panic fades). Panic UI / recover not built |
| F-X01-harmonic-granular-resynthesis | no | - | - |
| F-X02-spectral-freeze-and-morph | no | - | - |
| F-X03-automatic-key-detection | no | - | - |
| F-X04-multi-section-arrangement-awareness | no | - | - |
| F-X05-local-preference-learning | no | - | - |
| F-X06-multi-track-cooperative-gapfill | no | - | - |
| F-X07-adaptive-performance-quality-modes | no | - | - |
| P-01-free-pro-build-flag | no | - | out of scope per DECISIONS 4 (never implemented) |
| Q-01-release-qa-matrix-harness | no | - | - |
| T-01-meter-peak-hold-verification | no | - | - |
| T-02-top-bar-8px-grid-pass | no | - | - |
| W-01-windows-editor-automation-crash | no | - | - |
| W-03-variable-block-transport-prediction | partial | Source/PluginProcessor.cpp updateTempoInfo (master #12) | Tests/TestPlugin.cpp variable_host_buffers_do_not_trigger_false_transport_jumps. Full spec matrix not run |

Also landed (specified in docs/dev/12, no file in features/): F-D02 event capture/replay (`Source/features/events/*`, `TestEvents.cpp`), E-3 reasons, N-USE-b health footer, N-EASY fix-it (`Source/features/fixit/*`, `TestFixit.cpp`), N-FX1 Reverse Swell (`Source/features/swell/*`), N-FX2 Granular Cloud (`Source/features/cloud/*`). F-D02 has no features/ file so it is not counted below.

Totals over the 75 tickets in docs/dev/features: yes 5, partial 6, no 64.
