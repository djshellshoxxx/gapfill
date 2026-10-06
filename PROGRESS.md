# GapFill — Progress

Tracks where the project stands against `include.md` (required features) and
`theme.md` (visual identity spec). Update this at the end of every work session.

Version: 1.0.0 · Type: audio **effect** (not an instrument)

## include.md checklist

| Requirement | State | Notes |
|---|---|---|
| Help section in-plugin | **Done** | `Source/gui/HelpView.cpp` covers features/workflow/GUI, and now shows the version. |
| Custom exe icon | **Done** | `Resources/icon.png` + `icon_small.png`, wired via `ICON_BIG`/`ICON_SMALL`. |
| Preset bank with descriptive names | **Done** | Factory bank in `Source/Presets.cpp`; user presets load from disk. |
| Reset button | **Done** | Header button → `GapFillAudioProcessor::resetAllParameters()`. |
| Random button | **Done** | Random + Random All; each press reseeds from defaults so no two presses match. |
| Save / Save As / Open / Options dropdown | **Done** | The FILE button in the top bar opens it. Save/Save As/Open use `.gapfill` files anywhere on disk. |
| Options page (tooltips on/off, audio + MIDI device choice) | **Done** | Tooltip toggle works and persists. The Standalone device hookup now compiles and ships in both `GapFill.exe` and the VST3 (where the runtime `StandalonePluginHolder::getInstance()` check disables it). Still unexercised by hand - see Next up. |
| Export audio to WAV | **N/A** | `include.md` exempts effects. Revisit only if a "special function" export is wanted. |
| Right-click on any control | **Done** | Reset to default, set exact value, MIDI learn / forget CC / clear all. |
| Hover tooltips | **Done** | Set on every control; the on/off switch belongs to the Options page. |

## theme.md checklist

| Element | State |
|---|---|
| Palette, typography, 8px grid | **Done** — `Source/gui/LookAndFeel.cpp` |
| Knobs (value arc, indicator, centre dot, hover readout, 80ms ease-out) | **Done** — `Source/gui/Knob.cpp` |
| Sliders, buttons/toggles | **Done** |
| Meters (gradient, peak hold, numeric readout) | **Done** - was wrongly marked done before; `LevelMeter` was a flat tinted bar with none of it. |
| Plugin header strip | **Done** |
| Signature diagonal notch | **Done** — painted by `GapFillAudioProcessorEditor::paint()`. |
| Version in 9px muted mono, bottom-right | **Done** — `footerBounds()` reserves a 12px strip. |

## Session log

### 2026-10-06 - preset loader hardening

- Added test-first coverage for oversized and malformed external preset files.
- Added a single bounded preset loader in the processor with a 1 MB maximum file size.
- Empty, missing, malformed XML, wrong-state-type, duplicate-known-parameter, non-numeric, non-finite and out-of-range known values are rejected before the current sound is changed.
- Both FILE -> Open and user-preset dropdown loading now use the same validated path.
- Rejected preset files show a user-facing reason and the preset browser is restored to the still-active sound instead of visually selecting a file that never loaded.
- Updated Help and the CDL compliance matrix to document the behavior.


### 2026-10-06 - state validation, compliance and CI preflight

- Continued from merged PR #1 on a fresh branch.
- Added atomic validation for external `.gapfill` parameter trees and saved project state. Known parameter values must be numeric, finite, unique, and normalised to 0..1 before anything is applied.
- File Open now uses the processor's validated state path instead of calling `APVTS::replaceState()` directly.
- Reordered project-state restore so malformed sound state cannot partially change editor preferences or MIDI mappings before being rejected.
- Added regression coverage for malformed parameter state and for metadata/MIDI atomicity.
- Added `tools/validate_repo.py` plus unit tests. It checks stable parameter IDs, duplicate IDs, factory-preset references, shared-standard/compliance files, and expected CMake/CI wiring without needing JUCE or a compiler.
- Added a dependency-free CI preflight job before the Linux and Windows builds.
- Added `docs/COMPLIANCE.md` with the applicable CDL profiles, implemented areas, exceptions and remaining manual validation.
- README now explicitly documents privacy/network behavior and links the shared standard/compliance matrix.
- Connector-level execution of the same structural preflight logic passes: 64 stable IDs, 52 unique preset parameter references, zero errors.
- GitHub-hosted Actions runners remain an external infrastructure issue: jobs are still being terminated before step 1, including documentation-only commits on master.


### 2026-10-06 - CDL baseline follow-through: bounded MIDI learn

