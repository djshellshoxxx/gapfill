#pragma once

// W-03: the callback-level jump predicate extracted from PluginProcessor.
// One owner/audio thread. No JUCE, allocation, locks, clock or side effects.

#include <cmath>

namespace gapfill::transport
{

/** One host callback's immutable timing, shared by all internal segments. */
struct HostTiming
{
    double bpm = 120.0, ppq = 0.0;
    bool tempoValid = false, ppqValid = false, playing = false, havePosition = false;
    int tsNum = 0, tsDen = 0;
};

/** Complete HOST callback snapshot, never an internal sub-chunk. The adapter
    supplies finite PPQ when ppqValid is true, and a resolved finite BPM > 1
    (120 when host tempo is invalid), just as updateTempoInfo did before. */
struct TransportSample
{
    bool playing = false;
    bool ppqValid = false;
    double ppq = 0.0;
    double bpm = 120.0;
    int blockSamples = 0;
};

/** Start/Discontinuity reset the musical decision key and panic wet. Stop only
    panics wet: it must not increment the processor's transport-jump count. */
enum class JumpKind { None, Start, Stop, Discontinuity };

class TransportTracker
{
public:
    /** Called off the processing path with the processor's prepared sample rate
        (finite, at least 8000 Hz). Preserves callback history: the existing
        processor clears that only in reset(), including releaseResources(). */
    void prepare (double sr) noexcept
    {
        sampleRate = sr;
    }

    void reset() noexcept
    {
        expectedPpq = 0.0;
        previousPpq = 0.0;
        previousBpm = 120.0;
        previousCallbackSeconds = 0.0;
        wasPlaying = false;
        hadPosition = false;
    }

    /** Exactly once per host callback, including bypassed/stopped callbacks.
        A changed tempo permits two explicit endpoints: average(previous,current
        BPM), exact for a linear ramp, or the previous-BPM endpoint for an
        instantaneous step at this boundary. Each has the same inclusive
        0.25-beat tolerance; positions between them are not implicitly accepted.
        Constant tempo retains the released operation order exactly. */
    JumpKind update (const TransportSample& s) noexcept
    {
        auto kind = JumpKind::None;

        if (s.playing)
        {
            if (! wasPlaying)
                kind = JumpKind::Start;
            else if (s.ppqValid)
            {
                if (! hadPosition)
                    kind = JumpKind::Discontinuity;
                else
                {
                    const bool tempoChanged = s.bpm > previousBpm || s.bpm < previousBpm;
                    const auto averagedEnd = tempoChanged
                        ? previousPpq + previousCallbackSeconds * (previousBpm + s.bpm) / 120.0
                        : expectedPpq;
                    const bool matchesRamp = std::abs (s.ppq - averagedEnd) <= kToleranceBeats;
                    const bool matchesBoundaryStep = std::abs (s.ppq - expectedPpq) <= kToleranceBeats;
                    if (! matchesRamp && ! matchesBoundaryStep)
                        kind = JumpKind::Discontinuity;
                }
            }
            else if (hadPosition)
                kind = JumpKind::Discontinuity;

            const auto samplesPerBeat = 60.0 / s.bpm * sampleRate;
            expectedPpq = s.ppqValid ? s.ppq + (double) s.blockSamples / samplesPerBeat : 0.0;
            previousPpq = s.ppqValid ? s.ppq : 0.0;
            previousBpm = s.bpm;
            // Preparation can change the rate before the following callback.
            // Retain this interval at the rate that actually produced it.
            previousCallbackSeconds = (double) s.blockSamples / sampleRate;
            hadPosition = s.ppqValid;
        }
        else if (wasPlaying)
        {
            kind = JumpKind::Stop;
        }

        wasPlaying = s.playing;
        return kind;
    }

private:
    static constexpr double kToleranceBeats = 0.25; // real-host measurement required before retuning
    double sampleRate = 48000.0;
    double expectedPpq = 0.0;
    double previousPpq = 0.0, previousBpm = 120.0, previousCallbackSeconds = 0.0;
    bool wasPlaying = false;
    bool hadPosition = false;
};

} // namespace gapfill::transport
