# Detailed remainder reconciliation —2026-10-31

This reopens the historical75-ticket register against the governing detailed
specifications and actual integration source through `3e06362`. The old register
is a discovery index, not an acceptance oracle. Source-presence, implementation,
integration, native verification and external acceptance remain different states.
Coordinator-owned Capture/HG/Tape edits in progress are tracked separately; an
absent hook in an uncommitted adapter is not listed as a newly discovered defect.
No old parameter ID, enum normalization, saved active behavior or recipe byte may
be changed to close the gaps below. Add bounded opt-in/versioned capabilities.

## Immediate correctness and shared contracts

1. **TAIL —host tail is under-reported.** `PluginProcessor.h:68` returns2.5s.
   Legacy `DubCard.h` permits2–16s (default8), `FsDelayCard.h`4s and
   `SpFreezeCard.h`4.5s; `BusCardBase.h` adds a250ms cap fade. Current
   `FxChain.cpp` runs these serially. Detailed L14§3.1/L15§3.4/L19§7 and
   DECISIONS16 require active-stage reporting. Approved next slice: pure tail
   prediction over an atomic parameter snapshot and published prepared caps;
   conservative legacy serial sum, new parallel-stage max, baseline2.5s when
   allFXoff. Never read audio-owned `isIdle()` from the host query. No audio/PDC
   change. Test pre-first-block automation, all active combinations, configured
   16s Dub, off ramps, mono, rates and concurrent host queries.
2. **EVENT —recording cannot reproduce the host callback/configuration.**
   `ExplainState.cpp` feeds the first internal chunk length to the recorder;
   `PluginProcessor.cpp` splits a host callback by preparedBlockSize.
   `Replay.cpp` clamps recorded BlockSize back to preparedBlockSize. Thus a
   prepared512/host8192 callback is recorded/replayed as512. `EventRecorder::Chunk`
   carries only transportValid; `ReplayPlayHead` maps invalid toStopped, losing
   playing-withoutPPQ/invalid-tempo distinctions. ModelVersions and other
   subtree-only changes while recording have no typed event. Add versioned full
   callback length/transport validity/configuration records, or explicitly stop
   and mark unsupported transitions incomplete; never silently claim replay.
   Acceptance:8192+changing32..8192 callback sequence, playing/noPPQ/tempo changes,
   a model upgrade midrecord, supported state transitions and honest divergence.
3. **STATE-GRAPH —whole-state thread ownership is not proved by the Lock fix.**
   Lock now has a private restore gate/bounded atomic capture, but scene/macro/
   other model vectors remain message-owned. Host/background get/setState can
   intersect that graph. Preserve the private sound/metadata snapshot strategy
   and audit every nonparameter serializer/application path; test controlled
   interleavings and TSan. Do not call whole-state concurrency accepted based on
   one mutex/cache. Coordinator retains this shared ownership.
4. **CORE-CALLBACK —known active-parameter callback-phase defect remains open.**
   Immutable652 scene diagnostics found PCM differences with the same absolute
   parameter boundary but restarted host callback phase, even with FsDelay off;
   isolated FsDelay is byte-identical. The strict co-op constant-parameter M1/M3
   matrix passes and does not disprove it. Diagnose the real history/analysis/
   smoothing cause; no global32-sample workaround or weakened PCM criterion.

## Unimplemented or incomplete detailed production capabilities

Each row is an executable package. Its named source is direct evidence of the
current limitation. New group registration and shared hot hooks remain root-owned.

