# 12 - GapFill GUI and space-efficient feature layout

**Status:** implementation specification; compact shell and first tile slice implemented 2026-10-20.  
**Profiles:** audio effect plug-in; standalone wrapper uses host-independent device
setup only where the wrapper provides it.  
**Shared source:** `docs/standards/circuitdriftlabs-shared/standards/AUDIO_APP_BASELINE.md`
and `docs/standards/CDL_PLUGIN_BASELINE.md`.  
**Scope:** the current GapFill editor and the feature slots described in `03`-`11`.

This document is the product-specific GUI source of truth. Keep the current orange /
teal GapFill identity, but use tabs, compact tiles, collapsible sections, and scroll
viewports so the editor stays usable in small host windows. Do not put unimplemented
features into the shipped GUI as dead controls or misleading placeholders.

## 1. Goals and non-goals

### Goals

1. Keep the active controls legible and reachable in a small plug-in window.
2. Put related controls together by the task the user is performing.
3. Preserve fast access to current detection, fill, protection, mix, preset and
   performance controls.
4. Give every new feature in `10`/`11` an assigned tab and tile before implementation.
5. Keep control diameters and hit areas stable while the editor resizes; resizing
   changes reflow, columns and scroll space, not the control scale.
6. Provide keyboard access, visible focus, text alternatives for meters/status, and
   respect reduced-motion/tooltips settings already supported by GapFill.

### Non-goals

- Do not introduce a webview or replace JUCE's native controls.
- Do not expose every advanced parameter on the overview page.
- Do not create floating child windows for each tile. Collapse sections in the main
  editor; use modal dialogs only for focused tasks such as file selection or editing
  a scene.
- Do not add network, telemetry, or device-opening behavior to the plug-in editor.

## 2. Information architecture

Use one horizontal tab strip below a compact global header. Tabs are task-oriented;
only tabs with implemented content are visible. Each page scrolls independently and
contains compact tiles. A tile groups one cohesive feature and can collapse while
retaining its title, current state and one-line status. The first implementation has
five focused tabs: Detect, Creative, Weights, Spectral and Protect. The visualizer and
mix remain available in the scrollable page. Overview, Performance, Routing, Analysis
and Presets are target groupings for future tickets and appear only when those pages
have production-ready content.

| Tab | Contents | Existing/new feature IDs |
|---|---|---|
| **Overview** | Waveform/gap visualization, detector source and sensitivity summary, main fill amount, mix/output meters, protection status, current activity | Existing core; F-M03 adds optional timeline summary |
| **Creative** | Fill recipe controls, algorithm weights, spectral lanes, capture and sound shaping | Current Creative/Weights/Spectral; C-1 to C-11; F-M14-F-M16, F-M20, F-L14-F-L15, F-L18-F-L21 |
| **Performance** | Audition, recent recipes, momentary/latch gestures, panic, scenes and controller mappings | E-1, U-1, F-S07-F-S08, F-S12, F-M10-F-M11, F-M18-F-M19, F-L07, F-L10, F-L17, F-L24-F-L25 |
| **Routing** | Main/sidechain detector choice, protection routing, optional wet bus and multiband routing | Existing E/C routing controls; F-L12-F-L14 |
| **Analysis** | Gap timeline, groove/energy/phrase confidence and optional arrangement/key analysis | E-3, F-M03, F-L09, F-L22, F-X03-F-X04 |
| **Presets** | Factory/user browser, favorites, save/import/export, A/B and suggestions | U-4, U-10, F-S06, F-S10-F-S11, F-M22, F-L23 |

**Feature gating:** a tab is displayed when at least one production-ready feature in
that group exists. If a tab has one feature, show it directly rather than a single
redundant sub-tab. Unimplemented tickets stay in documentation only. The tab labels
and destinations are stable once shipped; adding a feature adds a tile in its assigned
tab unless a reviewed usability change requires a migration.

The top header contains product name/version access, preset selector/previous-next,
compact A/B state, Options and Help. Secondary actions (Save, Save As, Open, export,
Random, Surprise, Undo, Reset, Panic) live in a responsive action row or a clearly
named `More` menu. Keep Bypass visible at all sizes. Do not let a crowded header shrink
labels to unreadable sizes.

