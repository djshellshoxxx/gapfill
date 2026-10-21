# F-L10 Call and response
Status: implementation spec, Gate C candidate (backlog `08` F-L10). Category MUS/FUN, value H, size L. No full spec exists: this is the canonical spec. Requirements source: `10` section 4.4 "Call and response" (bounded descriptors of recent accepted recipes, not audio; response mode selecting complementary or contrasting settings; length and strength controls; inspectable and lockable; fall back to ordinary selection with a visible "no response" when the motif is unclear).

## 1 Summary & user-visible behaviour
- GapFill watches its own last few natural fills (the **call**), summarises them as a motif (dominant character, length, regularity), and shapes the next few fills (the **response**) to answer it: **Complement** (different family, opposite length and pitch direction) or **Echo** (same family and band, similar length, a musical "answer in kind").
- Cycle: collect `calls` natural fills (2-4, default 3) -> if the motif is clear, bias the next `replies` natural fills (1-4, default 2) -> collect again. Status chip: `CALL 2/3`, `RESPONSE 1/2`, or `No response: motif unclear (48 %)`.
- It biases weights and lengths only. It never creates or removes gaps, never changes probability, protection or eligibility, and leaves Fill Type forced, Fill Lock and More Like That in charge (they pause it).
- Response fills are marked **R** in History so the user can Lock one. Nothing is stored as audio.

## 2 Success/quality metric
- Complement, strength 60 %, flat weights, fixture where the call is three rhythmic-family fills: over 200 cycles the rhythmic-family share among response fills is < 60 % of its baseline share (20 % with 15 flat weights, so < 12 %) and the combined textural + pitch/tape share is >= 1.6x its baseline (baseline 60 %, capped by 100 %).
- Echo, strength 60 %: dominant-family share among response fills >= 1.5x baseline.
- Unclear motif (three different families at random): no response is generated in >= 95 % of such cycles and the status says so. Draw count per gap identical with the feature on and off (determinism).

