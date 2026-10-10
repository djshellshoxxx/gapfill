# 07 - QA and release

## 1. Test layers

| Layer | What | Where it runs | Gate |
|---|---|---|---|
| Repo preflight | stable IDs, lock files, hot files, presets resolve, docs present | every PR (<2 min) | required |
| Unit/DSP/Plugin tests | `GapFillTests` groups (Dsp, Signals, Plugin, features...) | every PR, Linux + Windows | required |
| Golden regression | scenarios A-F vs stored output (per platform) | every PR | required |
| State fixtures | 0.0.1 states + corrupt/oversize files | every PR | required |
| Combinatorial | parameter sweeps (~13 min) | nightly | required for release |
| Sanitizers | ASan+UBSan full suite; TSan on queue/threading tests | nightly | required for release |
| Plugin validators | pluginval strictness 10 (VST3), clap-validator (CLAP), `auval` (AU) | every PR (Linux/Win), nightly (mac) | required |
| Perf | scenario E timing vs `perf_baseline.json` | nightly | advisory -> required at M3 |
| Standalone smoke | launch 5 s and exit cleanly | every PR | required |
| Manual host scripts | real DAWs, real MIDI, real audio device | each milestone beta + release | required for release |
| Soak | 8 h continuous playback with random automation | before 1.0 and each minor | required for 1.0 |

## 2. Compatibility matrix (what must be tested, minimum)

| Axis | Values |
|---|---|
| OS | Windows 10/11 x64, Ubuntu LTS x64, macOS 12+ (arm64 + x86_64, after E-5) |
| Formats | VST3, CLAP, Standalone, AU (macOS) |
| Hosts (manual) | Reaper (all OS), Ableton Live (Win/mac), FL Studio (Win), Bitwig (CLAP, Linux/Win), Logic (AU, mac), Cubase or Studio One (stretch) |
| Sample rates | 44.1, 48, 88.2, 96, 192 kHz |
| Block sizes | 32, 64, 128, 480, 512, 1024, 4096, and host variable-size |
| Channel layouts | stereo, mono-in/stereo-out where the host allows, stereo + stereo sidechain |
| Transport | playing, stopped, looping, seek, tempo change, no tempo (standalone) |
| UI | 100/125/150/200 % scale, minimum size, large size, editor open/close while playing |

Automated tests cover sample rate x block size x layouts; hosts/OS rows are manual.

## 3. Automated scenario catalogue (shared by golden, perf, fuzz)

| ID | Material | Notes |
|---|---|---|
| A | 8 bars drum loop, 120 BPM, default settings, seed 1 | primary golden |
| B | same, Spectral detector mode | band logic |
| C | click track in sidechain, Sidechain source | sidechain path |
| D | forced Tape Stop, then each algorithm in turn | per-algorithm finite/bounded |
| E | complexity 100 / chaos 100 / fill 100 / density 100, 4 voices busy | stress + perf |
| F | bypass on/off every bar | transitions, latency crossfade |
| G | transport jumps/loops every 2 bars | `noteTransportJump`, loop wrap |
| H | tempo ramp 60 -> 180 BPM | sync correctness |
| I | silence, DC, full-scale noise, denormals | robustness |

## 4. Manual scripts (run in each host; record pass/fail + evidence)

Evidence template `docs/qa/<release>/<host>-<os>.md`: GapFill version/commit, host + version,
OS, interface/driver, format, result per step, notes, screenshots/WAV.

**M-1 Load and basics (every host, every format).**
1. Scan/insert GapFill on a stereo drum bus with a looped drum clip; press play.
2. Expected: loads without errors; visualiser scrolls; meters move; latency reported matches Lookahead (host PDC display or alignment test against a duplicated dry track: null test below -60 dB with Wet 0).
3. Raise Fill to ~50 %: fills audible only in gaps; kick/snare transients intact (A/B bypass).
4. Bypass via the host and via the plugin bypass: no click, state returns.
5. Close/reopen editor 10x while playing: no crash, UI correct.
6. Save project, close, reopen: sound identical (same seed -> same fills at the same positions). Render offline twice: bit-identical files.

