# 08 - Stabilization first, modularity second, features later
Status: execution plan of 2026-10-19, **updated 2026-10-21: the feature gate is lifted.** The owner reports beta1 shipped on all platforms and authorized implementing every catalogued item. Gates A and B below remain the *quality bar* (green CI on every supported platform, neutral-default goldens, old-state fixtures, no audio-thread allocation) for each PR, but they no longer block feature work. Features that depend on the Phase 0 seams (`02`, F0-1..F0-10) still wait for those seams. Every Gate C entry now has a full spec in `12-SPEC-BACKLOG-AND-NEW.md` (coverage map in its §1).

## Decision and sequence (overrides sequencing in 01 for the near term)

**Gate A: shipping confidence** is first. Deliver individually verified **Windows x64 VST3 + Standalone**, then **Linux x64 VST3 + Standalone**, then **macOS arm64 and x86_64 VST3 + AU + Standalone**, then **CLAP on Windows, Linux and macOS**, in that priority order. CLAP already exists; "fourth" means final acceptance/release gating, not postponing preservation of existing CLAP tests. Never deliberately regress existing platforms while prioritizing another. An independently verified platform may be distributed before another is ready, with truthful per-platform release labels; a cross-platform release is complete only when every required target passes.

**Gate B: modular and safe extension surface** is second. Refactor without changing output, parameters, public IDs, state serialization, latency, preset sound or supported host behavior; prove equivalence with tests. Do not implement creative backlog until Gates A and B are explicitly signed off.

**Gate C: feature candidates** is third and is now **open**. The backlog below was a catalogue; each entry is now *specified* (see `12` §1 for the canonical spec per item, `12` §13 for work packages and `12` §12 for merge order). Selection is replaced by the dependency order; an owner is still assigned per work package before a ticket starts.

### Evidence boundaries

At planning time, master `CMakeLists.txt` declares `FORMATS VST3 Standalone`; CLAP is added conditionally using `clap-juce-extensions`. Master `.github/workflows/ci.yml` contains Linux and Windows jobs, not macOS/release jobs. Open PRs #9 (packaging), #12 (transport/state/DSP), #14 (artifact selection and pluginval wait), #15 (modularity policy) overlap the planned work. Issue #13 documents a Windows pluginval editor-automation crash; issue #10 documents variable-buffer transport prediction. Older PR/issue reports describe some 91-test passes and Linux validation, but those are historical claims, not today's green Gate A. Inspect fresh CI logs and release listings before publishing any new status. The current default branch has doc roadmap #8 merged; compare PR status again before any merge.

## Operating rules and coordinated work

1. Before code, snapshot master SHA, active branches and PRs, exact Actions runs and failing log steps, releases/assets, relevant issue state, and installed dependency versions into `docs/progress/release-baseline-YYYYMMDD.md`. Do not assume PR status from old conversations. Confirm the authoritative branch from repository state (currently master).
2. Keep a **completion register**: ticket, owner, changed files, expected output, baseline evidence, tests run on actual OS, links to runs, current status/blocker, reviewer, merge SHA, release asset checksum. "Compiles" != "passes validation" != "released". `docs/progress/stabilization-register.md` is the intended file.
3. One small scope per PR. Merge already-validated fixes before adding new work. No parallel edits to the same hot files. Freeze creative feature work. No speculative CI bypass, no ignoring failed validators, no fabricated platform evidence.
4. Build on native GitHub-hosted Windows/macOS/Linux runners for the primary proof. Cross-compilation may supplement, never replace, native execution and plugin hosting. Pin JUCE/CLAP validator/toolchain refs with recorded versions, rather than silently upgrading during release repair. Preserve all working formats.
5. C++ DSP is realtime: no allocation, locks, disk/network, exceptions or UI calls on audio thread. Treat UI callbacks/parameter listeners, host automation and editor destruction as concurrency-sensitive.
6. Preserve parameter IDs, order, normalization, state compatibility, latency/PDC and deterministic sound. Capture pre-change reference hashes/golden renders. Any intentional audible change belongs in a separate later PR and requires approval.
7. Prefer build failures with logs and artifacts retained over tests that silently skip or pass zero cases. Bound CI cost through cached dependency/tool downloads and targeted checks, never by dropping required release validation.
8. Any external signing credentials live in protected GitHub environments/secrets, never in source, artifacts or PR logs.

