# 02 - Foundations (Phase 0)

Every later feature plugs into the seams defined here. Phase 0 is built **once,
by one developer, merged before any feature work starts**, and is
behaviour-preserving: after it merges, the plugin sounds exactly as it does
today (proved by the golden test, F0-7). Its job is to remove the places where
two feature branches would otherwise edit the same lines.

Why this matters (measured from the repo): `PluginProcessor.cpp` (1163 lines),
`FillEngine.cpp::trySchedule` (one 185-line function), `Parameters.h`'s single
`GAPFILL_PARAMS` list, `Presets.cpp`, `HelpView.cpp`, `PluginEditor.cpp` and the
2022-line `Tests/TestPlugin.cpp` are touched by nearly every feature in the
creative spec. Left alone, 20 features means ~20 conflicting edits to each.

---

## F0-1 Per-feature file layout and registries   (size L)

### Directory layout (all new)

```
Source/features/<feature>/          engine + state code for one feature
Source/features/<feature>/Gui.*     its editor component(s)
Source/params/<group>.h             that feature's parameter list (X-macro)
Source/params/<group>.cpp           that feature's layout builder
cmake/sources/<feature>.cmake       list(APPEND GAPFILL_SOURCES ...)
cmake/tests/<feature>.cmake         list(APPEND GAPFILL_TEST_SOURCES ...)
Tests/features/Test<Feature>.cpp    that feature's tests
tools/param_ids/<group>.lock        append-only ID list (see below)
docs/progress/<ticket>.md           per-ticket session notes (see 01-PLAN)
```

### Parameters split by group

1. `Source/Parameters.h` keeps the existing list, renamed `GAPFILL_PARAMS_CORE(X)`,
   byte-for-byte unchanged (IDs, order). It then defines
   ```cpp
   #include "params/Groups.h"
   #define GAPFILL_PARAMS(X) \
       GAPFILL_PARAMS_CORE(X) \
       GAPFILL_PARAMS_MACROS(X) GAPFILL_PARAMS_PERSONALITY(X) \
       GAPFILL_PARAMS_CAPTURE2(X) GAPFILL_PARAMS_ENVELOPE(X) \
       GAPFILL_PARAMS_LOCK(X) GAPFILL_PARAMS_BREATHING(X) \
       GAPFILL_PARAMS_PHRASE(X) GAPFILL_PARAMS_MISTAKES(X) \
       GAPFILL_PARAMS_SYNC(X) GAPFILL_PARAMS_BANDTYPE(X) GAPFILL_PARAMS_EVOLVE(X)
   ```
   The group order is **permanent** (it fixes parameter index order). All 11
   group macros are created now, each defined empty (`#define GAPFILL_PARAMS_LOCK(X)`).
   After Phase 0 no feature edits `Parameters.h` or `Parameters.cpp`; it edits only
   its own `params/<group>.h/.cpp`.
2. `createParameterLayout()` ends with one call per group:
   `addMacrosParameters (layout); addPersonalityParameters (layout); ...` in the
   same fixed order. Phase 0 adds the 11 empty functions.
3. `ParamCache` needs no change: it is generated from `GAPFILL_PARAMS`.

### Append-only ID lock (per group)

`tools/param_ids/<group>.lock` lists that group's IDs in order, one per line.
`tools/validate_repo.py` is extended (and its unit tests updated) to:

* scan `Source/Parameters.h` **and** `Source/params/*.h` for `X(member, "id")`;
* fail if any ID in a `.lock` file is missing, renamed, or reordered;
* fail if a new ID exists in a group header but not in its `.lock` (the author
  must add the line; this makes "adding a permanent ID" a deliberate act);
* fail on duplicates across all groups, and on IDs not matching `^[a-z][A-Za-z0-9]*$`;
* keep checking that every factory preset ID resolves.

Per-group lock files mean two feature PRs never touch the same lock file.

### Source and test lists

`CMakeLists.txt` replaces its literal `GAPFILL_SOURCES` additions with
```cmake
file(GLOB GAPFILL_SOURCE_FRAGMENTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/sources/*.cmake")
foreach(f IN LISTS GAPFILL_SOURCE_FRAGMENTS)
    include("${f}")        # no CONFIGURE_DEPENDS needed; a new fragment forces re-configure
endforeach()
```
(`file(GLOB)` is used only on the small fragment directory, never on source
files, so the source lists stay explicit and reviewable.) Same for tests.
The existing source list moves unchanged to `cmake/sources/core.cmake`. A
fragment is added by the feature that owns it; adding a *file* inside a feature
edits only that feature's fragment.