**M-2 State and presets.**
1. Cycle all factory presets during playback: no dropout, no loud transient (> 0 dBFS from the drum loop).
2. Save a user preset, load it in a second instance: identical.
3. FILE menu Save / Save As / Open on a `.gapfill`; Open a corrupt file: friendly message, sound unchanged.
4. Undo 5 times after Random / Surprise / presets.
5. Load a project saved with the previous release: loads, sounds the same (fixtures list).

**M-3 Essentials (E-1, E-2, E-3).**
1. Audition: playing -> press AUDITION 5x: previews audible, distinct variation, not in History/Lock; visualiser shows preview colour. Stop host -> press: "Start playback to audition", nothing plays when restarting.
2. Audition during bypass: nothing, message shown. Audition with Wet 0: silent.
3. Reasons: provoke each reason (checklist in `03` E-3 table; e.g. Fill 0 -> FillOff; Min Gap 400 ms -> TooShort with numbers; Retrig Guard high -> TooSoon; Density 5 % -> DensityRoll). Status text and advice correct; "Why so few fills?" lists the top 3.
4. Sync: at 60 and 174 BPM set Stutter 1/2 and 1/32, Delay 1/4: the effective ms shown matches the grid; clamped badge appears when expected; changing host tempo during playback retimes the next fill.
5. Standalone: Sync follows the Options tempo; with no host tempo it does not silently use 120.

**M-4 MIDI.**
1. MIDI Learn a CC to Fill, to a Macro, and to AUDITION (command); knob moves; command fires once per press and re-arms.
2. Note trigger (U-3): learn a note; velocity changes level; other channels ignored when a channel is set.
3. Learn timeout (10 s) and cancel (right-click again).
4. Hardware controller (not a virtual port) in at least one host.

**M-5 Performance features (Phases 2-4).**
1. Capture/Recapture: latch, change the incoming audio, fills still sourced from the latch; release returns.
2. Lock: lock a fill; 20 following gaps keep character; unlock; save/reload project -> lock restored but silent until next gap.
3. History: 8 chips, audition/lock/like/save recipe/export.
4. Like: next 4 fills lean to the liked character; stops after 4.
5. Macros: sweep each 0 -> 100 during playback: no zipper/clicks; automation of macro in the host; Edit mappings; reset; rename.
6. Personalities: each of 8 sounds distinct on the same loop; Off == original.
7. Breathing / Phrase: with the host looping 8 bars and a known time signature (4/4, 6/8, 7/4) density/probability follow bars; with stopped transport neutral.
8. Mistakes at 100 %: audible rare variations, no clicks, no spill into next hit.
9. Auto-Calibrate on 3 different tracks: sensible results, Revert works.
10. Export: export a fill, drag into the DAW; waveform matches what was heard.

**M-6 Robustness.**
1. Change sample rate and buffer size while loaded; reload plugin.
2. Rapid parameter automation on every parameter (host "write" all) for 5 minutes: no crash/noise burst.
3. Extreme settings (Fill 100, Complexity 100, Chaos 100, Max Fill 2000 ms, Lookahead max): output limited, no runaway level.
4. Offline bounce vs realtime: identical audio (determinism).
5. Two instances (different seeds) on two tracks: independent; CPU reasonable.
6. 8 h soak (before 1.0): no memory growth (> 1 % RSS over 8 h), no xruns attributable to GapFill, no stuck voices.

**M-7 UI and accessibility (Phase 5).**
1. Scale 100-200 %, min size, large; Simple <-> Advanced; keyboard navigation mode traverses visible controls only; screen reader names announce every control; Reduced Motion removes decorative animation; first-run overlay appears once per user.
2. Preset browser: search, categories, favourites, prev/next, rename/delete user preset, 500 user presets scan does not stall the editor.

