#pragma once

// F-L10 Call and response (docs/dev/features/F-L10): the motif of GapFill's own last fills.
// Pure, allocation-free, <= 4 entries.

#include "../lock/LikeBias.h"

#include <cstdint>

namespace gapfill::callresponse
{

inline constexpr int kMaxCalls = 4;
inline constexpr float kMotifGate = 0.5f;

struct CallEntry
{
    std::uint8_t algo = 0, lengthClass = 0;
    std::int8_t  band = -1;
    bool   hasBeat = false;          ///< transport was valid when the fill started
    double startBeat = 0.0;
    double startSeconds = 0.0;
};

struct Motif
{
    AlgoFamily family = AlgoFamily::Rhythmic;
    float familyShare = 0.0f;
    std::uint8_t meanLengthClass = 0;
    std::int8_t band = -1;
    std::int8_t pitchDir = 0;        ///< +1 more Pitch Rise, -1 more Pitch Fall
    float regularity = 0.5f;
    float confidence = 0.0f;
};

/** Describes `n` (1..4) entries. Intervals are measured in beats when every entry has transport, else in seconds. */
Motif describe (const CallEntry* calls, int n) noexcept;

} // namespace gapfill::callresponse
