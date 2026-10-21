# Implementation status of docs/dev/features tickets

Head: integration branch with work packages WP3-WP11 merged, plus the completeness audit of 2026-10-21
(`docs/audits/2026-10-21-completeness-audit.md`, `tools/audit/completeness_audit.py`).
`yes` = implemented, wired and tested in this branch; `partial` = implemented with the stated gap; `no` = not started;
`out of scope` = deliberately not built (reason given). "Unverified" items (real DAW, hardware, Windows/macOS, audibility) are
listed in the WP progress files and are never claimed here. The mechanical audit fails when a feature spec has no row below.

| Ticket | Status | Key source files | Tests / notes |
|---|---|---|---|
| D-01-compliance-and-docs-refresh | yes | docs/COMPLIANCE.md, docs/TEST-TRACEABILITY.md, docs/architecture/MODULES.md, PROGRESS.md | refreshed by the completeness audit; keep updating per package |
| F-FX0-effect-module-seam | yes | Source/features/fx/{FxChain,VoiceFxChain,EffectCard,FxHooks}.*, Source/gui/slots/EffectsSection.* | Tests/features/TestFx.cpp, TestVoiceFxCards.cpp, TestFxSection.cpp |
| F-L01-fill-lock | yes | Source/features/lock/*, Source/params/lock.*, recipes drawer LOCK switch | TestLock.cpp; `fl01_lock` is performance state; Fix-it R8 live |
| F-L02-fill-history-save-recipe | yes | Source/features/history/*, Source/features/recipes/RecipeDrawer.cpp | TestHistory.cpp; FILE > Fill history... |
| F-L03-more-like-that | yes | Source/features/lock/LikeBias.h, recipes drawer MORE LIKE | TestLock.cpp |
| F-L04-fill-export-daw-drag | yes | Source/features/export/*, recipes drawer EXPORT / chip drag | TestExport.cpp; FILE > Export last fill...; drag into a DAW unverified |
| F-L05-auto-calibrate | yes | Source/features/calibrate/*, Options rows `calibrate/OptionsRow.cpp` | TestCalibrate.cpp; duration (4-15 s) and apply-automatically rows added by the audit |
| F-L06-performance-macros | yes | Source/features/macros/*, Source/gui/slots/MacroEditorSlot.cpp | TestMacros.cpp; per-mapping smoothing and LFO editor added by the audit |
| F-L07-performance-scenes | yes | Source/features/scenes/*, Source/gui/slots/ScenePanelSlot.cpp | TestScenes.cpp, TestSceneBank.cpp; `seed` not stored (DECISIONS 5) |
| F-L08-phrase-aware-scheduling | yes | Source/features/phrase/*, Source/features/effective/StagePhrase.cpp | TestPhrase.cpp |
| F-L09-groove-inheritance | yes | Source/features/groove/*, Source/gui/slots/GrooveStrip.cpp | TestGroove.cpp. Scramble not shifted; see docs/progress/WP7.md |
| F-L10-call-and-response | yes | Source/features/callresponse/*, Source/gui/slots/CallStrip.cpp | TestCallResponse.cpp |
| F-L11-midi-output | yes | Source/features/midiout/*, Source/params/midiout.*, Options row `midiout/OptionsRow.cpp` | TestMidiOut.cpp; CMake option GAPFILL_MIDI_OUT default OFF (DECISIONS 3); `midiOutOffsetMs` persisted in `<Perf>` by the audit; Hit/CC/velocity extras of the feature file not built (doc-12 registry decided) |
| F-L12-wet-only-output-bus | yes | Source/features/wetbus/*, PluginProcessor `makeBuses` | TestWetBus.cpp; AU declaration withheld until auval + Logic pass |
| F-L13-enhanced-sidechain-fills | yes | Source/features/scdrive/*, Source/params/scdrive.* | TestScDrive.cpp |
| F-L14-multiband-routing | yes | Source/features/fx/FxChain.*, EngineBlockIO.h, Source/params/bandfx.* | TestFx.cpp |
| F-L15-dub-feedback-network | yes | Source/features/fx/bus/DubCard.*, Source/params/dub.* | TestBusFx.cpp |
| F-L16-controlled-evolve | yes | Source/features/evolve/*, Source/features/effective/StageEvolve.cpp, SHAPE panel | TestEvolve.cpp |
| F-L17-mutation-lab | yes | Source/features/mutation/* | TestMutation.cpp; FILE > Mutation Lab...; spec F-L17 (four children), not the doc-12 eight-candidate variant |
| F-L18-manual-scale-key | yes | Source/features/scale/* (N-MUS), Source/gui/slots/ScaleStrip.cpp | TestScale.cpp; reaches resonator / formant / cloud / harmonic through `scaleMask` |
| F-L19-frequency-shifted-delays | yes | Source/features/fx/bus/FsDelayCard.*, Source/params/fsdelay.* | TestBusFx.cpp |
| F-L20-formant-transformations | partial | Source/features/fx/bus/FormantCard.*, Source/params/formant.* | TestBusFx.cpp. Built as the doc-12 BUS card (21.3 ms latency); the per-voice `fl20_*` design (Preserve, Detail, formant pool, ~10.7 ms) is not built |
| F-L21-transient-convolution-reverb | yes | Source/features/fx/bus/ConvoCard.*, Source/params/convo.* | TestBusFx.cpp; IR never saved |
| F-L22-musical-energy-tracking | yes | Source/features/context/EnergyTracker.*, Source/features/effective/StageEnergy.cpp, EnergyStrip | TestEnergy.cpp |
| F-L23-recipe-ab-comparison | yes | Source/features/abcompare/RecipeAB.*, recipes drawer SET A / SET B / A/B | TestAbCompare.cpp |
| F-L24-controller-integration-feedback | yes | Source/features/commands/ControllerProfile.*, Source/gui/slots/ControllerOptionsSlot.cpp | TestControllers.cpp; verified against a mock sink only, no hardware |
| F-L25-live-safety-recovery | yes | Source/features/panic/WetSafety.h, Source/gui/slots/LiveSafetySlot.cpp, F-D03 Auto | TestSafety.cpp; WET SAFETY switch and LAST GOOD SCENE button added by the audit; overload fallback = quality Auto (WP10) |
| F-M01-fill-audition | yes | Source/features/audition/* | TestAudition.cpp; FILE > Audition a fill |
| F-M02-why-didnt-it-fill | yes | Source/features/reasons/*, WaveformGapView | TestReasons.cpp |
| F-M03-visual-gap-timeline | partial | Source/features/timeline/* | TestTimeline.cpp. Expands over the gap view instead of growing the editor (height growth deferred) |
| F-M04-simple-advanced-view | yes | Source/features/viewmode/* | TestViewMode.cpp |
| F-M05-fill-intensity-envelope | yes | Source/features/envelope/*, SHAPE panel | TestEnvelope.cpp |
| F-M06-fill-personalities | yes | Source/features/personality/*, SHAPE panel | TestPersonality.cpp |
| F-M07-density-breathing | yes | Source/features/breathing/*, SHAPE panel | TestBreathing.cpp |
| F-M08-capture-v2-recapture | yes | Source/features/capture2/*, Source/gui/slots/CaptureSlot.cpp | TestCapture2.cpp |
| F-M09-per-band-fill-type | yes | Source/features/bandtype/*, band-lane combos | TestBandType.cpp |
| F-M10-momentary-effects-and-latch | yes | Source/features/momentary/*, Source/gui/slots/HoldPadsSlot.cpp | TestMomentary.cpp, TestHoldVoices.cpp; `fm10_stutter/freeze/reverse/tape` are automation mirrors of the pads |
| F-M11-quantized-scene-preset-changes | yes | Source/features/scenes/SceneOverlay.* (shared quantiser) | TestScenes.cpp (absorbed by F-L07, DECISIONS 5) |
| F-M12-preset-browser | yes | Source/features/presetbrowser/* | TestPresetBrowser.cpp |
| F-M13-controlled-mistakes | yes | Source/features/mistakes/*, SHAPE panel | TestMistakes.cpp |
| F-M14-rhythmic-bitcrushing | yes | Source/features/fx/RcrushCard.*, Source/params/rcrush.* | TestVoiceFxCards.cpp |
| F-M15-comb-filter-resonator | yes | Source/features/fx/bus/ResonatorCard.*, Source/params/resonator.* | TestBusFx.cpp; Scale-degrees mode follows Scale Lock |
| F-M16-rhythmic-ratcheting | yes | Source/features/fx/RatchetCard.*, Source/params/ratchet.* | TestVoiceFxCards.cpp |
| F-M17-bit-perfect-wet-only-output | partial | Source/features/wetout/*, `wetSolo`, RoutingSlot WET ONLY | TestWetOut.cpp. Preserve rule (presets, Reset, Undo, file load keep the mode) added by the audit; the Mix-panel OUTPUT selector and the footer "WET ONLY" chip of the feature file are not built (the switch is in Performance > Routing with a status line) |
| F-M18-beat-battles | yes | Source/features/battle/*, Source/gui/slots/BattleSlot.cpp | TestBattle.cpp; card with STORE A / STORE B / CLEAR added by the audit; header A/B does not feed the styles (own slots, session state) |
| F-M19-freeze-and-scrub | yes | Source/features/scrub/ScrubVoice.*, Source/gui/slots/ScrubStrip.cpp, Source/params/scrub.* | TestScrub.cpp; snapshot length follows `captureLength` (wired by the audit); standalone performance voice, not a F-M10 hold |
| F-M20-tape-destruction-expansion | yes | Source/features/fx/TapeDestrCard.*, Source/params/tapedestr.* | TestVoiceFxCards.cpp; line 128 ms vs spec 25 ms (documented) |
| F-M21-tension-release-curves | partial | Source/features/tension/*, Source/gui/slots/TensionStrip.cpp, `ContextState::tensionFxSend` | TestTension.cpp. No curve editor popover (Custom curves come from the saved `<Tension>` subtree). The FX lane now scales the effect Amounts (wired by the audit) |
| F-M22-smart-preset-suggestions | yes | Source/features/suggest/* | TestSuggest.cpp |
| F-M23-advanced-modulation-macro-targets | yes | Source/features/macros/*, MacroEditorSlot | TestMacros.cpp; per-mapping smoothing and the LFO row are now in the GUI |
| F-S01-cpu-latency-display | yes | Source/features/telemetry/*, Source/features/health/* | TestTelemetry.cpp, TestHealth.cpp; shown in the status details call-out |
| F-S02-parameter-info-status-line | yes | Source/features/info/* | TestInfo.cpp; allow-list is empty |
| F-S03-ui-scale | yes | Source/features/uiscale/* | TestUiScale.cpp |
| F-S04-midi-note-trigger-audition | yes | Source/features/audition/MidiTrigger.*, `audition/OptionsRow.cpp` | TestAudition.cpp; Options rows added by the audit (the AUDITION right-click menu stays) |
| F-S05-effective-value-feedback | yes | Source/features/effective/Effective.*, Knob effective marker | TestEffectiveDisplay.cpp |
| F-S06-ab-compare-level-matching | yes | Source/features/abcompare/MatchLevels.* | TestAbCompare.cpp; the FILE-menu "Compare" group of the feature file is the drawer A/B row |
| F-S07-stutter-storm | yes | Source/features/momentary/HoldVoices.*, HoldPadsSlot (STORM pad) | TestHoldVoices.cpp |
| F-S08-glitch-roulette | yes | Source/features/live/Roulette.* | TestRoulette.cpp |
| F-S09-first-run-guide | yes | Source/features/tour/* | TestTour.cpp |
| F-S10-genre-performance-presets | yes | Source/Presets.cpp (41 presets) | TestPresets.cpp |
| F-S11-preset-favorites-next-previous | partial | Source/features/presetbrowser/* | TestPresetBrowser.cpp. Favourites and `<` `>` stepping work; FILE > Add preset to favourites added by the audit; Ctrl/Cmd+D and the "favourites only" step scope are not built |
| F-S12-safe-return-panic | yes | Source/features/panic/Panic.*, Source/gui/slots/PanicSlot.cpp, LiveSafetySlot.cpp | TestPanic.cpp; footer chip is 22 px high (footer height), the 36 px PANIC button is in the Performance tab; DECISIONS 9 |
| F-X01-harmonic-granular-resynthesis | yes | Source/features/harmonic/*, Source/params/hgran.* | TestHarmonic.cpp; simplified (no partial tracking), not scene-recallable (128-id cap) |
| F-X02-spectral-freeze-and-morph | yes | Source/features/fx/bus/SpFreezeCard.*, Source/dsp/spectral/Stft.* | TestBusFx.cpp, TestSpectral.cpp; capture/release also by commands 24/25 (audit) |
| F-X03-automatic-key-detection | yes | Source/features/keydetect/*, Source/gui/slots/KeyStrip.cpp | TestKey.cpp; Window and Min confidence knobs added by the audit |
| F-X04-multi-section-arrangement-awareness | yes | Source/features/arrangement/*, ArrangementStrip | TestArrangement.cpp; prediction accuracy +-2 s, not +-1 frame |
| F-X05-local-preference-learning | partial | Source/features/taste/*, Source/gui/slots/TasteStrip.cpp | TestTaste.cpp. No Skip button in the History drawer (Taste strip has it); M1 +10 points not +15 |
| F-X06-multi-track-cooperative-gapfill | partial | Source/features/coop/* | TestCoop.cpp. Safe in-process subset (32 slots, three modes); rendezvous-hash slots, priority/yield, group scene recall not built |
| F-X07-adaptive-performance-quality-modes | yes | Source/features/quality/* | TestQuality.cpp (F-D03 modes + Auto) |
| P-01-free-pro-build-flag | out of scope | - | DECISIONS 4: never implemented, no edition gating anywhere |
| Q-01-release-qa-matrix-harness | no | - | QA track (`tools/qa/` absent); owned by the robustness audit / release process, not a feature |
| T-01-meter-peak-hold-verification | no | - | UI test debt: meter arithmetic still inside `LevelMeter` (PROGRESS "Next up") |
| T-02-top-bar-8px-grid-pass | no | - | needs visual review (the spec forbids doing it blind) |
| W-01-windows-editor-automation-crash | no | - | Windows-only investigation; cannot be reproduced on Linux CI |
| W-03-variable-block-transport-prediction | partial | Source/PluginProcessor.cpp updateTempoInfo (master #12) | TestPlugin.cpp variable_host_buffers_do_not_trigger_false_transport_jumps. Full spec matrix not run |

## Doc-12 tickets that have no file in `docs/dev/features`

| Ticket | Status | Where |
|---|---|---|
| E-1 / E-3 audition, reasons | yes | see F-M01 / F-M02 |
| E-2 tempo sync audit | yes | Source/features/sync/*, Options row `shaping/Gui.cpp` (fallback BPM), TestSync.cpp; `FreeTimingFallback` health input wired by the audit |
| C-0..C-11, U-1..U-10 | yes | the doc-12 / doc-04 / doc-05 ids of the rows above (C-1 = F-M06, C-2 = F-L06, C-3 = F-M13, C-4 = F-M05, C-5 = F-L01, C-6 = F-M07, C-7 = F-L08, C-8 = F-L16, C-9 = F-M09, C-10 = F-M08, C-11 = F-L03, U-1 = F-L02, U-2 = F-L04, U-3 = F-S04, U-4 = F-M12, U-5 = F-M04, U-6 = F-L05, U-7 = F-S09, U-8 = F-S03, U-9 = F-S02, U-10 = F-S10) |
| X-1 MIDI command map | yes | Source/features/commands/CommandMap.* (commands 1-30 bindable; 31/32 Skip are GUI only), TestCommandMap.cpp |
| FX-0 effect-card framework | yes | see F-FX0 |
| SP-0 shared spectral processor | yes | Source/dsp/spectral/Stft.*, TestSpectral.cpp (latency N-1) |
| UI-0 Performance tab / Effects section / status shell | yes | Source/gui/SlotHost.*, SlotPanels.*, TestUi0.cpp, TestSlots.cpp; OptionsExtra is now a scrolling Stack (audit) |
| N-FX1 Reverse Swell | yes | Source/features/swell/*, TestSwell.cpp |
| N-FX2 Granular Cloud | yes | Source/features/cloud/*, TestCloud.cpp (smoothness figure at registry defaults not met, documented) |
| N-USE-a Panic and Recover | yes | see F-S12 |
| N-USE-b Health footer | yes | Source/features/health/*, TestHealth.cpp; `FreeTimingFallback` input wired by the audit |
| N-MUS Scale Lock | yes | see F-L18 |
| N-VAL Scene Bank and Exchange | yes | Source/features/bank/BankFile.*, ScenePanelSlot (EXPORT / IMPORT / UNDO), TestSceneBank.cpp |
| N-EASY Fix-it suggestions | yes | Source/features/fixit/*, TestFixit.cpp (R1-R13; R8 and R11 live) |
| F-P02 Quantised Fill Now | yes | FillEngine `trySchedule(forced)`, LiveControl, HoldPadsSlot (FILL NOW pad), TestFillNow.cpp |
| F-D01 Fill budget / quiet zones | yes | Source/features/budget/*, TestBudget.cpp |
| F-D02 Event capture / replay | yes | Source/features/events/*, TestEvents.cpp |
| F-D03 Quality modes + Auto | yes | see F-X07 |
| F-D04 Accessibility / contrast | yes | Source/features/access/*, TestAccess.cpp |
| F-P01 / F-P03 decision ADRs | yes | docs/adr/0001-no-free-pro-split-in-1x.md, 0002-aax-auv3-ios-out-of-scope-in-1x.md (status Proposed until the maintainer accepts) |
| F-P04 Localisation | no | WP2 (not part of WP3-WP11) |

Totals over the 75 tickets in docs/dev/features: yes 62, partial 8 (F-L20, F-M03, F-M17, F-M21, F-S11, F-X05, F-X06, W-03), no 4 (Q-01, T-01, T-02, W-01), out of scope 1 (P-01). Counted by `tools/audit/completeness_audit.py`.
