#pragma once

// F-M10 / F-S07 hold voices: the sound of the engaged performance effect
// (docs/dev/features/F-M10-momentary-effects-and-latch.md section 3).
//
// Two dedicated FillVoice instances OUTSIDE the pool of four natural voices and the
// preview voice. A long hold is a chain of seamless segments: segment N+1 starts on
// the other voice while N fades out. Everything here is audio-thread, allocation-free
// after prepare(), and never touches the gap state machine, the history strip, the
// Fill Lock or the legacy Rng (seeds come from kSalt_Storm-derived hashes).

#include "../../dsp/FillVoice.h"
#include "HoldController.h"

#include <cstdint>

namespace gapfill
{

class SpectralSmear;
struct EngineSettings;

namespace momentary
{

/** What the engine tells the hold voices once per render call. */
struct HoldFrame
{
    int top = -1;                  ///< newest engaged Effect, -1 = none
    float level01 = 1.0f;
    bool respectProtection = true;
    float protectionDuck = 1.0f;   ///< the engine's current duck (1 = none)
    int userSeed = 1;
    float stutterSeconds = 0.125f;
    float intensity01 = 0.5f, complexity01 = 0.4f, chaos01 = 0.25f;
    double ppq = 0.0;
    bool ppqValid = false;
    double samplesPerBeat = 0.0;
};

inline constexpr double kSegmentSeconds = 0.25;       ///< Reverse / Stutter / Freeze segment length (<= 1 s)
inline constexpr double kEngageCrossfade = 0.020;     ///< switching effect crossfades <= 20 ms
inline constexpr double kReleaseFade = 0.008;         ///< release fades over 8 ms
inline constexpr double kPanicFade = 0.001;           ///< Panic: <= 48 samples at 48 kHz

class HoldVoices
{
public:
    void prepare (double sampleRate, int numChannels);
    void reset();

    /** Panic / reset: fades both voices over <= 48 samples and forgets the hold. */
    void panic() noexcept;

    /** Renders `n` samples (one analysis hop) into the wet bus. `anchorNow` is the
        history position of "now" (outputPos - historyOffset, already clamped). */
    void render (juce::AudioBuffer<float>& wet, int startSample, int n, std::int64_t outputPos,
                 std::int64_t anchorNow, const SourceHistory& history, const HoldFrame&) noexcept;

    bool isActive() const noexcept             { return voices[0].isActive() || voices[1].isActive(); }
    int  getCurrentEffect() const noexcept     { return current; }
    int  getSegmentsStarted() const noexcept   { return segmentCounter; }
    std::size_t getBufferBytes() const noexcept { return voices[0].getBufferBytes() + voices[1].getBufferBytes(); }

private:
    bool startSegment (int effect, std::int64_t outputPos, std::int64_t anchorNow, const SourceHistory&,
                       const HoldFrame&, int lengthSamples, float amp, float stutterSeconds) noexcept;
    void stopAll (int fadeSamples) noexcept;

    FillVoice voices[2];
    int active = 0;                       ///< index of the voice that carries the current segment
    int current = -1;                     ///< Effect being played, -1 = none
    std::int64_t segmentEnd = 0;          ///< output position at which the current segment ends
    std::int64_t freezeAnchor = 0;        ///< Freeze: the slice is cut once, at press
    std::int64_t stormStepStart = -1;     ///< Storm: output position of the step that is playing
    int stormStepIndex = -1;
    bool tapeDone = false;                ///< Tape stop is one-shot: silent while held
    int segmentCounter = 0;
    double sr = 48000.0;
};

} // namespace momentary
} // namespace gapfill
