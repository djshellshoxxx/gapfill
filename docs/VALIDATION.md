# GapFill Validation Matrix

This document defines the release validation procedure for GapFill. It separates automated gates from checks that require a real DAW, MIDI controller, or audio interface.

## Automated gates

### Repository preflight

```bash
python tools/validate_repo.py
python -m unittest tools/test_validate_repo.py
```

The preflight checks stable parameter IDs, factory-preset references, required specifications, and the presence of the VST3, CLAP, Standalone and host-validation CI gates.

### JUCE regression suite

Linux:

```bash
xvfb-run -a build-ci/artefacts/GapFillTests
```

Windows:

```powershell
.\build-ci\artefacts\Release\GapFillTests.exe
```

The suite covers DSP stability, state/preset migration, latency and bypass alignment, sidechain fallback, editor lifecycle, MIDI mapping/learn, file validation, reduced motion, keyboard navigation, and construction of JUCE's audio/MIDI device selector.

### VST3 host validation

GapFill is validated with Tracktion pluginval at strictness level 10 on Linux and Windows.

```bash
pluginval --strictness-level 10 --validate /path/to/GapFill.vst3
```

A release passes only when pluginval exits 0.

### CLAP host validation

GapFill is validated with free-audio clap-validator on Linux and Windows.

```bash
clap-validator validate --only-failed /path/to/GapFill.clap
```

A release passes only when clap-validator exits 0.

### Standalone startup smoke

CI builds the actual `GapFill_Standalone` target and launches it for five seconds. The process must remain alive for the complete smoke window; an early exit is a failure.

The unit suite also constructs `juce::AudioDeviceManager` and `juce::AudioDeviceSelectorComponent` so the device-settings UI is exercised even on runners with no physical audio hardware.

## Hardware / DAW acceptance

These checks cannot be truthfully substituted by a headless runner because they depend on a real host, physical MIDI input, and an operating-system audio device.

### VST3 MIDI Learn

1. Load GapFill VST3 in a DAW.
2. Right-click Wet and choose MIDI Learn.
3. Move a physical MIDI CC control.
4. Confirm the status line exits Learn state and the mapping persists.
5. Move the control again and confirm Wet follows it smoothly.
6. Save and reopen the DAW project.
7. Confirm the mapping survives.
8. Reassign the same CC to another parameter and confirm the old assignment is released.
9. Start Learn and do nothing for 10 seconds; confirm it automatically disarms.

Automated counterpart: `midi_learn_works_through_the_processor_midi_path`, timeout/cancel tests, CC flood test, and state-round-trip mapping tests.

### Standalone device settings

1. Launch GapFill Standalone.
2. Open FILE -> Options -> AUDIO / MIDI SETTINGS.
3. Confirm the system audio driver/backend appears.
4. Select the desired output device.
5. Change sample rate and buffer size and confirm the application remains responsive.
6. Enable a MIDI input and confirm MIDI Learn receives a controller.
7. Close and reopen the device panel and confirm settings remain valid.
8. Run audio for at least one minute and confirm no dropouts, runaway level, or UI lockup.

Automated counterpart: Standalone target build, five-second startup smoke, device-selector construction test, multiple sample-rate/block-size tests, and long-run DSP stability tests.

### Keyboard navigation

1. With Keyboard Navigation off, open the plugin and confirm normal DAW shortcuts are not captured merely because the editor opened.
2. Enable FILE -> Options -> Keyboard Navigation.
3. Use Tab and Shift+Tab to move through visible interactive controls.
4. Use arrow keys on sliders/combos.
5. Use Space or Return on buttons.
6. Open Help and Options and confirm their interactive controls join traversal when visible.
7. Disable Keyboard Navigation and confirm control focus participation stops.

Automated counterpart: `keyboard_navigation_is_opt_in_and_reaches_interactive_controls` verifies the editor itself remains non-focusable while at least 20 interactive descendants become focusable when opted in.

## Release rule

A release candidate is code-complete only when repository preflight, JUCE tests, pluginval, clap-validator, and Standalone startup smoke pass. Hardware/DAW acceptance must be completed on at least one supported Windows system before a public binary is labelled release-ready.
