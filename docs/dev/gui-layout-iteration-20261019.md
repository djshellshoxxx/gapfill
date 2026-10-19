# GapFill GUI layout iteration (2026-10-19)

This feature branch is independent from release/build stabilization. Do not merge until reviewed and verified. Existing DSP, parameters and factory presets must remain unchanged.

## Confirmed behavior

- Left and right future-control drawers are collapsed by default and located adjacent to the upper waveform area. They reserve space for future, unspecified controls. Future implementation should support true outward expansion where the plugin host supports editor resizing.
- Detection is visible by default, collapses to the left edge, and releases its horizontal space to the center tabbed area.
- Protection is visible by default, collapses to the right edge, and releases its horizontal space to the center tabbed area.
- Collapsed Detection and Protection need narrow vertical name strips and red outward arrows. Expanded states show red inward arrows.
- Creative / Auto Weights / Spectral remain distinct tabs; future tabs must be easy to register without duplicate layout code.
- Auto Weights needs a scrollable, minimum-height list so every algorithm remains adjustable at the minimum editor size.
- Mix / Output starts expanded. It collapses to a thin labeled header with an obvious red downward arrow, and collapsing it must not increase the height of the other panels.
- Knob diameter is fixed and should remain usable at all supported editor sizes. Extra window area goes to gaps, waveform and panel padding.
- Initial editor minimum remains 1020x620 until tested. The default editor size should be reviewed against DAW dimensions.

## Current implementation and remaining work

The first pass includes independent collapse state and buttons, layout reflow, Auto Weights viewport, and knob diameter cap. The following are **not finished**:

1. Style the edge buttons as thin vertically lettered strips with explicit red inward/outward arrows (currently arrow-only buttons with tooltips).
2. Enable actual outward drawer expansion via editor resizing; current implementation reallocates the upper waveform region inside the existing editor bounds.
3. Adjust the future-control drawers to be truly anchored to the top 2/5 of the overall window rather than the existing upper visualization region.
4. Replace the current hard-coded three-tab layout with a modular tab registry before additional feature tabs arrive.
5. Check all GUI accessibility labels, keyboard focus settings, and component bounds when panes are toggled.
6. Build and smoke-test VST3 / CLAP / standalone on supported platforms, run pluginval, and verify resize behavior in real hosts.
7. Test Auto Weights scrolling with every algorithm enabled at minimum, default and expanded editor sizes.

No release/build branches should be modified by this work. Do not claim the layout has passed compilation or GUI validation until the checks run.
