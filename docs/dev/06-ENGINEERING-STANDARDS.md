# 06 - Engineering standards

Rules that keep quality high and merges cheap. They extend, not replace,
`docs/standards/CDL_PLUGIN_BASELINE.md` and the conventions already visible in the
code (namespace `gapfill`, `noexcept` on audio-thread functions, `juce::jlimit`,
`kConstantName`, doc comments `/** ... */`, preallocate in `prepare`). Match the
surrounding file's style; do not reformat code you aren't changing.

## 1. Real-time safety (audio thread)

"Audio thread" = `processBlock`, `processBlockBypassed`, `processChunk`, everything
`FillEngine`/`FillVoice`/`AnalysisEngine`/`SpectralSmear` do per sample or per hop,
and the MIDI scan.

**Forbidden there:** allocation (`new`, `std::vector::push_back/resize`, `juce::String`
construction, `std::function` construction, `juce::AudioBuffer` resize), locks
(`std::mutex`, `juce::CriticalSection`, `ScopedLock`), blocking I/O (files, logging,
`DBG` in release paths), system calls that can block (`Time::getCurrentTime()` is
allowed only as `getMillisecondCounter()`), exceptions, `std::shared_ptr` copies,
unbounded loops, `dynamic_cast`, calls into the message thread or any UI object.

**Allowed:** preallocated buffers sized in `prepareToPlay`, `std::atomic` with
`memory_order_relaxed`/acquire-release as justified, `juce::AbstractFifo`-based SPSC
queues (`SpscQueue`), POD copies, math, `juce::ScopedNoDenormals`.

Rules:
1. Every new buffer is allocated in `prepare()` for the **maximum** supported
   sample rate (192 kHz) and channel count, and re-sized only there.
2. Per-block work from a command (copy, export, calibrate record) is **bounded**
   (<= 4096 samples/block) and resumable.
3. Cross-thread data: one writer per field. Message -> audio via `commands`
   (SPSC); audio -> message via `fillEvents` (SPSC) or relaxed atomics for single
   values. No shared mutable structs without a generation-counter protocol.
4. Never read a `juce::AudioProcessorValueTreeState` value from the audio thread
   except through the `ParamCache` atomics.
5. Every generator output stays finite and bounded: it passes through the existing
   sanitiser; new stages MUST NOT bypass it. Divide-by-zero/`log(0)` guards are
   explicit (`jmax (x, eps)`), denormals handled by `ScopedNoDenormals`.
6. Any state needing a reset on `reset()`, `releaseResources`, sample-rate change,
   bypass, or transport jump has its handling listed in the PR (a table of the five
   events x new members).

**Enforcement:**
* `Tests/support/AllocGuard.h` (Phase 0, F0-7): replaces global `operator new/delete` in
  the test binary with a counter that a scope (`AllocGuard g;`) turns into a test
  failure while active. Each feature adds a `*_no_audio_thread_allocation` test that
  renders its scenario under the guard (JUCE internals that allocate on first use are
  warmed up before the guard).
* Nightly CI: ASan+UBSan build runs the full suite; a TSan build runs the queue,
  Audition, Capture and Export tests.

## 2. Determinism

The project guarantees identical playback for the same seed + transport position,
and decisions independent of host block size.

1. The RNG draw order inside `trySchedule` is frozen (`02` F0-2). **New features
   never draw from the legacy `Rng`**; each uses a salted sub-RNG
   (`hashCombine (key, kSalt_<Feature>)`; salts in `Source/features/Salts.h`, never reused or renumbered).
2. A feature at its neutral setting performs **zero** RNG draws and zero float
   operations that could change legacy values (`if (amount == 0) return;`, not "multiply by 0").
3. Anything PPQ-dependent is evaluated in the decision stage with the PPQ of the
   decision sample, never the block start (`04` §Two-stage).
4. No wall-clock time (`Time::`), no `std::random_device`, no uninitialised memory,
   no iteration over unordered containers in decision code.
5. `Audition` previews use their own counter and salt (`kSalt_Preview`) and never
   advance gap counters.
6. Tests: block-size invariance (F0-7) must pass with every feature enabled in
   the `E` stress scenario.

## 3. Parameters and state

* IDs are permanent: lower camelCase `^[a-z][A-Za-z0-9]*$`, appended in their group
  file, locked in `tools/param_ids/<group>.lock`. Never rename, reorder, or reuse.
* Defaults are neutral (output identical to before the feature). The one exception
  must be documented in `02` (none today; macros use `macrosOn = Off` for this reason).
* Choice parameters: never reorder or remove entries (stored as index). Append only.
* New numeric ranges: choose units users see (%, ms, Hz) and set a skew where a
  linear slider would waste resolution; provide `valueFromText` matching the
  display (the 0.0.1 kHz-parsing bug).
