#pragma once

// C-2 Performance Macros and F-M23 modulation targets: audio-thread half
// (docs/dev/04 C-2, docs/dev/12 F-M23).
//
// Four macros (Activity, Chaos, Space, Destruction), each with up to 8 target
// mappings. A mapping's effective value is  lerp (min01, max01, curve (value01))
// in the target parameter's NORMALISED range; the base parameter is never
// written, the macro "owns the effective value" while the mapping exists
// (and `macrosOn` is set). F-M23 adds per-mapping smoothing and bipolar mode,
// 15 more targets (the effect Amount parameters, table indices 21..35) and an
// optional LFO per macro that replaces the knob value.
//
// Pure functions + one plain runtime struct: no JUCE, no allocation, no threads.

#include "../../dsp/EngineSettings.h"
#include "../common/EngineTargets.h"

#include <cstdint>
#include <type_traits>

namespace gapfill::macros
{

inline constexpr int kNumMacros = 4;
inline constexpr int kMaxMappings = 8;
inline constexpr int kNumC2Targets = 21;       ///< fill .. wTapeStop (persisted order)
inline constexpr int kNumTargets = 36;         ///< + 15 F-M23 amount targets (indices 21..35)
inline constexpr int kNameBytes = 16;

struct TargetInfo
{
    const char* id;       ///< permanent parameter id
    float lo, hi;         ///< the parameter's real range (all macro targets are linear)
    const char* label;
};

/** The persisted, append-only target table. */
const TargetInfo& targetInfo (int index) noexcept;

/** Start-up: resolves each target to its EngineSettings field (message thread). */
void initTargets();

struct Mapping
{
    std::uint8_t target = 0;
    std::uint8_t curve = 0;          ///< 0 linear, 1 ease-in (x^2), 2 ease-out (1-(1-x)^2), 3 S-curve (smoothstep)
    std::uint8_t bipolar = 0;        ///< F-M23: -1..1 around the macro centre
    std::uint8_t pad = 0;
    float min01 = 0.0f, max01 = 1.0f;
    float smoothMs = 0.0f;           ///< F-M23: 0 = none
};

struct LfoSettings
{
    std::uint8_t enabled = 0;
    std::uint8_t shape = 0;          ///< 0 sine, 1 triangle, 2 saw, 3 square, 4 seeded sample-and-hold
    std::uint8_t tempoSync = 1;      ///< 1: PPQ-locked when the transport is valid, else rateHz
    std::uint8_t division = 2;       ///< Division index (1/4 default)
    float rateHz = 1.0f;             ///< 0.01 .. 20
    float depth01 = 1.0f;
    float phaseDeg = 0.0f;           ///< 0 .. 360
};

struct MacroConfig
{
    char names[kNumMacros][kNameBytes + 1] {};
    std::uint8_t count[kNumMacros] {};
    Mapping map[kNumMacros][kMaxMappings] {};
    LfoSettings lfo[kNumMacros] {};
};

static_assert (std::is_trivially_copyable_v<MacroConfig>);

/** The factory mappings (spec table): what "Reset macro" and a new instance get. */
MacroConfig defaultConfig();

float applyCurve (int curve, float x) noexcept;

/** Effective NORMALISED target value for a macro value `v01` (0..1), before smoothing. */
float mappingValue (const Mapping&, float v01) noexcept;

/** LFO in 0..1 (centre 0.5). `cycles` is the unwrapped phase in cycles; for
    sample-and-hold the integer part selects the held value via hash(seed, cycle). */
float lfoValue (const LfoSettings&, double cycles, int seed) noexcept;

/** Phase of an LFO at a PPQ position (tempo-synced) or at a free-running time. */
double lfoCyclesAtPpq (const LfoSettings&, double ppq) noexcept;
double lfoCyclesAtTime (const LfoSettings&, double seconds) noexcept;

//==============================================================================
/** Audio-thread state. One instance, owned by LiveControl. */
struct MacroRuntime
{
    MacroConfig cfg;
    float knob01[kNumMacros] {};
    float chunkSeconds = 512.0f / 48000.0f;
    double freeTime = 0.0;                         ///< advances by chunkSeconds per evaluated block
    int seed = 1;

    float smoothed[kNumTargets] {};
    bool  hasSmoothed[kNumTargets] {};
    float effective01[kNumTargets] {};             ///< last effective normalised value per target
    bool  active[kNumTargets] {};                  ///< claimed by a mapping this block
    float width01 = 1.0f;
    bool  widthOn = false;

    /** Block stage: knob values (or LFO at the free-run clock) -> EngineSettings. */
    void evaluateBlock (EngineSettings&, const float knob01In[kNumMacros], std::uint32_t& ownedMask) noexcept;

    /** Decision stage: re-evaluates LFO-driven macros at the decision PPQ. */
    void evaluateDecision (EngineSettings&, double ppq) const noexcept;

    bool anyLfo() const noexcept;

private:
    void applyTarget (EngineSettings&, int target, float eff01) const noexcept;
};

} // namespace gapfill::macros
