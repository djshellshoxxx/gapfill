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
