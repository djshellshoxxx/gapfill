# F-L08 Phrase-aware scheduling
Status: implementation spec, Gate C candidate (backlog `08` F-L08, existing ticket C-7). Category MUS, value H, size M. Canonical: `04` C-7 (decision-stage probability multiplier ramp over the final bar, validity rules shared with C-6) and `10` section 4.4 "Phrase awareness". Not repeated. This file adds the bar offset, a per-bar boundary preference, the exact invalid-tempo behaviour and meter-change rules the backlog row asks for.

## 1 Summary & user-visible behaviour
- **Phrase** length (Off, 2, 4, 8, 16 bars), **Bias** (0-100 %), **Offset** (0-15 bars: where bar 1 of the phrase sits relative to the project start) make fills more likely near the end of a phrase (C-7 behaviour: up to x2.5 probability in the last bar at full bias, ramped by smoothstep).
- New: **Bar bias** (0-100 %) gives a smaller lift in the last beat of every bar (up to x1.5), so fills prefer bar lines even without a phrase setting.
- Never forces a fill, never overrides eligibility (min gap, protection, voice limits, Fill Type fit).
- Invalid tempo/transport/meter: the feature is **neutral** (multiplier exactly 1) and the UI says why ("Phrase: waiting for host tempo", "Phrase: meter unknown"). No free-running fallback clock; a clock started at an arbitrary sample would put "bar 1" in the wrong place and break determinism (explicitly rejected).
- A small phrase-position bar under the visualiser shows position within the phrase and the lift currently applied.

## 2 Success/quality metric
- With Phrase = 4 bars, Bias = 100 %, on an 8-bar drum loop with identical gap opportunities: >= 2.0x as many fills start in each phrase's last bar as in its first three bars on average over 50 seeds; with Bias = 0 the ratio is within +-10 % of 1 (statistical bands; seeds fixed).
- Decisions identical across host block sizes 32-8192, and identical after seek/loop to the same PPQ.
- Zero fills in ineligible gaps (protected, too short) with Bias 100 %.

## 3 Design
Files (`cmake/sources/phrase.cmake`, `Tests/features/TestPhrase.cpp`; `Source/params/phrase.{h,cpp}`):
- `Source/features/phrase/Phrase.{h,cpp}` (pure): 
  ```cpp
  struct PhraseSettings { int bars; float bias01; int offsetBars; float barBias01; };   // bars 0 = off
  struct PhraseInfo { bool valid; double beatsPerBar; double lenBeats; double phase01; double barPhase01; float lift; };
  PhraseInfo phraseAt (const PhraseSettings&, double ppq, const TransportInfo&) noexcept;
  float phraseMultiplier (const PhraseSettings&, double ppq, const TransportInfo&) noexcept;   // >= 1, == 1.0f exactly when neutral
  ```
  Validity (same rule as C-6): `ti.valid && tsNum > 0 && tsDen > 0 && tsDen is a power of two && ppq finite`. `beatsPerBar = tsNum * 4.0 / tsDen`; `lenBeats = bars * beatsPerBar`; `off = offsetBars * beatsPerBar`; `p = frac ((ppq - off) / lenBeats)` using `x - floor (x)` so negative PPQ (pre-roll) and offsets work; `f = 1 - 1/bars`; `r = smoothstep (f, 1, p)`; phrase lift `1 + bias01 * 1.5 * r`. Bar lift: `q = frac (ppq / beatsPerBar)`, `w = clamp (1.0 / beatsPerBar, 0.1, 0.5)` (the last beat as a fraction of the bar), `rb = smoothstep (1 - w, 1, q)`; lift `1 + barBias01 * 0.5 * rb`. Final multiplier = phrase lift x bar lift (max 2.5 x 1.5 = 3.75), then the existing probability clamp to 1.
- Hook: `Effective::applyAtDecision` (F0-5/C-7) exposes `perf.phrase`; `trySchedule` multiplies `probability` by `phraseMultiplier (...)` only inside the density roll. No RNG draw added; the multiplier changes only the threshold given to the existing `chance` call.
- Meter/tempo change handling: phase is computed from the PPQ and the meter **at the decision**, so a mid-phrase meter change causes a phase step; this is documented and tested to be bounded (lift in [1, 3.75], no NaN). Tempo changes need nothing (PPQ domain). Transport loop/seek: pure function of PPQ, so no stale state; no event queue is touched. `noteTransportJump` needs no phrase work.
- `Source/features/phrase/Gui.{h,cpp}`, `Help.cpp`, `Info.cpp`; exposes `PhraseInfo` for the position bar via a relaxed-atomic pack (`phase01`, `lift`, `valid`) written by the audio thread per decision and read by the editor timer.
- Shared with F-L07: `PhraseInfo`/`phraseLengthBeats()` helper so scene quantization uses the same phrase definition.
Minimal hooks in existing files: one multiply in `trySchedule`, one `gather` call in `FeatureHooks.h`, `TransportInfo` time-signature read in `updateTempoInfo` (F0-5 already requires it).

