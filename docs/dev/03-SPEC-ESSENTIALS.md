# 03 - Essential features

Tickets: **E-1** Fill Audition, **E-2** Tempo-sync audit and fixes, **E-3** "Why
didn't it fill?" reasons, **E-4** Working CI, **E-5** macOS / AU, **E-6** Signing
and installer trust, **E-7** Real-host QA (process, scripts in `07`).

All engine tickets depend on Phase 0 (`02-FOUNDATIONS.md`): `RejectReason`,
`FillRecipe`, the queues, `PerformanceSettings`, editor slots.

---

## E-1 Fill Audition   (size L, depends F0-2, F0-4, F0-9)

### Goal
Let the user hear a representative fill *now*, using the current settings,
without waiting for the audio to contain a gap - the fastest possible
design loop ("turn a knob, hear the result").

### User-facing behaviour
* **AUDITION** button in the `CreativeExtra` slot, next to CAPTURE. Tooltip:
  "Play one fill right now from the last few seconds of audio, using the current
  settings. Needs audio playing through the plugin."
* Also triggerable by: right-click -> MIDI Learn on the button (CC, rising edge
  >= 64 fires once, re-arms below 48 - the creative spec rule), and a MIDI note
  (U-3). Not host-automatable (command, not a parameter) - creative spec §8.
* While the preview plays: button shows an active state, the visualiser draws the
  fill in the **preview colour** (distinct from natural fills), status line says
  "Preview: <Algorithm>". Pressing again during playback *restarts* with a new
  variation (the previous preview fades out over 5 ms); presses queued while a
  command is pending collapse to one.
* Rejections show a one-line status for 2 s (no dialogs):

| Condition | Status text |
|---|---|
| Host not calling `processBlock` (stopped transport that stops the callback) | "Start playback to audition" |
| No usable audio in the last ~0.5 s (`reference < 3e-4`) | "Nothing to sample - play some audio" |
| Spectral mode and every band disabled | "Enable a band to audition" |
| Preview voice cannot start (`FillVoice::start` false twice) | "Couldn't build a fill - try again" |
| Bypassed | "Bypassed" |

### What the preview does and does not do (acceptance-critical)
| Does | Does NOT |
|---|---|
| Use current Fill Type, weights, complexity, chaos, intensity, stutter/delay divisions, band settings, personality/macros (effective settings) | Change `inGap`, `gapEvaluated`, `gapIndex`, `lastFillStartPos`, `fillsStarted`, `recentAlgos` |
| Go through wet gain, dry/wet, width, output trim, low/centre protect | Appear in `RecipeHistory`, More Like That bias, or Lock (flag `FillRecipe::flags & 1`) |
| Obey transient/next-event/sidechain duck (a preview played over a hit is ducked - "what you hear is what it will do") | Alter detector state or change any later natural decision |
| Be capped at `min(maxFill, 2000 ms)` (hard cap 5 s) | Hold a trigger across a stopped host |

### Engine design
New files: `Source/features/audition/Audition.{h,cpp}`, `Gui.{h,cpp}`, `Help.cpp`;
tests `Tests/features/TestAudition.cpp`; fragments under `cmake/`.

1. `FillEngine` gets `FillVoice previewVoice;` (R5: dedicated, outside the pool of
   4; `maxConcurrent` logic untouched), prepared/reset/panicked with the others.
2. New method
   `bool FillEngine::startPreview (std::uint8_t velocity, std::int64_t outputPos, const AnalysisEngine&, const SourceHistory&, const EngineSettings&) noexcept`
   * `frame = analysis.frameAt (outputPos)`; reject `NoSource` if `frame.reference < 3e-4`.
   * Key: `hashCombine (hashCombine (seed*0x9E3779B1, ++previewCounter), kSalt_Preview)`.
     `previewCounter` is reset by `reset()` only, so a sequence of presses is
     reproducible for a given seed (test `audition_deterministic_sequence`).
   * Length: `usable = min(maxFillMs, 2000 ms)`; apply the same `scale` formula as
     `buildRecipe` with `conf = 1`; no `nextEvent` boundary (it is not a gap).
   * Algorithm: `chooseAlgorithm` with the preview Rng (or forced Fill Type).
   * Band (spectral mode): choose among enabled bands weighted by `bandProb`
     ignoring live confidence; none enabled -> reject.
   * `amp = 0.8 * lerp (0.5, 1.0, velocity/127)`; `setup.sourceAnchor` as natural fills
     (`outputPos - historyOffset`, clamped to `[0, history.getWritePosition()]`).
   * Calls the same `toVoiceSetup`/`startVoice` stages as natural fills, so a fix
     in one is a fix in both.