* `performanceParameterIDs()` lists parameters excluded from Random / Surprise / Undo;
  adding a parameter without classifying it fails `validate_repo.py`.
* Non-parameter state lives in versioned subtrees (F0-6) with size caps, validated
  atomically, and **fails soft**: a bad optional subtree resets to default, the rest loads.
* Never serialise audio, momentary state, or running voices.
* Compatibility fixtures: `Tests/fixtures/state_0.0.1_<preset>.bin` (real states saved by
  the 0.0.1 beta, generated in Phase 0 from each factory preset + 3 hand-edited sessions).
  Every release runs "load fixtures -> all parameters equal preset values -> render
  scenario A equals golden". A format change that breaks them needs a migration + new fixture, not a deleted fixture.
* Every file reader (preset, recipe, settings) has a size cap, bounded string lengths,
  type/range validation, no path traversal (`File::getFileName()` only), and returns a
  user-facing reason string. Parsing is fuzz-tested (random truncation/bit flips) in
  `Tests/features/TestFuzzFiles.cpp`.

## 4. Testing standards

* Test names: `<feature>_<behaviour>` in group `<Feature>`; one assertion theme per test.
  Register with `GF_TEST (Group, name)` (existing macro); new files under `Tests/features/`.
* Required categories per feature: **neutral identity**, **bounds/clamping**, **determinism**,
  **state round trip + old-state load**, **threading/allocation** (if audio path),
  **edge cases** (very short gap, 192 kHz, 32-sample block, transport jump, bypass, reset).
* Assertions use tolerances stated in the test (`GF_EXPECT_NEAR (a, b, 1e-6)`); never
  `==` on floats except bit-identity tests (`memcmp`).
* Use the TestRig (`02` F0-7): `drumLoop`, `renderOffline`; do not hand-roll signal generators.
* No sleeps/timeouts for synchronisation; drive the processor deterministically
  (`flushLatencyReportForTests`-style hooks). Tests needing a message loop use JUCE's `MessageManager`
  test helper and are in group `Gui`.
* **A flaky test is a bug**: fix or delete within one working day; "re-run until green" is not a fix.
* Coverage targets (measured with `llvm-cov` in the nightly job, advisory then gating at M3):
  >= 85 % lines in `Source/features/**` and `Source/dsp/**`, 100 % of `RejectReason`, `Command`
  and `FillEvent` enum values exercised.
* Fuzz/stress: pluginval strictness 10; a Combinatorial test for each new parameter group (nightly);
  a parameter-fuzz test that sets random values on every parameter from a seeded RNG every block for
  10 s of audio and asserts finite, bounded output and no crash.
* Property tests (small, seeded loops): macro curves monotonic; envelope within bounds;
  quantised lengths are multiples of the unit; sync clamping is idempotent.

## 5. Performance and memory budgets

Baselines are measured in F0 (`docs/progress/F0.md`) on the reference machine and
then enforced. Until measured, use these provisional limits:

| Metric | Budget |
|---|---|
| `processBlock` mean CPU with every new feature **at defaults** | <= baseline + 1 % (noise) |
| Same, worst case (all features on, 4 voices + preview, complexity 100, spectral) | <= baseline x 1.35 |
| Worst-case single block time at 48 kHz/64 samples | < 50 % of the block duration, p99.9 |
| Preallocated memory at 192 kHz stereo | <= baseline + 16 MB (+ 12 MB only while WetTap is armed) |
| Editor timer work (15-30 Hz) | < 1 ms per tick; no allocation per tick in steady state |
| Editor open time | < 300 ms to first paint; preset scan is async |
| Auto-Calibrate | < 3 s analysis for 8 s of audio |

`Tests/Perf.cpp` (group `Perf`, nightly + on-demand) renders scenario `E` and prints
mean and p99.9 per-block time; it fails if the budget above is exceeded by > 10 % relative
to the stored baseline (`Tests/perf_baseline.json`, regenerated only by a `[perf]` PR).

## 6. Code quality

* Warnings: `-Wall -Wextra -Wpedantic -Wconversion -Wshadow` (clang/gcc) and `/W4` (MSVC)
  on `Source/features/**` and any file you create; zero warnings (`-Werror` in CI for those paths).
  Do not silence with pragmas; fix the cause.
* Static analysis (nightly, advisory -> gating at M3): `clang-tidy` with
  `bugprone-*, performance-*, concurrency-*, cppcoreguidelines-pro-type-member-init,
  modernize-use-nullptr, readability-else-after-return`; `cppcheck --enable=warning,performance`.
* `.clang-format` is added in Phase 0 matching the existing style **but only applied to new
  and fully-new files** (`git clang-format` on the diff), never mass-reformatting.
