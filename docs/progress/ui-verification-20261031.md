# UI verification and timeline completion

Base: integration snapshot `1432394` (reviewed stabilization, acceptance templates and finalization notes).

## Initial slice

- LevelMeter uses an injectable test clock while production continues to use JUCE's millisecond counter. New raster checks exercise the 25/26 px readout boundary and the component's 1500 ms hold/decay behavior.
- GUI QA now checks the full Cartesian combination of its sizes, scales, modes and tabs. The previous test rotated scales instead of covering each combination. Screenshot generation includes the missing 150/200 percent minimum/default/large screens and asserts write failures.
- Timeline frame ingestion has a bounded, pure message-thread history. It tracks actual frame periods, 32-bit sample-position wrapping, dropped regions and explicit discontinuities. The VisualisationFifo observer runs only in the existing consumer's pop call; it is not an audio callback or a second consumer.
- The existing compact renderer remains unchanged. The audio producer's position/PPQ/discontinuity hook is a coordinator-owned dependency and must land before frame-driven timeline acceptance.

## Verification so far

- A standalone C++20 FrameHistory probe passes with `-Wall -Wextra -Wconversion -Werror`, exercising wrap, loss and capacity bounds.
- Syntax-only compilation using the pinned JUCE/generated baseline definitions passes for TestMeter, TestTimeline, TestGuiQa and OverlaySlot.
- Repository preflight, completeness audit and layout-literal ratchet pass; no new exemption was added.
- Native test execution, retained before/after images, per-platform meter golden images, visual approval, screen readers and real-host behavior remain unverified pending the available native test build.

## Remaining work

Finish expanded timeline frame lanes, PPQ/seam/loss drawing, freeze/scrub/inspector/Event-list interaction and persistence. Wire coordinator-owned producer and editor expansion contracts, preserving the outer editor dimensions and access to Mix/bypass/panic. Execute focused suites and the size/scale image matrix, then inspect retained images. T-01/T-02 and F-M03 remain open until their actual acceptance evidence exists.

## Timeline interaction slice

Implemented source paths now include frame-driven audio/confidence/protection/band lanes, PPQ beats and time-signature-derived bar labels, explicit discontinuity/loss markers, frozen snapshot retention, bounded keyboard/mouse scrubbing, engine-record inspector text, accessible Event-list mode and persisted expansion/list choices. The coordinator-owned editor hook supplies the expansion region and reruns layout; it cannot include Mix/footer safety controls.

State compatibility: existing zoom is already in the optional Health subtree. The new booleans extend that validated subtree rather than adding a second writer of Perf. Old readers ignore those optional properties. Freeze and display buffers remain transient. The 20-event list is retained to preserve shipped behavior while meeting the detailed spec's accessible alternative requirement.

Screenshot integrity: JUCE snapshots default to an unscaled raster even when an editor transform is active. GUI screenshots now use the actual applied UI scale as their raster factor. Capture requests assert requested scale equals applied scale; run with a sufficiently large virtual display (6000x4000) instead of labelling clamped 100 percent images as 150/200 percent evidence.

Also added a regression for the meter's 32-bit display-clock wrap and use modular deadline comparison. Native focused results and retained images are still pending the coordinator's build slot.

## Retained baseline evidence

The immutable baseline object set produced a separate UiReference executable; its two capture tests passed. It retained 12 LevelMeter PNGs and the full 3-size by 3-scale before set under the task evidence directory. The meter header's Git blob exactly matches release `e89aecd`. The default 100-percent and minimum 200-percent images were visually inspected with no overlap/clipping found; review of the remaining retained images and owner visual acceptance remain open.

The released MeterMath header independently reproduces premature hold expiry across the 32-bit clock wrap (probe exit 4); patched source passes the same wrapped hold/expiry checks (exit 0). This is a demonstrated cosmetic correction, not an audio-path change. Full capture commands, source/image hashes and raw results are retained with the coordinator's evidence.

## Native meter and localization follow-up

A focused native executable now runs the actual LevelMeter and Linux meter golden tests: **12 passed, 0 failed**. It compiles the current meter test/component source and links the pinned JUCE module objects, with no baseline processor/editor objects. Commands, source hashes and raw output are retained in `meter-component/` with the coordinator's task evidence. This result covers the injected clock, wrap regression, narrow readout and four retained 25-pixel-wide pixel references; it does not replace the integrated plugin test suite or other platforms' visual acceptance.

Timeline canvas labels, engine-record descriptions, inspector/list summaries, compact visualizer labels and the confidence caption now use the shared message-thread localization service. Language changes reword a frozen snapshot without advancing its captured records. The overlay toggle listens for language changes and accommodates its translated caption. GUI QA defaults to an English-scoped test setting and accepts `GF_GUI_LANGUAGE` for explicit pseudo-locale capture runs. Integrated pseudo-locale image inspection remains open.

