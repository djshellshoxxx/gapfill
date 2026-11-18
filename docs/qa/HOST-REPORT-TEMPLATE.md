# Host acceptance report — UNVERIFIED TEMPLATE

Copy this file before use. This template records no executed checks.

## Identity and environment

| Field | Value |
|---|---|
| Tester and date | Not recorded |
| GapFill version / source commit | Not recorded |
| Package URL / SHA-256 | Not recorded |
| Loaded binary path / SHA-256 | Not recorded |
| Host and version / plugin format | Not recorded |
| OS / architecture | Not recorded |
| Audio interface / driver / driver version | Not recorded |
| Physical MIDI controller / connection / routing | Not recorded |
| Sample rate / block size / channel layout | Not recorded |
| Input material / project / evidence location | Not recorded |
| Prior release artifact used for compatibility | Not recorded |

Follow [07 QA and release, section 4](https://github.com/djshellshoxxx/gapfill/blob/master/docs/dev/07-QA-AND-RELEASE.md#4-manual-scripts-run-in-each-host-record-passfail--evidence)
and [VALIDATION](https://github.com/djshellshoxxx/gapfill/blob/master/docs/VALIDATION.md). The descriptions below identify steps; the
linked scripts define their full procedure and expected behaviour.

## M0 results

Use pass, fail, blocked, or unverified. Include evidence for each pass and a
reproduction/defect for each failure. Record all six M-1, five M-2, five M-3,
and four M-4 steps; do not mark a whole group passed from a single smoke check.

| Step | Check | Result | Observation / evidence / defect |
|---|---|---|---|
| M-1.1 | Scan and insert on stereo drum bus | Unverified | Not run |
| M-1.2 | Visualiser/meters; reported latency and dry null below -60 dB | Unverified | Not run |
| M-1.3 | Fill around 50%; audible gaps and protected transients | Unverified | Not run |
| M-1.4 | Host and plugin bypass transitions / state | Unverified | Not run |
| M-1.5 | Editor close/reopen ten times during playback | Unverified | Not run |
| M-1.6 | Project reload, seeded recall and two identical offline renders | Unverified | Not run |
| M-2.1 | All factory presets during playback; no dropout or excessive peak | Unverified | Not run |
| M-2.2 | User preset in a second instance | Unverified | Not run |
| M-2.3 | Save / Save As / Open and corrupt file rejection | Unverified | Not run |
| M-2.4 | Five Undo operations after Random / Surprise / presets | Unverified | Not run |
| M-2.5 | Previous-release project recall | Unverified | Not run |
| M-3.1 | Audition while playing and stopped; preview isolation | Unverified | Not run |
| M-3.2 | Audition during bypass and Wet 0 | Unverified | Not run |
| M-3.3 | Rejection reasons, numbers, advice and top-three list | Unverified | Not run |
| M-3.4 | Sync at 60/174 BPM, stutter/delay grids and tempo changes | Unverified | Not run |
| M-3.5 | Standalone Options tempo and missing host tempo | Unverified | Not run |
| M-4.1 | CC learn for Fill, Macro and Audition; command re-arm | Unverified | Not run |
| M-4.2 | Note learn, velocity and channel filtering | Unverified | Not run |
| M-4.3 | Ten-second learn timeout and cancellation | Unverified | Not run |
| M-4.4 | Physical controller in a real host | Unverified | Not run |

For checks involving several actions, retain the result of each action in the
observation/evidence. Record standalone checks separately from DAW checks even
when they share this report. For any version-specific missing feature, retain
the row and record its disposition instead of inventing a pass.

## Hardware and keyboard results

These rows supplement M-1..M-4 using `docs/VALIDATION.md`. Record device
identity, actual observations and evidence. They remain unverified until run.
The VST3 MIDI report applies to VST3; for a CLAP/AU host report, link a separate
VST3 report rather than claiming these checks passed in a different format.

| Step | Check | Result | Observation / evidence / defect |
|---|---|---|---|
| VST3 MIDI Learn 1 | Load VST3 in DAW | Unverified | Not run |
| VST3 MIDI Learn 2 | Right-click Wet; choose MIDI Learn | Unverified | Not run |
| VST3 MIDI Learn 3 | Move physical MIDI CC | Unverified | Not run |
| VST3 MIDI Learn 4 | Learn exits; mapping persists | Unverified | Not run |
| VST3 MIDI Learn 5 | Move CC; Wet follows smoothly | Unverified | Not run |
| VST3 MIDI Learn 6 | Save/reopen project | Unverified | Not run |
| VST3 MIDI Learn 7 | Mapping survives | Unverified | Not run |
| VST3 MIDI Learn 8 | Reassign CC; old assignment released | Unverified | Not run |
| VST3 MIDI Learn 9 | Ten-second Learn timeout | Unverified | Not run |
| Standalone device settings 1 | Launch Standalone | Unverified | Not run |
| Standalone device settings 2 | Open Options audio/MIDI settings | Unverified | Not run |
| Standalone device settings 3 | System driver/backend appears | Unverified | Not run |
| Standalone device settings 4 | Select output device | Unverified | Not run |
| Standalone device settings 5 | Change sample rate/block size; responsive | Unverified | Not run |
| Standalone device settings 6 | Enable MIDI input; Learn receives controller | Unverified | Not run |
| Standalone device settings 7 | Reopen panel; settings valid | Unverified | Not run |
| Standalone device settings 8 | Audio for at least one minute; no dropout/runaway/lockup | Unverified | Not run |
| Keyboard navigation 1 | Navigation off; DAW shortcuts remain available | Unverified | Not run |
| Keyboard navigation 2 | Enable Keyboard Navigation | Unverified | Not run |
| Keyboard navigation 3 | Tab/Shift+Tab traverse visible controls | Unverified | Not run |
| Keyboard navigation 4 | Arrow keys operate sliders/combos | Unverified | Not run |
| Keyboard navigation 5 | Space/Return operate buttons | Unverified | Not run |
| Keyboard navigation 6 | Help/Options visible controls join traversal | Unverified | Not run |
| Keyboard navigation 7 | Disable Navigation; focus participation stops | Unverified | Not run |

## Defect triage

No findings have been assessed in this template. “No findings” is not a test result.

| Issue / B-n ID | Affected artifact / host | Reproduction / observed result | Severity and rationale | Disposition / fix / retest evidence |
|---|---|---|---|---|
| Not assessed | Not assessed | Not assessed | Not assessed | Not assessed |

## Acceptance decision

- M-1: Unverified.
- M-2: Unverified.
- M-3: Unverified.
- M-4: Unverified.
- VST3 MIDI Learn hardware checks: Unverified.
- Standalone device settings: Unverified.
- Keyboard navigation: Unverified.
- Failures, blocked steps, missing evidence and version exceptions: Not assessed.
- Reviewer, review date and decision: Not reviewed.

This is one host report. M0 additionally requires a second DAW report, green
integration CI and a triaged defect list. M7 signing, platform acceptance and
soak requirements are not established by this report.
