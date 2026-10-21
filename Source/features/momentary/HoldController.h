#pragma once

// F-M10 hold/latch state machine and F-S07 Stutter Storm pattern
// (docs/dev/features/F-M10-momentary-effects-and-latch.md, docs/dev/12 F-S07).
//
// Pure logic, no JUCE, fixed arrays, audio-thread safe. It decides WHICH
// performance effect is engaged; rendering the engaged effect needs the
// dedicated performance voice of E-1 (WP4) and is not part of this module.

#include <cstdint>

namespace gapfill::momentary
{

enum class Effect : std::uint8_t { Stutter = 0, Freeze, Reverse, TapeStop, Storm, Count };
enum class Source : std::uint8_t { Param = 0, Midi, Gui, Count };

inline constexpr int kNumEffects = (int) Effect::Count;

class HoldController
{
public:
    /** Latch: every press toggles. Momentary: engaged while any source is down. Changing mode releases everything. */
    void setLatch (bool latch) noexcept;
    bool isLatch() const noexcept                    { return latchMode; }

    /** Level input from one source; edges are detected here. */
    void setLevel (Effect, Source, bool down) noexcept;

    /** GUI click in Latch mode (a no-op in Momentary mode). */
    void toggle (Effect) noexcept;

    /** Once per chunk: timeout accounting for Momentary holds. */
    void tick (std::int64_t samples, std::int64_t timeoutSamples) noexcept;

    /** Panic / bypass / reset: clears every level, latch, stack entry and time-out. */
    void panic() noexcept;

    std::uint8_t engagedMask() const noexcept;       ///< bit per Effect
    std::uint8_t timedOutMask() const noexcept       { return timedOut; }
    int topEffect() const noexcept;                  ///< newest engaged effect or -1

    /** Delays an engage to `samplePos` (quantised engage, F-M11/F-S07); a release before then cancels it. */
    void engageAt (Effect, std::int64_t samplePos) noexcept;
    void advanceTo (std::int64_t samplePos) noexcept;

private:
    bool levelOr (int e) const noexcept;
    void pushStack (int e) noexcept;
    void removeStack (int e) noexcept;

    bool latchMode = false;
    bool level[kNumEffects][(int) Source::Count] {};
    bool prevOr[kNumEffects] {};
    bool latched[kNumEffects] {};
    std::uint8_t timedOut = 0;
    std::int64_t heldSamples[kNumEffects] {};
    std::int64_t engageAtPos[kNumEffects] {};
    bool engagePending[kNumEffects] {};
    std::uint8_t stack[kNumEffects] {};
    int stackSize = 0;
};

//==============================================================================
/** F-S07: the curated 8-step Stutter Storm, advancing on the 1/32 grid. */
struct StormStep
{
    int index = 0;                  ///< 0..7
    bool gateOpen = true;           ///< first 80 % of the step
    float accentDb = 0.0f;          ///< +1.5 dB on the first step
    double stepStartPpq = 0.0;
    double stepLengthPpq = 0.0;
};

inline constexpr int kStormSteps = 8;
inline constexpr int kStormGridUnits = 11;            ///< 2+2+1+1+2+1+1+1 thirty-second notes
inline constexpr double kStormGridPpq = 0.125;        ///< one 1/32 note in quarter notes
inline constexpr float kStormGate = 0.8f;
inline constexpr double kStormTailPpq = 0.5;          ///< ends with a 1/8 tail

/** Length of step `index` in 1/32 grid units (1/16 = 2, 1/32 = 1). */
int stormStepUnits (int index) noexcept;

/** The step playing at `ppq` (PPQ-anchored at 0, so seeks and loops reproduce it). */
StormStep stormStepAt (double ppq) noexcept;

/** Seed of the stutter slice for pattern iteration `iteration` (deterministic, no legacy Rng). */
std::uint64_t stormSeed (int userSeed, std::int64_t iteration) noexcept;

} // namespace gapfill::momentary