| Package / governing source | Actual source gap | Bounded implementation and acceptance |
| --- | --- | --- |
| CV —F-L21§1–7 | `fx/bus/ConvoCard.h:13–16,33` is zero-latency JUCE convolution, max0.5s, passes dry withoutIR. Worker polling and `queueIr` are asynchronous; no nonRealtime context. Only cvAmount/Size/Damp exist. | Add opt-in detailed engine beside legacyCV: deterministic fallbackIR, Last/Strongest/Manual/Hold, isolated candidate gates,0.8s/96partitions, pre-delay/decay/stereo,50ms generations/swap, deterministic offline preparation, bounded4096 copy and teardown. Contract in F-L21-EXTENSION-CONTRACT-20261031.md. Keep direct-convolution error≤1e-6, swap step≤-50dBFS/time≤1.6×, output≤1.5, CPU3%/high-rate target. |
| BANDS —F-L14§1–4 | `FxChain.cpp:16–26,150–173` is a permanent serial chain. `bandLowFx/MidFx/HighFx` bypass voiceFX; they do not split the finished wet bus into independently routed sends. | New default-off LR4 compensated split, linked/independent crossovers, three -48..+12dB levels and Direct/Dub/Shift/Reverb routes, parallel sends and tail host. Flat sum±.05dB, stopband≥23dB, route/level step<-60dBFS, CPU1.5%; off exact. Preserve released serial mode. |
| DELAYS —F-L15§3–4/F-L19§3–4 | Dub/FsDelay params offer synced division only. `DubCard.cpp` LFO runs from its free phase; hard feedback caps replace delay-dependent6s bound. FsDelay uses its legacy FIR method/ranges. No free-time control or detailed tail proof. | Add opt-in detailed stage controls/free-time, PPQ-locked motion, proven maxFeedback(delay,6s), guard filtering and exact direction/tuning. Preserve old active cards. Measure all6s decay grids,60snoise, low-frequency sideband≥40dB/alias≥25dB, tempo/seek, host tails and neutral legacy. |
| ENERGY —F-L22§1–4 | `StageEnergy.cpp` adds density+.15Low/-.15High (opposite the detailed density goal), no style weights. `energyAdapt/Target` cannot express FollowStyle. | New neutral followDensity/followStyle path using continuous index and family table. Preserve oldadapt path. M3High/Low fillcount≥1.5; M4rhythmicshare+20points;8sneutral/ladder/flicker/zero-weight/no-RNG/CPU. |
| QUALITY —F-X07§1–4 | `quality/Quality.h` hard85%/3s thresholds; no target or benchmark. Pending tier after1s forces fading existing voices. Caps differ from detailed per-fill table. | Versioned opt-in governor/target40–90%, bounded background benchmark/cache, per-fill latched caps without changing running voices; keep existing manual normalized choices and legacyAuto. M3at80% steps1.5–2.5s, M6runningvoice exact, noisy5min/recovery20–25s, CPU85%/65%, offlineAutoHigh. Coordinate Safety ladder separately. |
| CRUSH —F-M14§1–4 | `RcrushCard.h` has one fixed8step hold pattern, integral holdcounter, noAA/dither, separate probability or protected clean edges. | New stage with Constant/Gate/Down/Up/Random, fractional rate, AA/dither, independentprob/mix and3ms/end clean fades. Keep rc* untouched. Test canonicalSNR, fractionalhold, absolutegrid, seek, AA≥30dB, clean first1/last5ms, allrates/AllocGuard. |
| RESONATOR —F-M15§1–4 | `fx/bus/ResonatorCard.h` is a tail-bearing busNote/Scale effect. No pervoiceFreeHz/Note/Key, no shared tuning of Algo::Comb/skip-double-processing. | Add default-off pervoice fractional tuning30–4000Hz/Note/Key and same-settingComb path, end atfilldeadline. ≤5c over7frequencies×4rates×damping, finite≤4, 10snoise stability, mono, offidentity. Reuse Scale resolver; no duplicate key state. |
| FREEZER —F-X02§1–4 | Existing `fx/bus/SpFreezeCard` is a bus capture/morph effect with sfz* controls. It is not the detailed replacement of an individual Spectral fill. | New Off/Freeze/Morph fill path with Earlier/Brighter/Darker targets, instantaneous-frequency/peak locking, decay/shimmer, exclusive shared spectral claim, unchanged oldsfz path. M1<1c, M2±1dB, M3width1.5×, M4second-difference1.2×, M5decay±10%, M6target±1.5dB, M7CPU1%/p99.9start35%,≤400KiB. |
| DUEL —F-L23§1–4 | `abcompare/RecipeAB.{h,cpp}` manually alternates two historical recipes, learns RMS after hearing them; no generated Variation/Contrast, same-source snapshot, pre-match/Pick/Undo. | Add bounded independently seeded candidate generation/immutable shared-source audition, shared K-weighted loudness estimate, Variation/Contrast, Pick→Lock+History once/Undo, stale cancellation and commands. Natural RNG/history unchanged; corpus±1.5/2.5dB, switch1block/5msfade, worker lifecycle. |
| SAFETY —F-L25§1–4 | `panic/WetSafety.h` is an optional stateless -1dB soft limiter. `LiveSafetySlot.cpp` LastGood only recalls lastScene; QualityAuto substitutes for an independently timed load ladder. No Safety subtree/modes/healthy snapshots. | Add versioned guard Runaway/Wet/Output, fault panic+reset/1Hzlimit, independent nonRealtime-disabled overload ladder and settled LastGood ring/exactUndo. Preserve oldwetSafety behavior. Test≤25mspanic/dryexact, huge/NaN≤+12dB/recover50ms, ladder.5s/4s,1000bypass toggles, LastGood conditions/skips. |
| LIKE —F-L03§1–4 | `lock/LikeBias.h` hardcodes4starts/x2.5/x1.4; nostrength/count/KeepTiming. | Tail controls with defaults reproducing those constants, deterministic retaineddivision/length onlywhenKeepTiming. 1000gapbias2–3.2×,≥90%division,±35%length, zero-weight/drawcount/Lock precedence. |
| PHRASE —F-L08§1–4 | `params/phrase.*` hasbars/bias/offset only, noBarBias. | Neutral perbar bias, lastbeat up to1.5×, combines defined multiplicatively with phrase;50seeds/2×phrase lift, zero biasratio±10%, invalidmeterneutral/exactblock/seek. |
| SIDECHAIN —F-L13§1–4 | `params/scdrive.cpp` onlySnap/Gate/Threshold. NoHP/LP/trim/channel/WeightedHybrid/alignwindow/timeout controls; existing Snap uses coarse analysis timing. | Separate neutral sidechain preprocessing/weighted confidence, exact32hop align-window queue, timeout default3s. Preserve existingSnap/Gate. Syntheticrumble/click efficacy and≥95%eligible alignswithin32samples, allneutral exact/alias-safeWet bus/no allocation. |
| EVOLVE —F-L16§1–4 | `evolve/Evolve.h` fixes.6pull/25%transition; noHold/Commit or newcontrols. | Strength/transition defaults reproduceoldC8; runtimeHold and oneUndoCommit tobase, nootherparamwrites.10000randomPPQ oracle, heldtransportjump/meter semantics, CommitUndo exact, scenes/Like/macros precedence. |
| SCALE —F-L18§1–4 | `params/scale.cpp`13choices have noLocrian/Chromatic; noStrict/Loose orscope. ExistingScaleLock uses its doc12 interval model. | Add explicit opt-in detailed constraints/scope and combined UI mapping without appending oldchoicecounts. Fitness rule, exactsource-PC hook, all14scale×12rootsComb≤5c,10000slideendintervals, zeros/RNG/off exact. Preserve root-relative internal scale mask contract at adapters. |
| BAND-PHASE —F-M09§1–3 | `features/bandtype` hasHelp only; noCompensatedBandSplitter/VoiceSetup selector. DECISIONS14 protects old output but does not implement the specified new forced-lane path. | Add opt-in compensated splitter for forced-band voices, preserve oldAuto and oldenabledstates.20pairs including1.5ratio flat±.1dB/finitephase, exactforcedlane/fallback/neededms, allocation/block matrix. Reuse for BANDS. |
| MISTAKES —F-M13§1–4 | `mistakes/Mistakes.h` enum ends atStereoBounce5 and params expose onlyamount. | AppendFlam/Slip without renumbering; typesmask defaultold5 and variation0 preserveold decisions. Keep16Brecipe; existingmistakeType supports≤15.100000protectedboundary cases, probability20±2%, click≤1.5×+.02, drawisolation/allrates. |
| SCRUB —F-M19§1–4 | `scrub/ScrubVoice.h` is scrubOn-triggered granularloop with synchronoussnapshotcopy, fixedsmooth30ms/speed2×. NoScan-still-silent mode, boundedCopying state, command-only engagement, fullcontrols/timeout. | Newopt-in Hold/Scan copy-state voice (≤4096copy/chunk), privateimmutable capture,8×Hermite/AA Scan andequalpowerHold, dedicatedsmooth/grain/level/follow/latch/timeout. OldscrubOn semantics kept. ≤100msengage192k, stillsilence, exactspeed, command/state-load safety, Capture integration, RT/TSan. |
| STORM —F-S07§1–4 | `HoldVoices` has a fixedStorm pattern; noRate/Style/Quant controls orautomationgate. HoldController::engageAt exists but specprofile/quantplanner is unused. | Sharedholdvoice profile/engageAt adapter; nosecondhold/duck implementation. Fourstyles/fiverates/quantOff..bar, sampleexactgrid20–300BPM/32..8192, earlyreleasecancel, restoreforcesgateoff, timeout/Panic. |
| ROULETTE —F-S08§1–4 | `live/Roulette.cpp:9–17` pushesUndo and calls permanentSurprise writes. No temporaryoverlay/duration/quantize/autoRestore. | Add separate requested temporarySpin mode; do not silently change oldSpin semantics.12profiledenylist, seededselection/no-repeat,1/2/4/8baroverlay/transportlessseconds, exactRestore/emptyUndo, cancelPanic/Scene/Preset. |
| PRESET-QUANT —F-M11§1–4 | Scenequantizer exists, but `PresetNavigation.cpp` immediatelycallsloadPreset; noquantGrid/preference/pendingpresetstate. | Sharefixed-boundary overlay for prepared preset snapshots, exactaudible boundary thenmessagecommit/oneUndo; preserveImmediate/hostsetCurrentProgram.32..8192×1000cases, stop/noPPQ/unknownmeter fallbacks,120saudiotimeexpiry, supersession/Panic/cancel. |
| AB-MODEL/MATCH —F-S06§1–4 | HeaderAB nowprocessor-owned but onlyruntimeValueTrees/noABsubtree/undoCopy. `MatchLevels.h` wetRMS±6dB canboost and needs3s; detailedspec requires K-weighted input/output ratio, attenuationonly0..-12dB after1.5s. | VersionedABsnapshot state andundoCopy, oldabsence safe, sharedloudness module withDUEL; explicit matcher version preservesoldenabledsound. ≤1dBcorpus, neverboost, coherentA/Bmetadata/latencyexclusions/clickdip/rapid100toggles/backgroundserialization. |
| EXPORT —F-L04§1–7 | Exportownerconfirmed2chonly,24PCM only/noformat-pref/metadata, sharedtempfolder/no16cap/24hcleanup, lostdeferreddrag intent, unsupported/failure silence, no nonRealtimeguard. Currentchooser canobserve mutable completedbuffer. | Finishowner's racefix plusmono/float32/preferences/BWF-INFO, immutablecompletedjob, perinstancetempRAIIcap/cleanup, asyncdeferreddrag/feedback, offline refusal. ExactwetTap segment≤1LSB/0offset, callbackzeroIO, cancel/rearm/teardown/failure tests. RealDAWdrag remains external aftersoftware complete. |
| PERSONALITY —F-M06§2 | `TestPersonality.cpp:269–279` replaces35/40%absolutefloors with1.25×relative lift. Productiontable remainsold. | Preserveoldmodel; calibrate/define supported eligiblegap corpus and add reviewednewmodel meetingallcanonicalfamilyhistograms500seeds/distinctness>.15. Do not weakenabsolute targets or claimarbitraryshortgaps canfit everyfamily. Blind5listener preference remains external. |
| BREATH —F-M07§1/4 | `breathing/Breathing.h` anchorsPPQ0; params haveenable/bars/depth/curve only. | Add neutral0 baroffset0–7; retainedoldIDs/choices; randomaccessPPQ/loop tests andcorrectUI/status. Existingbreathingengine otherwise retained. |
| RATCHET —F-M16§3/4 | `RatchetCard.cpp` replacesvoice andsilencesremainingfill; no2ms return-to-clean/free-time/level control/shared2bufferpool. | Opt-in detailedratchet besidelegacy, returnedcleanvoice afterroll, correctexponentialaccel/freefallback/level/shortfillskip, poolclaimrelease2. Exactonsets/min8ms/end/levelbounds/AllocGuard. |
| INFO —F-S02/F-S05 | `StatusBoard.cpp:78` printsname/value/effectivearrowonly; no range/choicecount/inactivereason. `EffectiveCreative` has20floats+valid, no timestamp/maxFill/reason. Publishprecedesbypassfill0. | SmallUI/published-POD extension: correctpost-override publication, timestamp/stale500ms, effectiveMaxFill andreason IDs, focus/hoverrange/inactivereasons. No audio-thread strings/APVTSwrites; samechunkexactness, focus<100ms/rate20Hz, idlepaintidentity. |
| MIDI-AUDITION —F-S04§1–3 | `MidiTrigger::scan` returnsbool+velocityonly; `RecipeServices.cpp:11–12` requestsPreview beforeallchunks. `samplePosition` is discarded. | Versionedbinding/timing path with latchedlastmatchingoffset (lastvelocity) andengine requestAt seam.1000randomoffsets32..8192 start0sampleerror, wrongnote/channel10000fuzz, stale/held/bypass. Preserveoldstates' timing unlessexplicitversionedupgrade. |
| REASONS —F-M02§1–3 | `reasons/Reasons.cpp` and RejectReason have noBypassed; `FillEvent` lacksprobPct; ReasonInfo hasproseadvice butnoactionparameterID. | AppendBypassed withoutmovingoldvalues, carryactualrollprobability andtypedactionIDs through telemetry/timeline/status. BypassmustnotreportFillOff; exactavail/needed/probabilityequalsdecision, allreasonscovered, actionablefocus/tabpath, zeroaudiochange. OptionalFillTruncated remains explicitlynon-v1. |
| FX-CONTRACT —F-FX0§1/3 | `VoiceFxChain::start` canselectall3legacycards; amountsemanticspercard, no uniformmax2decision/history mask. Detailedpervoice chainorder conflicts withpermanentdoc12order. | Implementnewinsertpath's independentlyseededprobability/max2mask in additiveversionedmetadata; preserveoldchainorder/amountsound. Reconcilecanonicalorderingonce; neversteal16Bflags. Conformancebatteryeverynewmodule/Historyeffectnames. |

