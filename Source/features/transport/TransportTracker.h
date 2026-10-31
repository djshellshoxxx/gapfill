#pragma once

// W-03: the callback-level jump predicate extracted from PluginProcessor.
// One owner/audio thread. No JUCE, allocation, locks, clock or side effects.

#include <cmath>

namespace gapfill::transport
{

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
        wasPlaying = false;
        hadPosition = false;
    }

    /** Exactly once per host callback, including bypassed/stopped callbacks.
        This preserves the released previous-tempo/end-position prediction and
        inclusive 0.25-beat tolerance. Tempo averaging/tolerance changes are
        separate behavior changes, not part of this extraction. */
    JumpKind update (const TransportSample& s) noexcept
    {
        auto kind = JumpKind::None;

        if (s.playing)
        {
            if (! wasPlaying)
                kind = JumpKind::Start;
            else if (s.ppqValid ? (! hadPosition || std::abs (s.ppq - expectedPpq) > 0.25) : hadPosition)
                kind = JumpKind::Discontinuity;

            const auto samplesPerBeat = 60.0 / s.bpm * sampleRate;
            expectedPpq = s.ppqValid ? s.ppq + (double) s.blockSamples / samplesPerBeat : 0.0;
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
    double sampleRate = 48000.0;
    double expectedPpq = 0.0;
    bool wasPlaying = false;
    bool hadPosition = false;
};

} // namespace gapfill::transport