## 3. Layout and responsive rules

### Editor bounds

- Initial editor size: **1000 x 660 logical px**. Persisted sizes are clamped when
  reopened so a stale saved dimension cannot create an off-screen window.
- Minimum size: **820 x 540 logical px**; maximum: 2600 x 1800. If a host enforces a
  smaller surface, preserve the header and tab strip, then scroll the active page.
- Control sizes remain aligned with the shared baseline: normal knobs 48 px, compact
  knobs 36 px, large knobs 64 px, buttons 28 px high. Do not scale knobs to fill a
  larger editor.
- Default outer padding is 16 px and the layout follows an 8 px grid. At the minimum
  size, the content may use 8 px tile gaps while maintaining at least 8 px from the
  window edge; text and control hit targets do not shrink.

### Reflow breakpoints

| Available editor width | Tile layout | Header behavior |
|---:|---|---|
| >= 1240 px | 3 columns for independent tiles; 2 columns for dense control groups | Full labels and primary actions |
| 900-1239 px | 2 columns; wide tiles may span both | Move lower-priority actions into `More` |
| 820-899 px | 1 column; compact summary tile may sit above it | Preset text truncates with accessible full name; action overflow menu |

At all widths, the active tab page uses a `Viewport` with a visible vertical scrollbar
when content exceeds the view. Horizontal scrolling is not permitted for ordinary
controls. A dense parameter table may scroll horizontally only when each field keeps
its minimum readable width and an equivalent accessible single-column view exists.

### Height behavior

The header and tab strip stay visible while the active content scrolls. The gap
visualization has a compact mode and a user-controlled hide/collapse state. Meter and
status tiles can collapse to a one-line summary. Advanced sections start collapsed on
the Overview page; the selection remains visible so collapsed state is discoverable.

## 4. Tile and collapsible-section behavior

A **tile** is a compact visual group, not a large generic card. It uses the existing
panel surface and border tokens, a concise title, optional help affordance, and a
single content column or small responsive control grid. Avoid unnecessary nested
boxes. Tiles have a minimum width of 240 px where the page allows it; under that width
they take the full column.

A **collapsible section** has a full-width keyboard-operable disclosure header with:

- section title and expanded/collapsed icon;
- a non-color-only state cue when active (for example `ON`, `3 active`, or `2 pending`);
- optional one-line summary such as `Sidechain: connected` or `Density: 72%`;
- an accessible expanded state and logical focus order.

Clicking the header toggles content without changing parameter values. Use a short
height transition only when reduced motion is off; otherwise expand immediately.
Remember collapsed/expanded choices as editor-view preferences, not in host sound
state or preset state. Provide `Expand all` / `Collapse all` for pages with at least
four sections. At startup, restore a valid last-view preference; otherwise use the
defaults below. Collapse state must never hide an error, active panic/safety status, or
the currently focused control without moving focus predictably.

### Defaults by page

- Overview: visualization visible; Detector and Mix summaries expanded; advanced
  protection details collapsed.
- Creative: core fill controls expanded; algorithm weights collapsed unless the user
  last opened them; per-band/spectral advanced controls collapsed.
- Performance: current command/scene strip expanded; history and controller mapping
  sections may collapse independently.
- Routing: active routing summary expanded; inactive/advanced routing rows collapsed.
- Analysis: timeline expanded; confidence and diagnostic details collapsed.
- Presets: search/category/favorites and current preset details expanded; import/export
  help collapsed.

## 5. Feature placement and affordances

Every ticket in `03`-`11` must appear in a feature-to-UI map in its implementation PR.
This table is the initial assignment; do not invent an extra global tab for one knob.

