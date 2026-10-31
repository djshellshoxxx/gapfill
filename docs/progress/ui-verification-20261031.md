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
