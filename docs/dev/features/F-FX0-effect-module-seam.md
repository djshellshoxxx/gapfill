# F-FX0 Effect module seam (prerequisite for the rhythmic and spectral FX tickets)

Status: specified (this pass). Size M. Depends on F0-1/2/3/4/5, Gate B. Required by F-M14, F-M15, F-M16, F-M20, F-L15, F-L19, F-L20, F-L21 and by the panic contract in F-S12. Implements the rule in `10` section 5: effects are wet-fill modules selected per fill, not a global serial chain.

## 1. Summary and user-visible behaviour
No audible change by itself. It defines how an effect attaches to a fill: each effect has an **Amount** (probability that a given fill uses it, 0 = off) plus its own controls; a fill can use at most two effects; the History chip shows which. Effects process only the wet material of that fill (the voice output) and never the dry signal.

## 2. Success / quality metric
With every effect Amount at 0: bit-identical goldens, zero extra RNG draws, no measurable CPU (< 0.1 % of baseline). A conformance suite runs the same battery against every registered module (reset, panic, NaN, denormal, sample-rate change, tail bound, determinism, AllocGuard).

## 3. Design
`Source/features/fxseam/{FillFx.h,Module.h,Registry.h,FxChoice.cpp,Conformance.h}`.
### Recipe extension (decision D3 in `00-SPEC-GAP-REPORT.md`)
`FillRecipe` stays 16 bytes (F0-3). A companion POD is added next to it: `struct FillFx { std::uint8_t module[2]; std::uint8_t amount[2]; std::uint8_t a, b, c; std::uint8_t flags2; }` (8 bytes; `module` ids are permanent, 0 = none) and `struct FillRecipeEx { FillRecipe base; FillFx fx; }` (24 bytes). `<History>`/`<Lock>` entries are written as version 2 (24 bytes base64); version-1 entries (16 bytes) load with `fx = {}`. `flags2` carries the feature bits that do not fit in `FillRecipe::flags` (bits assigned: 0 `harmonicFellBack`, 1 `harmonicUsed`, 2 `freezeFellBack`; F-X01/F-X02 may use either location as long as bits are unique and listed in `FillFx.h`). `describeRecipe` appends " + Ratchet" etc.
### Two module kinds
```cpp
struct FxSetup { double sampleRate; int lengthSamples; std::uint64_t seed; float amount01; float a,b,c; int division; double samplesPerBeat; bool transportValid; };
class IInsertModule {            // one instance per voice; per-voice state; small memory
public:  virtual void prepare (double sr, int maxBlock, int channels) = 0;
         virtual void reset() noexcept = 0;
         virtual void panic (int fadeSamples) noexcept = 0;               // F-S12 contract
         virtual void begin (const FxSetup&) noexcept = 0;
         virtual void process (float* l, float* r, int n, int posInFill) noexcept = 0;   // in place, wet only
         virtual int  latencySamples() const noexcept { return 0; } };
class ISendModule {              // one instance per engine; long tails; bounded
public:  prepare/reset/panic as above;
         virtual void setSend (float gain) noexcept = 0;                  // set by voices at fill start
         virtual void process (const float* inL, const float* inR, float* outL, float* outR, int n) noexcept = 0;  // adds to out
         virtual int  tailSamples() const noexcept = 0; };                // must be <= kMaxFxTailSeconds * sr
inline constexpr float kMaxFxTailSeconds = 2.5f;                           // equals getTailLengthSeconds()
```
* **Insert path:** `FillVoice::render` produces the voice's block into a preallocated per-voice scratch, then runs its active insert modules (<= 2) in the fixed module-id order, then adds to the wet bus. Hook: 3 lines in `FillVoice::render` plus a member array of module pointers.
* **Send path:** a voice also adds `scratch * sendGain[module]` into `FillEngine`'s preallocated `sendBus[module]`; after all voices rendered, `FillEngine::process` runs each send module over its bus and adds the output to the wet bus (before the protection duck and `PanicGain`). Hook: ~8 lines in `FillEngine::process`.
* **Selection (`adjustRecipe`, the F0-2 hook):** for each module in id order with `amount01 > 0`: `Rng sub; sub.seed (hashCombine (key, kSalt_Fx<id>))`; apply if `sub.chance (amount01)`; stop at 2 modules. A module is skipped (not an error) when the fill is shorter than its `minFillMs`; exclusion table (e.g. Ratchet is skipped for Stutter, whose slices already repeat) is `constexpr` and tested.
* **Registry:** `FxRegistry::add (ModuleDescriptor{ id, name, kind, factory, minFillMs, panicTested })` at static init (append-only, order by explicit id as for Help); the conformance suite iterates it; the panic contract test fails if any registered module lacks `panic`.
* **Tails:** send modules must decay to < -80 dB within `kMaxFxTailSeconds` after the last send, enforced by (a) feedback ceilings per module, (b) a forced fade when `silenceSamples > kMaxFxTailSeconds * sr`; `FillEngine` reports the active tail in `MeterState` for the UI and for F-S12's flush.
* **Quality tiers:** `QualityCaps` (F-X07) gains `allowSend` and `maxInserts`; Light disables send modules F-L21 and F-L20.
* **Parameters rule for all modules:** `<id>_amount` (probability %, default 0) plus module controls; all defaults neutral; module controls that only matter when applied are not randomised unless the amount > 0.