| Feature family | UI component / location | State and interaction |
|---|---|---|
| Fill Audition, MIDI audition trigger | Performance / audition tile | Momentary action; keyboard/MIDI release and cancel behavior match E-1/U-3 |
| Why Didn't It Fill, confidence/reason | Overview summary and Analysis details | Exact reason text plus actionable advice; no color-only indicators |
| History, lock, More Like That, mutation | Performance / recipe tiles | Bounded history, explicit audition/keep/restore actions |
| Macro/personality/effective values | Creative / macro tile; effective ring on mapped controls | Base/effective values remain distinct; never imply a stored parameter changed |
| Momentary/latch, Stutter Storm, Beat Battles, scenes | Performance / gesture and scenes tiles | Visible armed/fired/latch state; Panic remains highest-priority action |
| Panic and safe return | Header action and Performance safety tile | Header action has text/tooltip/accessibility name and immediate state feedback |
| Controller profiles and learn | Performance / controller tile | Profile selection, learn countdown, conflict/error message, disconnect recovery |
| Per-band types, wet bus, sidechain | Routing / routing tiles | Show only host-supported inputs/outputs; clearly distinguish source and destination |
| Crusher, ratchet, resonator, dub, tape and pitch effects | Creative / effects tiles | Effect family headings and one collapsed tile per module; do not combine unrelated parameters |
| Groove, energy, arrangement and key estimates | Analysis tiles | Show value and confidence; label estimates and unknown states plainly |
| A/B and loudness-matched comparison | Header state plus Presets comparison tile | State being heard is explicit; preview-only match gain is labeled |
| CPU/latency telemetry | Overview footer/status tile and Options visibility switch | Separate host-reported latency from measured/estimated workload |
| Genre presets and smart suggestions | Presets page | Suggestions never auto-load; recommendation reason/confidence visible |
| Export | Presets/More action, as applicable | Progress, cancel, destination/result and export metadata visible |

## 6. Interaction and accessibility requirements

- All tabs, disclosure headers, tiles with actions, menus and controls are keyboard
  reachable when keyboard navigation is enabled; focus remains visible in every
  theme. Tab/Shift+Tab traversal must not steal DAW shortcuts while the feature is off.
- Arrow-key tab selection is optional; when present, follow standard left/right
  behavior and do not trap focus. Enter/Space toggles collapsible headers.
- Each control exposes a meaningful accessible title, value, unit and state. Meters
  announce a throttled text summary rather than every repaint.
- Selected tab, expanded state, signal/error, armed state and confidence use text/icon
  or shape as well as color. Respect high contrast and system scaling.
- Normal text must meet 4.5:1 contrast and large text 3:1 against its actual tile
  surface. Disabled controls remain readable and explain why they are unavailable.
- Tooltips remain suppressible. Focus can show the same help text without requiring
  hover. Safety and routing instructions remain visible in Help/inline text.
- No decorative animation or live meter may run from the audio thread. UI refresh is
  bounded (meter/timeline <= 30 Hz; status/CPU <= 10 Hz) and stops when the editor is
  hidden or the relevant page is inactive when safe to do so.

## 7. Persistent UI state

Persist the following as editor preferences separate from APVTS and sound presets:
window size, selected tab, tile collapse map, visualization visibility/compact mode,
and optional tile order if user ordering is later implemented. Use stable string IDs,
versioned state and a small size cap (<= 4 KiB). Unknown IDs are ignored; invalid
entries reset to defaults without affecting audio state. Do not persist transient focus,
hover, pending file chooser, live command, panic, suggestion, or preview state.

## 8. Implementation architecture

Add a small native UI composition layer rather than growing `PluginEditor.cpp`:

```text
Source/gui/layout/
  EditorShell.{h,cpp}         header, tab strip, viewport, responsive breakpoints
  FeaturePage.{h,cpp}         page ownership and tab registration
  FeatureTile.{h,cpp}         compact tile and disclosure state
  ResponsiveTileGrid.{h,cpp} column calculation and bounds only
  EditorViewState.{h,cpp}     versioned UI-only preference model
```

The first slice implements the `DisclosureTile` seam in
`Source/gui/layout/DisclosureTile.{h,cpp}`. It borrows existing feature panels, owns
only its disclosure presentation, and never owns processor state. `PluginEditor` is
the composition root: it owns each panel/tile, registers tabs explicitly, hosts the
page in a vertical `juce::Viewport`, and keeps the visualizer and mix available. A
tile's collapsed state is editor-session-only in this slice. Feature pages continue
to use their existing APVTS attachments and bounded visualizer/level snapshots.

