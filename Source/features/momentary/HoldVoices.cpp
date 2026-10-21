#include "HoldVoices.h"

#include "../../dsp/DspUtils.h"
#include "../Salts.h"

#include <algorithm>
#include <cmath>

namespace gapfill::momentary
{

void HoldVoices::prepare (double sampleRate, int numChannels)
{
    sr = std::max (8000.0, sampleRate);

    for (auto& v : voices)
        v.prepare (sr, numChannels);

    reset();
}

void HoldVoices::reset()
{
    for (auto& v : voices)
        v.reset();

    active = 0;
    current = -1;
    segmentEnd = 0;
    freezeAnchor = 0;
    stormStepStart = -1;
    stormStepIndex = -1;
    tapeDone = false;
    segmentCounter = 0;
}

void HoldVoices::stopAll (int fadeSamples) noexcept
{
    for (auto& v : voices)
        if (v.isActive())
            v.requestStop (std::max (16, fadeSamples));
}

void HoldVoices::panic() noexcept
{
    stopAll ((int) (sr * kPanicFade));
    current = -1;
    stormStepIndex = -1;
}

bool HoldVoices::startSegment (int effect, std::int64_t outputPos, std::int64_t anchorNow, const SourceHistory& history,
                               const HoldFrame& f, int lengthSamples, float amp, float stutterSeconds) noexcept
{
    const int fade = (int) (sr * kEngageCrossfade);
    const int next = 1 - active;

    VoiceSetup s;
    s.sampleRate = sr;
    s.lengthSamples = std::max (64, lengthSamples);
    s.fadeInSamples = std::min (fade, s.lengthSamples / 2);
    s.fadeOutSamples = std::min (fade, s.lengthSamples / 2);
    s.amp = 0.8f * f.level01 * amp;
    s.intensity01 = f.intensity01;
    s.complexity01 = f.complexity01;
    s.chaos01 = f.chaos01;
    s.sinceEventSamples = 0;
    s.band = -1;
    s.stutterSeconds = stutterSeconds;
    s.pingPong = false;
    s.rngSeed = hashCombine (hashCombine ((std::uint64_t) (std::uint32_t) f.userSeed, kSalt_Storm + (std::uint64_t) effect),
                             (std::uint64_t) segmentCounter);

    switch ((Effect) effect)
    {
        case Effect::Freeze:    s.algo = Algo::Freeze;   s.sourceAnchor = freezeAnchor; break;
        case Effect::Reverse:   s.algo = Algo::Reverse;  s.sourceAnchor = anchorNow; break;
        case Effect::TapeStop:  s.algo = Algo::TapeStop; s.sourceAnchor = anchorNow; break;
        case Effect::Stutter:
        case Effect::Storm:
        case Effect::Count:     s.algo = Algo::Stutter;  s.sourceAnchor = anchorNow; break;
    }

    if (! voices[next].start (s, history, nullptr))
        return false;

    if (voices[active].isActive())
        voices[active].requestStop (fade);          // seamless hand-off: the old segment fades while the new one fades in

    active = next;
    segmentEnd = outputPos + s.lengthSamples;
    ++segmentCounter;
    return true;
}

void HoldVoices::render (juce::AudioBuffer<float>& wet, int startSample, int n, std::int64_t outputPos,
                         std::int64_t anchorNow, const SourceHistory& history, const HoldFrame& f) noexcept
{
    const int fade = (int) (sr * kEngageCrossfade);

    if (f.top != current)
    {
        stopAll (f.top < 0 ? (int) (sr * kReleaseFade) : fade);
        current = f.top;
        tapeDone = false;
        segmentEnd = 0;
        stormStepIndex = -1;
        stormStepStart = -1;

        if (current == (int) Effect::Freeze)
            freezeAnchor = anchorNow;                 // the slice is cut once, at press
    }

    if (current >= 0)
    {
        const auto segLen = (int) (kSegmentSeconds * sr);

        switch ((Effect) current)
        {
            case Effect::Stutter:
            case Effect::Reverse:
            case Effect::Freeze:
                if (! voices[active].isActive() || outputPos + n + fade >= segmentEnd)
                    if (! startSegment (current, outputPos, anchorNow, history, f, segLen + fade, 1.0f, f.stutterSeconds))
                        segmentEnd = outputPos + n;   // retry on the next hop
                break;

            case Effect::TapeStop:
                if (! tapeDone && ! voices[active].isActive())
                {
                    tapeDone = true;                  // one-shot: decelerates, then silent while held
                    startSegment (current, outputPos, anchorNow, history, f, (int) (0.5 * sr), 1.0f, f.stutterSeconds);
                }
                break;

            case Effect::Storm:
            {
                // PPQ-anchored when the transport is valid, otherwise a 120 bpm grid from the output clock.
                const bool useHost = f.ppqValid && f.samplesPerBeat > 1.0;
                const double spb = useHost ? f.samplesPerBeat : 0.5 * sr;
                const double ppq = useHost ? f.ppq : (double) outputPos / spb;
                const auto step = stormStepAt (ppq);
                const auto key = (std::int64_t) std::llround (step.stepStartPpq / kStormGridPpq);

                if (key != stormStepStart)
                {
                    stormStepStart = key;
                    stormStepIndex = step.index;
                    const auto gateSamples = (int) std::max (64.0, kStormGate * step.stepLengthPpq * spb);
                    const auto slice = (float) (step.stepLengthPpq * spb / sr);        // 1/16 or 1/32 note repeat
                    const auto accent = std::pow (10.0f, step.accentDb / 20.0f);
                    startSegment (current, outputPos, anchorNow, history, f, gateSamples, accent, slice);
                }
                break;
            }

            case Effect::Count:
                break;
        }
    }

    for (auto& v : voices)
    {
        if (! v.isActive())
            continue;

        v.setDuck (f.respectProtection ? f.protectionDuck : 1.0f);
        v.render (wet, startSample, n, history);
    }
}

} // namespace gapfill::momentary