- Audited the active development branch against the newly adopted Circuit Drift Labs shared plugin baseline.
- Found a concrete compliance gap in MIDI Learn: the existing right-click flow could stay armed indefinitely after the menu closed, and the editor gave no persistent indication that it was waiting for a controller.
- Added a regression test first: `midi_learn_times_out_and_can_be_cancelled`.
- MIDI Learn now has a 10-second bounded session driven by the processor's existing 15 Hz message-thread timer. Explicit cancellation remains available by right-clicking the same parameter again.
- The editor status line now takes priority while learning and shows the target parameter, remaining time, and cancellation instruction.
- Help now documents the bounded learn flow and includes the Circuit Drift Labs project URL.
- GitHub Actions is currently failing before either Linux or Windows job records any workflow steps. The same immediate failure is present on recent documentation-only `master` commits, so this is an Actions/runner startup problem rather than evidence of a compile/test regression. Local/runner compilation remains required before merging this branch.


### 2026-09-16, session 17 - CLAP, GitHub, and a broken VST3 bundle

- **The VST3 bundle was empty and nobody had noticed.** `GapFill.vst3/Contents`
  held only `Resources/moduleinfo.json` - no `x86_64-win/GapFill.vst3` binary.
  Cause: the `foreach(fmt VST3 Standalone)` loop set `RUNTIME_OUTPUT_DIRECTORY`
  on `GapFill_VST3`, which made JUCE link the DLL to `build/artefacts/<Config>/`
  *beside* the bundle rather than inside it. Every previous pluginval run
  passed because it was pointed at that loose DLL, which is a valid VST3 on
  Windows - but a host given the path this README documented would have found
  an unloadable shell. Only the Standalone exe is redirected now, and the loose
  stale DLL was deleted so it cannot be picked up again by mistake.
- **CLAP support added** via `clap-juce-extensions` (CLAP 1.2.7), checked out
  beside the source and gitignored, exactly like `JUCE/`. CMake skips the CLAP
  target with a message if the checkout is absent, so the other formats never
  break because of it. `GapFill.clap` is 8.9MB.
  Note: adding `clap_juce_extensions_plugin` puts `HAS_CLAP_JUCE_EXTENSIONS=1`
  and new include dirs on the shared `GapFill` target, so the first CLAP build
  recompiles all 37 SharedCode TUs and invalidates the existing VST3/Standalone
  binaries. Relink both afterwards, or you ship mismatched artefacts.
- **Pushed to GitHub**: `github.com/djshellshoxxx/gapfill`, private, `master` at
  372114e. There was no remote before this. `instructions.md` said not to push
  without being asked; this was asked for explicitly.
- **Full suite green against current code**: 42 tests in the main run plus the
  Combinatorial group 8/8 in a separate run - 50 tests, 0 failures. The first
  attempt at the Combinatorial group was killed part-way when it was competing
  with the CLAP build for a 4GB machine; re-run on an idle machine it passed in
  about 13 minutes.
- pluginval strictness 5 against the **fixed bundle**: 19 sections, exit 0, no
  failures, warnings or exceptions. "GapFill Audio: GapFill v1.0.0", 17 programs.
  Checked the real exit code, not `tail`'s through a pipe.
- README corrected: both the VST3 and the Standalone artefact paths it gave
  were wrong, and the test-runner path was missing `<Config>`. Added the CLAP
  and Linux build sections.
- **Linux build: prepared but NOT finished.** WSL Ubuntu 26.04 has gcc, g++,
  cmake, ninja and every JUCE dependency needed once `JUCE_WEB_BROWSER=0` and
  `JUCE_USE_CURL=0` are taken into account - only `libglu1-mesa-dev` and
  `mesa-common-dev` are missing, and neither matters because `juce_opengl` is
  not in `GAPFILL_MODULES`. So **no apt install and no sudo are required**,
  which matters because passwordless sudo is off in that distro. JUCE 8.0.15
  and clap-juce-extensions are cloned to `~/gapfill-linux` on the WSL side.
  The build itself was stopped by the harness's low-memory reaper during the
  CMake configure step (building `juceaide`); it had produced no objects. The
  build script is at `wslbuild.sh` in the session scratchpad. Nothing is wrong
  with it - it needs re-running on a machine with memory free.
- Memory management was most of the difficulty this session. Two orphaned
  `link.exe` processes from the previous session were holding 1.1GB before
  anything started. Killing stray `cl.exe`/`link.exe`/`MSBuild.exe` and running
  `wsl --shutdown` when the Linux side is idle are both worth doing before any
  build here.

