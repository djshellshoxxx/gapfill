# WP9 - Bus and spectral effects progress

Branch: `worktree-agent-ad37f35f697acd9d7` (not pushed; based on `claude/inspiring-fermi-pv9gv5` after WP3 and WP8).
Scope: SP-0, F-M15, F-L19, F-L15, F-L21, F-L20, F-X02, F-X01 (doc 12 sections 3 and 7).
Neutral settings (every bus Amount 0) leave the golden renders and the 0.0.1 state fixtures unchanged: the chain returns before
touching the buffer (`defaults_chain_identity_and_cards_never_called`, `plugin_with_every_bus_amount_zero_is_bit_identical`).

## Status per ticket

| Ticket | Status | Where |
|---|---|---|
| SP-0 shared spectral processor | Done, one latency deviation | `Source/dsp/spectral/Stft.*` |
| F-M15 Comb resonator | Done | `Source/features/fx/bus/ResonatorCard.*`, `Source/params/resonator.*` |
| F-L19 Frequency-shifted delay | Done | `bus/FsDelayCard.*`, `params/fsdelay.*` |
| F-L15 Dynamic dub feedback | Done | `bus/DubCard.*`, `params/dub.*` |
| F-L21 Transient-derived convolution | Done (threaded loader) | `bus/ConvoCard.*`, `params/convo.*` |
| F-L20 Formant shifting | Done, one latency deviation | `bus/FormantCard.*`, `params/formant.*` |
| F-X02 Spectral freeze and morph | Done | `bus/SpFreezeCard.*`, `params/spfreeze.*` |
| F-X01 Harmonic granular resynthesis | **Not done**: depends on N-MUS (WP7), which has not landed on the integration branch | `params/hgran.*` stays empty |

## New parameters (34, permanent, append-only, locks updated)

`resAmount resNote resMode resDecay resDamp resSpread` | `fsdAmount fsdShift fsdTime fsdFeedback fsdDamp fsdStereo` |
`dubAmount dubDiv dubFeedback dubLow dubHigh dubMove dubSat dubPing` | `fmtAmount fmtShift fmtTilt fmtVowel` |
`sfzAmount sfzLength sfzDecay sfzSmear sfzMorph sfzPitch sfzWidth` | `cvAmount cvSize cvDamp`.
Ranges and defaults are those of `docs/dev/02` (permanent registry); IDs keep their descriptive names (DECISIONS.md item 18).
Not part of Random / Surprise.

## New files

`Source/dsp/spectral/Stft.{h,cpp}`, `Source/features/fx/bus/{BusDsp.h,BusCardBase.h,BusCards.h,ResonatorCard.*,FsDelayCard.*,DubCard.*,FormantCard.*,ConvoCard.*,SpFreezeCard.*}`,
`Tests/features/{TestSpectral.cpp,TestBusFx.cpp}`. Changed (seam files WP8 created): `FxSettings.h`, `FxHooks.cpp` (gather lines), `FxChain.*`
(owns `bus::BusCards`; six `addBusCard` lines in `build()`), `EffectCard.h` (`BusFxContext` gained settings/tempo/scale/history),
`EngineBlockIO.h`, `FxUiState.cpp` (auto-capture flags in `<Fx>`), `EffectsSection.*` (six cards, action buttons, help 4080-4130),
`cmake/sources/fx.cmake`, `cmake/tests/fx.cmake`.

## Hot-file touches (need the `hot-file-ok` label)

`Source/dsp/FillEngine.cpp`: four lines at the top of `FillEngine::process` inside the existing WP8 HOOK block publish
`io->samplesPerBeat`, `io->history` and `io->historyOffset` to the bus cards (tempo for the synced delays, the dry source for IR /
freeze capture). Nothing else in a hot file changed.

## Design notes

* Every bus card derives from `BusCardBase`: 20 ms Amount ramp, off cards are never called, panic `fadeOut` mutes the effect
  contribution and clears the tail (a new fill start un-mutes), per-card tail cap (input silent for `tailCapMs` -> 250 ms fade ->
  state cleared), engine duck applied per sample to the effect contribution (or to the whole output for the latency cards).
* Output form: resonator / delays / convolution / freeze add `amount x effect` to the bus (the dry wet signal is never altered);
  formant is a true blend `(1-a) x delayed dry + a x processed`.
* Command seam: capture / release are atomic request counters on the cards (`requestCapture`, `requestRelease`, `setAutoCapture`), not
  entries of the `Command` queue; `dispatchCommand` only reaches the engine and the cards live in the processor's `FxChain`.
* F-L21 hand-off is a single-slot mailbox (one producer, one consumer) instead of the SPSC queue: the audio thread writes the shaped
  IR into a preallocated buffer, a small worker thread (20 ms poll, joined in the destructor) loads it with
  `Convolution::loadImpulseResponse`. IR audio is never saved in the project; `<Fx>` stores only the two auto-capture flags.
* Scale Lock seam (N-MUS not landed): `BusFxContext::scaleMask` / `scaleRoot` (0 = off). The resonator Scale-degrees mode and the
  formant semitone snap read it; with N-MUS absent both behave as Manual. The Lite quality seam is `FxSettings::lite`
  (F-L11/quality group not landed); the formant, convolution and freeze cards are disabled while it is true.

## Deviations from the specs (and why)

1. **SP-0 latency is N - 1, not N - hop.** A sample-by-sample stream (arbitrary host block size) needs the last overlapping frame, so
   the exact figure is N - 1 (1023 samples = 21.3 ms at N = 1024; 2047 at N = 2048). `latencySamples()` reports it and the tests pin it.
2. **F-L20 delay** is therefore 21.3 ms at <= 48 kHz (spec: <= 16 ms; DECISIONS item 17 accepts ~10.7 ms for the per-voice design in
   `docs/dev/features/F-L20`). The registry (4 parameters, `fmtAmount` ...) and doc 12 describe a **bus** card, which is what is built;
   the per-voice `fl20_*` design of the feature file (Preserve, Detail, formant pool) is not implemented.
3. **Dub "darkening"**: an extra in-loop one-pole at 0.9 x 3 x the low-pass cutoff (the spec's "x0.9 cutoff per pass" read as a gentle
   repeated roll-off). The LFO is tempo-locked in rate (2 bars), free-running in phase (no ppq in the bus context).
4. **Auto-capture of the IR** looks at the dry source history for the first peak >= -12 dBFS and captures `cvSize` later; "deterministic
   for the same input" holds because it depends only on the history.
5. **F-X02 capture slots**: first capture -> A, second -> B (`hasSecondCapture`), later captures shift B into A. Phases of the last of
   the four frames are kept and the sustained phases are initialised by the sub-RNG and identity-phase-locked around spectral peaks.
6. **Spectral freeze length** counts from the capture, and the sound starts one SP-0 latency later; a very short length therefore
   yields little sound.
7. **Not done**: F-X01 (needs N-MUS). The `Command` ids `IrCapture`, `SpectralCapture`, `SpectralRelease` are unused (see above).

## Unverified

* Real DAW / host behaviour, pluginval / clap-validator / auval for the new parameters.
* Audible character of every effect; only measured properties are tested.
* Windows (MSVC) and macOS builds; cross-platform FFT tolerance of the freeze (`sr_change_reprepare_and_determinism` pins same-platform determinism).
* The Effects section in the real editor (no reserved rectangle until UI-0), file dialogs for Save / Load IR.
* Scale Lock and Lite integration (seams only).