## 4. Parameters and state
None for the seam. Subtrees `<History>` and `<Lock>` gain version 2 entries (migration `migrate (ValueTree&, 2 -> 3)` in F0-6: no data change, version attribute only). Old sessions load: v1 recipes -> `fx = {}`. Neutral proof: `FillFx` zero -> no module touched; `memcmp` of `EngineSettings` and goldens A-F.

## 5. Real-time safety
Modules preallocate in `prepare` for 192 kHz; no virtual dispatch cost issue (<= 4 calls per voice per block); every module output passes `sanitise` at the voice boundary; denormals handled by `ScopedNoDenormals` plus per-module DC/flush constants. Reset table per module (reset, releaseResources, sample-rate change, bypass, transport jump) is part of `Conformance.h` so each PR answers it mechanically.

## 6. UI
Shared component `FxCard` (Source/features/fxseam/FxCard.h): foldable card with an Amount knob in the header, controls hidden until Amount > 0 ("show effect-specific controls only after it is enabled", `11`), header chip "off" / "20 %". Placement: Creative tab effects group (rhythmic: Ratchet, Bitcrush, Tape; resonant/delay: Resonator, Freq-shift delay, Dub; spectral: Formant, Freeze (F-X02), Convolution). Accessible name pattern "<Effect>, amount N percent, <state>".

## 7. MIDI / automation / CLAP / host
All amounts and controls are normal automatable parameters. Send tails extend beyond the fill: `getTailLengthSeconds()` stays 2.5 s (already declared) so hosts keep processing.

## 8. Tests (`Tests/features/TestFxSeam.cpp`, `TestFxConformance.cpp`)
`fillfx_zero_is_noop_golden` | `recipe_v1_loads_into_v2` | `recipe_v2_roundtrip_base64` | `describe_recipe_lists_effects` | `at_most_two_modules` | `selection_deterministic_by_seed_and_ppq` | `selection_uses_salted_sub_rng_only` | `exclusion_table_complete` | `registry_ids_unique_and_append_only` | conformance per module: `fx_<id>_reset_clears_state` | `fx_<id>_panic_fades_within_20ms` | `fx_<id>_nan_in_finite_out` | `fx_<id>_denormal_in_no_cpu_spike` | `fx_<id>_sample_rate_change_safe` | `fx_<id>_tail_under_limit` | `fx_<id>_deterministic` | `fx_<id>_no_audio_thread_allocation` | `fx_<id>_neutral_when_amount_zero`.

## 9. Dependencies and build order
After F0-2/F0-3 and F-S12's interface PR. First ticket in the FX train; then F-M16, F-M14, F-M20, then F-M15, F-L19, F-L15, then F-L20, F-X02, F-L21 (per `10` section 6).

## 10. Effort and risks
~4 ideal days. Risks: recipe size change touches U-1/C-5 serialisation (versioned, tested); too many cards in the Creative tab (grouped, folded); per-voice insert memory (small by design; long-memory effects must be send modules).

## 11. Acceptance checklist
- [ ] Conformance suite runs with one dummy module in CI
- [ ] v1 recipes load; v2 round trip; hot-file budget respected
- [ ] Owner decision D3 recorded