### 2026-09-16
- Finished the MIDI-learn feature that was left half-wired:
  - added `Source/MidiLearn.cpp` to `GAPFILL_SOURCES`; set `NEEDS_MIDI_INPUT TRUE`
    and `JucePlugin_WantsMidiInput=1` so CCs actually reach the plugin;
  - processor now owns a `MidiLearnManager`, connects it after the parameters
    exist, feeds it MIDI from both `processBlock` and `processBlockBypassed`,
    and drains its FIFO on the existing 15Hz timer;
  - mappings are saved into and restored from the plugin state, and state
    written before this feature existed loads without wiping mappings;
  - implemented `findMidiLearnManager()`, which the right-click menu needs.
- Fixed a compile error in the new knob code: `Slider::valueToProportionOfLength`
  is non-const, so `FineSlider::targetProportion()` goes via
  `getNormalisableRange().convertTo0to1()` instead (same skew).
- `GapFillTests` builds clean (Debug). Suite was still running at end of session;
  everything through the Combinatorial group passed.

## Next up (in order)

Everything in `include.md` and `theme.md` is implemented, the full test suite is
green (50 tests, 0 failures), and the Release VST3 bundle passes pluginval. The
work is committed and pushed to `github.com/djshellshoxxx/gapfill` (private).
What is left needs a person at the keyboard, or a machine with more RAM:

1. Manual pass on the new UI: FILE menu save / save as / open, the options page
   and its tooltip toggle, right-click MIDI learn on a few controls, and the
   audio/MIDI device dialog in the standalone build (`GapFill.exe`).
2. Load the Release VST3 in a real DAW and check the MIDI-learn path end to end
   with a hardware controller - pluginval does not send CCs.
3. Check the meters by eye against the spec: the gradient, the 1.5s peak hold
   and its 20dB/s fall are only verified by construction, not by a test.
4. Finish the Linux build. Everything is staged for it (see session 17); it
   only needs a machine with memory free.
5. Load the CLAP in a CLAP host (Bitwig, Reaper 7, MultitrackStudio). It builds
   and links, but no CLAP host has opened it - pluginval does not validate CLAP.

### 2026-09-16, session 2
- The earlier full-suite run was killed by the OS for low memory, not by a
  failure: 48 passed, 0 failed before it died.
- Closed out `theme.md`: the signature notch is now painted, and the version
  marker sits bottom-right in 9px muted mono. `resized()` reserves a 12px
  footer strip so the mix panel cannot sit under it.
- The version has one source of truth: `project(... VERSION)` in CMakeLists
  reaches the UI as `GAPFILL_VERSION` and is read through `versionString()`.
- Help view now opens with the version under the title.
- Tooltip delay was hard-coded to 600ms; it now uses `metrics::tooltipDelayMs`
  (400ms), which is what the spec asks for.
- Editor tests pass. Use `GapFillTests.exe <group>` to run one group - the full
  Debug run takes over ten minutes and risks another OOM kill.

### 2026-09-16, session 3
- Added the FILE dropdown to the top bar, replacing the lone SAVE button:
  Save / Save As... / Open... / "Save as user preset..." / Options...
- `currentPresetFile` is what Save writes to. It is set by Save As, Open and by
  naming a user preset, and cleared when a factory preset is chosen so Save
  cannot silently overwrite the last file with unrelated settings. Save is
  greyed out until there is a file.
- New `Source/gui/OptionsView.{h,cpp}`: a centred panel with the tooltip toggle
  and the audio/MIDI row. The device button is deliberately disabled with an
  explanation when the host owns the devices, rather than being hidden.
- Tooltips on/off persists in the plugin state (root property "tooltips") and
  is applied by creating or destroying the `TooltipWindow`, so switching it off
  cannot leave a stray window behind.
- Editor tests pass.

### 2026-09-16, session 4
- Standalone audio/MIDI hookup written in `PluginEditor.cpp`: guarded on
  `JucePlugin_Build_Standalone` *and* a runtime
  `StandalonePluginHolder::getInstance()` null check. Both are needed - the
  macro is defined for the shared code whenever Standalone is in FORMATS, so
  the VST3 built from the same sources would otherwise offer a device dialog it
  does not own. NOT YET COMPILED: the test target excludes this branch.
