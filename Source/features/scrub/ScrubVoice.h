#pragma once

// F-M19 Freeze & Scrub (docs/dev/12 section 7).
//
// A dedicated performance voice, independent of the gap scheduler: while
// scrubOn is set it loops a `scrubWidth` window of a snapshot of the source and
// scrubPos (0-100 %) moves that window through the snapshot. It is a granular
// scan, not varispeed: the window position is smoothed (30 ms) and its speed is
// bounded so scanning cannot alias.
//
//  - Snapshot: on the rising edge of scrubOn the audio thread copies the last
//    `windowMs` of the source history (the frozen Capture material while Capture
//    holds the history, else the most recent audio) into a preallocated buffer.
//    Nothing is read across threads, and later input cannot change the sound.
//  - Loop: 8 ms equal-power crossfade across the loop point (seamless wrap).
//  - Off / panic: 8 ms fade. A panic holds the voice off until scrubOn is
//    switched off and on again.
//  - The engine's protection duck is applied, so protected hits stay protected.
//
// No allocation, locks or I/O after prepare().

#include "../../dsp/DspUtils.h"
#include "../fx/FxSettings.h"

namespace gapfill::fx
{

class ScrubVoice
{
public:
    /** Snapshot length until Capture v2 (C-10, WP5) provides `captureLength`. */
    static constexpr float kDefaultWindowMs = 1000.0f;
    static constexpr float kMaxWindowMs = 2000.0f;
    static constexpr float kFadeMs = 8.0f;
    static constexpr float kSmoothMs = 30.0f;
    static constexpr float kMaxWidthsPerSecond = 8.0f;
    static constexpr float kMaxSpeed = 2.0f;           ///< the window never moves faster than 2x real time
    static constexpr float kAmp = 0.8f;

    void prepare (double sampleRate, int channels);
    void reset() noexcept;

    /** Fades the voice and holds it off until scrubOn goes low again. */
    void panic() noexcept;

    /** Snapshot length in ms (clamped 250-2000); takes effect at the next switch-on. */
    void setWindowMs (float ms) noexcept { windowMs = juce::jlimit (250.0f, kMaxWindowMs, ms); }

    /** Renders and ADDS `n` samples into wet[start, start + n). `anchor` is the
        absolute history position the snapshot ends at when the voice starts. */
    void render (juce::AudioBuffer<float>& wet, int start, int n, const SourceHistory& history,
                 std::int64_t anchor, const ScrubSettings& s, float duck) noexcept;

    bool isActive() const noexcept { return gain > 0.0f || state != State::Off; }
    bool isHeldOffByPanic() const noexcept { return suppressed; }

    // --- test observability -------------------------------------------------------
    int   snapshotLength() const noexcept { return snapLen; }
    double windowStart() const noexcept { return start; }
    float lastMaxStep() const noexcept { return maxStep; }

    /** Maximum change of the window start per sample for these settings. */
    static double maxStartStep (double sampleRate, float widthMs, float windowMs) noexcept;

private:
    enum class State { Off, On };

    float readSnap (int channel, double index) const noexcept;

    double sr = 48000.0;
    int channels = 2;
    juce::AudioBuffer<float> snap;
    int snapLen = 0;
    float windowMs = kDefaultWindowMs;

    State state = State::Off;
    bool suppressed = false;
    float gain = 0.0f;
    double start = 0.0;          ///< smoothed window start (index into the snapshot)
    int phase = 0;               ///< position inside the loop window
    float maxStep = 0.0f;
};

} // namespace gapfill::fx
