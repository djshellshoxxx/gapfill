#pragma once

// C-3 Controlled Mistakes (docs/dev/04-SPEC-CREATIVE.md C-3).
//
// A mistake is a small human flaw added to ONE fill: a vibrato, an octave jump,
// a dropped repeat, a tape-style brake, a stereo swing. It is decided by the
// scheduler (FillEngine::adjustRecipe, via Roller) and rendered by the voice
// (MistakeStage). Hard limits, each asserted by a test: it never extends the
// fill, never crosses the gap boundary, never clicks (all gain changes have
// >= 2 ms ramps), keeps output finite, and mono stays mono.
//
//   * mistakes = 0  ->  no roll, ZERO RNG draws, nothing allocated or computed.
//   * per-fill probability p = 0.20 * mistakes01, rolled on the salted sub-RNG
//     hashCombine (key, kSalt_Mistakes); the legacy scheduler Rng is never used.
//   * exactly one type, picked uniformly among the types that apply to the
//     chosen algorithm and are not skipped by their own conditions.
//
// Recipe encoding: FillRecipe::mistakeType = Type, mistakeAmt = round (mistakes01 * 100).
// The octave direction is not stored: it is the parity bit octaveUp (recipe seed).

#include "../../dsp/DspUtils.h"
#include "../../dsp/FillRecipe.h"

#include <array>
#include <cstdint>
#include <vector>

namespace gapfill::mistakes
{

enum Type : std::uint8_t { None = 0, Wobble = 1, OctaveDisplace = 2, MissedRepeat = 3, Brake = 4, StereoBounce = 5, NumTypes };

inline constexpr float kMaxProbability = 0.20f;
inline constexpr double kBrakeStart = 0.75;          // the last 25 % of the fill
inline constexpr float kBrakeEndRate = 0.15f;
inline constexpr double kMinBrakeMs = 120.0;
inline constexpr double kMinBounceMs = 80.0;
inline constexpr int kMinRepeats = 3;

/** Per-algorithm applicability (Algo index 0..14). */
bool applicable (int type, int algo) noexcept;

/** Everything a skip decision may look at. */
struct Facts
{
    int algo = 0;
    std::int64_t lengthSamples = 0;
    double sampleRate = 48000.0;
    bool mono = false;                    ///< main bus is mono
    std::int64_t sourceAvailable = 0;     ///< samples of source material before the fill's anchor
    bool stereoCoherent = false;          ///< capture L/R differ by less than -60 dB
    int repeats = 0;                      ///< stutter slices / delay taps / scramble slices in the fill
    std::uint32_t seed = 0;               ///< FillRecipe::seed
};

/** Octave direction for a fill (true = up, x2; false = down, x0.5). */
bool octaveUp (std::uint32_t seed) noexcept;

/** True when `type` must not be used for these facts (its own skip rule), or does not apply to the algorithm. */
bool skipped (int type, const Facts&) noexcept;

struct Roll
{
    std::uint8_t type = None;
    std::uint8_t amount = 0;              ///< 0..100
};

/** One roll per fill. Construct it only when mistakes01 > 0 is possible: with
    mistakes01 <= 0 hit() is false and the RNG is never touched. */
class Roller
{
public:
    Roller (float mistakes01, std::uint64_t key) noexcept;

    /** Does this fill get a mistake? (one draw; false without a draw when mistakes01 <= 0) */
    bool hit() noexcept;

    /** Picks the one type (one draw), re-rolling among the remaining valid types; None if there is none. */
    Roll pick (const Facts&) noexcept;

    /** Total RNG draws made so far (tests: 0 when mistakes01 == 0). */
    int draws() const noexcept   { return drawCount; }

private:
    float amount01;
    Rng rng;
    int drawCount = 0;
};

/** Estimated repeats in a fill for the Missed Repeat rule. */
int estimateRepeats (int algo, std::int64_t lengthSamples, double sampleRate, float stutterSeconds,
                     float delaySeconds, float complexity01) noexcept;

//==============================================================================
/** Voice-side renderer. Allocation free; one per FillVoice. */
struct StageConfig
{
    int type = None;
    float amount01 = 0.0f;
    double sampleRate = 48000.0;
    int lengthSamples = 0;
    int algo = 0;
    std::uint32_t seed = 0;               ///< recipe seed (direction, LFO rate/phase, which repeat is dropped)
    int repeatLen = 0;                    ///< samples per stutter slice / delay tap / scramble slice
    int repeats = 0;
    bool mono = false;
};

class MistakeStage
{
public:
    static constexpr int wobbleBufferSize = 2048;       // power of two; > 2 * maximum modulation depth at 192 kHz

    /** Allocates the wobble delay lines (heap, once, from FillVoice::prepare). Without it Wobble stays inactive. */
    void prepare();

    /** Re-validates the type against the algorithm; an invalid combination leaves the stage inactive. */
    void start (const StageConfig&) noexcept;

    bool isActive() const noexcept   { return type != None; }
    int  getType() const noexcept    { return type; }

    /** Read-rate multiplier for the source (1 unless OctaveDisplace / Brake). */
    float rateMultiplier (float t01) const noexcept;

    /** One stereo frame (after the algorithm, before the fill envelope / duck). */
    void process (float& l, float& r) noexcept;

private:
    int type = None;
    float amount = 0.0f;
    double sr = 48000.0;
    int length = 1;
    int n = 0;
    bool octUp = false;

    // Wobble
    float wobblePhase = 0.0f, wobbleInc = 0.0f, wobbleDepth = 0.0f, wobbleBase = 0.0f;
    int wobbleWrite = 0;
    std::vector<float> wobbleStore;                    // 2 * wobbleBufferSize, allocated in prepare()
    float* wobbleBuf[2] { nullptr, nullptr };

    // Missed repeat
    int muteStart = 0, muteEnd = 0, ramp = 1;

    // Stereo bounce
    float bouncePhase = 0.0f, bounceCycles = 2.0f, bounceDepth = 0.0f;
};

} // namespace gapfill::mistakes