- Two new Editor tests, both passing:
  - `preset_files_round_trip_through_disk` - writes a real `.gapfill`, resets,
    reloads, compares every parameter. Covers the FILE menu's contract.
  - `midi_mappings_survive_a_state_round_trip` - save/restore, the
    one-CC-one-parameter rule, and that state written before MIDI learn existed
    does not wipe current mappings.
- Both tests failed first time, and both were faults in the tests:
  - they compared `RangedAudioParameter::getValue()`, but a bool parameter keeps
    the raw normalised value it was set with while the tree stores the rounded
    one, so `pingPong` "changed" across a round trip while meaning the same
    thing. They now compare `getRawParameterValue()` - the value the DSP reads.
  - they used "out" as the output parameter id. It is "output"; `setMapping`
    silently ignores unknown ids, so the mapping was never made.
- Low memory (65-103MB free) blocked building for most of one cycle.

### 2026-09-16, session 5
- First attempt at building the VST3 in Release failed: including
  `juce_StandaloneFilterWindow.h` from `PluginEditor.cpp` gave 100+ errors
  (`AudioIODeviceCallback`, `AudioDeviceManager` undefined). That header is
  written to be pulled in by the standalone wrapper, which has already included
  `juce_audio_devices` and `juce_audio_utils`; it does not include them itself.
  Fixed by including both before it. This is exactly what the test target could
  not have caught - it excludes that branch.
- Gave the top-bar BYPASS button the shared right-click menu by making it a
  `ParamButton`. It was the one parameter-backed control that had no right-click
  and so could not be MIDI-mapped, which `include.md` asks for on every control.
  The button list in the constructor now names `juce::TextButton*` explicitly,
  since the initialiser list is no longer homogeneous.
- Release VST3 build re-running with `-p:CL_MPCount=1`.

### 2026-09-16, session 6
- **The plugin target now builds.** `GapFill.vst3` and `GapFill.exe` (Standalone)
  are in `build/artefacts/Debug/`.
- Three full-build attempts were OOM-killed on this 4GB machine. The fix was to
  compile one translation unit per MSBuild call - see the build notes below.
  Memory *rose* from 350MB to 800MB free while doing it, which confirms the
  concurrent `cl.exe` instances were the whole problem, not the build size.
- Compiling `PluginEditor.cpp` on its own found a second error in the BYPASS
  change: the C-style cast used to make the button list homogeneous broke
  `initializer_list` deduction (MSVC deduced `_Ty=int`). Replaced with an
  explicitly typed `std::initializer_list<juce::TextButton*>`.
- pluginval at strictness 5: 19 test sections, no failures, exit code 0. It
  reports "GapFill Audio: GapFill v1.0.0", so the version wiring from
  CMakeLists through to the plugin metadata works end to end.

### 2026-09-16, session 16 - COMPLETE
- Release link finished: `GapFill.vst3` and `GapFill.exe` rebuilt at 17:06,
  newer than the last source edit at 16:18, so they do contain the slider
  ticks, the resetPeak wiring and the flashProperty fix. Checked the timestamps
  rather than trusting exit code 0, because the previous artefacts were stale.
- pluginval strictness 5 on the Release VST3: 19 sections, exit 0, no failures,
  warnings or exceptions.
- Everything that can be done without a person at the keyboard is done.

### 2026-09-16, session 15
- README brought back in line with the code: `MidiLearn.*` and the options
  overlay added to the source layout, the editor line now mentions the FILE
  menu, the test-runner path gained its missing `<Config>` directory, and the
  build section notes the `-m:1 -p:CL_MPCount=1` pair needed on a small machine.
- Release link started with 828MB free. The Release objects on disk date from
  14:26 and so predate the slider ticks, the resetPeak wiring and the
  flashProperty change - but the link triggers MSB8015, which forces a full
  sequential recompile, so the artefacts it produces will be current. Verify the
  timestamps before trusting them.

### 2026-09-16, session 14
- **Full suite verified green in one pass against current code**: 22 across the
  small DSP groups, Plugin 24/24, Editor 4/4, Combinatorial 8/8. 58 tests, no
  failures.
- Turned the audit on my own changes and found the same smell twice:
  - `LevelMeter::resetPeak()` had no callers. Given one: RESET now clears the
    held peaks through `MixPanel::resetMeterPeaks()`, since a peak from the old
    settings means nothing once they have been thrown away.
  - `FlashButton::flashProperty` existed but `drawButtonBackground` compared
    against the literal `"gapfillFlash"`. Renaming the constant would have
    silently broken the flash. The LookAndFeel now uses the constant.
