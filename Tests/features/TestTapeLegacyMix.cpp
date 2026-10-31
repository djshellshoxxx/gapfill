#include "../TestFramework.h"
#include "../TestSignals.h"
#include "../support/AllocGuard.h"
#include "features/fx/TapeDestrCard.h"

#include <cstring>
#include <limits>

using namespace gapfill;
using namespace gftest;

namespace
{
    juce::AudioBuffer<float> tapeOutput (const fx::VoiceFxStart& start, const juce::AudioBuffer<float>& input,
                                         double sr, int block)
    {
        fx::TapeDestrCard card;
        card.prepare (sr, fx::kMaxVoiceBlock, 2);
        auto output = input;
        GF_EXPECT (card.startFill (start));
        fx::VoiceFxContext context;
        context.sampleRate = sr; context.voiceLength = output.getNumSamples();
        for (int pos = 0; pos < output.getNumSamples(); pos += block)
        {
            const int count = std::min (block, output.getNumSamples() - pos);
            context.samplesLeft = output.getNumSamples() - pos - count;
            card.processVoice (output.getWritePointer (0, pos), output.getWritePointer (1, pos), count, context);
        }
        return output;
    }
}

GF_TEST (TapeLegacyMix, explicit_mix_is_independent_of_amount_and_negative_sentinel_is_exact)
{
    for (const double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        const auto input = makeNoise (sr, 0.25, 0.3f);
        fx::VoiceFxStart start;
        start.seed = 44;
        start.settings.tape.amount01 = 0.37f;
        start.settings.tape.wow01 = start.settings.tape.flutter01 = start.settings.tape.sat01
            = start.settings.tape.dropout01 = start.settings.tape.splice01 = 1;
        const auto original = tapeOutput (start, input, sr, 64);
        start.settings.tape.mixOverride01 = 1;
        const auto wet = tapeOutput (start, input, sr, 7);
        start.settings.tape.mixOverride01 = 0;
        const auto dry = tapeOutput (start, input, sr, 13);
        start.settings.tape.mixOverride01 = 0.25f;
        const auto quarter = tapeOutput (start, input, sr, 32);
        double wetDifference = 0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < input.getNumSamples(); ++i)
            {
                const auto x = input.getSample (ch, i), y = wet.getSample (ch, i);
                GF_EXPECT (dry.getSample (ch, i) == x);
                GF_EXPECT (quarter.getSample (ch, i) == x * 0.75f + y * 0.25f);
                GF_EXPECT (original.getSample (ch, i) == x * (1 - 0.37f) + y * 0.37f);
                wetDifference += std::abs ((double) y - x);
            }
        GF_EXPECT (wetDifference > 1);
        for (const float sentinel : { -1.0f, -2.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() })
        {
            start.settings.tape.mixOverride01 = sentinel;
            const auto copy = tapeOutput (start, input, sr, 3);
            for (int ch = 0; ch < 2; ++ch)
                GF_EXPECT (std::memcmp (original.getReadPointer (ch), copy.getReadPointer (ch),
                           sizeof (float) * (std::size_t) input.getNumSamples()) == 0);
        }
    }
}

GF_TEST (TapeLegacyMix, amount_zero_still_disables_card_and_active_mix_allocates_nothing)
{
    fx::TapeDestrCard card;
    card.prepare (48000, fx::kMaxVoiceBlock, 2);
    fx::VoiceFxStart start;
    start.settings.tape.mixOverride01 = 1;
    GF_EXPECT (! card.enabled (start.settings) && ! card.startFill (start));
    float l = 0.3f, r = -0.4f;
    card.processVoice (&l, &r, 1, {});
    GF_EXPECT (l == 0.3f && r == -0.4f);
    start.settings.tape.amount01 = 0.5f;
    start.settings.tape.mixOverride01 = 0.2f;
    long allocations = -1;
    {
        AllocGuard guard;
        const bool active = card.startFill (start);
        for (int i = 0; i < 2000; ++i) { l = 0.3f; r = -0.4f; card.processVoice (&l, &r, 1, {}); }
        allocations = guard.count();
        GF_EXPECT (active);
    }
    GF_EXPECT (allocations == 0);
}