## Coherent timeline generations

The event and frame queues now share an explicit epoch/rate pair. The message-thread model adopts a newer nonzero epoch before either queue inserts records, clears both histories and transient freeze/scrub once, and rejects older records regardless of which queue drains first. A repeated epoch must retain its sample rate; unmanaged epoch-zero fixtures remain supported until managed data arrives. Display windows, list times, inspector times and scrub increments use the accepted epoch's rate. A view clear preserves the accepted epoch so queued stale data cannot reopen an old coordinate system. Event-loss tracking also ignores previous-generation positions.

The dedicated native epoch suite passes **6/6** tests, covering both drain orders, stale records, modular wrap, frozen state, unmanaged compatibility, coherent rate changes and constant history memory. It was first compiled against the old model and failed for the absent API; the retained red log and passing executable provenance live in `timeline-epoch/` with task evidence. The existing standalone frame probe still passes with conversion warnings treated as errors. Syntax checks also pass for the changed GUI, observer and ExplainState adapters. These focused results do not claim integrated plugin or DAW acceptance.

## Separate Wet output extension

The additive WETBUSEXT contract introduces `wetBusMainMode` (Dry + Wet / Dry only, default Dry + Wet) and `wetBusLevel` (-24 to +12 dB, default 0). Both are performance routing parameters; the coordinator appends their group after all existing groups and adds them to routing-preserve operations. The underscore IDs in the feature spec are reconciled to the repository's permanent lowerCamel ID rule rather than weakening the validator.

`wetbus::mainMixTargets` delegates neutral targets to the shipped wet-only helper and changes only the Wet target when Dry only is selected with the host Wet bus enabled; Wet Only takes precedence. The processor reuses its existing 20 ms dry/wet smoothers. `LevelSmoother` owns the independent 20 ms bus trim with JUCE's linear smoother, performs no allocation, and returns exact unity when neutral. Mono uses the stereo half-sum. Host layout validation now accepts a disabled, mono or stereo Wet bus while retaining the AU gate and rejecting extra/surround outputs.

The Options > Outputs row exposes the two controls, shows the actual host bus availability/channel count, and explains the bus-off fallback. Labels and dynamic status use the shared localization path; no host enable state is serialized. Help is registered at 6410, next to the existing Routing/system section, because the canonical 1210 order is already used by Timeline. The existing Performance status now also reports mono accurately.

The native coefficient/trim suite passes **7/7**, covering bitwise neutral targets, routing precedence, exact ramp length, bounded levels, mono summing and allocation-free target/per-sample changes. Actual integration tests additionally cover a 60-second Dry-only render, bus-off identity, independent level, mono/sidechain aliasing, bypass/Panic, Wet Only precedence and project/legacy state. Those integration tests and the new Options row are syntax-checked but await the coordinator-owned processor/parameter integration and native suite; no render or host pass is claimed here.

All nine retained before images (minimum/default/large at 100/150/200 percent) have now been individually viewed. No clipping, interactive overlap or safety-control occlusion was observed in that baseline matrix. Matching after images, expanded-timeline/Options/pseudo-locale views, native integrated tests and real host checks remain open.

## Additional display localization

Tour step counters, titles, body text, actions and accessibility captions now translate at presentation time; a language change preserves the active step, completion preference and raw target IDs. Harmonic pitch/idle/fallback readouts and manual/automatic/locked key summaries now use complete parameterized display patterns while preserving pitch names, detected values and parameter IDs.

Health uses `DisplayText.*`, a message-thread presentation adapter for health descriptions, structured Fix-it changes/spoken text, telemetry and event-recording summaries. The monitor, stable health codes, rule proposals and recorder/schema text retain their original English core contracts. Dynamic counts/numbers and diagnostic identifiers are passed verbatim. The details popup rebuilds on language generation changes, including buttons that live outside the editor's generic translation walk. Native file dialogs translate errors through the shared display-only error adapter, preserving paths. Save-dialog completion uses a safe component pointer and reports a save failure instead of accessing a destroyed details panel.

Before adding those sinks, the existing detail-row layout helper was extracted in a separate unchanged-behavior commit. The previously 622-line GUI source is now below 600 lines, and the extracted helper is independently bounded. The English display-adapter parity probe passes **1/1** natively against all health code descriptions and the existing telemetry wording. New pseudo-locale display/progress tests are syntax-checked but still depend on the coordinator's final catalog regeneration and integrated native run. No full localization or screen-reader pass is claimed by this focused result.

## Native accessibility fixture