- Release link still not done: it needs ~600MB free and the machine has been
  sitting at 115-400MB. The compile is finished (37/37 objects), so only the
  link and a pluginval run remain.

### 2026-09-16, session 13
- The knob dial cap and the HelpView shadow/radius change from last session
  both compile; Editor 4/4 and Plugin 24/24 green.
- Font chain verified against the spec and correct: Inter -> Space Grotesk ->
  system sans, and JetBrains Mono -> IBM Plex Mono -> system mono, resolved once
  against the installed typefaces with a fall-through to the platform default.
- **Open deviation: the 8px grid.** The spec says all spacing is a multiple of
  8, or 4 for fine detail. Measuring every inset and gap in the layout code
  gives 40 values that are not: mostly `reduced (2, 0)` and `reduced (1, 0)`
  hairlines between adjacent top-bar buttons, plus `reduced (10, 8)`,
  `reduced (10, 6)`, `removeFromLeft (10)` and `removeFromRight (6)`. Panels and
  sections do use `metrics::grid`; it is the top bar that is ad hoc.
  This has NOT been changed. The top bar packs fixed-width buttons into a
  880px minimum window, so widening the insets could overlap them, and the
  result cannot be checked without looking at it. Worth one deliberate layout
  pass by someone who can see the window, not a blind edit.

### 2026-09-16, session 12
- Release COMPILE finished (37/37 objects); the LINK was OOM-killed, and memory
  then sat at 108-150MB for the whole session, so no build ran. The two changes
  below are WRITTEN BUT UNCOMPILED.
- `metrics::knobSmall` / `knobLarge` were declared but never used: `GapKnob`'s
  `large` flag only changed label font sizes, and the dial grew to fill whatever
  cell it was given. Since the LookAndFeel clamps its arc/indicator scaling at
  1.6x, past that point the dial kept growing while its details did not. The
  dial is now capped at 1.6x the spec size, so the two stop together.
- The help and options overlays disagreed: `HelpView` used a hardcoded 8px
  radius and no shadow, `OptionsView` used `metrics::panelRadius` and
  `drawShadow`. HelpView now matches.
- Help text coverage: checked every parameter against the help prose. An exact
  name match flagged 9, but reading the text shows all are covered - "SYNC MAX
  FILL", Stutter, Delay "with optional ping-pong", "SOLO / AUDITION", and the
  per-lane enable/amount/intensity/probability paragraph. The only controls not
  named outright are the three tempo-division selectors, which are described by
  behaviour instead. Not worth changing.
- `drawShadow` is used only by the overlays, which is the right reading of the
  spec: it says panels get depth from shadows and gradients, but also "never
  boxes-inside-boxes", so the inline panels stay flat.

### 2026-09-16, session 11
- **Slider tick marks were never implemented.** The spec asks for 1px muted
  lines at a logical interval outside the track; `drawLinearSlider` drew only
  track, fill and thumb. Added ticks at quarters (the sensible generic reading -
  these sliders carry percentages and amounts, not decibel scales), 1px, muted
  at 45%, sitting 3px outside the track on both orientations.
- Verified and found correct, rather than assumed:
  - knob value arc: 3px wide, 4px gap, drawn outside the body, and the 270-degree
    7-to-5 o'clock sweep really is applied - `FineSlider` sets
    `setRotaryParameters (metrics::rotaryStart, metrics::rotaryEnd, true)`;
  - knob indicator (2px, rounded cap), centre dot (4px, muted at default);
  - section header accent bar: 2px wide, 12px tall, left of the title;
  - slider thumb: 16x24 rounded rect with the knob body gradient.
- All 50 non-Combinatorial tests still green after the tick change.

### 2026-09-16, session 10
- Audited both spec files against the code instead of trusting this file.
- **The 100ms momentary flash was never implemented.** `metrics::flashMs` had no
  users, and the comment in `drawButtonBackground` claimed the while-held
  brighten *was* the flash - it is not, it lasts as long as the button is held.
  Added `FlashButton` (Knob.h): on click it sets a component property for
  `metrics::flashMs` and repaints, and the LookAndFeel reads that property, so
  the drawing stays in one place. The momentary top-bar buttons use it; the A/B
  and BYPASS toggles keep showing state instead.
- `metrics::windowRadius` (6px main window) is also unused, but legitimately so:
  a hosted editor cannot round its own window, the host owns the frame. Said so
  at the declaration rather than leaving it looking unfinished.
