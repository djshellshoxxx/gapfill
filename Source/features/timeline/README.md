# Gap timeline

The timeline displays engine records and visualisation frames; it does not make scheduling or rejection decisions. `TimelineModel` retains 512 event records. `FrameHistory` retains at most 24,000 columns, including output sample position, actual producer period, PPQ/time-signature metadata and explicit loss/discontinuity markers.

## Ownership and threading

Both histories are message-thread owned. Events arrive through the existing fill-event drain. `VisualiserOverlayComponent` attaches an observer to the existing visualisation FIFO consumer; it never creates a second consumer. The observer is removed before the component is destroyed. The audio producer only fills POD metadata in its existing enabled-visualiser path. Frame storage is configured on the message thread; ingestion performs no allocation.

`TimelineView` retains at most 2,048 display columns and a bounded event window. Freeze retains these visible snapshots while ingestion continues. Frozen scrubbing is bounded to retained history. Inspector text comes from recorded engine outcomes. Aggregate FillEnd records do not identify a voice, so filled blocks display the engine-recorded planned length rather than inventing a pairing.

## Integration and state

The coordinator-owned FillEngine hook supplies the first accumulated hop's position, PPQ, time signature and actual visualisation period, plus a discontinuity on reset/seek. Invalid PPQ hides the grid; unknown time signature shows beat timing without guessed bar labels. Compact visualisation decimation and painting remain unchanged.

The editor supplies an available expansion rectangle ending above Mix/footer and invokes its layout when `onExpansionChanged` fires. Expanded height is bounded by three compact heights and available space. It never requests an outer-window resize.

Existing zoom remains in the optional Health subtree for compatibility. Additive `timelineExpanded` and `timelineList` booleans use the same subtree, avoiding a competing owner of the existing Perf schema. Invalid Health data fails soft through its existing validation. Freeze, scrubbing position and running display history are not serialized.

## Interaction and verification

Keyboard shortcuts run only when Keyboard Navigation is enabled: F freezes, Home returns live, Left/Right scrub 100 ms (Shift: 1 s), I toggles inspector, L toggles the Event-list alternative and Esc closes. Host Ctrl/Alt/Command shortcuts pass through. Screen-reader clients get a group summary and a real ListBox alternative. Reduced Motion updates at four page steps per second.

Tests: `Timeline`, `TimelineGui`, `TestTimelineInteraction`, `GuiQa`, plus legacy audio goldens. Native producer integration, visual screenshots, real-host shortcuts and screen-reader acceptance must be verified on the integrated candidate; source wiring alone is not acceptance.
