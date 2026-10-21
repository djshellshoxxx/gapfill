# Feature candidates and GUI placement plan

Status (2026-10-21): placement plan for the features now authorized in `12-SPEC-BACKLOG-AND-NEW.md`. Before coding a UI ticket, run the GUI implementation sequence below and record the decision. All candidates in the table have a spec in `12` (N-USE-b health, F-D01 budget, F-L07/N-VAL scenes and banks, N-USE-a panic, N-VAL exchange, F-D02 events, F-D03 quality, F-D04 accessibility); UI-0 in `12` creates the Performance tab / Effects section shell.

## Current interface constraints

GapFill already has a compact global header with preset and file actions, A/B comparison, randomize/reset/help/undo/bypass actions, three main tabs (Creative, Auto Weights, Spectral), a persistent gap visualizer, and mix/protection controls. The header and the existing primary controls are at capacity. New features must use the current content area, an existing menu or drawer, or a new tab that earns its space.

Keep the musical task visible: understand the incoming gap, shape a fill, and control its level and protection. Do not make essential controls depend on tooltips, hover, or a hidden mode.

## Placement proposal

| Interface area | Keep here | Add here | Guardrail |
|---|---|---|---|
| Header | Existing preset, file, A/B, undo, help, reset and bypass actions | None by default; related actions can live in the existing File or overflow menus | Do not add another permanent button row or shrink the title/action targets |
| Gap visualizer | Gap, fill activity, input/output context | Expandable timeline detail, queued scene/fill marker, short skip/health status | Compact by default; advanced detail expands in place |
| Creative tab | Core fill character, amount, timing and protection | Simple/Advanced disclosure; a small set of assignable performance macros | Default view stays short; advanced rows are collapsed |
| Auto Weights tab | Current weighting controls | Keep weighting and confidence explanations together | No duplicate controls from Creative |
| Spectral tab | Current spectral controls | Keep spectral processing parameters together | Effects that need deep parameters remain collapsed until enabled |
| Performance tab (proposed) | New performer workflow | Scene slots, macro assignment, MIDI learn, quantized transition options, panic/recovery, transport health | One coherent place for live operation; do not put every MIDI mapping on the main screen |
| Effects section (proposed within Creative/Spectral) | Effects that are part of fill shaping | Foldable effect cards grouped by function: rhythmic, tape/time, resonant/delay, spectral | Effects off by default, neutral at defaults, advanced parameters appear only when enabled |
| Status/footer | Current status and latency | Compact transport/sidechain confidence, CPU or overload warning when actionable | Show only the highest-priority live message; never turn into a dashboard of permanent meters |
| File menu / history drawer | Existing file actions | Export rendered fill, compare/restore recent recipe, import/export scene bank | Keep file and history actions out of the permanent control area |

The exact placement must be checked against the current editor size before implementation. If adding a tab makes the tab strip crowded at the minimum supported size, use a grouped More menu or a secondary drawer instead of shrinking all tabs.

## Useful feature candidates beyond the current backlog

These ideas complement docs/dev/03-SPEC-ESSENTIALS.md, 04-SPEC-CREATIVE.md, 05-SPEC-USABILITY-VALUE.md, and 10-SPEC-LIVE-CREATIVE-EFFECTS.md. Check those specs before writing a duplicate ticket.

