# F-M03 Visual gap timeline

Status: implementation-ready spec. Canonical background: `10-SPEC-LIVE-CREATIVE-EFFECTS.md` 4.1 (timeline requirements), `03-SPEC-ESSENTIALS.md` E-3 (legend, markers, threshold line), `02-FOUNDATIONS.md` F0-4 (reserved `VizFrame` fields). No full spec exists for the timeline drawing itself; this file is that spec. F-M02 supplies the data and footer; F-M03 only draws and inspects it.

## 1 Summary & user-visible behaviour
The existing compact visualiser (`WaveformGapView`, 2048 columns, 3 frames per column) stays the default. A small chevron on its top-right edge expands it in place into a timeline showing, on one time axis (default 8 s, choice 4 s / 8 s):
- lane 1 audio peak/energy with detected events (ticks);
- lane 2 gap confidence with the 0.55 enter-gap threshold line (from `kEnterGapConf`) and shaded eligible gaps;
- lane 3 protection: hit-protect zone, next-event guard, lookahead (three distinct hatch patterns);
- lane 4 outcomes: filled (solid block labelled with algorithm abbreviation), skipped (hollow marker + reason code), preview (hatched, distinct colour);
- optional lanes 5-7 (spectral mode only): low/mid/high band confidence;
- a beat/bar grid when the host supplied PPQ, with bar numbers.
Controls: Freeze (pause the view, audio keeps running), scrub with mouse drag / Left-Right keys while frozen, hover/focus inspector ("1.84 s ago: skipped - Gap too short, 38 ms free, need 60 ms"), and a "view incomplete" hatch where events were dropped. "Event list" toggle replaces the canvas with a text list of the last 16 outcomes (accessibility alternative, also useful for bug reports).

## 2 Success / quality metric
- Explainability: in a scripted test of 10 representative skipped gaps, a tester names the correct reason from the timeline alone for >= 9 (manual M-3).
- Accuracy: the marker/label drawn for every outcome equals the engine record (test compares `TimelineModel` output to the injected events, no inference).
- Performance: painting at 30 Hz (expanded, 2600x1800 worst case) < 2 ms/frame on the message thread; memory of the model <= 1.5 MB at 192 kHz.
- Zero effect on audio: golden A-F bit-identical; audio-thread cost of the new fields < 0.05% CPU.

## 3 Design
Files (all new): `Source/features/timeline/{TimelineModel.h,TimelineModel.cpp,TimelineView.h,TimelineView.cpp,Gui.cpp,Help.cpp}`, `Tests/features/TestTimeline.cpp`, cmake fragments.
- Audio-side additions (reserved-field PR, shared with F-M02 so `VisualisationState.h` is edited once): `VizFrame` gains `std::uint32_t posLo` (low 32 bits of output sample position at frame start), `std::int32_t ppqTick` (ppq*96 at frame start, `INT32_MIN` = invalid), plus the F0-4 fields (`reason`, `flagRejected`, `flagPreview`). Frame period is `hop (32) * vizDecim (2) = 64` samples; expose `constexpr int kVizFramePeriod = 64` so the GUI never hard-codes it. `VizFrame` stays trivially copyable; FIFO memory grows ~0.1 MB.
- `class TimelineModel` (pure, no JUCE UI, message thread): 
  ```cpp
  struct Column { float peak, conf, energy, bandConf[3], duck; std::uint8_t flags, algo, band, reason; std::uint32_t posLo; std::int32_t ppqTick; };
  struct Outcome { std::int64_t pos; RejectReason reason; FillRecipe recipe; float availMs, needMs; std::uint8_t probPct; bool started, preview; };
  void configure (double sampleRate, double windowSeconds);          // sizes ring: ceil(window*sr/64) columns, max 24000
  void pushFrames (const VizFrame*, int n);                          // detects posLo gaps -> markIncomplete
  void pushOutcomes (const Outcome*, int n);                         // joins by pos, tolerant to out-of-order arrival
  void setFrozen (bool); void scrub (double seconds);
  const Column* columns (int& n) const; std::span<const Outcome> outcomes() const;
  std::optional<Inspect> inspectAt (double secondsAgo) const;        // text fields for hover/focus
  bool isIncomplete (double fromSecondsAgo, double toSecondsAgo) const;
  ```
  Ring is preallocated in `configure` (message thread); `pushFrames` never allocates.
- `WaveformGapView` today owns FIFO draining. Minimal hook: it forwards drained frames to the model (`model.pushFrames`) and the compact painting path stays as is. `TimelineView` is a second component in slot `VisualiserOverlay` painting from `TimelineModel`; compact mode paints nothing extra except F-M02 markers.
- Outcomes come from the processor-owned `GapOutcomeLog` (F-M02) via `snapshotSince (lastSeq)`; no second consumer of `fillEvents`.
- Paint: one pass per lane, columns decimated to pixels with min/max; no per-frame allocation (reuse `juce::Path` members). Colours via `LookAndFeel` tokens only.