The integrated inspector test exposed an incorrect fixture assumption: pinned JUCE returns no accessibility handler for a detached component without a native window handle (`juce_Component.cpp`, `getAccessibilityHandler`). The test now attaches its actual TimelineView to a native temporary peer and asserts the handler's group role, title and summary in addition to its existence.

A fresh bare Xvfb also lacks standard window-manager atoms. Pinned JUCE queries those atoms with `only_if_exists`, then uses WM protocol/state atoms while creating/querying a native window; this produced real BadAtom failures. `tools/qa/x11_test_session.py` establishes those standard atom names and holds its X connection until the child exits, before JUCE initializes. It suppresses no X errors and preserves the command's exit code. Four Python lifecycle/failure tests pass. An actual pinned-JUCE peer probe now verifies detached-null, attached-nonnull and correct role/title under this session; the actual TimelineView assertion still awaits the next integrated native run. Linux CI/nightly native, QA, validator and standalone invocations now use the same session inside Xvfb, with valid YAML and shell syntax. This fixture is not evidence of a screen-reader or real-DAW pass.

The actual current TimelineGui tests have since been verified natively: **10 passed, 0 failed**, including the inspector's real-peer accessibility role/title/summary assertions. Only the updated interaction test was compiled, against the immutable integrated `86a3f6c` headers, and linked with that checkout's matching complete product object set; no child product ABI was mixed in. The checked-out sources and existing objects were not modified. The retained `timeline-native/` provenance records this scope. The new dense TimelineVisual matrix/benchmark and later display-localization additions still await the final integrated executable.

## Low-rate frame retention

An additional source/probe check found that preallocation assumed 64-sample periods, while the actual visualizer emits one 32-sample control hop per frame at low sample rates. At 32 kHz the old allocation retained only eight seconds of a requested sixteen-second window. The retained native probe reproduces this (8 seconds, exit 4) and passes after sizing from the true minimum hop (16 seconds, exit 0). The fixed capacity remains capped at 24,000 columns and ingestion still allocates nothing. The epoch suite is now **7 passed, 0 failed**, including full sixteen-second retention at 8/16/32 kHz under the unchanged model-memory ceiling.

## Complete retained-data budget

Preallocation now follows the released producer's exact rate-dependent cadence through the shared pure `dsp/VisualisationRate.h` contract, rather than reserving the maximum ring at every rate. This preserves low-rate retention and leaves room for all frozen-view data. Formatted list text is generated one row at a time from retained structured records; twenty formatted strings are no longer held in the view. The native row-name adapter exposes the same engine-record text through `ListBoxModel::getNameForRow`, instead of leaving screen-reader rows unnamed.

The pure memory probe measures the actual current types and vector capacities: 56-byte columns, 16,520-byte model object, 3,760-byte view object (including its structured list), 114,688-byte frame snapshot and 16,384-byte event snapshot. With an additional full 256 KiB text allowance, the total envelope is **1,010,848 bytes at 192 kHz** and **1,354,296 bytes at the worst cadence boundary tested (33,599 Hz)**, below the unchanged 1,500,000-byte ceiling. The native epoch suite now passes **8/8**, including independently specified released cadence values and full low-rate retention. A new live/frozen view test accounts for all actual retained vectors and conservatively charged text in each locale; that native GUI test still awaits the integrated candidate.

Expanded redraw now follows the detailed specification's 30 Hz cadence (33 ms timer), while Reduced Motion remains 4 Hz. The opt-in paint probe reports all size/scale cases and the specified 2600x1800 canvas before asserting the unchanged worst-case 2 ms target. It does not stop collecting measurements after the first miss. No paint-budget pass is claimed before that probe actually runs.

## Canonical Event-list outcomes

The visible Event-list mode now follows the detailed requirement: sixteen newest evaluated-gap or Preview records at or before the inspected edge, with one outcome per row. Started records copy their engine-provided recipe into their paired evaluation record so a successful row includes the algorithm and length. Engine reasons and record ordering remain unchanged. Canvas-mode callers retain the shipped twenty-generic-event getter. A frozen list keeps its structured snapshot while ingestion continues; scrubbing rebuilds the list only through the new inspected time.

The focused native model suite passes **9/9**, including the sixteen-outcome selector, future/non-outcome exclusion and copied recipe data. The actual view-type memory probe, including the additional sixteen inline outcome records and a full 256 KiB text allowance, measures **1,011,368 bytes at 192 kHz** and **1,354,816 bytes at 33,599 Hz**, below the unchanged 1,500,000-byte ceiling. The new GUI regression is syntax-checked and awaits the integrated native executable; the previous 10/10 native GUI result predates this list-mode reconciliation.