| Candidate | Why it helps | Live musician value | Size | Suggested placement | Priority |
|---|---|---|---|---|---|
| Transport and sidechain health indicator | Makes sync/source problems visible before a set goes wrong | High | S | Footer plus expandable visualizer detail | P1 |
| Per-bar fill budget and quiet zones | Prevents repeated fills from taking over a section; gives performers control over density | High | M | Creative tab advanced section | P1 |
| Setlist scene bank with next/previous and quantized change | Enables hands-free transitions between song sections without opening preset dialogs | Very high | M-L | Performance tab; map next/previous through MIDI | P1 |
| One-action panic and recover | Quickly silences stuck or runaway wet voices and restores a known safe state | Very high | S-M | Fixed, high-visibility control in Performance tab and a configurable MIDI mapping; optional keyboard shortcut | P1 |
| Portable scene and macro bank exchange | Makes sharing, backup, and gig preparation easier | Medium-high | M | Existing File menu | P2 |
| Event capture and replay, without audio capture | Replays parameter, recipe, transport and seed events for bug reports and reproducible sound design | Medium-high | L | Help/diagnostics drawer, opt-in local file | P2 |
| CPU/latency budget mode | Gives users a predictable quality-versus-resource setting on low-power stage machines | High | M | Options panel or Performance tab; expose only when there is a meaningful tradeoff | P2 |
| Accessibility and contrast presets | Helps users read controls in dim venues and supports keyboard-only operation | Medium-high | S-M | Existing Options view | P2 |

Order these only after the platform and stability gates in docs/dev/08-STABILIZATION-MODULARITY-FEATURE-BACKLOG.md. The live effects spec and feature specs define detailed behavior; this page only records product value and placement.

## Progressive disclosure and interaction rules

1. Use a Simple default that exposes the few controls needed to get a musical result. Keep Advanced as an explicit, persistent choice.
2. Put related controls in one tab or expandable group. Avoid mirrored parameter controls in multiple panels.
3. Show effect-specific controls only after the effect is enabled. Bypass must remain clear and immediate; neutral defaults must preserve existing output within the project's golden tolerance.
4. Prefer menu commands or a history drawer for occasional actions such as exporting, comparing, or restoring. Keep real-time performance actions reachable from the Performance tab.
5. Make panic/recovery independent of quantized scene changes and automation smoothing. Its exact implementation and MIDI behavior require a feature spec and regression tests.
6. Use concise status with a reason and next action where available, e.g. “No sidechain — using input” or “Transport unavailable — free timing”. Avoid vague green check marks.
7. Preserve visible focus, keyboard navigation, reduced-motion behavior, and screen-reader names for new controls.

## GUI implementation sequence

Before implementation:

1. Capture the current editor at minimum, default, and large sizes on each supported UI scale. Record the editor's actual minimum dimensions and tab behavior.
2. Make a feature-to-control placement table for the selected feature, including all states: disabled, enabled, automation, MIDI learn, error, and recovery.
3. Wireframe the proposed screens using the existing component style. Show Creative, Effects, and Performance states without adding mock controls to the shipping interface.
4. Check the placement against docs/theme.md, docs/dev/05-SPEC-USABILITY-VALUE.md, accessibility requirements in docs/dev/06-ENGINEERING-STANDARDS.md, and this module map.
5. Review the wireframe at minimum size and with keyboard focus. Confirm no clipped labels, overlapping controls, or hidden panic/status action.

Implementation then proceeds in a separate feature branch, with its spec and tests. This plan is updated first if the control placement changes.

## Acceptance criteria for the layout

- No permanent header control row is added.
- Existing Creative, Auto Weights, and Spectral workflows remain easy to find.
- Common sound-shaping actions remain on the primary surface; occasional tasks remain in existing menus or a drawer.
- Performance actions are grouped together, and a panic/recovery path is available without relying on a popup or quantized timing.
- At minimum editor size, tabs remain understandable and controls do not overlap or clip. Scrolling is confined to the content panel if unavoidable.
- Simple mode does not expose every parameter at once; Advanced controls remain discoverable.
- New features preserve the project's parameter automation, state migration, accessibility, real-time, and regression rules.

## Decision record

For each approved feature, record:
- Feature/spec ticket and user problem.
- Controls added, removed, or moved.
- Default and Advanced mode behavior.
- Minimum-size and accessibility review evidence.
- CPU/memory/latency implications.
- Test, host, and live-performance acceptance evidence.

Use docs/adr/README.md if the placement depends on a lasting architecture or compatibility decision.