### Help registry

`HelpView.cpp` gains `HelpRegistry::add (int order, juce::String title, juce::String body)`
and a static-registration helper mirroring `GF_TEST`. Order is an explicit
integer, sorted at runtime (static-init order between translation units is
unspecified, so never rely on it). Existing sections are registered with
orders 100, 200, ...; features register 1000+ so they never collide. Each
feature registers its section from `Source/features/<f>/Help.cpp`.

### Presets rule

Factory presets (`Presets.cpp`) are edited **only** in the end-of-phase "preset
pass" ticket (see plan), never in feature PRs. Features MUST be neutral at their
defaults so existing presets keep sounding the same without edits.

### Acceptance

* Build, tests, `tools/validate_repo.py`, pluginval and clap-validator unchanged.
* `git diff --stat` for the feature tickets that follow touches none of:
  `Parameters.h`, `Parameters.cpp`, `CMakeLists.txt`, `Presets.cpp` (checked by a
  CI script `tools/check_hot_files.py` that fails a PR labelled `feature` if it does).

---

## F0-2 Scheduler broken into stages (behaviour-preserving)   (size L)

`FillEngine::trySchedule` (src `Source/dsp/FillEngine.cpp:316`) currently
returns `bool`, decides eligibility, rolls the dice, picks length, band,
algorithm, builds `VoiceSetup`, starts the voice and bookkeeps - in one body.
Six features need to change *different* parts of it, and the rejection-reason
feature needs *every* early `return false` labelled. Split it:

```cpp
enum class RejectReason : std::uint8_t {
    None = 0, FillOff, NoSource, TooSoon, NoVoice, TooShort, GuardEaten,
    NoBand, DensityRoll, NoAlgoFits, SourceUnavailable, LockSkipped, Count };

struct GapContext {                    // everything stage code may read
    std::int64_t outputPos, blockStartPos, available, usable, maxFillSamples,
                 minGapSamples, preGuard;
    const AnalysisFrame* frame; std::uint64_t key; float conf;
};

struct ScheduleResult { bool started; RejectReason reason; FillRecipe recipe;
                        float availableMs, neededMs; };

RejectReason checkEligibility (GapContext&, ...);   // fill>0, source, retrig, voices, room
bool         rollDecision     (GapContext&, Rng&, int& band, ...);   // band + probability
FillRecipe   buildRecipe      (GapContext&, Rng&, ...);              // length, algo, division...
void         adjustRecipe     (FillRecipe&, GapContext&, ...);       // HOOK: lock, mistakes (empty now)
VoiceSetup   toVoiceSetup     (const FillRecipe&, const GapContext&, ...);
bool         startVoice       (FillVoice*, VoiceSetup&, ...);        // incl. Granular fallback
```

Rules (these are what keep Phase 0 behaviour-preserving):

1. **RNG draw order is frozen.** Today the sequence on `Rng rng; rng.seed(key)` is:
   `chooseBand` (spectral mode only) -> `rng.chance(probability)` ->
   `rng.nextBipolar()` (length scale) -> `chooseAlgorithm` draws. The refactor
   MUST draw in exactly this order. `setup.rngSeed = hashCombine(key, (algo+1)*0x27D4EB2F)`
   is unchanged.
2. **New features never draw from `rng`.** Each new random decision uses its own
   sub-generator `Rng sub; sub.seed (hashCombine (key, kSalt_<Feature>))` with a
   constant salt defined in `Source/features/Salts.h` (one `constexpr` per
   feature, values never reused). This is the rule that lets features merge in any
   order without perturbing each other's or the legacy decisions. A test asserts
   that enabling a feature at a neutral setting does not change `rng` state.
3. `trySchedule` keeps its name and becomes a ~40-line orchestrator calling the
   stages. Each early return becomes `return {false, RejectReason::X, ...}`.
4. Mapping of today's returns to reasons (the contract for E-3):