## Tour and status-details overflow

An actual isolated Tour component probe reproduced an English layout defect: step 2 gave LOAD EXAMPLE 38 pixels for text needing about 106 pixels, and the retained before image showed an ellipsis. Actions now measure their real button font, wrap inside the card, and leave Back/Next on a separate navigation row. The card measures its title/body/status instead of using a fixed height. The same native probe passes after the change, with 110 pixels allocated to LOAD EXAMPLE. A second run using the coordinator's actual English/pseudo catalog snapshot also passes; its inspected pseudo image shows the complete title, body and all five actions.

The first after-image inspection also exposed a font mismatch: the global LookAndFeel derives a Label font from its height, while these panels calculated wrapped height from their explicitly assigned font. The local read-only TextLabel helper now uses the same font and TextLayout for measurement and painting while retaining normal Label text/accessibility. The global LookAndFeel is unchanged.

The actual private Health row helper likewise reproduced a pseudo confirmation action ending at x=568 inside a 450-pixel row (exit 4). Its glyph-measured wrapping now places every button inside two rows (exit 0); retained before/after images were inspected. This helper-only probe uses a compiler access-control override to instantiate the private helper and no processor/editor objects. New integrated PresentationLayout tests check real Tour controls in both locales at three sizes, and the actual Health confirmation UI without applying its proposal. These are syntax-checked and await the final integrated executable. Standalone component evidence does not replace the final editor matrix or owner visual approval.

## Native visibility-fixture correction

The first integrated TimelineVisual execution identified two incorrect fixture assumptions. Its detached editor was invisible, so JUCE correctly returned no hit target; its detached TimelineView was invisible, so the timer correctly remained stopped. The fixture now makes the editor visible before testing input and exercises real show/hide transitions for the 250 ms/33 ms timer, also requiring zero while hidden. The safety selector explicitly targets the always-visible footer Panic slot; the separate Panic button inside the off-tab Performance card is not the global safety control being exercised.

An unchanged-assertion diagnostic retained the actual invisible/null/zero values. Recompiling only the corrected test against the immutable integrated `652592c` headers and matching product object set gives **4 passed, 0 failed**, including all nine size/scale input cases and actual retained memory of 1,052,468 bytes at 32 kHz and 828,468 bytes at 192 kHz in that candidate. The opt-in paint probe was not requested in this run, so this is not a paint-budget result. Commands, exact source/test hashes and raw logs are in `timeline-diagnostic/` with task evidence. Final candidate capture/owner acceptance remains pending.

## Export request ownership and overwritten data

The assigned export audit demonstrated two real failures. A semaphore-gated native test showed that the message thread could successfully cancel/reuse a Requested header after the audio thread had decided to read it (0 passed, 1 failed). A separate absolute-position ramp probe returned 12,300 corrupted samples in an accepted 20,400-sample export after the live ring overwrote a pending copy (maximum error 0.064, exit 4).

The mailbox now atomically claims a request before reading its plain header. Logical cancellation keeps Claimed/Copying storage owned by audio until its next acknowledgement; Ready/Failed storage can be retired immediately. A late cancelled Ready result is rejected even if callbacks stop. Copying checks the source run and oldest available sample every invocation, and fails before returning overwritten/mixed-generation audio. Existing enum values and the two-argument take API remain; Claimed/Cancelled values and cancellation/generation/result-metadata APIs are additive.

Preparation uses a configuration mutex outside audio. UI request/take/cancel operations try-lock and return immediately during preparation; process never locks, waits, frees or calls a UI function. Preparation still requires stopped audio callbacks. Completed audio and its sample rate/generation transfer together, so a later preparation cannot relabel a chooser's immutable result. The coordinator owns the RecipeDrawer timeout/generation/immutable-chooser hooks.

The focused native suite passes **13/13**, including precise before/after-claim and completion interleavings, stopped-callback cancellation, reprepare vs UI, completed metadata, 1,000 concurrent request/cancel/take operations and ring-overwrite rejection at 8/44.1/48/192 kHz with 8192-sample input blocks. TSan-instrumented service/test translation units pass **13/13**, exit 0. ASan/UBSan bounds/undefined-behaviour checks also pass **13/13**, exit 0, with leak inspection explicitly disabled after the retained leak-enabled run completed all tests then failed because LeakSanitizer cannot perform its ptrace inspection in this sandbox. Leak checking remains unverified. Pinned JUCE module objects are uninstrumented in these focused executables; integrated plugin, full dependency sanitizers and the broader F-L04 acceptance remain separate checks. Raw red/green runs, sanitizer limitations and exact commands/source hashes are retained in `export-audit/` and `export-ownership/` with task evidence.