Future extraction into `EditorShell`, `FeaturePage`, `ResponsiveTileGrid` and a
versioned persistent `EditorViewState` is deliberately incremental. Do not duplicate
the current panel classes to satisfy those proposed names. The first slice does not
implement unbuilt tickets. It preserves parameter attachments, preset menu, A/B,
Undo, Surprise, Randomize, bypass and help behavior. The compact header exposes
secondary commands through `MORE` below 1180 px.

## 9. Acceptance tests

### Automated

1. Layout bounds at 820x540, 900x600, 1024x768, 1240x800, 1920x1080 and 150%/200%
   scaling: no clipped control, overlapping labels, unreachable content or horizontal
   overflow in ordinary controls.
2. Resize repeatedly while switching tabs and expanding/collapsing sections; verify
   focus remains valid and no component receives a negative/zero invalid bound unless
   it is intentionally hidden.
3. Expand/collapse state restores from UI preferences; old/malformed/oversized view
   state is safely ignored; sound/preset state remains unchanged.
4. Existing UI/parameter tests confirm all controls still attach to the same stable
   parameter IDs and their host automation values remain correct.
5. Accessibility audit: every interactive control is named; tab/disclosure state is
   exposed; text/status is available without color; keyboard path reaches Help,
   Options, Panic, Bypass and all active-page controls.
6. Visual snapshots for each breakpoint and all pages use realistic names, long preset
   names, disconnected sidechain, active MIDI learn, overflow, collapsed error and
   maximum-width labels.
7. Repeated editor open/close and tab switching while audio runs cause no crash, data
   race, audio-thread allocation or unbounded UI timer work.

### Manual host QA

In REAPER and one additional host on each claimed OS, open/resize the editor, visit
each enabled tab, scroll to the bottom, collapse/expand sections, use keyboard focus,
load/save a preset, toggle A/B, close/reopen the editor and reopen the project. Record
host, format, OS, display scale and screenshot evidence. Standalone device setup is
tested only in the standalone target; plug-in Options never expose host-owned audio or
MIDI device selectors.

## 10. Shared baseline compliance snapshot

| Shared requirement | GapFill UI decision | Current status |
|---|---|---|
| CDL palette/control language | Preserve current shared dark/teal/orange palette and knob/menu behavior | Implemented in existing LookAndFeel; visual audit still required after layout changes |
| Presets/A-B/help/options | Keep current controls and place them in header, Presets tab or menus by width | Existing implementation; expanded browser spec remains U-4 |
| Resize/reflow/scroll | Fixed-size controls, focused tabs, active-page viewport, smaller minimum window | First slice implemented; breakpoint screenshots and scale QA remain pending |
| Collapsible groups | Section headers keep state/status visible; collapse state is view-only | First slice implemented; persistent UI-only state remains future work |
| CPU/latency display | Accurate host latency and separately labeled estimate; hide when unsupported | F-S01 specified, UI placement defined |
| Visualization | Current gap waveform remains available, compact/hide options | Existing; hide/compact UI needs audit |
| Device setup | Only standalone target may open device settings | Existing Options callback is standalone-only |
| Accessibility/reduced motion/tooltips | Keep current preferences and verify tabs/tiles/status in every scale | Existing preferences partial; expanded acceptance defined here |
| First-run and shared online features | Implement only if the specific U-7 or approved future spec is selected | Deferred |

## 11. Rollout sequence

1. Implement the native disclosure tile, five focused tabs, vertical `Viewport`, and
   compact action overflow while preserving audio and state behavior. **Done.**
2. Add regression coverage for restored/minimum editor bounds and compile the modular
   tile as an independent GUI unit. **In progress.**
3. Audit screenshots at the minimum/default sizes and verify 150%/200% display scale;
   revise tile heights and controls if evidence shows clipping. **Pending.**
4. Add a `FeaturePage` entry only in the same PR as the first shipped feature for that
   page. Keep unselected `10`/`11` tickets out of the interface.
5. Update screenshot evidence, `PROGRESS.md`, shared-standard compliance and `07`
   manual QA results for every platform actually tested.