## Already active implementation owners and closed recorded deviations

- Root: HG opt-in production adapters, Tape source/drain/protection, Capture
  detail/Scan/generation/overwrite guard, fixed-deadline scene boundaries and hot
  integrations. HG core42native tests plus strict four-rateM4 pass; actual new
  full-pluginM5 remains pending. NewmetadataBypassed preserves detailedplain at0send.
- State owner: sceneV2/fullscope, MacroV3/Battle/exact clocks and model adapters.
  GrooveV2 now moves Scramble slices (`FillVoice.cpp` newboundary code); the old
  WP7 "Scramble not shifted" deviation is closed for the newmodel. Arrangement
  remains deliberately outside canonicalscene scope, not a missingScene tag.
- Runtime owner: pervoiceformant/previewclaimfix, Tape fullstage/limits, CloudV2
  defaults, TasteV2 calibration. Tape strict0.6% four-instance CPU remains open;
  source/metadata/adapters require integrated verification. No independent claim
  of forced exact3/3 Tasteexplainability from a weaker supporteditemspass.
- MIDI owner: ProgramChange targetprecedence, command31/32, MIDIextras, Wetbus,
  RecipeExtras and fullF-L24 mapping/feedbacksoftware. Devices/hosts remain open.
- UI owner: Capture/exportlifecycle, timeline/meters/layout and physicalrender
  verification. Exportsoftware extras above are realimplementation, nothostonly.
