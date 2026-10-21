#pragma once

// Vocabulary of the fill scheduler stages (docs/dev/02-FOUNDATIONS.md F0-2).
// Plain data only: no JUCE, no engine state, so stage code and its tests can
// share these types freely.

#include "FillRecipe.h"

#include <cstdint>

namespace gapfill
{

struct AnalysisFrame;

/** Why a gap did not produce a fill. `trySchedule` runs once per gap, so a gap
    yields at most one reason. Values are persisted in logs and visualiser
    frames: append only, never renumber. */
enum class RejectReason : std::uint8_t
{
    None = 0,
    FillOff,            ///< Fill macro at 0
    NoSource,           ///< no recent musical content to build a fill from
    TooSoon,            ///< previous fill started less than half a Minimum Gap ago
    NoVoice,            ///< all voices busy (or none free when one was needed)
    TooShort,           ///< gap shorter than Minimum Gap
    GuardEaten,         ///< room existed, but the pre-event guard ate it
    NoBand,             ///< spectral mode, no lane qualified
    DensityRoll,        ///< lost the probability roll
    NoAlgoFits,         ///< no algorithm fits the available length
    SourceUnavailable,  ///< the voice could not start (even after the fallback)
    LockSkipped,        ///< Fill Lock active and the locked recipe cannot fit (C-5)
    // Appended by docs/dev/12 section 2.2 (permanent values):
    BudgetFull,         ///< F-D01 per-bar fill budget reached
    QuietZone,          ///< F-D01 inside a quiet zone
    ScGated,            ///< F-L13 sidechain gate closed
    Panicked,           ///< N-USE-a panic hold
    CoopYield,          ///< F-X06 yielded to another instance
    ForcedUnsafe,       ///< F-P02 forced fill would hit a protected region
    Count
};

inline constexpr int numRejectReasons = (int) RejectReason::Count;

const char* rejectReasonName (RejectReason r) noexcept;

/** Why a gap with less than Minimum Gap of usable room was rejected: GuardEaten
    when the gap itself (up to the next event) was long enough and only the
    pre-event guard took the room, TooShort otherwise. `boundary` is the next
    event/protected position (<= outputPos when none is known). */
inline RejectReason noRoomReason (std::int64_t boundary, std::int64_t outputPos,
                                  std::int64_t minGapSamples) noexcept
{
    return (boundary > outputPos && boundary - outputPos >= minGapSamples) ? RejectReason::GuardEaten
                                                                           : RejectReason::TooShort;
}

/** Everything the stage code may read about the gap being evaluated. Filled
    by checkEligibility() and the decision-key step; stages treat it as const
    apart from the stage that owns a field. */
struct GapContext
{
    std::int64_t outputPos = 0;        ///< decision sample (output timeline)
    std::int64_t blockStartPos = 0;
    std::int64_t available = 0;        ///< samples of room before the next protected event
    std::int64_t usable = 0;           ///< min (available, maxFillSamples)
    std::int64_t maxFillSamples = 0;
    std::int64_t minGapSamples = 0;
    std::int64_t preGuard = 0;
    const AnalysisFrame* frame = nullptr;
    std::uint64_t key = 0;             ///< deterministic decision key (seed + musical position)
    float conf = 0.0f;                 ///< clamped gap confidence at the decision sample
};

/** What the scheduler decided to play, with the exact figures the recipe only
    approximates (the recipe stores whole milliseconds, the voice needs samples). */
struct FillPlan
{
    FillRecipe recipe;
    std::int64_t lengthSamples = 0;
    float lengthMs = 0.0f;
    float neededMs = 0.0f;         ///< set with NoAlgoFits: the shortest fill that would have fit
    int algoIndex = 0;
    int band = -1;
};

struct ScheduleResult
{
    bool started = false;
    RejectReason reason = RejectReason::None;
    FillRecipe recipe;             ///< valid when started (and for LockSkipped diagnostics)
    std::int64_t lengthSamples = 0;///< when started: the exact planned length (the recipe keeps whole ms)
    float availableMs = 0.0f;      ///< room that was available when rejected
    float neededMs = 0.0f;         ///< room that would have been needed
};

} // namespace gapfill