| Today (`FillEngine.cpp`) | Reason |
|---|---|
| `fill01 <= 0.001` | `FillOff` |
| `frame.reference < 3e-4` | `NoSource` |
| `getActiveVoiceCount() >= maxConcurrent` | `NoVoice` |
| `outputPos - lastFillStartPos < minGapSamples/2` | `TooSoon` |
| `available < minGapSamples` and `boundary - outputPos < minGapSamples` | `TooShort` |
| `available < minGapSamples` but `boundary - outputPos >= minGapSamples` (the pre-guard ate it) | `GuardEaten` |
| spectral mode `chooseBand < 0` | `NoBand` |
| `rng.chance(probability)` false | `DensityRoll` |
| `chooseAlgorithm < 0` or forced algo shorter than `minMs*0.6` | `NoAlgoFits` |
| `findFreeVoice == nullptr` | `NoVoice` |
| `voice->start` false twice | `SourceUnavailable` |
| Fill Lock active and the locked recipe cannot fit the gap (added by C-5) | `LockSkipped` |

Note: `trySchedule` runs **once per gap** (`gapEvaluated`), so a gap produces at
most one reason. Regions that never reach `kEnterGapConf = 0.55` are not gaps and
produce no event; the visualiser shows the threshold line instead (E-3).

Acceptance: golden test (F0-7) bit-identical; all 87 existing tests pass; a
new test asserts each reason in the table is reachable from a synthetic input.

---

## F0-3 `FillRecipe`   (size M)

New `Source/dsp/FillRecipe.h`. Plain-old-data, fixed 16 bytes, no pointers, no PCM.

```cpp
struct FillRecipe {
    std::uint8_t  algo;          // Algo index
    std::int8_t   band;          // -1 broadband, 0..2
    std::uint8_t  lengthClass;   // 0..3: <25%, <50%, <75%, <=100% of maxFill
    std::uint8_t  division;      // Division index, 0xFF = free/none
    std::int8_t   pitchSemis;    // signed, for pitch/tape algorithms, else 0
    std::uint8_t  modifierMask;  // bit per Modifier (FilterSweep..PanMotion)
    std::uint8_t  stereoMode;    // 0 mono-ish, 1 wide, 2 ping-pong, 3 motion
    std::uint8_t  mistakeType;   // 0 none (see C-3)
    std::uint16_t lengthMs;      // planned length after clipping to the gap
    std::uint8_t  mistakeAmt;    // 0..100
    std::uint8_t  flags;         // bit0 preview, bit1 fromLock, bit2 biased
    std::uint32_t seed;          // VoiceSetup::rngSeed (low 32 bits)
};
static_assert (sizeof (FillRecipe) == 16);
bool isValid (const FillRecipe&) noexcept;     // range-checks every field
```

* `toVoiceSetup` gains optional overrides so a recipe can *force* choices the
  voice currently derives internally: add to `VoiceSetup`
  `std::uint8_t modifierOverride = 0xFF;` (0xFF = derive as today) and
  `std::int8_t pitchOverride = 127;` (127 = derive). `FillVoice::start` honours
  them only when set. Neutral by default.
* The recipe records **decisions**, not randomness inside the voice. Lock (C-5)
  keeps the recipe but draws a fresh `seed` per gap, so a locked fill keeps its
  character without being a literal loop (an `exact` option is out of scope).
* Serialisation: one recipe = 16 bytes -> base64 attribute. 8 history + 1 lock
  = 144 bytes; the whole feature-state budget is 32 KiB (F0-6).

---

## F0-4 Lock-free queues   (size M)

New `Source/dsp/SpscQueue.h`: `template <typename T, int N> class SpscQueue`
(wrapper over `juce::AbstractFifo`, same pattern as `VisualisationFifo`,
`push` never blocks and returns `false` if full, `pop` returns count).

Two instances, both owned by the processor:

| Queue | Direction | Payload | Size | Enabled |
|---|---|---|---|---|
| `commands` | message thread -> audio | `Command { type, velocity, index, FillRecipe recipe }` | 16 | always |
| `fillEvents` | audio -> message thread | `FillEvent { type, FillRecipe recipe, RejectReason reason, float availMs, float needMs, std::int64_t pos }` | 64 | recipes always, rejections only while `viz` enabled |

* `Command::type`: `Preview`, `LikeLatest`, `LikeRecipe`, `LockLatest`, `LockRecipe`,
  `Unlock`, `Recapture`, `ReleaseCapture`.
