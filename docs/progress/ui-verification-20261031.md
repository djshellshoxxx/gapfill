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
