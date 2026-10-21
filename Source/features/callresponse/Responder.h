#pragma once

// F-L10 Call and response: the audio-thread state machine and the weight bias.
//
// Collecting -> (motif clear) Responding(repliesLeft) -> Collecting, or Unclear
// (keep collecting with a sliding window). The bias only changes the weights and
// the length scale seen by the existing decisions; it never draws from the RNG,
// never creates or removes gaps, and Fill Type forced, Fill Lock and More Like
// That take precedence (they pause the machine).

#include "Motif.h"
#include "../../dsp/FillRecipe.h"

#include <atomic>

namespace gapfill::callresponse
{

struct ResponderSettings
{
    int   mode = 0;              ///< 0 Off, 1 Complement, 2 Echo
    float strength01 = 0.6f;
    int   calls = 3;             ///< 2..4
    int   replies = 2;           ///< 1..4
};

enum class ResponderState : std::uint8_t { Collecting = 0, Responding = 1, Unclear = 2 };

struct ResponderStatus
{
    ResponderState state = ResponderState::Collecting;
    int k = 0, n = 0;            ///< Collecting: k of n; Responding: k of n replies done
    int confidencePercent = 0;
    AlgoFamily family = AlgoFamily::Rhythmic;
    bool off = true;
};

std::uint32_t packStatus (const ResponderStatus&) noexcept;
ResponderStatus unpackStatus (std::uint32_t) noexcept;

/** Multipliers of the Complement bias: row = motif family, column = candidate family. */
float complementMultiplier (AlgoFamily motif, AlgoFamily candidate) noexcept;

class Responder
{
public:
    /** Once per gap, from trySchedule: the settings, and whether a higher-precedence feature
        (forced Fill Type, Fill Lock, More Like That) pauses the machine. */
    void configure (const ResponderSettings&, bool paused) noexcept;

    /** A natural fill started (previews and rejected gaps never reach this). */
    void onNaturalFillStarted (const FillRecipe&, bool transportValid, double ppq, std::int64_t outputPos, double sampleRate) noexcept;

    bool active() const noexcept { return cfg.mode != 0 && ! paused && state == ResponderState::Responding; }

    float algoFactor (int algo) const noexcept;       ///< 1 when inactive
    float bandFactor (int band) const noexcept;
    float lengthScale() const noexcept;

    void reset() noexcept;
    ResponderStatus status() const noexcept;
    std::uint32_t statusWord() const noexcept { return word.load (std::memory_order_relaxed); }
    const Motif& motif() const noexcept { return currentMotif; }

private:
    void publish() noexcept;

    ResponderSettings cfg;
    bool paused = false;
    ResponderState state = ResponderState::Collecting;
    CallEntry window[kMaxCalls];
    int collected = 0;
    int repliesLeft = 0;
    int repliesDone = 0;
    Motif currentMotif;
    std::atomic<std::uint32_t> word { 0 };
};

} // namespace gapfill::callresponse