3. `process()`: render `previewVoice` in the same hop loop as `voices`;
   `controlUpdate` calls `previewVoice.setDuck (currentDuck)`; the existing
   "protected event imminent -> `requestStop`" loop includes it.
4. `panic()` / `reset()` / bypass / `releaseResources` / sample-rate change fade
   it out (`requestStop (48)`), never a hard cut.
5. Set the reserved `VizFrame::flagPreview` (F0-4) while `previewVoice.isActive()`.
6. Use the reserved `MeterState` fields (F0-4): `std::atomic<std::uint8_t> previewStatus` (enum: Idle, Playing,
   RejectedNoSource, RejectedNoBand, RejectedBuild, RejectedBypassed) and
   `std::atomic<std::uint32_t> blockCounter` (incremented once per `processBlock`).

### Threading
* Editor/button: checks `blockCounter` advanced within the last 400 ms (tracked
  by the editor timer); if not -> show "Start playback to audition" and **do not
  push**. Otherwise `commands.push ({Preview, velocity=100, issuedMs=Time::getMillisecondCounter()})`.
* Audio thread: drains `commands` at the top of `processBlock`; collapses
  multiple `Preview` into the last; **discards** a `Preview` older than 500 ms
  (`getMillisecondCounter()` is lock-free) so a stalled host can never fire it later;
  ignores/clears it while bypassed.
* MIDI CC and MIDI note triggers are detected inside `processBlock` and set a
  local `previewRequested` flag directly (no queue, lowest latency).
* No allocation, locks, logging or file I/O in any of the above.

### Standalone
Identical path; standalone calls `processBlock` from its device callback. If no
audio device is open the heartbeat check produces the "Start playback" message.

### Memory
Record `sizeof(FillVoice)` and total `previewVoice` buffers in `docs/progress/E-1.md`.
Budget: the extra voice must add < 1 MB at 192 kHz stereo (else cap its capture
length at 2 s).

### Tests (`Tests/features/TestAudition.cpp`)
`audition_starts_without_gap` | `audition_leaves_gap_counters_untouched` (gapIndex,
fillsStarted, lastFillStart, recentAlgos unchanged) | `audition_not_in_recipe_events` |
`audition_respects_wet_zero_and_output_trim` | `audition_ducked_by_protection` |
`audition_rejects_no_source` | `audition_rejects_no_band` | `audition_restart_fades_old_voice` |
`audition_coalesces_pending` | `audition_stale_command_discarded` |
`audition_ignored_when_bypassed` | `audition_fades_on_panic_reset_samplerate_change` |
`audition_deterministic_sequence_for_same_seed` | `audition_does_not_change_next_natural_fill`
(render scenario A with and without an audition in the middle: natural fills after
the preview identical) | `audition_no_audio_thread_allocation` (see `06` §3) |
`audition_midi_cc_edge_and_rearm` | `audition_finite_output_all_algorithms` (loop all 15).

### Acceptance
All tests pass; golden scenarios unchanged; pluginval strictness 10 with random
parameter fuzzing still passes; manual: audition works in Reaper playing, Ableton
playing, standalone; shows "Start playback" in a stopped Reaper.

---

## E-2 Tempo-sync audit and fixes   (size L, depends F0-5)

### Goal
Every time-valued behaviour that *should* follow the song tempo does, nothing
silently misbehaves at extreme tempos, and standalone (which has no host
tempo) works sensibly.

### Findings from the current code (verified by reading; the audit ticket re-verifies)