* Functions < ~60 lines, one responsibility; no new file > ~600 lines (split by stage/component).
* Prefer pure functions + small structs for logic (testable without JUCE UI), thin JUCE glue.
* No global mutable state. Static registries (Help, ParamInfo, Slots, Tests) are append-only
  at start-up and read-only afterwards.
* Comments explain **why** (invariants, units, ordering constraints), not what. Units in names
  (`lengthMs`, `seconds`, `samples`, `01` for normalised) as in existing code.
* Dependencies are pinned (JUCE 8.0.15, clap-juce-extensions commit). Any new dependency
  needs an ADR (`engineering:architecture`) and licence review (AGPL project).
* API use is verified against the checked-out `JUCE/` source, never from memory
  (relevant for AI-assisted work: do not invent JUCE calls; grep the module).

## 7. UI, accessibility, UX

* All colours/spacing/typography from `theme.md` via `LookAndFeel`; no hard-coded colours
  in components. 8 px grid.
* Every interactive control: tooltip (via ParamInfo), accessibility title + description,
  keyboard operable when keyboard navigation is enabled, 2 px focus ring, hit target >= 24 px.
* No information by colour alone (shape/text/pattern too). Contrast >= 4.5:1 for text.
* Reduced Motion disables decorative animation; status changes still appear.
* Errors: user-facing text states what failed and what to do; no raw exception text, no paths with
  usernames beyond the file name.
* Message-thread only: all file I/O, dialogs, scanning. Long operations run on a background
  thread with Cancel and a progress indication.
* Editor lifetime: components must not outlive the processor, must unregister listeners in the
  destructor, and tolerate being created/destroyed while audio runs.

## 8. Security and privacy

* No network access in the plugin (README promise). A PR adding `juce::URL`/sockets is rejected
  unless it is a user-initiated "open link" via `URL::launchInDefaultBrowser`.
* Untrusted inputs: preset/recipe/settings files, host state blobs, MIDI. All bounded and validated (§3).
* File names from users are sanitised (`File::createLegalFileName`), written only inside the
  chosen folder; reject `..` and absolute paths in any embedded name.
* No telemetry. Crash logs are not collected; the optional debug overlay stays local.
* Third-party code is not vendored without a licence note in `COPYRIGHT-TRADEMARK.md`.

## 9. Pull-request self-review checklist (copy into the PR)

```
[ ] Neutral at default: golden unchanged / identity test added
[ ] No new RNG draws on legacy stream; sub-RNG salt registered
[ ] Audio thread: no alloc/lock/IO (AllocGuard test added where relevant)
[ ] Resets handled: reset(), releaseResources, sample-rate change, bypass, transport jump
[ ] 192 kHz and 32-sample block considered (buffer sizes, loops)
[ ] Cross-thread data: single writer, SPSC/atomic only; editor closed is safe
[ ] State: new data versioned/validated; old-state fixture loads; corrupt data fails soft
[ ] New IDs: appended in group, lock file updated, classified for Random/Undo
[ ] Hot files untouched (or label + justification)
[ ] UI: tooltip, a11y name, keyboard, Reduced Motion, min-size, no colour-only cue
[ ] Help section + ParamInfo entries + COMPLIANCE/README updated if needed
[ ] Warnings zero; clang-tidy/cppcheck clean on new files
[ ] Tests: listed in the ticket + edge cases; CI green on Linux and Windows
[ ] Unverified items listed honestly (real DAW / hardware MIDI / macOS)
```

## 10. Commit, PR and documentation conventions

* Commit subject: `<TICKET>: <imperative summary>` (<= 72 chars); body explains why;
  one logical change per commit; keep commits buildable.
* PR title = squash commit subject. Link the spec section. Include before/after
  screenshots for UI and the measured numbers for DSP (CPU/memory).
* Docs live next to code: feature docs in `Source/features/<f>/README.md` (<= 1 page: purpose,
  data flow, threading, state, tests), user docs in the Help registry, decisions in `docs/adr/NNNN-title.md`
  (use the `engineering:architecture` ADR format). Update the relevant spec file in `docs/dev/` if
  implementation reality differs from it (the spec must stay true).
* Release notes are assembled from PR titles; each user-visible change has a one-line note.

## 11. Bug policy

* Severities: **S1** crash / audio dropout / data loss / host hang / loud output (> 0 dBFS
  from silent input); **S2** wrong behaviour in a core feature, preset/state not restoring;
  **S3** UI/usability defects; **S4** cosmetic.
* S1/S2 get a regression test in the fixing PR (test first, shown failing). S1 blocks any
  release and any merge of unrelated work until a fix or revert is on `master`.
* Triage within 2 working days; fix S1 immediately, S2 before the next milestone.