## 3 Design
Files (`cmake/sources/callresponse.cmake`, `Tests/features/TestCallResponse.cpp`; `Source/params/callresponse.{h,cpp}`):
- Reuses `Source/features/common/AlgoFamily.{h,cpp}` from F-L03 (family of each algorithm). Reuses `RecipeCodec` only for History display, not for state.
- `Source/features/callresponse/Motif.{h,cpp}` (pure):
  ```cpp
  struct CallEntry { std::uint8_t algo, band, lengthClass; std::int8_t pitchDir; double startBeat; double startSeconds; };   // from recipe + time
  struct Motif { Family family; float familyShare; std::uint8_t meanLengthClass; std::int8_t band; std::int8_t pitchDir; float regularity; float confidence; };
  Motif describe (const CallEntry* calls, int n, bool useBeats) noexcept;
  ```
  `family` = mode of `familyOf (algo)` (ties: lowest enum value); `familyShare = count/n`; `meanLengthClass = round (mean)`; `pitchDir` = sign of (#PitchRise - #PitchFall); `band` = mode of band (ties: broadband). `regularity`: inter-start intervals `d_i` in beats (if `useBeats`, i.e. transport valid at all entries) else seconds; `cv = stddev(d)/mean(d)`; `regularity = n>=3 ? 1 - clamp (cv/0.5, 0, 1) : 0.5`. `confidence = 0.6*familyShare + 0.4*regularity`. Gate `confidence >= 0.5` (const `kMotifGate`).
- `Source/features/callresponse/Responder.{h,cpp}` (audio thread, ~120 bytes, fixed arrays of 4):
  ```cpp
  struct ResponderSettings { int mode; float strength01; int calls; int replies; };   // mode 0 off, 1 complement, 2 echo
  class Responder {
  public:
      void onNaturalFillStarted (const FillRecipe&, const TransportInfo&, double ppq, std::int64_t outputPos) noexcept;   // advances the state machine
      bool active() const noexcept;                       // Responding && settings.mode != 0 && ! paused
      void biasWeights (float (&w)[numAlgos]) const noexcept;   // multiplies in place, zero stays zero
      float bandWeight (int band) const noexcept;
      float lengthScale() const noexcept;                 // multiplier applied to the length scale, then clamped by the existing limits
      void pause (bool lockOn, bool likeOn, bool forcedType) noexcept;
      void onTransportJump() noexcept;  void reset() noexcept;
      Status status() const noexcept;                     // for the UI atomics: Collecting(k/n), Responding(k/n), Unclear(conf)
  };
  ```
  State machine: `Collecting` (push entries; slide the window if full) -> when `collected == calls`: `describe`; confidence >= gate -> `Responding(repliesLeft = replies)` and store the motif; else `Unclear` and keep collecting with a sliding window of `calls` entries. In `Responding` each started natural fill (including ones that were biased) decrements `repliesLeft`; at 0 back to `Collecting` with an empty window. Entries produced while responding are never added to the next call (flag `response` on the recipe, new bit `flags & 8`).
  Bias (strength `s`), applied by `biasWeights` via the motif family `F`:
  - Complement (table `kComplement[F][G]`, constexpr, tested for all 4x4): other-family multipliers `1 + s*m` where `m` = 2.0 for the primary complement and 1.0 for the secondary (Rhythmic -> Textural, PitchTape; Textural -> Rhythmic, Destructive; PitchTape -> Destructive, Rhythmic; Destructive -> PitchTape, Textural); motif family `1 - 0.7*s`; pitch contrast: if `pitchDir > 0` multiply `PitchFall` by `1 + s`, if `< 0` multiply `PitchRise`; band contrast in Spectral mode: other bands x `(1 + s)`.
  - Echo: motif family `1 + 2*s`, all others `1 - 0.3*s`; band `bandWeight` `1 + s` for the motif band.
  - `lengthScale()`: Complement: call mean length class <= 1 -> `1 + 0.3*s`, >= 2 -> `1 - 0.3*s`; Echo: pulls toward the motif's class midpoint by factor `0.5*s`. Result is applied to the existing `scale` before clamping, so it can never exceed `usable` or the minimum.
  - Zero weights stay zero; if every biased weight is zero use the unbiased weights; RNG draw count identical (only thresholds change).
- Hook order and precedence (shared rule in F-L01): Fill Type forced > Lock > Like > Call/Response > normal. `pause` is called per gap from `trySchedule` with those three flags; while paused the machine freezes (does not count fills).
- Resets: `reset()`, bypass, sample-rate change, transport jump (`noteTransportJump`) all clear to `Collecting`. A transport stop clears too (fills there are not a musical sequence).
- `Source/features/callresponse/Gui.{h,cpp}`, `Help.cpp`, `Info.cpp`. The status travels as one packed `std::atomic<std::uint32_t>` (state, k, n, confidence%, family) from the audio thread; the editor timer reads it.
Minimal hooks in existing files: `chooseAlgorithm`/`chooseBand` (one `biasWeights`/`bandWeight` line each, next to F-L03's), the length-scale line, `adjustRecipe` sets `flags |= response`, `onNaturalFillStarted` call after a fill starts, `EngineSettings::perf` gains `ResponderSettings`.

## 4 Parameters & state
| ID | Type / range | Default | Unit |
|---|---|---|---|
| `fl10_mode` | choice: Off, Complement, Echo | Off | - |
| `fl10_strength` | float 0-100, step 1 | 60 | % |
| `fl10_calls` | int 2-4 | 3 | fills |
| `fl10_replies` | int 1-4 | 2 | fills |
All four in `performanceParameterIDs()`; appended in `params/callresponse.h`, locked in `tools/param_ids/callresponse.lock`. No state subtree: the window, motif and counters are momentary and never saved (F0-6); a loaded session starts in `Collecting`. Old sessions: IDs absent -> Off -> neutral. Neutral proof: `mode==0` returns from every hook first; `active()` false; golden A-F bit-identical; draw count asserted equal.

## 5 Real-time safety
Audio: per started fill O(4) updates; per gap 15 multiplies; `describe` runs once per cycle on <= 4 entries (no allocation, no sort needed beyond 4 elements). No locks, strings or heap. Resets as above; sample-rate change clears (time units).

## 6 UI
Slot `CreativeExtra` advanced disclosure under Lock/Like: Mode combo, Strength, Calls, Replies, plus a status chip. Chip shows text and segment pips (non-colour). Tooltip: "Call and response: GapFill listens to its last few fills and answers with a complementary (or similar) character. It only nudges choices." Motif summary in the chip tooltip ("Motif: rhythmic x3, regular, short"). Accessible name "Call and response status", live-region updates rate-limited to one per second. History marks response fills with "R" (F-L02). Reduced Motion: no pip animation. Narrow: chip shows only `CALL 2/3`/`RESP 1/2` abbreviations. Help section "Call and response" (order 1190) lists precedence with Lock/Like/Fill Type.

## 7 MIDI/automation/CLAP/host impacts
Four parameters (mode is stepped; check CLAP stepped hints). No MIDI beyond learn on parameters. Works without transport (regularity falls back to seconds, or neutral 0.5 with fewer than 3 intervals); with transport, intervals are in beats so tempo changes do not distort regularity. Deterministic given seed and PPQ; no extra RNG draws, no latency or PDC change.

## 8 Tests
Group `CallResponse`: `cr_off_is_bit_identical` | `describe_family_mode_and_ties` | `describe_regularity_beats_vs_seconds` | `confidence_gate_boundaries` (0.49/0.5) | `unclear_motif_no_response_and_status` | `state_machine_collect_respond_collect` | `window_slides_when_unclear` | `response_fills_not_added_to_next_call` | `complement_table_4x4_complete` | `complement_shifts_family_shares` (statistical, seeded) | `echo_boosts_motif_family` | `pitch_direction_contrast` | `length_scale_bounded_by_usable` | `zero_weight_stays_zero` | `all_zero_falls_back` | `lock_like_forced_type_pause_machine` | `transport_jump_stop_reset_clear` | `previews_and_rejected_gaps_do_not_count` | `draw_count_unchanged` | `block_size_invariant` | `response_flag_set_in_history` | `status_packing_roundtrip` | `cr_no_audio_thread_allocation`.

## 9 Dependencies on other tickets
Hard: B3 (stages, recipe flags), F-L03 (`AlgoFamily`, weight-bias pattern), F-L01 (precedence rule, `lastNatural`). Soft: F-L02 (R marker, Lock a response), F-L08 (optional: phrase-length windows as a v1.1 option; v1 counts fills). Build order: F-L01 -> F-L02 -> F-L03 -> F-L10.

## 10 Effort & risks
Effort L. Risks: perceptual value is subjective (complement table is tuning and may change in its own PR; keep constants in one file); the cycle structure can feel mechanical (the `replies` randomness is not added to preserve determinism; revisit after listening tests); interaction complexity with Lock/Like (pause rule is tested); motif by fills only (not by input rhythm), which is an intentional v1 limit.

## 11 Acceptance checklist
- [ ] Four IDs appended, locked, classified; nothing serialised
- [ ] Off is bit-identical; draw counts equal
- [ ] Statistical complement and echo bands met; unclear motif yields visible "no response"
- [ ] Precedence with Fill Type, Lock and Like tested
- [ ] Status chip, History "R" marker, tooltips, accessibility done
- [ ] Transport jump, stop, reset, bypass clear the machine
- [ ] AllocGuard green; Help written