| # | Finding | Where | Effect |
|---|---|---|---|
| 1 | With **Sync on** and no host tempo, `beatSeconds` falls back to a fixed **0.5 s (120 BPM)** | `PluginProcessor.cpp` `updateTempoInfo` (~line 333) | **Standalone always runs "sync" at 120 BPM**; some hosts report no tempo while stopped, so Sync jumps between real and 120 BPM |
| 2 | Stutter is clamped to 8-500 ms, delay to 10-1000 ms, max-fill to 20-2000 ms **after** conversion | same, lines ~346-352 | At 60 BPM a 1/2-note stutter (1 s) becomes 0.5 s = a 1/4 note, silently; at 40 BPM 1/4-note delay (1.5 s) -> 1 s. The knob says one thing, the audio does another |
| 3 | Tremolo (4-26 Hz) and Pan Motion (0.6-5 Hz) modifier rates are random free-running Hz | `FillVoice.cpp:301-304` | Never tempo related |
| 4 | Scramble slice length derives from usable length / slice count, not from the grid | `FillVoice.cpp:531-533` | Slices never land on beats |
| 5 | Fill **length** is `usable * scale` in samples with no relation to the grid | `FillEngine.cpp` length block | Pitch Rise/Fall, Filter sweep, Tape Stop, Reverse all end at arbitrary times |
| 6 | Only `stutterDiv`, `delayDiv`, `maxFillDiv` exist; the algorithms using them are Stutter and Delay (and Scramble/Comb do not) | `Parameters.h` | OK by design, but undocumented |
| 7 | Tempo is read once at fill start (`VoiceSetup` is captured) | design | A tempo change mid-fill does not retime that fill - acceptable, document it |
| 8 | The decision key uses `ppq*96` ticks - transport-reproducible | `FillEngine.cpp` key block | Good; keep. Must not change |

