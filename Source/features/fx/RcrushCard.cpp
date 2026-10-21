#include "RcrushCard.h"

#include "../../Parameters.h"
#include "../../dsp/DspUtils.h"

#include <algorithm>
#include <cmath>

namespace gapfill::fx
{

namespace
{
    constexpr float kClamp = 1.5f;

    float onePole (double sampleRate, float hz) noexcept
    {
        return (float) (1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * (double) hz / sampleRate));
    }
}

float RcrushCard::quantise (float x, int bits) noexcept
{
    if (! std::isfinite (x))
        x = 0.0f;

    const auto q = (float) (1 << (juce::jlimit (1, 16, bits) - 1));
    return std::floor (x * q + 0.5f) / q;
}

int RcrushCard::holdLength (const RcrushSettings& s, double samplesPerBeat, double sampleRate, int step) noexcept
{
    if (s.hold <= 0)
        return juce::jlimit (1, kMaxHold, s.rate);

    double beat = samplesPerBeat;

    if (beat < 1.0)
        beat = sampleRate * 60.0 / kFallbackBpm;

    const auto base = beat * divisionInBeats (juce::jlimit (0, (int) Division::NumDivisions - 1, s.hold - 1));
    const auto mult = kPattern[((step % kPatternLength) + kPatternLength) % kPatternLength];
    const auto len = std::llround (base * (double) mult);
    return (int) juce::jlimit ((long long) 1, (long long) kMaxHold, len);
}

void RcrushCard::prepare (double sampleRate, int /*maxBlock*/, int /*channels*/)
{
    sr = sampleRate;
    dcR = 1.0f - (float) (2.0 * juce::MathConstants<double>::pi * 20.0 / sr);
    reset();
}

void RcrushCard::clearState() noexcept
{
    step = 0;
    counter = 0;
    ch[0] = {};
    ch[1] = {};
}

void RcrushCard::reset()
{
    active = false;
    clearState();
}

bool RcrushCard::startFill (const VoiceFxStart& st)
{
    active = false;
    clearState();

    s = st.settings.rcrush;

    if (s.amount01 <= kOffThreshold)
        return false;

    spb = st.samplesPerBeat;
    blend = juce::jlimit (0.0f, 1.0f, s.amount01);

    const auto tone = juce::jlimit (-1.0f, 1.0f, s.tone);

    if (tone < -0.0005f)
    {
        // 18 kHz -> 1.5 kHz, geometric
        const auto hz = 18000.0f * std::pow (1500.0f / 18000.0f, -tone);
        lpCoeff = onePole (sr, std::min (hz, (float) (sr * 0.45)));
        toneMode = 1;
    }
    else if (tone > 0.0005f)
    {
        const auto hz = 20.0f * std::pow (800.0f / 20.0f, tone);
        lpCoeff = onePole (sr, hz);
        toneMode = 2;
    }
    else
    {
        toneMode = 0;
    }

    active = true;
    return true;
}

float RcrushCard::processOne (Channel& c, float in, bool refresh) noexcept
{
    if (refresh)
        c.held = quantise (in, s.bits);

    auto y = c.held;

    if (toneMode == 1)
    {
        c.lp += lpCoeff * (y - c.lp);
        y = c.lp;
    }
    else if (toneMode == 2)
    {
        c.lp += lpCoeff * (y - c.lp);
        y = y - c.lp;
    }

    if (! std::isfinite (c.lp))
        c.lp = 0.0f;

    // 20 Hz DC blocker
    const auto dc = y - c.dcX + dcR * c.dcY;
    c.dcX = y;
    c.dcY = std::isfinite (dc) ? dc : 0.0f;

    return juce::jlimit (-kClamp, kClamp, c.dcY);
}

void RcrushCard::processVoice (float* l, float* r, int n, const VoiceFxContext&) noexcept
{
    noteCall();

    if (! active)
        return;

    for (int i = 0; i < n; ++i)
    {
        bool refresh = false;

        if (counter <= 0)
        {
            refresh = true;
            counter = holdLength (s, spb, sr, step);
            ++step;
        }

        --counter;

        const auto dryL = std::isfinite (l[i]) ? l[i] : 0.0f;
        const auto dryR = std::isfinite (r[i]) ? r[i] : 0.0f;
        const auto wetL = processOne (ch[0], dryL, refresh);
        const auto wetR = processOne (ch[1], dryR, refresh);

        l[i] = dryL * (1.0f - blend) + wetL * blend;
        r[i] = dryR * (1.0f - blend) + wetR * blend;
    }
}

} // namespace gapfill::fx