## 4 Parameters & state
- NEW APVTS IDs: none (view-only; automation of a zoom level has no value).
- State in `<Perf>` (F0-6): `timelineExpanded` bool (false), `timelineWindowSec` int {4, 8} (8), `timelineList` bool (false). Out-of-range values reset to defaults. Not in presets, not in Undo.
- Neutral: expanded=false draws exactly the current compact view (screenshot test unchanged within tolerance). Old sessions: no `<Perf>` -> defaults.

## 5 Real-time safety
- Audio thread adds three plain stores into the `VizFrame` it already pushes (only when `viz->isEnabled()`); no new queue. Overflow semantics: FIFO drops the newest frame when full (existing); model detects via `posLo` jump and sets incomplete; decisions are never affected.
- All model/paint work on the message thread. Editor closed -> `viz` disabled -> zero audio work (existing behaviour preserved).
- Sample-rate change / `reset()` / transport jump: audio sets `flagDiscontinuity` (reuse reserved bit 7) on the next frame; model inserts a visible seam line and does not interpolate across it.

## 6 UI
- Placement (docs/dev/11): inside the visualiser, expandable in place. Expanded height = 3x compact, taken from the Creative content area; if the editor is at minimum size (1020x620) expansion becomes an overlay drawer over the Creative tab, closed by Esc or the chevron, never resizing the window or hiding Mix/bypass/panic-class controls. Scrolling is internal only.
- Tooltip (chevron): "Show the detailed gap timeline: events, gaps, protection and why fills were skipped." Freeze: "Pause the display to inspect it. Audio keeps running." Window: "Length of history shown."
- Help (order 1210): lane meanings, legend, "view incomplete" hatch, Freeze/scrub keys.
- Accessibility: canvas has an `AccessibilityHandler` role `group` with name "Gap timeline" and a live summary string ("Last 8 seconds: 6 gaps, 4 filled, 2 skipped"); Event list mode exposes a real list for screen readers; every lane colour has a pattern. Keyboard (when keyboard navigation is on): Tab to chevron, Enter expand, F freeze, Left/Right scrub 100 ms (Shift = 1 s), Home = live, I toggles inspector, L toggles Event list.
- Reduced Motion: no continuous scrolling; the canvas re-renders in 250 ms page steps (4 Hz) with the playhead drawn as a static line; hover/inspect unaffected. Respects `isReducedMotionEnabled()`.
- Narrow behaviour: below 1100 px width hide lanes 5-7 and label text, keep markers + inspector; below minimum the component never gets smaller than the compact view.

## 7 MIDI / automation / CLAP / host
No MIDI mapping (a "freeze display" MIDI command is out of scope). PPQ grid uses the transport snapshot already published for the decision key; when `ppqTick == INT32_MIN` the grid is hidden and the lane header shows "No host tempo - time only". Hosts that report no PPQ (many standalone cases) still get the full time-based view. Loop wrap/seek: PPQ jumps are drawn as seams (the model never assumes monotonic PPQ). CLAP/VST3 identical.

## 8 Tests (`Tests/features/TestTimeline.cpp`)
`frames_in_order_fill_ring` | `posLo_gap_marks_incomplete` | `posLo_wraparound_32bit` | `outcome_joined_to_correct_column_out_of_order` | `window_trims_to_configured_seconds_all_sample_rates_44k1_to_192k` | `freeze_stops_view_not_ingest` | `scrub_clamped_to_history` | `inspect_text_matches_engine_record` | `ppq_invalid_hides_grid` | `discontinuity_inserts_seam` | `model_no_allocation_after_configure` | `compact_mode_screenshot_unchanged` | `expanded_screenshot_min_default_large` (golden images with tolerance) | `event_list_has_one_row_per_outcome` | `reduced_motion_steps_at_4hz` | `keyboard_scrub_and_freeze` | `viz_new_fields_trivially_copyable_and_default_zero`. Fixtures: synthetic frame/outcome streams (seeded), scenario A with an artificial FIFO stall.

## 9 Dependencies & build order
Requires F-M02 (data, legend, markers) and F0-4 / F0-9; F-L02 history strip may later link a click on a filled block to its recipe. Build after F-M02. Independent of F-M04 except lane-visibility in Simple (timeline expandable in both views).

## 10 Effort & risks
Effort M-L (L if screenshot infra is new). Risks: (1) `VizFrame` layout change touching a shared header (do it in the F-M02 PR); (2) 192 kHz memory (cap at 24000 columns, documented); (3) screenshot tests are platform-fragile (tolerance + per-platform goldens as F0-7); (4) overlay mode at min size needs a layout review against docs/theme.md.

## 11 Acceptance checklist
- [ ] Compact view pixel-equivalent to current (within tolerance); golden audio unchanged
- [ ] All lanes, legend, threshold line and markers match engine records
- [ ] Freeze, scrub, inspector, Event list work by keyboard and mouse
- [ ] Incomplete (dropped) data visibly hatched; seams on jumps/resets
- [ ] Reduced Motion verified; screenshots at min/default/large and 100%/150%/200% scale
- [ ] No audio-thread allocation; CPU delta recorded
- [ ] Help and tooltips registered through the help registry