## Gate A - verified production-capable beta builds

### A0. Baseline, conflict resolution and CI trust (must complete before platform release work)

- **A0-01 Inventory**. Fetch master/PR SHAs, changed-file overlap, PR #9/#12/#14/#15 checks, current issues #10/#11/#13, recent workflow runs, release inventory and tags. Mark fixes as already merged, ready, stale or blocked. Review `docs/audits/2026-10-12-build-bug-performance.md` and pre-existing QA specs `03`, `07`, `docs/VALIDATION.md`.
- **A0-02 Reconcile PRs**. Adopt correct Release-path selection and explicit pluginval process wait from #14; valid Windows packaging output/test from #9; transport/state corrections and test semantics from #12 only with green evidence; apply #15 policy without conflicting with subsequent foundations. Rebase/combine as necessary, close superseded PRs with references; merge each tested change into master in dependency order. Do not assume all PRs should be merged unreviewed.
- **A0-03 Fix execution infrastructure**. Verify Actions permission/billing/runners/cache and reproducible preflight. Fast gate: repository validator, its Python tests, packager synthetic fixture and shell lint. All required CI jobs must actually start, run assertions and return meaningful exit codes.
- **A0-04 Normalize build matrix**. Record exact runner OS and CMake, MSVC, Apple Clang, GCC/Clang, JUCE 8.0.15 and locked CLAP extension SHA. Correct exact artifact paths, assert binaries inside VST3 bundles, explicitly validate correct Release config, fail if requested format is absent. Preserve warnings and compiler output as artifacts.
- **A0-05 Baseline test corpus**. Save fixtures of legacy .gapfill states and host project-state blobs, golden WAVs from representative algorithms and sidechains, parameter inventory/hash, preset hashes, deterministic seed/PPQ cases, 44.1-192kHz audio scenarios and performance baseline. Store only synthetic/permissively redistributable audio. Where current binaries cannot generate baseline, document gap rather than invent one.
- **A0-06 Reproducible developer commands**. One-paste Windows PowerShell and POSIX scripts for configure/build/test/validate/package. No scripts copying plugins into system locations as part of a compile. Cap build concurrency on constrained machines.
- **A0 exit**: source and CI build inputs are documented, conflicting pending fixes resolved or isolated, no skipped mandatory steps, baseline test/fixture gaps documented and assigned.

### A1. Windows first: x64 VST3 and Standalone