**M-8 Installer/platform.**
1. Windows: clean VM install of each component; upgrade-in-place over previous version; uninstall leaves user presets; SmartScreen/signature shown correctly.
2. macOS (after E-5): Gatekeeper opens the notarized pkg; AU validated in Logic; plugins found by Reaper/Ableton/Logic.
3. Linux: zips unpack to `~/.vst3` / `~/.clap`; Reaper and Bitwig find them.

## 5. Severity-driven release checklist

Block the release on any open S1/S2. Then:

```
[ ] master green on CI (required checks) at the release commit
[ ] nightly (Combinatorial, sanitizers, perf) green within the last 24 h
[ ] pluginval 10, clap-validator, auval (mac) pass on the release artefacts (not just the build tree)
[ ] manual scripts M-1..M-6 (+ M-7/M-8 when applicable) done on >= 2 hosts per OS, evidence committed
[ ] state fixtures from every previous public release load unchanged
[ ] version bumped once (CMake); README "What's new" + Help version text; GAPFILL_VERSION shown in UI
[ ] docs/COMPLIANCE.md and PROGRESS.md updated; unverified items listed explicitly
[ ] artefacts built from the tagged commit, signed (E-6), SHA256SUMS + detached signature published
[ ] installer: upgrade, uninstall, per-component, licence text (AGPL) verified
[ ] release notes list user-visible changes + known issues
[ ] tag pushed; GitHub release created as draft, reviewed, then published
[ ] post-release: monitor issues 7 days; hotfix process via release/<minor> branch
```

## 6. Risk register (top risks, highest first)

| # | Risk | Likelihood / impact | Detection | Mitigation |
|---|---|---|---|---|
| 1 | CI unavailable -> regressions go unseen | high / high | no green checks | E-4; `tools/dev/check.sh` as authoritative local gate; self-hosted runner |
| 2 | A feature alters legacy sound/determinism | med / high | golden + RNG tests | Neutral-default rule, frozen draw order, salted sub-RNGs |
| 3 | Audio-thread allocation/lock slips in | med / high | AllocGuard, TSan, review checklist | Preallocation rules, bounded per-block work |
| 4 | Old sessions/presets break | med / high | fixtures | stateVersion, soft-fail subtrees, fixtures from 0.0.1 |
| 5 | Never tested in real hosts | high / med | M-scripts | E-7 early (before Phase 0) and at every milestone |
| 6 | Merge conflicts in hot files stall work | high / med | hot-file checker | F0 refactor, slots/registries, <=3 PRs |
| 7 | Macros/Personalities confusing UX | med / med | user feedback after M4 | effective ring, Simple view, first-run, presets that demonstrate them |
| 8 | Export/Calibrate memory or CPU spikes | med / med | Perf + soak | on-demand allocation off audio thread, bounded copy, worker thread priority |
| 9 | macOS/AU surprises (AU state, notarization) | med / med | `auval`, Logic test | start E-5 early on a CI mac; unsigned beta zips as fallback |
| 10 | Tempo/ts info missing or wrong in some hosts | med / med | M-3.4, H scenario | neutral fallback, tempo memory, tests with invalid transport |
| 11 | Parameter-count growth (+23) overwhelms hosts/automation lists | low / med | host checks | group naming prefixes, hide advanced from automation via categories where supported |
| 12 | Scope creep / pro-version decision late | med / med | backlog review | decide Free/Pro split before U-2/C-2 UI (`05` backlog) |

## 7. Metrics worth tracking (cheap, local)

CI duration (warm), flaky-test count (target 0), open S1/S2, golden changes per
release, `processBlock` mean/p99.9 vs baseline, preallocated MB, number of hot-file
overrides, PR size median, time from PR open to merge, escaped defects per release
(bugs filed within 14 days).
