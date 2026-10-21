#pragma once

// C-8 Evolve (docs/dev/04-SPEC-CREATIVE.md C-8, doc 12 section 9 / F-L16).
//
// Set-and-forget drift of the creative character: every `period` bars the
// decision stage moves fill, intensity, complexity, chaos, density and the
// algorithm weights part of the way (60 %) towards the centre of the next
// Surprise profile ("waypoint"). The waypoint order is a seeded shuffle with no
// immediate repeat, computed per cycle with no state (random access), so seeks,
// loops and block sizes always reproduce the same sequence.
//
// Evolve never touches seed, detector, protection, mix, latency, Fill Type, effect
// cards, scale, scenes or a zero weight. Neutral (nothing written) when off or without a
// valid transport and time signature.

#include "../effective/Effective.h"

namespace gapfill::evolve
{

inline constexpr int kPeriodChoices[4] { 0, 8, 16, 32 };   // evolve choice index -> bars (0 = Off)
inline constexpr float kPull = 0.6f;                       // caps the pull so macros / personality stay audible
inline constexpr double kBlendFraction = 0.25;             // the first 25 % of a period crossfades the waypoints

/** The waypoint (Surprise profile index in [0, numProfiles)) of cycle `cycle` for a seed.
    Pure and random access. For consecutive cycles the result never repeats. */
int profileForCycle (long long cycle, int seed, int numProfiles) noexcept;

struct Waypoint
{
    long long cycle = 0;
    int profile = 0;
    int previousProfile = 0;
    float blend = 1.0f;          ///< 0..1: how far the waypoint crossfade has progressed (1 after the first 25 %)
};

/** Where Evolve is at `ppq`; false when it cannot be evaluated (off, invalid transport / time signature). */
bool waypointAt (const EngineSettings&, double ppq, const Effective::TransportInfo&, Waypoint&) noexcept;

/** The decision-stage function (called by Effective::applyEvolve). */
void apply (EngineSettings&, double ppq, const Effective::TransportInfo&) noexcept;

} // namespace gapfill::evolve