- Verified rather than assumed: tooltip pill styling is per spec; 4px panel and
  2px control radii are used throughout; RANDOM never repeating is genuinely
  covered by `reset_and_randomise_behave` (8 presses, no duplicate signature),
  as is reset restoring every default.
- Editor 4/4 and Plugin 24/24 still green after the flash change.

### 2026-09-16, session 9
- **Release builds done and validated.** `build/artefacts/Release/` now has
  `GapFill.vst3` (9.4MB) and `GapFill.exe` (8.8MB) - about a third the size of
  the Debug ones. pluginval at strictness 5 against the Release VST3: 19 test
  sections, exit 0, no failures, warnings or exceptions. Reports
  "GapFill Audio: GapFill v1.0.0", 17 programs.
- The MSB8015 forced rebuild completed without incident because
  `-p:CL_MPCount=1` kept it sequential.

### 2026-09-16, session 8
- **The whole suite is green.** Every test has now run and passed: 22 across the
  small DSP groups, Plugin 24/24, Editor 4/4, Combinatorial 8/8. The last
  Combinatorial test (`algorithm_weight_pairs_in_auto_mode`, 22s) had never run
  before - earlier runs were killed for memory before reaching it.
- Relinked and re-ran Editor and Plugin against the new `LevelMeter`: both green.
  The editor open/close test exercises the meter's paint and its new timer.
- Found and fixed a bug in the one-TU-at-a-time build script: it compared each
  object against its own `.cpp` mtime only, so a header edit did not mark the
  dependants stale. It had already skipped `WaveformGapView.cpp` and
  `Panels.cpp` in Release, which would have shipped a Release binary with the
  old meter in it. Our own translation units are now compared against the newest
  file anywhere in `Source/`.

### 2026-09-16, session 7
- Ran the suite group by group: 22 passed across the small DSP groups,
  Plugin 24/24, Editor 4/4. Combinatorial (8 tests, the slowest group) was
  still running at the end of the session.
- Re-reading `theme.md` against the code turned up one item this file had
  wrongly marked done: the meters. `LevelMeter` was a flat tinted bar that went
  red above 0.99 - no gradient, no peak hold, no readout. Now implemented per
  spec: one teal -> orange -> yellow -> red gradient across the track (clipped
  to the filled part, so a given level is always the same colour), a peak line
  holding 1.5s then falling at 20dB/s, and a numeric peak readout in 9px mono
  when the meter is at least 26px wide.
- The per-meter tint is gone: the spec says every meter uses the same gradient,
  so the colour argument no longer meant anything.
- That change compiles but is NOT yet linked or run - the Combinatorial test run
  had the test binary open. Relink and re-run Editor/Plugin before trusting it.

## Build notes

This machine has ~4 GB RAM and often under 1 GB free, so build single-threaded:

    cmake --build build --target GapFillTests --config Debug -- -m:1

Use `-m:1` (dash), not `/m:1` — Git Bash rewrites the slash form into a path.

`-m:1` only limits how many *projects* MSBuild builds at once. JUCE's
`juce_recommended_config_flags` also adds `/MP`, which lets a single project run
one `cl.exe` per core - six of them were seen during the Release plugin build,
taking free memory down to ~130MB. To cap that as well:

    cmake --build build --target GapFill_VST3 --config Release -- -m:1 -p:CL_MPCount=1

That is the setting to reach for if a build gets OOM-killed again.

Also: stopping a build leaves orphaned `cl.exe`/`MSBuild.exe` processes holding
a few hundred MB. Check for and kill those before retrying, or the retry starves.

To check that one file still compiles without building a whole target - useful
when the plugin target has never been built and RAM is short - compile it alone:

    MSBuild build/GapFill.vcxproj -t:ClCompile -p:Configuration=Debug         -p:Platform=x64 -p:CL_MPCount=1 -p:SelectedFiles=<abs path to .cpp>

Caveat: compiling file-by-file leaves MSBuild's tracker logs inconsistent, so
the next ordinary `cmake --build` reports MSB8015 and forces a full rebuild of
the target. That is survivable as long as `-p:CL_MPCount=1` is passed, which
keeps the forced rebuild sequential - without it, it will be OOM-killed.

This is the only way to check code behind `JucePlugin_Build_Standalone`, since
`GapFillTests` does not define it. Note that the macro is set on the shared
`GapFill` target, so the VST3 compiles that branch too - only the runtime
`StandalonePluginHolder::getInstance()` check keeps the two apart.