### Changes
1. **Tempo source with memory (fixes #1).** In `updateTempoInfo`: if the host
   provides a valid positive BPM, store it in `lastKnownBpm` (atomic float) and
   use it; else use `lastKnownBpm` if set; else use `fallbackBpm`. `fallbackBpm`
   is a new **non-automatable setting in `<Perf>` state**, default 120, range
   40-300, edited in the Options page ("Tempo when host provides none") and
   shown only when `StandalonePluginHolder::getInstance() != nullptr` or the host
   reported no tempo in the last 5 s. Never let the value be 0/NaN/negative.
2. **Musical clamping instead of silent ms clamping (fixes #2).**
   `float clampToDivision (float seconds, float lo, float hi, int divIndex, double beatSeconds)`:
   if `seconds > hi`, halve the *division* (Whole->Half->Quarter->...; triplet/dotted
   stay in family) until it fits; if `< lo`, double it. Returns the resulting
   seconds and sets a `bool clamped`. Surface `clamped` through `MeterState::syncClamped`
   (atomic) so the knob tooltip/status can say "1/2 -> 1/4 at 60 BPM (limit 500 ms)".
   Unit-test the table below.
3. **Mod sync (fixes #3, #4).** New param `modSync` (bool, off). When on and
   Sync is on:
   * Tremolo rate `= 1 / stutterSeconds`, then doubled/halved into `[2, 32] Hz`
     (preserves the rhythmic relationship).
   * Pan Motion rate `= 1 / (delaySeconds * 4)` folded into `[0.25, 8] Hz`.
   * Scramble `sliceLen = stutterSeconds` (clamped to `usable/2`), `numSlices = usable/sliceLen`.
   * Passed in `VoiceSetup` as `modSyncSeconds` (0 = off). With `modSync` off the
     voice code path is byte-identical to today.
4. **Fill length quantise (fixes #5).** New param `fillQuant` (Off, 1/4, 1/8, 1/16, 1/32).
   When not Off and the transport/tempo is valid: after `buildRecipe` computes
   `lengthSamples`, set `lengthSamples = floor (lengthSamples / unit) * unit` where
   `unit` is the division in samples, **only if** the result is at least one unit
   and at least `minGapSamples`; never lengthen, never exceed `usable`. Apply before
   `adjustRecipe`. Off = identical to today.
5. **Docs + UI (fixes #6).** Help gets a "What follows tempo" table (below). Tooltips
   on the Sync controls list exactly which algorithms they affect. The Sync
   panel shows the *effective* time in ms next to each division (live, using
   `lastKnownBpm`), and a small "clamped" badge from change 2.

### "What follows tempo" (Help text, final)
| Element | Follows tempo when | Control |
|---|---|---|
| Stutter slice length | Sync on | Stutter division |
| Delay tap spacing | Sync on | Delay division |
| Maximum fill length | Max Fill Sync on | Max Fill division |
| Fill length grid | `fillQuant` != Off and host tempo known | Quantise Length |
| Tremolo / Pan Motion modifiers, Scramble slices | Sync on **and** Sync Mods on | Sync Mods |
| Everything else (grain rate, comb pitch, filter sweep, pitch ramps) | never - they follow fill length | - |

### Test matrix (`Tests/features/TestSync.cpp`)
For each BPM in {40, 60, 90, 120, 174, 300} x each Division x {44.1, 48, 96, 192 kHz}:
* `effective_seconds == division_seconds` when inside limits; when outside, the
  returned seconds equals a *power-of-two related division* and `clamped == true`.
* No NaN/inf/negative; always within `[lo, hi]`.
* Tempo memory: valid -> invalid -> returns last known; never valid -> fallback;
  fallback bounds enforced; state round trip of `fallbackBpm`.
* `modSync` off: voice output bit-identical to baseline (golden). On: tremolo rate
  equals the formula within 1e-6.
* `fillQuant`: lengths are multiples of the unit or equal to the unquantised value;
  never exceed `usable`; `Off` identical to baseline; invalid transport -> unchanged.
* Mid-fill tempo change does not change an already-started voice (determinism).

### Acceptance
Golden unchanged with all new controls at default; the table above is true when
measured in a Reaper project at 60 and 174 BPM (E-7 script); standalone Sync follows
the Options tempo.

---

## E-3 "Why didn't it fill?" reasons and visualiser legend   (size L, depends F0-2, F0-4, F0-9)

### Goal
Remove the top first-run question ("it's doing nothing") by showing, per gap,
exactly why a fill did or didn't happen, and what to change.

### Reason catalogue (from `02` F0-2) with advice
| Reason | Plain label | Advice shown |
|---|---|---|
| `FillOff` | Fill is at 0 | Raise Fill |
| `NoSource` | No audio to sample | Play audio before the gap; check input level / Detector source |
| `TooSoon` | Too soon after last fill | Lower Retrig Guard / Min Gap |
| `NoVoice` | All fill voices busy | Lower Complexity or Max Fill |
| `TooShort` | Gap too short (X ms free, need Y ms) | Lower Min Gap, or raise Lookahead off / reduce Protect Next |
| `GuardEaten` | Next-event guard used the space | Lower Protect Next |
| `NoBand` | No spectral band qualified | Enable a band or raise band probability |
| `DensityRoll` | Skipped by Density / Fill probability | Raise Density or Fill |
| `NoAlgoFits` | No algorithm fits this length | Raise Max Fill, enable shorter algorithms, or choose Fill Type = Auto |
| `SourceUnavailable` | Fill couldn't be built | (internal) report if frequent |
| `LockSkipped` | Locked fill doesn't fit this gap | Unlock, or lengthen gaps (see Fill Lock) |
| `BudgetFull` | Fill budget for this bar used | Raise Fills per bar (doc 12 F-D01) |
| `QuietZone` | Inside a quiet zone | Change Quiet Zone (F-D01) |
| `ScGated` | Sidechain gate closed | Lower SC threshold or change gate mode (F-L13) |
| `Panicked` | Panic is active | Press Recover (N-USE-a) |
| `CoopYield` | Yielded to another GapFill | Change Co-op mode (F-X06) |
| `ForcedUnsafe` | Fill Now skipped - next hit too close | Fire earlier or lower Protect Next (F-P02) |

### Data path
1. `RejectReason` returned by `trySchedule` (F0-2). `controlUpdate` already knows the
   frame: when a gap is evaluated it pushes `FillEvent { GapEvaluated, reason (None = started), recipe (if started), availMs, neededMs, pos }` to `fillEvents`
   **only if `viz->isEnabled()`** (so zero work with the editor closed, as today)
   — except `FillStarted` recipes, which are always pushed (history needs them).
2. Use the reserved `VizFrame::reason` byte and `flagRejected` (F0-4). The
   frame that contains the evaluation carries the flag + reason, so the scrolling
   view can mark the gap start even though it is decimated by `vizDecim`.
3. `MeterState::reasonCount[RejectReason::Count]` (relaxed atomics) incremented by
   the audio thread on every evaluation (cheap, always on). Used for the "Why"
   tooltip and tests.
4. Processor timer drains events into `GapOutcomeLog` (64 entries, message thread).

### UI (`Source/features/reasons/Gui.*`, slot `VisualiserOverlay` + `FooterStatus`)
* **Legend strip** under the visualiser: Event, Gap-eligible, Fill, Protected,
  Lookahead, Preview, Skipped. Each swatch uses colour **and** a pattern/shape
  (accessibility, `theme.md` palette; no colour-only encoding). Collapsible
  and remembered in `<Perf>`.
* **Threshold line** on the confidence lane at `kEnterGapConf = 0.55`
  (export the constant from `FillEngine.h`; a region below the line is "quiet
  but not a gap" and is not an event).
* **Skipped markers**: a small hollow marker at the gap start for rejected gaps;
  hover shows `<label> - <X> ms free, <Y> ms needed. <Advice>`.
* **Status line** (`FooterStatus`): "Last gap: skipped - Gap too short (38 ms free, need 60 ms)".
  Updates at most 4 Hz; respects Reduced Motion (no fade).
* **"Why so few fills?"** link in the status chip: opens a popover listing the
  top 3 reasons by `reasonCount` over the session with their advice. Keyboard
  reachable; screen-reader text provided.

### Accuracy rules
* A reason is shown only if the code path that returned it is exactly the one in
  the F0-2 table; `GuardEaten` vs `TooShort` is decided from measured numbers
  (`boundary - outputPos` vs `minGapSamples`), never guessed.
* `availMs`/`neededMs` come from `GapContext` (the same integers the decision used).
* Preview outcomes never produce `GapEvaluated` events.

### Tests (`Tests/features/TestReasons.cpp`)
One synthetic scenario per reason asserting the exact reason and the numbers;
`reason_none_when_started`; `reasons_not_published_when_viz_disabled` (queue
stays empty); `fill_started_recipes_published_always`; `reasonCount_matches_events`;
`viz_flags_reject_and_preview_distinct`; `gui_legend_has_text_and_shape_for_every_state`
(component test); `status_line_rate_limited`.

### Acceptance
Every reason reproduced in a manual scripted session (07 §Manual script M-3);
zero audio-thread cost measured with the editor closed (CPU delta < 0.1%).

---

## E-4 Working CI   (size M, no code dependencies - START HERE)

**Status 2026-10-21: done.** CI runs on Windows, Linux and macOS and beta1 assets were published for all platforms (owner statement; see `docs/COMPLIANCE.md`). The text below is the original diagnosis and stays as the definition of done for regressions (required checks, caching, nightly, hot-file check).

### Goal
Required green checks on every PR for build + tests + validators. Today GitHub
Actions jobs "terminate before step 1" with no logs (PROGRESS.md, since
2026-10-06); nothing is independently verified and the Windows release was
cross-compiled by hand.

### Diagnosis procedure (do in order, record findings in `docs/progress/E-4.md`)
1. Repo -> Settings -> Actions -> General: Actions enabled; "Allow all actions" or
   the used ones (`actions/checkout@v4`) allowed.
2. Settings -> Billing & plans (account level): Actions minutes and **spending
   limit**. A private repo with exhausted minutes, or a $0 spending limit with a
   failed payment method, fails jobs immediately with no logs. If this is the cause
   the cheapest fix is to make the repo public (README already calls the project
   free/open, licence AGPL-3.0): public repos get free Actions minutes.
3. Run `workflow_dispatch` manually on `master` and read the run's "annotations"
   panel; a billing or runner-quota cause is stated there.
4. If runners still do not start: add a **self-hosted runner** on the Linux
   server you already use over PuTTY (`Settings -> Actions -> Runners -> New`).
   Requirements: Ubuntu 22.04+, 8 GB RAM (the project OOMs on 4 GB - PROGRESS
   2026-09-16), 25 GB disk, the apt packages from the README. Use label
   `gapfill-linux` and change `runs-on` to it **only for the Linux job**.
   Security: never run self-hosted jobs for fork PRs on a public repo; require
   approval for first-time contributors.
5. Windows: keep `windows-latest` if hosted runners work; otherwise the existing
   clang-cl cross build (`packaging/package_release.sh`) becomes a CI job on the
   self-hosted Linux runner, with Wine+pluginval as the Windows validation (the
   method used for 0.0.1).

### CI definition of done
* Jobs: `preflight` (<2 min), `linux` (build, tests excluding `Combinatorial`,
  pluginval strictness 10, clap-validator), `windows` (build, tests, pluginval),
  `macos` (E-5, once that lands), `nightly` (cron, full tests incl. Combinatorial ~13 min,
  pluginval with `--repeat 3`).
* Add test filter exclusion: `GapFillTests -Combinatorial` (small change in
  `TestFramework.cpp::run`: a filter starting with `-` excludes matches).
* `concurrency: group: ci-${{ github.ref }}, cancel-in-progress: true`.
* Cache: `ccache` for compiles (key = compiler + `JUCE` tag), `actions/cache` for the
  `JUCE/` clone. Target: warm Linux job < 12 min.
* Upload artefacts (VST3/CLAP/Standalone zips) from every `master` build.
* Branch protection on `master`: require `preflight`, `linux`, `windows`; require
  PR; require branch up to date (merge queue if available).
* Add `tools/check_hot_files.py` (F0-1) as a `preflight` step.

---

## E-5 macOS build, AU, universal binary   (size XL - split into 5a-5c)

Goal: the largest audience gap (Logic needs AU; many producers are on Mac).

**Status 2026-10-21:** 5a Build and 5b Validate shipped with beta1 (macOS Intel and Apple Silicon assets published). Open: 5c notarization and the real-host checks (Logic AU, Intel native), tracked in `12` §2.6; they stay *unverified* until a human runs them.

* **5a Build**: add `AU` to `FORMATS` on `APPLE` only; `PLUGIN_CODE Gpfl`,
  `PLUGIN_MANUFACTURER_CODE Gfau` already set (the manufacturer code has an
  uppercase letter as AU requires). `AU_MAIN_TYPE kAudioUnitType_Effect`,
  `COMPANY_COPYRIGHT` and bundle id set. `set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0")`,
  `set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")` (before `project()`), CLAP + VST3 +
  Standalone + AU all universal. Remove MSVC-only flags from the Apple path.
* **5b Validate**: `auval -v aufx Gpfl Gfau` must pass; pluginval and
  clap-validator on macOS; sidechain bus verified in Logic (aux input) and
  Ableton. Add a `macos` CI job on `macos-14` (Apple silicon) building universal.
  Golden files get a `macos` directory.
* **5c Distribution**: Developer ID Application cert, hardened runtime
  (`codesign --options runtime --timestamp`), `xcrun notarytool submit --wait`,
  `xcrun stapler staple`; installer `.pkg` via `pkgbuild` + `productbuild`
  installing to `/Library/Audio/Plug-Ins/{Components,VST3,CLAP}`. Needs an Apple
  Developer Program membership (paid, annual) - a business decision; until then
  ship zips with Gatekeeper instructions and label the build "unsigned".
* Risks: JUCE `juce_audio_devices` standalone needs microphone usage description
  (`NSMicrophoneUsageDescription`) for the Standalone; AU state/parameter
  rescan behaviour differs (test state save/load in Logic); Apple clang may
  differ from gcc/clang-cl numerically -> per-platform goldens.

---

## E-6 Signing and installer trust   (size M)

**Status:** open, blocked only on credentials. Rule (`12` §2.6): build everything that needs no secret now (conditional sign steps, detached checksum signature, installer checklist tests); until certificates exist keep publishing clearly labelled unsigned packages.

* **Windows**: sign `GapFill.exe`, `GapFill.vst3` binary, `GapFill.clap`, and the
  NSIS installer with an Authenticode certificate, RFC 3161 timestamp. Options,
  cheapest first: SignPath Foundation (free for qualifying open-source projects,
  apply), Azure Trusted Signing (low monthly fee; eligibility depends on
  organisation/region), a standard OV code-signing certificate. Pick one in the
  ticket; do not self-sign. Add the sign step to `packaging/GapFill.nsi`'s build
  script and verify with `signtool verify /pa`.
* **Checksums**: `SHA256SUMS` already produced; add a detached signature
  (minisign or GPG) and publish the public key in the README.
* **Installer quality checklist**: upgrade-in-place keeps presets/user data; clean
  uninstall removes only what was installed; per-component install (VST3/CLAP/Standalone)
  works; no admin prompt when installing only to per-user paths (if per-user install
  is offered); installer shows version and licence (AGPL) text.
* **SmartScreen** reputation builds with downloads of a signed binary; document this
  in release notes so users are not alarmed.

---

## E-7 Real-host QA   (process ticket)

The 0.0.1 beta was validated only by validators and synthetic tests. Before any
"1.0 candidate" tag, run the scripted passes in `07-QA-AND-RELEASE.md` §Manual
scripts in: Reaper (Windows or Linux), Ableton Live, FL Studio, Bitwig (CLAP), and,
after E-5, Logic and Reaper on macOS. Findings become bug tickets `B-<n>`;
evidence (version, host, OS, pass/fail, screenshot/WAV) is stored in
`docs/qa/<release>/<host>.md`. This is the gate that `docs/VALIDATION.md`
already describes as "separate release-validation activity".