- Release owner: i18n catalog/placeholder/fallback, QA/CI/perf/soak/evidence/status.
  ExternalWindows/macOS/DAW/hardware/listening/signing/longelapsedsoaks never pass
  by sourcepresence or a shortnative run. OnlyFreePro/AAX/AUv3/iOS stay excluded.

The shared parameter context menu also lacks the detailed F-L17 "Pin for Mutation
Lab" action even though the Lab's separate pin list exists. Add the message-thread
menu adapter without changing mutation RNG or preview state. F-S01's dynamic host
tail diagnostic must follow TAIL; its current fixed2.5s prose becomes inaccurate.

The older WP progress files are immutable historical checkpoints where appropriate;
append current dispositions and update the implementation register instead of
using their historical `yes`/"later" prose to waive the packages above.

## All75-ticket coverage inventory

The following is a coverage/disposition index, not75acceptance passes. "Existing"
means no additional production omission was established in the inspected primary
path; its detailed positive/negative/neutral/state/integration and external gates
still require final-candidate evidence. A package label points to the concrete
remaining behavior above; an active owner label does not imply verified completion.

| Ticket | Actual production disposition / remaining package | Primary source index |
| --- | --- | --- |
| [D-01-compliance-and-docs-refresh](../dev/features/D-01-compliance-and-docs-refresh.md) | Release owner / update historical register and exact provenance | docs/COMPLIANCE.md, docs/TEST-TRACEABILITY.md, docs/architecture/MODULES.md, PROGRESS.md |
| [F-FX0-effect-module-seam](../dev/features/F-FX0-effect-module-seam.md) | FX-CONTRACT | Source/features/fx/{FxChain,VoiceFxChain,EffectCard,FxHooks}.*, Source/gui/slots/EffectsSection.* |
| [F-L01-fill-lock](../dev/features/F-L01-fill-lock.md) | Existing primary implementation; final detailed acceptance/evidence remains required | Source/features/lock/*, Source/params/lock.*, recipes drawer LOCK switch |
| [F-L02-fill-history-save-recipe](../dev/features/F-L02-fill-history-save-recipe.md) | Existing primary implementation; final detailed acceptance/evidence remains required | Source/features/history/*, Source/features/recipes/RecipeDrawer.cpp |
| [F-L03-more-like-that](../dev/features/F-L03-more-like-that.md) | LIKE | Source/features/lock/LikeBias.h, recipes drawer MORE LIKE |
| [F-L04-fill-export-daw-drag](../dev/features/F-L04-fill-export-daw-drag.md) | EXPORT / UI owner | Source/features/export/*, recipes drawer EXPORT / chip drag |
| [F-L05-auto-calibrate](../dev/features/F-L05-auto-calibrate.md) | Existing primary implementation; final detailed acceptance/evidence remains required | Source/features/calibrate/*, Options rows `calibrate/OptionsRow.cpp` |
| [F-L06-performance-macros](../dev/features/F-L06-performance-macros.md) | Existing primary implementation; final detailed acceptance/evidence remains required | Source/features/macros/*, Source/gui/slots/MacroEditorSlot.cpp |
| [F-L07-performance-scenes](../dev/features/F-L07-performance-scenes.md) | State owner / current scene boundary verification | Source/features/scenes/*, Source/gui/slots/ScenePanelSlot.cpp |
| [F-L08-phrase-aware-scheduling](../dev/features/F-L08-phrase-aware-scheduling.md) | PHRASE | Source/features/phrase/*, Source/features/effective/StagePhrase.cpp |
| [F-L09-groove-inheritance](../dev/features/F-L09-groove-inheritance.md) | State owner GrooveV2 / native final-candidate evidence | Source/features/groove/*, Source/gui/slots/GrooveStrip.cpp |
| [F-L10-call-and-response](../dev/features/F-L10-call-and-response.md) | Existing primary implementation; final detailed acceptance/evidence remains required | Source/features/callresponse/*, Source/gui/slots/CallStrip.cpp |
| [F-L11-midi-output](../dev/features/F-L11-midi-output.md) | MIDI owner / ON+OFF native configurations | Source/features/midiout/*, Source/params/midiout.*, Options row `midiout/OptionsRow.cpp` |
| [F-L12-wet-only-output-bus](../dev/features/F-L12-wet-only-output-bus.md) | MIDI/routing owner / AU external declaration gate | Source/features/wetbus/*, PluginProcessor `makeBuses` |
| [F-L13-enhanced-sidechain-fills](../dev/features/F-L13-enhanced-sidechain-fills.md) | SIDECHAIN | Source/features/scdrive/*, Source/params/scdrive.* |
| [F-L14-multiband-routing](../dev/features/F-L14-multiband-routing.md) | BANDS | Source/features/fx/FxChain.*, EngineBlockIO.h, Source/params/bandfx.* |
| [F-L15-dub-feedback-network](../dev/features/F-L15-dub-feedback-network.md) | DELAYS + TAIL | Source/features/fx/bus/DubCard.*, Source/params/dub.* |
| [F-L16-controlled-evolve](../dev/features/F-L16-controlled-evolve.md) | EVOLVE | Source/features/evolve/*, Source/features/effective/StageEvolve.cpp, SHAPE panel |
| [F-L17-mutation-lab](../dev/features/F-L17-mutation-lab.md) | Existing engine; right-click Pin for Mutation not in shared parameter menu | Source/features/mutation/* |
| [F-L18-manual-scale-key](../dev/features/F-L18-manual-scale-key.md) | SCALE | Source/features/scale/* (N-MUS), Source/gui/slots/ScaleStrip.cpp |
| [F-L19-frequency-shifted-delays](../dev/features/F-L19-frequency-shifted-delays.md) | DELAYS + TAIL | Source/features/fx/bus/FsDelayCard.*, Source/params/fsdelay.* |
| [F-L20-formant-transformations](../dev/features/F-L20-formant-transformations.md) | Runtime/root formant integration + reference CPU | Source/features/fx/bus/FormantCard.*, Source/params/formant.* |
| [F-L21-transient-convolution-reverb](../dev/features/F-L21-transient-convolution-reverb.md) | CV + TAIL | Source/features/fx/bus/ConvoCard.*, Source/params/convo.* |
| [F-L22-musical-energy-tracking](../dev/features/F-L22-musical-energy-tracking.md) | ENERGY | Source/features/context/EnergyTracker.*, Source/features/effective/StageEnergy.cpp, EnergyStrip |
| [F-L23-recipe-ab-comparison](../dev/features/F-L23-recipe-ab-comparison.md) | DUEL | Source/features/abcompare/RecipeAB.*, recipes drawer SET A / SET B / A/B |
| [F-L24-controller-integration-feedback](../dev/features/F-L24-controller-integration-feedback.md) | Standards owner full software + hardware acceptance | Source/features/commands/ControllerProfile.*, Source/gui/slots/ControllerOptionsSlot.cpp |
| [F-L25-live-safety-recovery](../dev/features/F-L25-live-safety-recovery.md) | SAFETY | Source/features/panic/WetSafety.h, Source/gui/slots/LiveSafetySlot.cpp, F-D03 Auto |
| [F-M01-fill-audition](../dev/features/F-M01-fill-audition.md) | Existing primary implementation; final detailed acceptance/evidence remains required | Source/features/audition/* |
| [F-M02-why-didnt-it-fill](../dev/features/F-M02-why-didnt-it-fill.md) | REASONS | Source/features/reasons/*, WaveformGapView |
| [F-M03-visual-gap-timeline](../dev/features/F-M03-visual-gap-timeline.md) | UI owner / timeline final-candidate runtime and visual matrix | Source/features/timeline/* |
| [F-M04-simple-advanced-view](../dev/features/F-M04-simple-advanced-view.md) | Existing primary implementation; final detailed acceptance/evidence remains required | Source/features/viewmode/* |
| [F-M05-fill-intensity-envelope](../dev/features/F-M05-fill-intensity-envelope.md) | Existing primary implementation; final detailed acceptance/evidence remains required | Source/features/envelope/*, SHAPE panel |
| [F-M06-fill-personalities](../dev/features/F-M06-fill-personalities.md) | PERSONALITY | Source/features/personality/*, SHAPE panel |
| [F-M07-density-breathing](../dev/features/F-M07-density-breathing.md) | BREATH | Source/features/breathing/*, SHAPE panel |
| [F-M08-capture-v2-recapture](../dev/features/F-M08-capture-v2-recapture.md) | Root/UI owner Capture detail + Scan + router | Source/features/capture2/*, Source/gui/slots/CaptureSlot.cpp |
| [F-M09-per-band-fill-type](../dev/features/F-M09-per-band-fill-type.md) | BAND-PHASE | Source/features/bandtype/*, band-lane combos |
| [F-M10-momentary-effects-and-latch](../dev/features/F-M10-momentary-effects-and-latch.md) | Existing primary implementation; final detailed acceptance/evidence remains required | Source/features/momentary/*, Source/gui/slots/HoldPadsSlot.cpp |
| [F-M11-quantized-scene-preset-changes](../dev/features/F-M11-quantized-scene-preset-changes.md) | PRESET-QUANT | Source/features/scenes/SceneOverlay.* (shared quantiser) |
| [F-M12-preset-browser](../dev/features/F-M12-preset-browser.md) | Existing primary implementation; final detailed acceptance/evidence remains required | Source/features/presetbrowser/* |
| [F-M13-controlled-mistakes](../dev/features/F-M13-controlled-mistakes.md) | MISTAKES | Source/features/mistakes/*, SHAPE panel |
| [F-M14-rhythmic-bitcrushing](../dev/features/F-M14-rhythmic-bitcrushing.md) | CRUSH | Source/features/fx/RcrushCard.*, Source/params/rcrush.* |
| [F-M15-comb-filter-resonator](../dev/features/F-M15-comb-filter-resonator.md) | RESONATOR | Source/features/fx/bus/ResonatorCard.*, Source/params/resonator.* |
| [F-M16-rhythmic-ratcheting](../dev/features/F-M16-rhythmic-ratcheting.md) | RATCHET | Source/features/fx/RatchetCard.*, Source/params/ratchet.* |
| [F-M17-bit-perfect-wet-only-output](../dev/features/F-M17-bit-perfect-wet-only-output.md) | Existing primary implementation; final detailed acceptance/evidence remains required | Source/features/wetout/*, `wetSolo`, RoutingSlot WET ONLY, `MixPanel` OUTPUT selector, PanicSlot WET ONLY chip |
| [F-M18-beat-battles](../dev/features/F-M18-beat-battles.md) | State owner BattleV2 | Source/features/battle/*, Source/gui/slots/BattleSlot.cpp |
| [F-M19-freeze-and-scrub](../dev/features/F-M19-freeze-and-scrub.md) | SCRUB | Source/features/scrub/ScrubVoice.*, Source/gui/slots/ScrubStrip.cpp, Source/params/scrub.* |
| [F-M20-tape-destruction-expansion](../dev/features/F-M20-tape-destruction-expansion.md) | Runtime/root Tape opt-in; strict CPU open | Source/features/fx/TapeDestrCard.*, Source/params/tapedestr.* |
| [F-M21-tension-release-curves](../dev/features/F-M21-tension-release-curves.md) | State owner typed FX Send + current context interaction tests | Source/features/tension/*, Source/gui/slots/TensionStrip.cpp, TensionEditor.*, `ContextState::tensionFxSend` |
| [F-M22-smart-preset-suggestions](../dev/features/F-M22-smart-preset-suggestions.md) | Existing primary implementation; final detailed acceptance/evidence remains required | Source/features/suggest/* |
| [F-M23-advanced-modulation-macro-targets](../dev/features/F-M23-advanced-modulation-macro-targets.md) | State owner MacroV3 + typed dependency routing | Source/features/macros/*, MacroEditorSlot |
| [F-S01-cpu-latency-display](../dev/features/F-S01-cpu-latency-display.md) | TAIL diagnostic must use actual host query; reference timing pending | Source/features/telemetry/*, Source/features/health/* |
| [F-S02-parameter-info-status-line](../dev/features/F-S02-parameter-info-status-line.md) | INFO | Source/features/info/* |
| [F-S03-ui-scale](../dev/features/F-S03-ui-scale.md) | Existing primary implementation; final detailed acceptance/evidence remains required | Source/features/uiscale/* |
| [F-S04-midi-note-trigger-audition](../dev/features/F-S04-midi-note-trigger-audition.md) | MIDI-AUDITION | Source/features/audition/MidiTrigger.*, `audition/OptionsRow.cpp` |
| [F-S05-effective-value-feedback](../dev/features/F-S05-effective-value-feedback.md) | INFO | Source/features/effective/Effective.*, Knob effective marker |
| [F-S06-ab-compare-level-matching](../dev/features/F-S06-ab-compare-level-matching.md) | AB-MODEL/MATCH | Source/features/abcompare/MatchLevels.* |
| [F-S07-stutter-storm](../dev/features/F-S07-stutter-storm.md) | STORM | Source/features/momentary/HoldVoices.*, HoldPadsSlot (STORM pad) |
| [F-S08-glitch-roulette](../dev/features/F-S08-glitch-roulette.md) | ROULETTE | Source/features/live/Roulette.* |
| [F-S09-first-run-guide](../dev/features/F-S09-first-run-guide.md) | Existing primary implementation; final detailed acceptance/evidence remains required | Source/features/tour/* |
| [F-S10-genre-performance-presets](../dev/features/F-S10-genre-performance-presets.md) | Existing preset set; listening/quality corpus final evidence | Source/Presets.cpp (41 presets) |
| [F-S11-preset-favorites-next-previous](../dev/features/F-S11-preset-favorites-next-previous.md) | MIDI owner PC/navigation + PRESET-QUANT interaction | Source/features/presetbrowser/*, PluginEditor (`keyPressed`, `stepPreset`) |
| [F-S12-safe-return-panic](../dev/features/F-S12-safe-return-panic.md) | Existing Panic + new feature cancellation integrations | Source/features/panic/Panic.*, Source/gui/slots/PanicSlot.cpp, LiveSafetySlot.cpp |
| [F-X01-harmonic-granular-resynthesis](../dev/features/F-X01-harmonic-granular-resynthesis.md) | Root HG adapters; core/M4 pass, plugin M5 pending | Source/features/harmonic/*, Source/params/hgran.* |
| [F-X02-spectral-freeze-and-morph](../dev/features/F-X02-spectral-freeze-and-morph.md) | FREEZER + TAIL | Source/features/fx/bus/SpFreezeCard.*, Source/dsp/spectral/Stft.* |
| [F-X03-automatic-key-detection](../dev/features/F-X03-automatic-key-detection.md) | Existing estimator; strict key/chroma60us reference budget open | Source/features/keydetect/*, Source/gui/slots/KeyStrip.cpp |
| [F-X04-multi-section-arrangement-awareness](../dev/features/F-X04-multi-section-arrangement-awareness.md) | ArrangementV2 proof + actual Calm40seed/reference performance | Source/features/arrangement/*, ArrangementStrip |
| [F-X05-local-preference-learning](../dev/features/F-X05-local-preference-learning.md) | Runtime TasteV2 calibration/state + final interaction evidence | Source/features/taste/*, Source/gui/slots/TasteStrip.cpp, RecipeDrawer SKIP |
| [F-X06-multi-track-cooperative-gapfill](../dev/features/F-X06-multi-track-cooperative-gapfill.md) | Native real processor M1/M3 pass + group receiver/host evidence | Source/features/coop/* |
| [F-X07-adaptive-performance-quality-modes](../dev/features/F-X07-adaptive-performance-quality-modes.md) | QUALITY | Source/features/quality/* |
| [P-01-free-pro-build-flag](../dev/features/P-01-free-pro-build-flag.md) | Explicitly excluded: no Free/Pro gating | - |
| [Q-01-release-qa-matrix-harness](../dev/features/Q-01-release-qa-matrix-harness.md) | Release owner QA/CI/sanitizer/packaged validator evidence | tools/qa/run_matrix.py, tools/qa/summarise.py |
| [T-01-meter-peak-hold-verification](../dev/features/T-01-meter-peak-hold-verification.md) | UI owner clock/pixel/narrow-readout matrix | Source/gui/MeterMath.h, WaveformGapView.h (`LevelMeter`) |
| [T-02-top-bar-8px-grid-pass](../dev/features/T-02-top-bar-8px-grid-pass.md) | UI owner sizes/scales/grid and owner review | tools/check_layout_literals.py, tools/layout_literals_baseline.json |
| [W-01-windows-editor-automation-crash](../dev/features/W-01-windows-editor-automation-crash.md) | Runtime lifecycle improvements; native Windows crash/host evidence open | docs/progress/W-01.md |
| [W-03-variable-block-transport-prediction](../dev/features/W-03-variable-block-transport-prediction.md) | Pure tracker refactor verified; exact Event callback/validity capture + host jitter external | Source/PluginProcessor.cpp updateTempoInfo (master #12) |

Doc-12-only F-P04, N-FX2, F-D01/02/03/04, F-P02 and UI-0 are additional ledger rows, not part of the75-ticket count: i18n/Cloud are active owners, F-D02 maps to EVENT, F-D03 to QUALITY, accessibility/UI-0 require the final interactive/pseudo-locale matrix. Budget/FillNow retain their existing eligibility exemptions and need new-feature interactions. AAX/AUv3/iOS remain explicitly excluded.
