#include "Voice.h"
#include <algorithm>
#include <cmath>

namespace gapfill::harmonic::psola
{
namespace
{
    float bounded (float value, float low, float high, float fallback = 0) noexcept
    { return std::isfinite (value) ? std::clamp (value, low, high) : fallback; }
}

void Voice::reset() noexcept
{
    settings = {}; source = {}; epochs = {};
    streams.fill ({});
    dcInput[0] = dcInput[1] = dcOutput[0] = dcOutput[1] = toneLow[0] = toneLow[1] = 0;
    duration = elapsed = numStreams = 0;
    toneGain = 0;
    on = faulted = false;
}

bool Voice::configure (const Setup& setup, const Capture& capture, const EpochTable& marks,
                       double sampleRate, std::uint32_t recipeSeed, int plannedSamples) noexcept
{
    reset();
    if (! setup.settings.enabled || ! std::isfinite (setup.settings.mix01) || setup.settings.mix01 <= 0
        || ! setup.voiced || ! std::isfinite (setup.f0Hz) || setup.f0Hz < 50 || setup.f0Hz > 1000
        || ! std::isfinite (setup.clarity) || setup.clarity < 0.8f || setup.clarity > 1
        || ! std::isfinite (sampleRate) || sampleRate < 8000 || sampleRate > 768000 || plannedSamples <= 0
        || capture.left == nullptr || capture.length <= 0 || capture.length > kMaxCaptureSamples
        || capture.channels < 1 || capture.channels > 2 || (capture.channels == 2 && capture.right == nullptr)
        || marks.count < 4 || marks.count > kEpochs || ! std::isfinite (marks.spacingCv) || marks.spacingCv < 0 || marks.spacingCv > 0.15f)
        return false;
    rate = sampleRate;
    sourcePeriod = rate / setup.f0Hz;
    const int margin = (int) std::ceil (sourcePeriod);
    for (int i = 0; i < marks.count; ++i)
        if (marks.positions[(std::size_t) i] < margin || marks.positions[(std::size_t) i] >= capture.length - margin
            || (i > 0 && marks.positions[(std::size_t) i] <= marks.positions[(std::size_t) (i - 1)]))
            return false;
    settings = setup;
    settings.settings.motion01 = bounded (setup.settings.motion01, 0, 1);
    settings.settings.scatter01 = bounded (setup.settings.scatter01, 0, 1);
    settings.settings.tone = bounded (setup.settings.tone, -1, 1);
    settings.settings.snap01 = bounded (setup.settings.snap01, 0, 1);
    settings.settings.mix01 = bounded (setup.settings.mix01, 0, 1);
    auto ratios = intervalRatios (settings.settings, setup.f0Hz, setup.scale);
    if (ratios.count == 0) return false;
    random = detail::Random (recipeSeed);
    // First V entries are selected, then their voice/pan ordering is shuffled.
    for (int i = ratios.count - 1; i > 0; --i)
        std::swap (ratios.values[(std::size_t) i], ratios.values[(std::size_t) (random.next() % (std::uint32_t) (i + 1))]);
    source = capture;
    epochs = marks;
    duration = plannedSamples;
    numStreams = ratios.count;
    static constexpr float pans[] { 0, -0.3f, 0.3f, 0 };
    const double voiceGain = 1 / std::sqrt ((double) numStreams);
    for (int i = 0; i < numStreams; ++i)
    {
        auto& stream = streams[(std::size_t) i];
        stream.period = rate / (setup.f0Hz * ratios.values[(std::size_t) i]);
        if (stream.period < 8) { reset(); return false; }
        // Adaptive upward window avoids pure-sine octave cancellation of the
        // ticket's fixed2*T0 window. Source reads remain exactly rate1.
        const double halfWindow = std::min (sourcePeriod, stream.period);
        stream.length = std::max (8, (int) std::lround (2 * halfWindow));
        stream.normalise = (float) (voiceGain * stream.period / (0.5 * stream.length));
        const double rotation = 6.283185307179586 / stream.length;
        stream.cosStep = std::cos (rotation); stream.sinStep = std::sin (rotation);
        if (capture.channels == 1)
            stream.panL = stream.panR = 1;
        else
        {
            const float pan = 0.5f + 0.5f * pans[i];
            stream.panL = std::sqrt (2 * (1 - pan)); stream.panR = std::sqrt (2 * pan);
        }
    }
    dcCoefficient = std::exp (-6.283185307179586 * 5 / rate);
    toneCoefficient = 1 - std::exp (-6.283185307179586 * std::min (4000.0, rate * 0.45) / rate);
    toneGain = settings.settings.tone == 0 ? 0 : std::exp2 ((double) settings.settings.tone) - 1;
    on = true;
    return true;
}

void Voice::spawn (Stream& stream) noexcept
{
    const double motion = settings.settings.motion01;
    const double scatter = settings.settings.scatter01;
    const double phase = duration > 0 ? (double) elapsed / duration : 0;
    const double centre = epochs.count - 1;
    const double amplitude = std::max (0, epochs.count / 2 - 1);
    int index = (int) std::lround (centre + motion * amplitude * std::sin (6.283185307179586 * phase));
    const int jitter = (int) std::lround (3 * scatter);
    if (jitter > 0) index += (int) (random.next() % (std::uint32_t) (jitter * 2 + 1)) - jitter;
    index = std::clamp (index, 0, epochs.count - 1);
    for (auto& grain : stream.grains)
        if (! grain.active)
        {
            grain.position = epochs.positions[(std::size_t) index] - 0.5 * stream.length;
            grain.age = 0; grain.cosine = 1; grain.sine = 0; grain.active = true;
            break;
        }
    const double offset = scatter > 0 ? scatter * (0.5 * random.uniform() - 0.25) : 0;
    stream.countdown += std::max (8.0, stream.period * (1 + offset));
}

double Voice::read (const float* data, double position) const noexcept
{
    if (position < 0 || position > source.length - 1) return 0;
    const int index = (int) position;
    const double fraction = position - index;
    const double a = data[index], b = data[std::min (index + 1, source.length - 1)];
    return a + fraction * (b - a);
}

void Voice::fail() noexcept
{
    ++faults;
    faulted = true;
    source = {};
    for (auto& stream : streams) for (auto& grain : stream.grains) grain.active = false;
}

void Voice::render (float& left, float& right) noexcept
{
    if (! on) return;
    if (! faulted)
    {
        double values[2] {};
        for (int i = 0; i < numStreams; ++i)
        {
            auto& stream = streams[(std::size_t) i];
            if (stream.countdown <= 0) spawn (stream);
            stream.countdown -= 1;
            for (auto& grain : stream.grains)
            {
                if (! grain.active) continue;
                const double window = std::clamp (0.5 - 0.5 * grain.cosine, 0.0, 1.0) * stream.normalise;
                const double l = read (source.left, grain.position);
                const double r = source.channels == 1 ? l : read (source.right, grain.position);
                if (! std::isfinite (l) || ! std::isfinite (r)) { fail(); break; }
                values[0] += l * window * stream.panL; values[1] += r * window * stream.panR;
                grain.position += 1; // PSOLA changes epoch spacing, never source read rate
                const double cosine = grain.cosine;
                grain.cosine = cosine * stream.cosStep - grain.sine * stream.sinStep;
                grain.sine = grain.sine * stream.cosStep + cosine * stream.sinStep;
                if (++grain.age >= stream.length) grain.active = false;
            }
            if (faulted) break;
        }
        if (! faulted)
        {
            for (int channel = 0; channel < 2; ++channel)
            {
                double value = values[channel];
                if (toneGain != 0)
                {
                    toneLow[channel] += toneCoefficient * (value - toneLow[channel]);
                    value += toneGain * (value - toneLow[channel]);
                }
                // A shortened pitch-up window can have nonzero local mean.
                const double highpassed = value - dcInput[channel] + dcCoefficient * dcOutput[channel];
                dcInput[channel] = value; dcOutput[channel] = highpassed;
                values[channel] = std::clamp (highpassed, -2.0, 2.0);
            }
            left += (float) values[0]; right += (float) values[1];
        }
    }
    if (++elapsed >= duration)
    {
        on = false;
        source = {};
        for (auto& stream : streams) for (auto& grain : stream.grains) grain.active = false;
    }
}

int Voice::activeGrains() const noexcept
{
    int result = 0;
    for (const auto& stream : streams) for (const auto& grain : stream.grains) result += grain.active ? 1 : 0;
    return result;
}
} // namespace gapfill::harmonic::psola
