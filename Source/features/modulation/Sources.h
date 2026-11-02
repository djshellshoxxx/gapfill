#pragma once

#include <cstdint>
#include <type_traits>

namespace gapfill::modulation
{

inline constexpr int kMacroCount = 8;
inline constexpr int kLfoCount = 2;

enum class MacroSource : std::uint8_t { Knob, LfoA, LfoB, Energy };
enum class LfoShape : std::uint8_t { Sine, Triangle, Saw, Square, RandomStep, RandomGlide };

struct LfoSettings
{
    LfoShape shape = LfoShape::Sine;
    std::uint8_t division = 8; ///< Frozen sixteen-entry list; 8 is one host bar.
    float freeHz = 0.5f;       ///< 0.05..10, used without valid host PPQ.
};

/** One coherent parameter snapshot, published by LiveControl on the audio thread.
    The first four knobs are the existing macro parameters; the final four are
    new host parameters. Knob source ignores depth. */
struct ModInputs
{
    float knob01[kMacroCount] { 0.5f, 0.25f, 0.25f, 0.15f, 0, 0, 0, 0 };
    MacroSource source[kMacroCount] {};
    float depth[kMacroCount] { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };
    LfoSettings lfo[kLfoCount];
};

/** Decision position is the exact output sample; Energy is the corresponding
    deterministic analysis-hop frame, never a host-block average. */
struct DecisionInputs
{
    double ppq = 0.0;
    bool transportValid = false;
    std::int64_t outputPos = 0;
    double sampleRate = 48000.0;
    float energy01 = 0.0f;
    std::uint64_t seed = 1;
    int tsNum = 4, tsDen = 4;
};

/** PPQ-anchored or random-access sample-clock LFO. No stream RNG or state. */
float lfoValue (const LfoSettings&, const DecisionInputs&, int lfoId) noexcept;
double divisionBeats (int division, int tsNum, int tsDen) noexcept;
float macroValue (MacroSource, float knob01, float depth, float lfoA, float lfoB, float energy01) noexcept;

static_assert (std::is_trivially_copyable_v<ModInputs>);
static_assert (std::is_trivially_copyable_v<DecisionInputs>);

} // namespace gapfill::modulation
