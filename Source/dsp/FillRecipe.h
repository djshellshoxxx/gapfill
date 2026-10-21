#pragma once

// FillRecipe (docs/dev/02-FOUNDATIONS.md F0-3): the *decisions* behind one fill,
// as plain-old-data. No pointers, no PCM, fixed 16 bytes, so it can be copied
// through lock-free queues, kept in a history ring and serialised into the
// plugin state. It records choices, not the randomness used inside the voice.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace gapfill
{

struct FillRecipe
{
    std::uint8_t  algo = 0;            ///< Algo index
    std::int8_t   band = -1;           ///< -1 broadband, 0..2
    std::uint8_t  lengthClass = 0;     ///< 0..3: <25%, <50%, <75%, <=100% of maxFill
    std::uint8_t  division = 0xFF;     ///< Division index, 0xFF = free/none
    std::int8_t   pitchSemis = 0;      ///< signed, for pitch/tape algorithms, else 0
    std::uint8_t  modifierMask = 0;    ///< bit per Modifier (bit0 FilterSweep .. bit4 PanMotion)
    std::uint8_t  stereoMode = 0;      ///< 0 mono-ish, 1 wide, 2 ping-pong, 3 motion
    std::uint8_t  mistakeType = 0;     ///< 0 none (see C-3)
    std::uint16_t lengthMs = 0;        ///< planned length after clipping to the gap
    std::uint8_t  mistakeAmt = 0;      ///< 0..100
    std::uint8_t  flags = 0;           ///< see FillRecipe::flag*
    std::uint32_t seed = 0;            ///< VoiceSetup::rngSeed (low 32 bits)

    static constexpr std::uint8_t flagPreview  = 1 << 0;
    static constexpr std::uint8_t flagFromLock = 1 << 1;
    static constexpr std::uint8_t flagBiased   = 1 << 2;
    static constexpr std::uint8_t flagSwell    = 1 << 3;   ///< N-FX1: Reverse voice renders as a Reverse Swell
    static constexpr std::uint8_t flagCloud    = 1 << 4;   ///< N-FX2: Granular voice renders as a Granular Cloud
    static constexpr std::uint8_t flagForced   = 1 << 5;   ///< F-P02: started by Fill Now (forced scheduling)
    static constexpr std::uint8_t flagSnapped  = 1 << 6;   ///< N-MUS: Scale Lock changed this fill's pitch
    static constexpr std::uint8_t flagResponse = 1 << 7;   ///< F-L10: fill was shaped as a response (History "Response")
    static constexpr std::uint8_t knownFlags   = flagPreview | flagFromLock | flagBiased | flagSwell | flagCloud | flagForced | flagSnapped | flagResponse;

    static constexpr std::uint8_t noDivision      = 0xFF;
    static constexpr std::uint8_t modifierBits    = 0x1F;   ///< five Modifier values
    static constexpr int          serialisedBytes = 16;
};

static_assert (sizeof (FillRecipe) == 16, "FillRecipe is serialised as 16 bytes");
static_assert (std::is_trivially_copyable_v<FillRecipe>, "FillRecipe must be POD-like");

/** Range-checks every field. Used when loading recipes from untrusted state. */
inline bool isValid (const FillRecipe& r) noexcept
{
    constexpr int numAlgosLimit = 15;      // Algo::NumAlgos (kept literal: no dependency on Parameters.h)
    constexpr int numDivisionsLimit = 12;  // Division::NumDivisions

    return r.algo < numAlgosLimit
        && r.band >= -1 && r.band <= 2
        && r.lengthClass <= 3
        && (r.division == FillRecipe::noDivision || r.division < numDivisionsLimit)
        && r.pitchSemis >= -48 && r.pitchSemis <= 48
        && (r.modifierMask & ~FillRecipe::modifierBits) == 0
        && r.stereoMode <= 3
        && r.mistakeType <= 15
        && r.mistakeAmt <= 100
        && (r.flags & ~FillRecipe::knownFlags) == 0;
}

/** Byte layout used by the state subtrees: field order above, little-endian
    for the two multi-byte fields, independent of struct padding or host endianness. */
inline void toBytes (const FillRecipe& r, std::uint8_t (&out)[FillRecipe::serialisedBytes]) noexcept
{
    out[0] = r.algo;
    out[1] = (std::uint8_t) r.band;
    out[2] = r.lengthClass;
    out[3] = r.division;
    out[4] = (std::uint8_t) r.pitchSemis;
    out[5] = r.modifierMask;
    out[6] = r.stereoMode;
    out[7] = r.mistakeType;
    out[8] = (std::uint8_t) (r.lengthMs & 0xFF);
    out[9] = (std::uint8_t) (r.lengthMs >> 8);
    out[10] = r.mistakeAmt;
    out[11] = r.flags;
    out[12] = (std::uint8_t) (r.seed & 0xFF);
    out[13] = (std::uint8_t) ((r.seed >> 8) & 0xFF);
    out[14] = (std::uint8_t) ((r.seed >> 16) & 0xFF);
    out[15] = (std::uint8_t) (r.seed >> 24);
}

inline FillRecipe fromBytes (const std::uint8_t (&in)[FillRecipe::serialisedBytes]) noexcept
{
    FillRecipe r;
    r.algo = in[0];
    r.band = (std::int8_t) in[1];
    r.lengthClass = in[2];
    r.division = in[3];
    r.pitchSemis = (std::int8_t) in[4];
    r.modifierMask = in[5];
    r.stereoMode = in[6];
    r.mistakeType = in[7];
    r.lengthMs = (std::uint16_t) (in[8] | (in[9] << 8));
    r.mistakeAmt = in[10];
    r.flags = in[11];
    r.seed = (std::uint32_t) in[12] | ((std::uint32_t) in[13] << 8)
           | ((std::uint32_t) in[14] << 16) | ((std::uint32_t) in[15] << 24);
    return r;
}

/** Which length class (0..3) a fill of `lengthMs` falls into for a given Max Fill. */
inline std::uint8_t lengthClassFor (float lengthMs, float maxFillMs) noexcept
{
    if (maxFillMs <= 0.0f)
        return 0;

    const float f = lengthMs / maxFillMs;
    return f < 0.25f ? 0 : (f < 0.5f ? 1 : (f < 0.75f ? 2 : 3));
}

} // namespace gapfill
