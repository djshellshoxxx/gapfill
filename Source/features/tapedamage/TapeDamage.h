#pragma once

#include "TapeMetaCodec.h"
#include "../../dsp/DspUtils.h"

#include <array>
#include <vector>

namespace gapfill::tapedamage
{
/** Detailed opt-in F-M20 stage, separate from the released td* processor.
    One audio-thread owner. Prepare outside processing; no process-time allocation. */
class TapeDamage
{
public:
    static constexpr int kMaxEvents = 64;
    static constexpr float kMaximumDelayMs = 60.0f;
    static constexpr float kOnsetDelayMs = 4.0f;

    enum class EventType : std::uint8_t { Dropout, Splice };
    struct Event
    {
        int start = 0, end = 0, ramp = 0; // relative to first delayed output
        float value = 1.0f;              // minimum gain or splice offset in samples
        EventType type = EventType::Dropout;
        bool operator== (const Event&) const noexcept = default;
    };

    void prepare (double sampleRate);
    void reset() noexcept;

    /** outputSamples is the span available before a later stage's drain.
        minimumSourceSamples is the algorithm's source minimum.
        Replay always honours the recorded decision; absent metadata means off.
        Controls contain raw Mix. liveSend01 is the current carrier send for
        both original/replay renders; it is never stored in the decision. */
    bool begin (const fx::TapeExtraSettings&, std::uint64_t voiceSeed, int outputSamples,
                int minimumSourceSamples, const TapeMeta& replay = {}, bool replayRecipe = false,
                float liveSend01 = 1.0f) noexcept;
    void process (float& l, float& r) noexcept;

    bool isActive() const noexcept { return active; }
    int latencySamples() const noexcept { return renderSelected ? baseDelay : 0; }
    int nominalLatencySamples() const noexcept { return baseDelay; }
    int sourceInputSamples() const noexcept { return std::max (0, plannedSamples - latencySamples()); }
    bool skippedShort() const noexcept { return shortSkipped; }
    TapeMeta metadata() const noexcept { return meta; }

    int eventCount() const noexcept { return eventSize; }
    const Event& eventAt (int index) const noexcept { return events[(std::size_t) index]; }
    float wowFrequencyHz() const noexcept { return wowHz; }
    float lastWowDelaySamples() const noexcept { return lastWow; }
    float lastFlutterDelaySamples() const noexcept { return lastFlutter; }
    float lastDriftDelaySamples() const noexcept { return lastDrift; }
    float lastReadDelaySamples() const noexcept { return lastReadDelay; }
    std::size_t bufferBytes() const noexcept { return line.size() * sizeof (Stereo); }

private:
    struct Stereo { float l = 0.0f, r = 0.0f; };
    Stereo readTap (float delay) const noexcept;
    void createEvents() noexcept;
    void updateControl() noexcept;
    static float boundedTanh (float value, float drive) noexcept;

    double sr = 48000.0;
    int mask = 0, write = 0, baseDelay = 0, elapsed = 0, plannedSamples = 0;
    bool active = false, shortSkipped = false, renderSelected = false;
    std::vector<Stereo> line;
    TapeMeta meta;
    fx::TapeExtraSettings settings;

    float wowSin = 0.0f, wowCos = 1.0f, wowInitial = 0.0f, wowAmplitude = 0.0f;
    float wowSinStep = 0.0f, wowCosStep = 1.0f, wowHz = 0.0f;
    float noiseLp = 0.0f, noiseHp = 0.0f, noiseLpPrevious = 0.0f;
    float noiseLowPole = 0.0f, noiseHighPole = 0.0f, noiseNorm = 1.0f;
    float flutterPrevious = 0.0f, flutterNext = 0.0f, flutterDelay = 0.0f, flutterVelocity = 0.0f;
    float driftTarget = 0.0f, driftSpeed = 0.0f, driftDelay = 0.0f;
    float controlPhase = 0.0f, controlStep = 0.0f, driftPole = 0.0f;
    Rng flutterRng, driftRng;
    float drive = 1.0f, inverseDrive = 1.0f, emphasisPole = 0.0f, dcPole = 0.0f;
    float minimumDelay = 0.0f, maximumDelay = 0.0f, flutterLimit = 0.0f, driftLimit = 0.0f;
    float preL = 0.0f, preR = 0.0f, deL = 0.0f, deR = 0.0f;
    float dcInputL = 0.0f, dcInputR = 0.0f, dcOutputL = 0.0f, dcOutputR = 0.0f;

    std::array<Event, kMaxEvents> events {};
    int eventSize = 0, nextEvent = 0, dropoutIndex = -1;
    float spliceFrom = 0.0f, spliceTo = 0.0f;
    int spliceStart = 0, spliceEnd = 0;
    float lastWow = 0.0f, lastFlutter = 0.0f, lastDrift = 0.0f, lastReadDelay = 0.0f;
};
} // namespace gapfill::tapedamage