* Producers: **only the message thread** pushes commands (single producer). MIDI
  triggers handled on the audio thread (E-1) act directly, never via the queue.
* The audio thread drains `commands` once per block, at the top of `processBlock`.
* The processor's existing 15 Hz `timerCallback` drains `fillEvents` into a
  message-thread `RecipeHistory` (8 entries, newest first) that **lives in the
  processor, not the editor** (see R2) and a `GapOutcomeLog` (last 64 outcomes).
* A full `fillEvents` queue drops the **newest** event (producer cannot discard
  the consumer's oldest without locking) and increments an `std::atomic<uint32_t>
  droppedEvents` counter shown in the debug overlay.
* All payloads are trivially copyable; `static_assert(std::is_trivially_copyable_v<T>)`.
* **Reserve the visualiser fields now** so E-1 and E-3 never both edit `VisualisationState.h`:
  add `VizFrame::flagPreview = 1 << 5`, `VizFrame::flagRejected = 1 << 6`, and
  `std::uint8_t reason = 0;` in Phase 0 (unused, zero-initialised, `VizFrame` stays
  trivially copyable; the FIFO is 4096 frames so the size change is irrelevant). Add the
  matching `MeterState` fields (`previewStatus`, `blockCounter`, `reasonCount[]`,
  `syncClamped`) in the same PR.

---

## F0-5 Effective-settings pipeline   (size M)

Creative spec §8 defines the order: base -> personality -> macros -> breathing &
phrase -> (decision) -> Like/Lock -> mistake -> envelope -> protection. Today
there is one function, `GapFillAudioProcessor::gatherEngineSettings()`
(`PluginProcessor.cpp:227`). Doing the whole chain per block would make decisions
depend on where the host cut the block, so the chain has **two stages**
(rationale and table in `04-SPEC-CREATIVE.md` §Two-stage effective settings):

* **Block stage** (parameters only): `applyPersonality`, `applyMacros`.
* **Decision stage** (needs PPQ at the decision sample): `applyAtDecision`
  (Evolve, Breathing, Phrase), called inside the scheduler on a local copy.

New `Source/features/effective/Effective.{h,cpp}` (pure functions, no JUCE UI,
fully unit-testable):

```cpp
struct TransportInfo { bool valid; double ppq; int tsNum, tsDen; double bpm; };

// block stage
void applyPersonality (EngineSettings&, int personality);                        // C-1
void applyMacros      (EngineSettings&, const MacroState&, MacroOutputs&);       // C-2
void applyBlock       (EngineSettings&, const ModulationInputs&, MacroOutputs&); // fixed order

// decision stage (ppq = exact PPQ at the decision sample; ts from the block)
void applyAtDecision  (EngineSettings& sLocal, double ppq, const TransportInfo&); // C-8, C-6, C-7
```

`EngineSettings` gains one nested neutral struct (added in Phase 0, filled by
features):

```cpp
struct PerformanceSettings {
    bool  lockOn = false;             // C-5
    float mistakes01 = 0.0f;          // C-3
    int   envShape = 0;               // C-4: 0 Flat
    float envAmount01 = 0.0f;
    BreathSettings breath;            // C-6  { bool on; int bars; float depth01; int curve; }
    PhraseSettings phrase;            // C-7  { int bars; float bias01; }
    EvolveSettings evolve;            // C-8  { int periodBars; }
    bool  likeActive = false;         // C-11
    int   quantDivision = -1;         // E-2 fill-length quantise, -1 off
    bool  modSync = false;            // E-2
    float modSyncSeconds = 0.0f;
    int   bandType[3] { 0, 0, 0 };    // C-9: 0 Auto, else Algo+1
    int   tsNum = 0, tsDen = 0;       // valid when both > 0
};
PerformanceSettings perf;
```

Processor change is **one line** in `gatherEngineSettings()`:
`Effective::applyBlock (s, gatherModulationInputs(), macroOut);` plus
`gatherModulationInputs()` / per-feature `gather (EngineSettings&, const ParamCache&)`
functions declared in `Source/features/FeatureHooks.h` and called from one fixed list
(explicit calls, not dynamic registration, so call order is reviewable). The
scheduler change is one line at the top of `trySchedule`:
`EngineSettings sLocal = s; Effective::applyAtDecision (sLocal, ppqAtDecision, ts);`
and all later stages read `sLocal`.

Each stage MUST return immediately and leave its input untouched when its feature
is at the neutral setting (test: both `applyBlock` and `applyAtDecision` on default
inputs are the identity, checked with `memcmp` of `EngineSettings`).

`updateTempoInfo` (`PluginProcessor.cpp:~290`) already computes bpm/ppq; extend
it to also read `getTimeSignature()` into `TransportInfo` - required by C-6/C-7/C-8
and currently not read.

---

## F0-6 State versioning and subtrees   (size M)

Current state: APVTS tree + root properties (`tooltips`, reduced motion, keyboard
nav, editor size, MIDI mappings). Add:

* Root property `stateVersion` (int). Absent => 1 (everything shipped so far). Phase 0 writes 2.
* `Source/features/state/StateSubtrees.{h,cpp}`: a fixed array of
  `{ juce::Identifier name; size_t maxBytes; bool (*validate)(const ValueTree&); void (*apply)(Processor&, const ValueTree&); ValueTree (*serialise)(const Processor&); void (*reset)(Processor&); }`.
* Load order inside `setStateInformation` / `applyParameterState` (extends the
  existing atomic-validation contract, `PluginProcessor.cpp` `isParameterStateValid`):
  1. parse + size-check; 2. validate APVTS values (existing); 3. validate **every**
  subtree; 4. only if all pass, apply parameters, then subtrees, then UI prefs.
  A *bad optional subtree* (macros, history, lock) is **dropped and reset to default,
  with the rest of the state still applied** - a corrupt recipe must never block a
  valid preset (creative spec §4.8). A bad APVTS value still rejects everything (existing rule).
* Subtrees (each owned by one feature): `<Macros>` (C-2), `<Lock>` (C-5),
  `<History>` (U-1), `<Perf>` (U prefs: simple/advanced, ui scale, first-run flag, trigger note).
* Total subtree budget 32 KiB, each subtree capped individually; over-cap => dropped.
* `migrate (ValueTree&, int fromVersion)`: v1 -> v2 sets nothing (all new
  parameters default to neutral). The function exists so v3 has a place to go.
* Never serialise: audio samples, active capture latch contents, momentary/command
  state, preview state. `capture` (existing latched bool) stays a parameter.

---

## F0-7 Test rig and golden regression   (size L)

### `Tests/support/TestRig.h` (new)

```cpp
struct RenderResult { juce::AudioBuffer<float> out; int fills; /* + counters */ };
RenderResult renderOffline (GapFillAudioProcessor&, const juce::AudioBuffer<float>& in,
                            int blockSize, double sampleRate);
juce::AudioBuffer<float> drumLoop (double sr, double bpm, int bars);   // kick/snare/hat with real gaps
```
Built from the generators already in `Tests/TestSignals.*`. Tests for new
features use these instead of hand-rolling loops (reduces duplicated, divergent
test code and the 2022-line `TestPlugin.cpp` stops growing).

### Golden regression

* `Tests/golden/<platform>/<scenario>.f32` + `tools/dev/write_golden.sh`
  (`GapFillTests --write-golden`). Platform dirs `linux`, `windows` because
  clang-cl vs gcc may differ in the last ulps.
* Scenarios (each 8 bars, seed fixed, 48 kHz): `A` drum loop default settings;
  `B` spectral mode; `C` sidechain on with a click track; `D` forced Tape Stop,
  `E` complexity 100 / chaos 100 stress; `F` bypass->active transition.
* Comparison: max abs error <= `1e-5`, RMS error <= `1e-6`. On failure print the
  first differing sample index and the scenario.
* **The golden files change only in a PR whose title contains `[golden]` and
  whose description states which feature intentionally changed the sound.**
  `tools/check_hot_files.py` enforces that.
* **State fixtures**: `Tests/fixtures/state_0.0.1_<preset>.bin` - states saved by the
  0.0.1 beta for every factory preset plus 3 hand-edited sessions (extreme values, all
  weights 0, MIDI mappings present) and corrupt/oversize/truncated variants. A test loads
  each, checks every parameter against the preset, and renders scenario A against the
  golden. Generated once from the 0.0.1 build; never regenerated by a feature PR.
* **`Tests/support/AllocGuard.h`**: replaces global `operator new/delete` in the test
  binary with a counter; a scope object makes any allocation inside it fail the test
  (warm up JUCE internals first). Required by every feature touching the audio path (`06` §1).
* **`Tests/Perf.cpp`** (group `Perf`): renders scenario E, reports mean and p99.9 block
  time and compares with `Tests/perf_baseline.json` (`06` §5).
* Block-size invariance: a second test renders scenario A at block sizes
  32/64/128/480/512/1024/4096 and requires identical fill start positions
  (scheduling is hop-grid based so this holds today; keep it holding).

---

## F0-8 Developer tooling   (size S)

* `tools/dev/bootstrap.sh`: clone JUCE 8.0.15 and the pinned clap-juce-extensions
  commit if absent, configure Ninja Release, print next steps.
* `tools/dev/check.sh`: the same gates CI runs, fast-to-slow, stopping at the
  first failure: preflight -> build -> unit tests (non-Combinatorial) -> (opt) pluginval.
* Both are single files that can be pasted/run in one go (no flags needed).
* `tools/check_hot_files.py`: given the PR's changed-file list (from `git diff --name-only origin/master...`),
  fails when a PR titled with a feature/ticket prefix touches a hot file listed in
  `01-DEVELOPMENT-PLAN.md` §3 without the `hot-file-ok` label or the `[golden]` / `[perf]` title tag where applicable.
  Unit-tested like `validate_repo.py`.
* `.github/pull_request_template.md`: ticket, summary, neutral-at-default evidence,
  tests added, hot files touched, the self-review checklist from `06` §9, unverified items.
* `.clang-format` (matches the current code style; applied to new files only) and
  `.clang-tidy` (checks listed in `06` §6).

---

## F0-9 Editor slots   (size M)

**Revised by `docs/dev/12-GUI-AND-UX-SPEC.md`.** The original static-registration
slot-host proposal below is retired: static factories and plugin-wide registration
add indirection without solving the immediate space constraint. The first GUI slice
keeps `PluginEditor` as the composition root and extracts the reusable disclosure
presentation into `Source/gui/layout/DisclosureTile.{h,cpp}`. Current feature panels
are navigated with tabs and hosted in a vertical viewport; tiles own presentation
state only. Future layout extractions should follow the explicit page/tile boundaries
in spec 12 rather than adding global slot registration.

The prior proposal is retained below for historical context; it is not an active
implementation requirement.

`PluginEditor.cpp` lays everything out by hand in `resized()`; every UI feature in
the plan adds a button, strip, or overlay there. The retired proposal was to replace
hand-placed additions with named slots:

```cpp
enum class Slot { TopBarRight, CreativeExtra, PerformanceStrip, VisualiserOverlay,
                  OptionsExtra, FooterStatus };
struct SlotComponentFactory {
    Slot slot; int order;                       // order sorts within a slot
    std::function<std::unique_ptr<juce::Component> (GapFillAudioProcessor&)> make;
};
void registerSlotComponent (SlotComponentFactory);      // static-registration helper
```

* `Source/gui/SlotHost.{h,cpp}` (new) owns the instances, lays out each slot's
  components left-to-right (or top-to-bottom for `OptionsExtra`) with 8 px
  spacing per `theme.md`, and exposes `getSlotBounds (Slot)` so `resized()` only
  reserves one rectangle per slot.
* Phase 0 moves the existing CAPTURE button into `CreativeExtra` as the worked
  example. Slot components receive the processor, own their own `ParameterAttachment`s,
  and implement their own tooltips, accessibility titles and keyboard focus.
* A slot component MUST handle being created/destroyed at any time (editor
  re-open) and MUST NOT touch audio memory (read atomics / processor
  message-thread state only).
* Minimum window size (`kMinWidth/kMinHeight`) is a Phase 0 decision: measure the
  reserved slots and record the value; features that need more room raise it in
  *their* PR only with a screenshot at the new minimum.

---

## Revisions to the creative spec

These are deliberate changes; each states why.

| # | Creative-spec statement | Revision | Why |
|---|---|---|---|
| R1 | Macro defaults 50/25/25/15 with macros always owning mapped targets | Add `macrosOn` bool param, **default Off**. When Off the pipeline skips macros. Macro *values* keep the 50/25/25/15 defaults. | With macros always on, loading any pre-feature session would change how Density/Fill/Chaos/... sound, breaking the "older states load with all new modes neutral" rule (§5). |
| R2 | "The editor owns the visible ring" of 8 recipes | The **processor** owns `RecipeHistory` and `GapOutcomeLog`, drained by its existing timer. | History must persist and be saved while the editor is closed; the editor is destroyed/recreated by hosts constantly. |
| R3 | `captureHold`, `captureLength` | Capture already shipped as the `capture` parameter in 0.0.1. Do **not** add `captureHold`. Add only `captureLength` (C-10). | Avoid two IDs for one concept. |
| R4 | Fill Audition is a command only | Keep command. Add MIDI-note trigger (U-3) as a separate audio-thread path that sets the same coalescing flag. | Performance use needs a note trigger; no extra automation surface. |
| R5 | Preview "reserved from the fixed voice pool" | Add a **fifth, dedicated** `previewVoice` outside the pool of 4 (`FillEngine::numVoices` stays 4). | Today `maxConcurrent` is computed from the pool; stealing a pool voice would change natural behaviour. Cost: one more `FillVoice` of preallocated memory (measure and record it, see E-1). |

## Permanent parameter registry (new IDs only)

All appended, all permanent once released. `Default` is the neutral value.

| Group (file) | ID | Type / range | Default | Ticket |
|---|---|---|---|---|
| macros | `macroActivity` | float 0-100 | 50 | C-2 |
| macros | `macroChaos` | float 0-100 | 25 | C-2 |
| macros | `macroSpace` | float 0-100 | 25 | C-2 |
| macros | `macroDestruction` | float 0-100 | 15 | C-2 |
| macros | `macrosOn` | bool | off | C-2 |
| personality | `personality` | choice: Off, Subtle, Percussive, Glitchy, Psychedelic, Dub, Aggressive, Ambient, Unhinged | Off | C-1 |
| capture2 | `captureLength` | float 250-2000 ms | 1000 | C-10 |
| envelope | `fillShape` | choice: Flat, Rise, Fall, RiseFall, RandomWalk | Flat | C-4 |
| envelope | `fillShapeAmount` | float 0-100 | 0 | C-4 |
| lock | `fillLock` | bool | off | C-5 |
| breathing | `densityBreath` | bool | off | C-6 |
| breathing | `densityBreathBars` | choice: 1, 2, 4, 8 | 4 | C-6 |
| breathing | `densityBreathDepth` | float 0-100 | 0 | C-6 |
| breathing | `densityBreathCurve` | choice: Sine, RiseFall, Pulse | Sine | C-6 |
| phrase | `phraseBars` | choice: Off, 2, 4, 8, 16 | Off | C-7 |
| phrase | `phraseBias` | float 0-100 | 0 | C-7 |
| mistakes | `mistakes` | float 0-100 | 0 | C-3 |
| sync | `fillQuant` | choice: Off, 1/4, 1/8, 1/16, 1/32 | Off | E-2 |
| sync | `modSync` | bool | off | E-2 |
| bandtype | `bandLowType` / `bandMidType` / `bandHighType` | choice: Auto + 15 algorithms | Auto | C-9 |
| evolve | `evolve` | choice: Off, 8 bars, 16 bars, 32 bars | Off | C-8 |

Not parameters (state subtree or runtime only): Audition, Like, Lock-latest,
Recapture, Surprise, Undo (commands); macro mappings/names, history, lock
recipe (subtrees); Simple/Advanced, UI scale, first-run flag, trigger note (`<Perf>`).

Excluded from Random / Random All / Surprise / Undo snapshots (must not be
randomised): `macrosOn`, `fillLock`, `captureLength`, `evolve`, all `*Type`
band assignments unless stated in the owning ticket. Add them to a new
`performanceParameterIDs()` list next to `randomisableParameterIDs()` and a test
that the two lists are disjoint.

## Phase 0 definition of done (applies to F0-1 .. F0-9)

* All nine F0 tickets merged in one PR train (or one PR if reviewable), CI green.
* Golden files generated **from `master` before the refactor** and committed in
  the first commit of the PR; the refactor commits must leave them unchanged.
* `docs/progress/F0.md` records measured numbers: `sizeof(FillVoice)`, total
  preallocated memory at 48 kHz/2 ch, `processBlock` CPU at 512 samples (baseline
  for the budgets in `06`).
