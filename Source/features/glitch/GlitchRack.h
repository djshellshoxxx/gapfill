#pragma once

// F-G01 Glitch rack: the chain of glitch modules behind the GLITCH tab.
//
// Signal order (fixed): Stutter -> STAB -> Reverse -> Microgator -> Sweep filter
// -> Phaser -> Phase/pan flip, then a wet/dry mix against the rack input.
//
// Contract
//  - prepare() allocates every buffer (bounded by the kMax* constants in
//    GlitchTypes.h); process() never allocates, locks, logs or does I/O.
//  - While Settings::on is false (the default) and the output fade has finished,
//    process() returns without touching the samples: bit-identical bypass.
//  - Switching the rack, a module, or host bypass fades over 10-20 ms; module
//    state is reset once a module is fully idle, so a re-enable starts clean.
//  - Mono buses: pass right == nullptr; the stereo result is folded to mono
//    (0.5 * (l + r)), which is what a mono listener hears. Note that inverting
//    only one channel cancels in that fold.
//  - No latency is added. Reverse is a creative one-segment time shift.
//  - Level: the processed signal passes untouched up to ceiling / 2, then bends
//    softly and never reaches Settings::ceiling; the
//    dry/wet mix is convex, so |out| <= max (|in|, ceiling). The processor uses
//    2 x the Wet Safety ceiling for Whole output while Wet Safety is on, which
//    keeps the plug-in's +6 dBFS contract (dry <= 0 dBFS + wet <= -1 dBFS).

#include "GlitchClock.h"
#include "GlitchModules.h"
#include "GlitchTypes.h"

#include <atomic>

namespace gapfill::glitch
{

/** Lock-free readouts for the GUI (written on the audio thread). */
struct Meters
{
    std::atomic<bool>  stutterActive { false };
    std::atomic<bool>  stabRunning { false };
    std::atomic<int>   stabRepeat { 0 };
    std::atomic<float> gateGain { 1.0f };
    std::atomic<float> cutoffHz { 0.0f };
    std::atomic<bool>  rackActive { false };
};

class GlitchRack
{
public:
    /** Message thread (prepareToPlay). */
    void prepare (double sampleRate);

    /** Audio thread; also safe from prepare(). */
    void reset() noexcept;

    /** Processes `numSamples` in place. */
    void process (float* left, float* right, int numSamples, const Settings&, const Timing&) noexcept;

    bool isActive() const noexcept { return active; }
    const Meters& getMeters() const noexcept { return meters; }

private:
    void processSample (float& l, float& r, const Settings&, const Timing&) noexcept;
    void resetModules() noexcept;

    double sampleRate = 48000.0;
    bool prepared = false, active = false;
    float mix = 0.0f, mixStep = 1.0f / 960.0f;

    BeatClock clock;
    Stutter stutter;
    Stab stab;
    Reverser reverser;
    Gator gator;
    Sweep sweep;
    Phaser phaser;
    Flip flip;
    Engage gateEngage, sweepEngage, phaserEngage, flipEngage;
    Meters meters;
};

} // namespace gapfill::glitch