## 4 Parameters & state
| ID | Type / range | Default | Unit |
|---|---|---|---|
| `fl08_bars` | choice: Off, 2, 4, 8, 16 | Off | bars |
| `fl08_bias` | float 0-100, step 1 | 0 | % |
| `fl08_offset` | int 0-15 | 0 | bars |
| `fl08_bar_bias` | float 0-100, step 1 | 0 | % |
Replace `phraseBars`/`phraseBias` of `02`/`04` (needs owner OK; ID regex widened). Backlog text "manual 4/8/16 bars" is covered: 4, 8, 16 plus 2. All four in `performanceParameterIDs()`. Appended to `params/phrase.h`, locked in `tools/param_ids/phrase.lock`. State subtree: none. Old sessions: IDs absent -> Off/0 -> neutral. Neutral proof: `phraseMultiplier` returns the constant 1.0f before any arithmetic when `bars==0 && barBias01==0`, or bias terms are zero; golden A-F bit-identical; zero extra RNG draws.

## 5 Real-time safety
Audio: <= 10 flops and two `floor` calls per evaluated gap (once per gap, not per sample). No state, allocation or locks. The UI atomics are relaxed. Resets: stateless.

## 6 UI
Slot `CreativeExtra` advanced disclosure (Phrase combo, Bias knob, Offset stepper, Bar bias knob) and the position bar under the visualiser. Tooltips: "Phrase: fills become more likely in the last bar of every N-bar phrase. Needs host tempo and time signature." "Offset: shift where the phrase starts, in bars." Status text when neutral reasons apply. Position bar has a text readout ("Bar 4 of 4, x2.1") for screen readers and a non-colour marker; Reduced Motion draws it without easing. Narrow: position bar collapses to the text readout. Help section "Phrase awareness" (order 1170) with the invalid-transport table and the note that Standalone has no host bars.

## 7 MIDI/automation/CLAP/host impacts
Four automatable parameters; `fl08_bars` is stepped (CLAP stepped hint check). Relies on host-supplied time signature (`getTimeSignature()`); several hosts report 4/4 by default even in odd-meter projects, so the status line shows the meter being used. Standalone: no PPQ -> neutral. No latency change. Offline render: deterministic by PPQ.

## 8 Tests
Group `Phrase`: `phrase_off_or_bias0_is_identity` (memcmp EngineSettings, golden) | `multiplier_ramp_values` (p = 0, 0.74, 0.75, 0.875, 0.999 for 4 bars) | `offset_shifts_phase` | `negative_ppq_and_offset_wrap` | `bar_bias_last_beat_window` | `invalid_ppq_or_ts_neutral` (NaN, 0/0, tsDen=3, transport stopped) | `meter_change_mid_phrase_bounded` | `time_signature_6_8_7_4_3_4` | `loop_and_seek_reproducible` | `tempo_change_does_not_change_phase` | `never_fills_ineligible_gap` | `statistical_lift_final_bar` (50 seeds) | `block_size_invariant_decisions` | `no_extra_rng_draws` | `phrase_no_audio_thread_allocation` | `phrase_params_roundtrip_and_old_state`.

## 9 Dependencies on other tickets
Hard: B3 (stages), F0-5 (`TransportInfo` with time signature; decision stage), transport correctness (#10 variable-block prediction, Gate A W-03). Shares C-6 validity code (Density Breathing, F-M07) via `Source/features/common/Meter.h`. Feeds F-L07 (phrase quantization), F-L10 (response windows), F-L09 (shares ts/PPQ helpers). Build order: after Gate A/B; before F-L07 and F-L09.

## 10 Effort & risks
Effort M. Risks: unreliable host time signatures (mitigated by explicit status, never inferred); offset semantics confuse users (help text with a worked example); x3.75 peak lift may feel strong (tunable constants in `Phrase.cpp`, change only in a tuning PR); depends on resolved transport jump bug.

## 11 Acceptance checklist
- [ ] Four IDs appended, locked, classified as performance parameters
- [ ] Neutral when off or when tempo, PPQ or meter is invalid, with a visible reason
- [ ] Statistical lift and block-size/seek/loop determinism tests green
- [ ] Meter and tempo changes produce no NaN, stale state or duplicate events
- [ ] Position bar readable without colour; keyboard and narrow layout checked
- [ ] Golden and old-state fixtures unchanged