- **W-01 Reproduce crash** on native Windows 2022 CI and a Windows 10/11 system: `pluginval --strictness-level 10 --validate <exact Release VST3 bundle>`. Capture verbose logs, process exit status, WER/ProcDump dump and stack trace. Instrument editor creation/destruction, parameter listener registration, asynchronous callbacks, timers, shared states and host automation. Compare tests without GUI *for isolation only*; skip-GUI is not an acceptance pass.
- **W-02 Fix root cause with regression**. Create a minimized repro and failing test first. Stress editor open/close/recreate with concurrent host parameter automation and playback, repeated preset loads, state restore and transport changes. Resolve crash without weakening thread safety or host notifications.
- **W-03 Validate host DSP**. Resolve variable-block transport jump (#10) after comparing #12 to master; test alternating 8192/32/4096/64/512 callbacks, internal subchunking, actual seek/loop, stop/start, tempo ramp, varying lookahead, plugin bypass/PDC, changing sample rate, denormals, NaN/Inf and output bounds. Tests must fail on the prior bug.
- **W-04 Validate saved state**. Round-trip factory/user presets, old session states and unusual valid numbers; reject malformed/oversized values atomically; verify boolean and normalized automation consistency. Test CLAP compatibility preservation without making it the fourth-priority release yet.
- **W-05 Native Windows qualification**. Compile Release, run full registered tests (explicit nonzero count and zero fails), run pluginval level 10 including editor automation, smoke standalone with visible UI and audio device setup, manual REAPER plus another Windows host (e.g. FL Studio), save/reopen/offline render, stress 30+ editor lifecycles, sample rates and buffer sizes. Check CPU and audio-thread allocations.
- **W-06 Packaging**. Verify Windows installer (if retained), portable app, VST3 bundle and checksums against actual binary tree. Fix `artefacts/Release/GapFill.exe` path, validate NSIS real installer install/uninstall/upgrade and no preset deletion. No Debug/empty-bundle artifacts. Signature optional for early beta, status explicit.
- **W gate exit**: commit-linked native Windows x64 artifacts can be installed, discovered and run in real DAWs without crashes; required tests and GUI pluginval are green; SHA256 checksums and manual QA evidence recorded; release assets downloadable and reverified.

### A2. Linux second: x64 VST3 and Standalone

- **L-01 Reproduce native release** on documented Ubuntu LTS GitHub runner, pinned toolchain and package set, Ninja Release and exact bundle locations. Keep CLAP building/tested where enabled so it cannot regress.
- **L-02 Run full tests and pluginval** under Xvfb with explicit `Release/VST3` path and correct binary architecture. Full unit/combinatorial suite, pluginval level 10 including editor test if supported, standalone start/device setup, state fixture and golden tests.
- **L-03 Manual Linux QA** in REAPER plus a second suitable host, on the supported desktop environment; check glibc/library dependencies, missing runtime libs, writable config and home paths, CPU usage, varied rates/blocks/transport, sample project reopen, sidechain behavior.
- **L-04 Package** user-installable VST3/Standalone archives with install locations and dependency documentation; SHA256, ELF `file`/`ldd` checks, executable bits and script syntax. Never imply Linux VST3 bundle is a Windows-style flat file.
- **L gate exit**: native Linux binaries validate and load in the tested DAWs; no dependency or packaging errors; release assets verified. Historical 91-test passes are not substitutes for current logs.

### A3. macOS third: arm64 and x86_64 VST3/AU/Standalone

- **M-01 Add native macOS CI** with stable pinned macOS runner/Xcode version; separately build Apple Silicon and Intel, using arch-specific CMake build directories. Add `AU` to JUCE formats *only under APPLE*. Do not break other platforms. macOS CLAP compile/validator preservation is required if enabled, final cross-platform CLAP approval remains A4.
- **M-02 Build both architectures** with Apple Clang and JUCE/CLAP compatible locked refs; inspect code signing state, dynamic libraries, bundle metadata, Info.plist and architecture via `lipo`/`file`. Universal packaging is optional only after both architectures pass separately. Validate AUs through `auval -v aufx Gpfl Gfau` (confirm actual codes from built bundle) and VST3 pluginval level 10.
- **M-03 Mac real-host acceptance** in REAPER and Logic Pro (AU), plus another supported host if available; DAW scan, GUI lifecycle/automation, MIDI, state, sample rates, transport, audio device, Intel and arm64 native process. Note Rosetta-only tests as such, never present as native Intel validation.
- **M-04 Distribution** bundle .vst3/.component/.app (and .clap when A4) with correct executable permissions and resources; zip/dmg/pkg as appropriate. Notarization requires Developer ID and Apple signing credentials; when unavailable publish clearly labeled unsigned developer/test packages with installation guidance and do not claim notarized end-user readiness.
- **M gate exit**: native arm64 and x86_64 VST3 and AU validations and manual host tests succeed with verified checksums; signing status and minimum supported macOS are explicit.

### A4. CLAP fourth: Windows + Linux + macOS

- **C-01 Standardize** lock `clap-juce-extensions` commit/submodules, validate CLAP target generated on each OS, correct Release paths and binary architecture. Avoid silently skipping CLAP when requested; make feature presence explicit.
- **C-02 Test** run clap-validator on each platform; cover activation/deactivation, state restore, host parameter notifications, audio-thread isolation, bus layouts, automation, MIDI CC/note handling where supported and GUI lifecycle. Confirm no CLAP-specific value-refresh regression.
- **C-03 Real-host** load CLAP on Windows/Linux/macOS in one real CLAP host per platform, preferably Bitwig or REAPER where CLAP is supported; repeat the abbreviated project save/open, presets, offline render, and controls tests.
- **C-04 Packaging** validate each platform's CLAP naming/install path, host discovery, executable architecture, checksums and combined archive. Add CLAP entries only for platforms whose validators and host tests pass.
- **C gate exit**: CLAP binaries verified on three OS families, with no regression to VST3/AU/Standalone.

### A5. Single release pipeline and ongoing stability

- Matrix jobs have separate compilation, test, validator, package and publish stages; no publish from a PR, no publish on partial red jobs. Prefer manual release dispatch pinned to tagged commit, protected environment, required reviewers as appropriate, and a version/tag/tagged-source match.
- Upload raw candidate artifacts even on validation failure for debugging, with short retention; **public Release assets only from validated jobs**. Run download-back verification (zip entries, plugin executable, checksums, manifest) after publication.
- Release manifest records git SHA, JUCE/extension/toolchain versions, OS/CPU/format, validator versions/results, SHA256, signing/notarization status and host QA evidence. Use reproducible semantic names and distinguish release-candidate vs public beta.
- Required matrix checks on every PR for existing supported platforms; macOS CI mandatory once introduced; full CLAP gate retained even when not currently the highest priority. Nightly fuzz/ASan/UBSan where supported, race-oriented tests and heavier combinatorial tests. Never hide required failures with continue-on-error.
- **Overall Gate A**: W, L, M and C all signed off individually; at least one complete verified cross-platform prerelease; no untriaged severity-1/2 regression; rollback to previous artifacts documented.

## Gate B - modular, regression-resistant foundation (only after Gate A)

**Objective:** feature PRs normally touch only their own `Source/features/<name>`, `Tests/features/<name>`, a parameter group and source-list fragment; core audio scheduling, plugin wrapper, shared preset and editor files rarely change. This reduces risk; it cannot guarantee zero build breakages, so automated proof and quick rollback remain compulsory.

- **B0 Baseline lock**: Freeze Gate A tag and golden audio/parameter/state/release artifacts. Get benchmark timings, test counts, memory and toolchain manifests. All B PRs require exact before/after comparisons and revert plan.
- **B1 CMake feature registration**: Move explicit existing source/test lists into `cmake/sources/core.cmake` and `cmake/tests/core.cmake`; add explicit per-feature cmake fragments / registration mechanisms. No uncontrolled wildcard compilation. Add clean configure/build test on all OS (not just incremental local build). Keep release target names/artifact paths stable. Detect absent/duplicate registration and verify every enabled format.
- **B2 Parameter registry**: Preserve existing IDs/index/order/defaults/units and host parameter IDs. Use append-only per-feature parameter groups, lock files and a CI validator blocking accidental reorder, duplicate or missing ID. Pin old state fixtures. No parameter is renamed to simplify code.
- **B3 DSP seams**: Introduce immutable/per-block `EffectiveSettings` and a structured `FillRecipe`; extract the scheduler into named pipeline stages (detect, qualify, choose, adjust, schedule, render, protect). Audio-thread real-time ownership is explicit. Expose pure helper interfaces where possible. Existing behavior and RNG draw order remain identical by default; test this before any new behavior.
- **B4 Realtime communication**: Isolate engine <-> GUI updates behind bounded lock-free/single-writer queues or atomic snapshots with lifetime rules, dropped-event counters and sanitization. No GUI pointers in DSP callbacks. Editor destruction under automation gets a dedicated stress test. Avoid introducing thread-unsafe global/static registrations.
- **B5 UI slots and help registry**: Feature panel slots and separately registered help/tooltip metadata, so future UI work does not repeatedly edit `PluginEditor.cpp`, `Panels.cpp`, and `HelpView.cpp`. Preserve keyboard/reduced-motion/accessibility behavior. Stable plugin appearance/size at the default view.
- **B6 Persistent state/presets**: Versioned optional feature subtrees, bounded validation, atomic apply and explicit migration tests. Existing .gapfill and DAW project states load unchanged, corrupt data fails safely; version-neutral feature defaults preserve old sounds. Keep disk I/O entirely off audio thread.
- **B7 Tests and fixtures**: Pure DSP unit tests; deterministic golden render scenarios A-I in `07-QA-AND-RELEASE`; fixed-seed/block-size invariance; old state fixtures; realtime allocation guard; editor lifecycle, transport, silence/NaN; CMake registration tests; CLI must fail if zero tests execute. New modules ship tests first (red/green) and a default-disabled equivalence test.
- **B8 CI containment**: Fast local checks (format, registration, core unit tests), PR matrix for Windows/Linux/macOS VST3/AU/Standalone/CLAP, exact artifact checks and validators; nightly sanitizers/extended DSP/benchmark sweeps; release jobs pinned to passed SHA. Cache dependencies while preserving clean-build validation.
- **B9 Developer contract**: Add `Source/features/README.md` explaining file naming, ownership boundaries, permitted shared API hooks, adding a parameter, test, help panel, CMake registration and preset migration. PR template requires risk assessment, default-sound proof, old-state proof, real-time audit, changed-files list, actual OS validation evidence and rollback notes.
- **B10 Prove with a no-op sample feature**: Build a small compiled **disabled/neutral demonstration module** end-to-end that registers independently without changing audio or published parameters; validate in clean Release builds and package workflows for all formats. Remove or keep it internal according to tests. This proves that adding a module is repeatable, not just documented.
- **B11 Incremental merge**: Small reversible PRs (<~600 non-test LOC ideally), single hot-file owner per stage, merge after all platform gates; revert instead of piling hacks. Preserve other agents' branches and write each module's handoff to `docs/progress/<ticket>.md`.

**Overall Gate B exit (all required)**: clean fresh builds on W/L/M and CLAP; pluginval level 10, clap-validator and auval as appropriate; existing golden output/legacy-state tests unchanged; no new audio-thread allocation; module example exercises plug-in registration without core-file edits; new feature skeleton passes all jobs; docs/ownership/PR checklist complete. If a B-step breaks a baseline, revert and isolate before continuing.

## Gate C - deferred ideas catalogue: low implementation complexity first, then value within each tier

**Sorting rule**: Tier S (small localized/config/UI changes), M (moderate plumbing), L (cross-cutting DSP/UI/host integration), XL (new algorithms, heavy analysis or new interfaces). **Within each tier, sorted by expected practical value**, with tie-break favoring independent testability. Estimates are *relative*, not promises. Existing implemented features like Undo, Capture, Surprise and Tape Stop should not be reimplemented: enhancements only. Some concepts overlap existing `03`/`04`/`05` specifications; use one ticket per underlying behavior and cross-reference existing spec.

Value labels: H = high everyday/production utility, M = medium/niche, X = exploratory. Category key: FX effects, UX usability, VAL product value, ESS essentials, FUN playful, MUS musicality, LIVE live performance. **All entries below are candidates only, including previously planned features.**

### S - localized changes / mostly UI or control
| ID | Candidate | Category / value | What it does; implementation scope |
|---|---|---|---|
| F-S01 | CPU and latency display | UX/ESS H | Show reported PDC/lookahead and sampled workload metrics, distinguish estimated vs measured CPU; UI/status only, bounded telemetry. |
| F-S02 | Parameter information/status line (existing U-9) | UX H | Plain-language range, units and effective state on hover/focus; follow shared UI slot registry. |
| F-S03 | UI scale setting (existing U-8) | UX H | Persist zoom/scaling and keep controls usable at minimum size and HiDPI; test layout screenshots. |
| F-S04 | MIDI note trigger for Audition (existing U-3) | LIVE/UX H | Map an incoming note/channel to a preview, with bounded learn and velocity scaling; requires Audition first. |
| F-S05 | Effective-value feedback (existing C-0) | UX H | Display temporary macro/personality-adjusted values without altering base parameter values. |
| F-S06 | A/B comparison with level matching | UX/VAL H | Compare two states with undo and optional matched perceived output level; avoid altering host-saved state unexpectedly. |
| F-S07 | One-click stutter storm | FUN/LIVE M | Momentary curated rapid repeat pattern within existing engine limits, quantized when transport exists. |
| F-S08 | Glitch Roulette | FUN M | Randomized coherent fill profile choices with one-button restore; extend existing Surprise without duplicating it. |
| F-S09 | First-run guide (existing U-7) | UX M | Dismissible three-step tour and a small working example; persists in global settings. |
| F-S10 | Genre-inspired performance presets | FUN/LIVE M | Curated templates (breaks, dub, trance, ambient) using *existing* algorithms; no new DSP. |
| F-S11 | Preset favorites / next-previous | UX M | Store starred presets and allow keyboard/MIDI stepping; subset of the full preset browser (U-4). |
| F-S12 | Instant safe return / panic | LIVE/ESS H | User action to cancel active fills and return to a bounded dry-safe mode with click-free transition; audit host bypass and state. |

### M - isolated feature modules using existing DSP/scheduler
| ID | Candidate | Category / value | What it does; implementation scope |
|---|---|---|---|
| F-M01 | Fill Audition (existing E-1) | UX/ESS H | Preview the next or selected recipe through a separate bounded voice without committing it to history. |
| F-M02 | Why Didn't It Fill? explanations (existing E-3) | UX H | Surface the exact reason a candidate was skipped, along with actionable controls and a confidence legend. |
| F-M03 | Visual gap timeline | UX/MUS H | Overlay transients, eligible gaps, protection zones, fired/skipped fills on a common time axis. |
| F-M04 | Simple/Advanced view (existing U-5) | UX H | Reduce primary controls to core sound design; expose detailed detector/multiband controls in advanced mode. |
| F-M05 | Fill intensity envelope (existing C-4) | MUS/FX H | Shape volume/energy across each recipe rather than use a fixed wet level. |
| F-M06 | Fill Personalities (existing C-1) | UX/FUN H | Coherent reusable character profiles applied as effective settings, preserving user defaults. |
| F-M07 | Density Breathing (existing C-6) | MUS H | Slowly vary fill probability/density to avoid constant repetitive activity. |
| F-M08 | Capture v2 / Recapture (existing C-10) | LIVE/UX H | Refresh previously latched source material on command with predictable phase/state. |
| F-M09 | Per-band fill type (existing C-9) | FX/VAL H | Choose different algorithms for low/mid/high bands while preserving phase coherence. |
| F-M10 | Momentary effects and latch controls | LIVE H | Press-and-hold stutter/freeze/reverse/tape, or latch until disengaged; feature commands separated from GUI. |
| F-M11 | Quantized scene or preset changes | LIVE/MUS H | Apply requested states on next beat/bar/phrase; handle no tempo, loops and transport jumps. |
| F-M12 | Preset browser (existing U-4) | UX/VAL H | Search, categories, favorites and user preset management with safe background scanning. |
| F-M13 | Controlled Mistakes (existing C-3) | FUN/MUS M | Bounded flams, slips and imperfections; deterministic seed-preserving variations. |
| F-M14 | Rhythmic bitcrushing | FX/FUN M | Gap-only quantization/downsampling with synchronized patterns and protected transient boundaries. |
| F-M15 | Comb-filter resonator | FX/MUS M | Resonant pitched delay/filter tails tuned manually or to host scale, with gain and feedback bounds. |
| F-M16 | Rhythmic ratcheting | FX/LIVE M | Grid-aware accelerating/decelerating stutter subdivisions over one eligible gap. |
| F-M17 | Bit-perfect wet-only output mode | VAL/UX H | Expose only generated wet audio through a switchable safe render mode; separate from optional host multi-output buses. |
| F-M18 | Beat Battles / alternating styles | FUN/MUS M | Alternate two saved fill styles per bar/phrase; built atop deterministic settings switching. |
| F-M19 | UI-controlled Freeze & Scrub | FUN/LIVE M | Freeze a captured buffer and scan it with a bounded control; no audio-thread allocation. |
| F-M20 | Tape destruction expansion | FX/FUN M | Add configurable wow/flutter, dropouts, saturation, splice and unstable speed atop existing Tape Stop. |
| F-M21 | Tension / release automation curves | MUS/LIVE H | Map fill intensity, density and width across a user-defined multi-bar build and resolution. |
| F-M22 | Smart preset suggestions | UX/VAL M | Recommend existing preset families from simple measured transient density/energy, opt-in and deterministic. |
| F-M23 | Advanced modulation and macro targets | VAL/LIVE H | Host-automatable mappings from macro lanes to effective parameter values with smoothing and bounded ranges. |

### L - significant new feature plumbing, scheduling, history or analysis
| ID | Candidate | Category / value | What it does; implementation scope |
|---|---|---|---|
| F-L01 | Fill Lock (existing C-5) | LIVE/VAL H | Reuse the last chosen natural `FillRecipe` instead of selecting a new random one. |
| F-L02 | Fill History and Save Recipe (existing U-1) | UX/VAL H | Record recent recipes for replay, lock, saving and exploration without storing unbounded audio. |
| F-L03 | More Like That (existing C-11) | FUN/VAL H | Generate controlled variations of a selected recipe while retaining timing and core character. |
| F-L04 | Fill Export / DAW drag (existing U-2) | VAL H | Record a bounded wet-only segment and save/drag it as audio, with disk work off the DSP thread. |
| F-L05 | Auto-Calibrate (existing U-6) | UX/VAL H | Observe 4-15 seconds of source and tune detection sensitivity, threshold and min gap with undo. |
| F-L06 | Four assignable performance macros (existing C-2) | LIVE/VAL H | User-mappable, MIDI-learnable multi-target macro controls with smoothing and stable state. |
| F-L07 | Performance scenes | LIVE/VAL H | Save/recall 8-16 bounded musical configurations with quantization and click-safe transitions. |
| F-L08 | Phrase-aware scheduling (existing C-7) | MUS H | Favor fills at bar/phrase boundaries; handle manual 4/8/16-bar phrase length and invalid tempo fallback. |
| F-L09 | Groove inheritance | MUS/VAL H | Measure input microtiming/swing and use it to offset fill subdivision placement without false transport jumps. |
| F-L10 | Call and response | MUS/FUN H | Identify a recent rhythmic motif and generate complementary phrases in subsequent gaps. |
| F-L11 | MIDI output from gap/event detection | VAL/MUS H | Generate host MIDI notes and timing events from detection; requires plugin declaration, scheduling and host compatibility. |
| F-L12 | Flexible wet-only output bus | VAL H | Optional host-side separate wet bus alongside dry/stereo output; changes bus layout and latency/preset testing. |
| F-L13 | Enhanced sidechain-driven fills | VAL/LIVE H | External rhythm shapes gap detection and fill schedules; improve source routing and hybrid controls. |
| F-L14 | Multiband effects routing | VAL/FX H | Route differently processed low/mid/high fill paths with independent levels and phase-preserving crossover. |
| F-L15 | Dynamic dub feedback network | FX/MUS H | Stereo ping-pong echoes with time-varying filtering/saturation and strictly bounded feedback. |
| F-L16 | Controlled Evolve (existing C-8) | FUN/MUS M | Slowly alter recipe personality over several bars with deterministic transitions and snapshot recovery. |
| F-L17 | Mutation Lab | FUN M | Mutate selected parameters/recipes over generations; compare, pin, revert and save favorites. |
| F-L18 | Scale and key awareness - manual | MUS/VAL H | Explicit root and scale constrain pitch-bearing grains/delay effects; automated key recognition is separate XL. |
| F-L19 | Frequency-shifted delays | FX/FUN M | Delay network with subtle or radical frequency offsets, stable feedback and aliasing tests. |
| F-L20 | Formant transformations | FX/MUS M | Adjust spectral envelope of captured material independently of pitch for characterful fill timbres. |
| F-L21 | Transient-derived convolution reverb | FX/VAL M | Turn selected impulses/hits into bounded convolution textures; careful IR capture and CPU control. |
| F-L22 | Musical energy tracking | MUS/VAL H | Estimate section density/energy to modify fill style and density over time with predictable hysteresis. |
| F-L23 | Intelligent A/B recipe comparison | FUN/UX M | Audition and compare two candidate generated recipes quickly, maintaining loudness consistency and undo. |
| F-L24 | Controller integration and feedback | LIVE/VAL H | Reliable mapping profiles, hardware indicator feedback where feasible, disconnect recovery and MIDI learn. |
| F-L25 | Live safety and recovery suite | LIVE/ESS H | Consolidate panic, output limiting/bounds, safe bypass, overload fallback and quick last-good-scene recall. |

### XL - high research/engineering scope, defer until strong tests and product demand
| ID | Candidate | Category / value | What it does; implementation scope |
|---|---|---|---|
| F-X01 | Harmonic granular resynthesis | FX/MUS/VAL H | Detect source pitch regions, reconstruct harmonically coherent grains in available gaps with user-controllable motion, density and tone. |
| F-X02 | Spectral Freeze and Morph | FX/MUS/VAL H | Capture spectral frames and sustain/morph them into textured tails with pitch, formant and phase-vocoder controls. |
| F-X03 | Automatic key/pitch-class detection | MUS/VAL M | Estimate tonal center with confidence and a manual override; avoid false tonal guesses on drums/noise. |
| F-X04 | Multi-section arrangement awareness | MUS/VAL H | Track rhythmic patterns/section boundaries and modulate gap decisions over larger song structures. |
| F-X05 | User preference learning for variations | FUN/VAL M | Rank locally saved liked/disliked recipes without telemetry; explainable and opt-in; avoid opaque random shifts. |
| F-X06 | Multi-track cooperative GapFill | MUS/VAL M | Coordinate fill decisions across instances/tracks with host-safe communication and explicit synchronization; no networking required. |
| F-X07 | Adaptive performance/quality modes | LIVE/ESS H | Benchmark-aware DSP quality throttling with deterministic transitions, bounded CPU and no uncontrolled sonic changes. |

### Backlog use and decisions

- All suggestions from the 2026-10-19 review are recorded above, plus the earlier planned E/C/U tickets. Existing features **Undo, Capture v1, Surprise and Tape Stop** are foundations for enhancements, not missing features. No candidate is automatically approved.
- **2026-10-21:** all entries moved from `idea` to `specified`; unselected-entry preservation no longer applies. Lifecycle continues at `tests written`.
- (Historical rule) Before selecting any candidate: inspect actual current code to ensure it is not already available; map overlap to its E/C/U spec; draft user-visible behavior, success/quality metric, host/OS impacts, parameters/state design, real-time risks, tests/golden fixtures, value/complexity estimate and rollout toggle. Select one at a time.
- Re-sort based on measured implementation scope after Gate B. A simple, high-value addition still cannot precede Gate A/B. Some "S" UI tasks have dependencies on larger modules, and *must* wait for those prerequisites.
- Use a one-way lifecycle: `idea -> selected -> specified -> tests written -> implemented -> verified all platforms -> merged -> shipped`. Maintain unselected entries unchanged so nothing is lost.

## Immediate first work session (historical; superseded by `12` §12 wave 0)

1. Verify PR statuses/runs and current master against A0 evidence. Do not start feature coding.
2. Merge/reconcile low-risk already-validated CI/packaging changes in an order that does not break tests.
3. Reproduce Windows editor crash with logs + dump and identify actual stack. This is the first runtime blocker.
4. Obtain a *real* Release Windows VST3 and Standalone validation, then do Linux, macOS, and complete CLAP gate in that order.
5. Tag and archive Gate A baseline, then start B0/B1 only. Feature selection meeting occurs *after* Gate B.
