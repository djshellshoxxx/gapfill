# F-L18 Scale and key awareness (manual)

Status: specified (this pass). Size M. Depends on F0-1/5. Foundation for F-X03 (auto key), F-X01 (harmonic snap), F-M15 (resonator). Automatic recognition is F-X03 and is separate.

## 1. Summary and user-visible behaviour
The user picks a **Root** (C..B) and a **Scale** (Off, Major, Natural minor, Harmonic minor, Dorian, Mixolydian, Major pentatonic, Minor pentatonic, Blues). Key-aware parts of GapFill then choose notes from that scale: the existing **Comb** fill (today tuned to a fixed A-minor-pentatonic set), the new resonator (F-M15) and harmonic grains (F-X01). With Scale = Off everything behaves exactly as today.

## 2. Success / quality metric
Every pitch produced by a key-aware consumer is within +-2 cents of an in-scale equal-tempered pitch (snap = 100 %); Off is bit-identical to the pre-feature build; scale tables pass interval-pattern tests; the Comb fill with Off keeps its legacy distribution.

## 3. Design
`Source/features/tonal/Scales.h` (header-only, pure):
```cpp
using ScaleMask = std::uint16_t;                       // bit i set: semitone i above the root is in scale
enum class Scale : std::uint8_t { Off, Major, NatMinor, HarmMinor, Dorian, Mixolydian, MajPent, MinPent, Blues };
constexpr ScaleMask maskOf (Scale) noexcept;           // Major 0b101010110101 etc., tested against interval lists
struct KeySettings { int rootPc = 0; Scale scale = Scale::Off; float snap01 = 1.0f; };
float nearestInScale (float midi, int rootPc, ScaleMask) noexcept;   // ties go down
float snapMidi (float midi, int rootPc, ScaleMask, float snap01) noexcept;  // lerp(midi, nearest, snap01)
int   scaleTone (int index, int rootPc, ScaleMask, int octaveBase) noexcept; // nth tone upward, for deterministic selection
```
`KeyResolver::resolve` (shared with F-X03): returns `{rootPc, mask, source}`; with F-L18 only: `Manual` or `None`.
**First consumer: Comb algorithm** (`FillVoice::configureAlgorithm`, case `Algo::Comb`): the two legacy RNG draws (`nextInt(8)`, `nextInt(3)`) still happen in the same order. If a key is active, the pair maps to a scale tone: `tone = scaleTone (draw8, root, mask, octaveBase = A2 region)`, octave from `draw3`; else the legacy semitone table is used unchanged. `VoiceSetup` gets `KeySetup key;` (`active`, `rootPc`, `mask`), neutral default.
Gather: `EngineSettings::perf.key` from params in `Source/features/tonal/Gather.cpp`.

## 4. Parameters and state
Group `fl18`, file `Source/params/fl18.h`, lock `tools/param_ids/fl18.lock`, appended to `GAPFILL_PARAMS_EXT`.
| ID | Type / range | Default | Unit |
|---|---|---|---|
| `fl18_root` | choice: C, C#, D, D#, E, F, F#, G, G#, A, A#, B | C | - |
| `fl18_scale` | choice: Off, Major, Natural minor, Harmonic minor, Dorian, Mixolydian, Major pentatonic, Minor pentatonic, Blues | Off | - |
| `fl18_snap` | float 0-100 | 100 | % |
No subtree; `stateVersion` unchanged; old sessions: Off. Neutral proof: scale Off -> `KeySetup.active = false` -> legacy code path, same RNG draws, goldens A-F identical (scenario `D` forces other algorithms; add scenario `L18` forcing Comb with Off). `fl18_scale` stays out of Random/Surprise (key is a musical fact, not a sound-design knob): `performanceParameterIDs()`.

## 5. Real-time safety
Pure integer/float math, no allocation; tables `constexpr`. Reset events: none (stateless).

## 6. UI
Key card in the Creative tab Advanced area (`CreativeExtra`), shared with F-X03: Root combo, Scale combo, Snap knob, 12-note strip (text + shape per state: filled = in scale, ring = root). Tooltips: "Notes that key-aware effects may use. Off = no restriction." Accessible names; keyboard; Help "Key and scale" order 1710-ish (assign 1715). Reduced Motion: static strip.

## 7. MIDI / automation / CLAP / host
Root and scale automatable (stepped) and MIDI-learnable. Stretch: set Root from the lowest held MIDI note (opt-in), not in MVP. No latency effect.

## 8. Tests (`Tests/features/TestScales.cpp`)
`masks_match_interval_lists` | `nearest_in_scale_ties_down` | `snap_zero_is_identity_snap_one_is_nearest` | `scale_tone_octave_wrap` | `all_roots_all_scales_no_empty_mask` | `comb_off_matches_legacy_golden` | `comb_on_notes_in_scale_within_2_cents` | `comb_rng_draw_count_unchanged` | `state_roundtrip_old_state_off`.

## 9. Dependencies and build order
Before F-X03, F-X01 snap, F-M15. Needs `GAPFILL_PARAMS_EXT` (00-SPEC-GAP-REPORT conventions).

## 10. Effort and risks
~2.5 ideal days. Risk: users expect pitch shifting algorithms (PitchRise/Fall) to follow the key: they cannot without source pitch (F-X01 tracker); documented.

## 11. Acceptance checklist
- [ ] Off bit-identical; Comb draw order unchanged
- [ ] Scale tables verified by tests
- [ ] UI checklist; Help text lists which effects honour the key
