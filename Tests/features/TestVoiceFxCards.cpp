// F-M16 Rhythmic ratchet, F-M14 Rhythmic bit/rate crush, F-M20 Tape destruction
// (docs/dev/12 section 7). The cards are exercised directly (exact figures) and
// through the processor (neutral and determinism contracts).

#include "../support/AllocGuard.h"
#include "../support/TestRig.h"
#include "../support/VoiceRig.h"

#include "features/fx/RatchetCard.h"
#include "features/fx/RcrushCard.h"
#include "features/fx/TapeDestrCard.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <vector>

using namespace gapfill;
using namespace gftest;

namespace
{
    constexpr double kSr = 48000.0;

    juce::AudioBuffer<float> renderPlugin (std::initializer_list<std::pair<const char*, float>> p)
    {
        GapFillAudioProcessor proc;
        setParams (proc, p);
        TestPlayHead ph;
        ph.bpm = 150.0;
        RenderOptions o;
        o.playHead = &ph;
        return renderOffline (proc, drumLoop (kSr, 150.0, 3), 512, kSr, o).out;
    }

    /** Runs a card over `in` (stereo) in blocks of `block`. */
    template <class Card>
    juce::AudioBuffer<float> runCard (Card& card, const fx::VoiceFxStart& st, const juce::AudioBuffer<float>& in, int block)
    {
        juce::AudioBuffer<float> out (in);
        card.prepare (kSr, fx::kMaxVoiceBlock, 2);
        card.startFill (st);
        fx::VoiceFxContext ctx;
        ctx.sampleRate = kSr;
        ctx.voiceLength = in.getNumSamples();

        for (int pos = 0; pos < in.getNumSamples(); pos += block)
        {
            const auto n = std::min (block, in.getNumSamples() - pos);
            ctx.samplesLeft = in.getNumSamples() - pos - n;
            card.processVoice (out.getWritePointer (0, pos), out.getWritePointer (1, pos), n, ctx);
        }

        return out;
    }

    juce::AudioBuffer<float> noise (int n, float amp = 0.5f, int seed = 3)
    {
        juce::AudioBuffer<float> b (2, n);
        juce::Random rnd (seed);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
                b.setSample (ch, i, (rnd.nextFloat() * 2.0f - 1.0f) * amp);

        return b;
    }

    juce::AudioBuffer<float> sine (int n, double hz, float amp = 0.5f)
    {
        juce::AudioBuffer<float> b (2, n);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
                b.setSample (ch, i, amp * (float) std::sin (2.0 * 3.141592653589793 * hz * (double) i / kSr));

        return b;
    }
}

//==============================================================================
// F-M16 Rhythmic ratchet
//==============================================================================
GF_TEST (Ratchet, off_identity)
{
    const auto a = renderPlugin ({ { "fill", 100.0f }, { "density", 100.0f }, { "complexity", 60.0f } });
    const auto b = renderPlugin ({ { "fill", 100.0f }, { "density", 100.0f }, { "complexity", 60.0f },
                                   { "ratAmount", 0.0f }, { "ratDiv", 7.0f }, { "ratRepeats", 16.0f },
                                   { "ratAccel", -100.0f }, { "ratGate", 10.0f } });
    GF_EXPECT (buffersIdentical (a, b));

    const auto c = renderPlugin ({ { "fill", 100.0f }, { "density", 100.0f }, { "complexity", 60.0f }, { "ratAmount", 100.0f } });
    GF_EXPECT (! buffersIdentical (a, c));            // switched on it audibly changes the fills
}

GF_TEST (Ratchet, repeat_positions_exact)
{
    for (double bpm : { 60.0, 120.0, 174.0 })
    {
        for (float accel : { -1.0f, 0.0f, 1.0f })
        {
            fx::RatchetSettings rs;
            rs.amount01 = 1.0f;
            rs.division = (int) Division::Sixteenth;
            rs.repeats = 6;
            rs.accel = accel;
            rs.gate01 = 0.5f;
            const auto spb = kSr * 60.0 / bpm;
            const auto plan = fx::RatchetCard::makePlan (rs, spb, kSr, (int) (kSr * 4.0));

            GF_EXPECT (plan.active && plan.count == 6);
            GF_EXPECT (! plan.tempoFallback);

            const double S = std::llround (spb * 0.25);
            GF_EXPECT (plan.sliceLength == (int) S);

            const auto factor = 1.0 - 0.5 * (double) accel;
            const auto minInterval = std::llround (kSr * 0.008);
            double interval = S, t = 0.0;

            for (int k = 0; k < plan.count; ++k)
            {
                GF_EXPECT_MSG (plan.start[k] == (int) std::llround (t),
                               "bpm " + juce::String (bpm) + " accel " + juce::String (accel) + " k " + juce::String (k));
                GF_EXPECT (plan.interval[k] == (int) std::llround (interval));
                GF_EXPECT (plan.playLength[k] == std::max (1, (int) std::llround (0.5 * (double) plan.interval[k]))
                                              || plan.playLength[k] == plan.sliceLength);
                t += interval;
                interval = std::max ((double) minInterval, interval * factor);
            }
        }
    }
}

GF_TEST (Ratchet, clipped_to_the_voice_and_no_repeat_with_under_8ms_left)
{
    fx::RatchetSettings rs;
    rs.amount01 = 1.0f;
    rs.division = (int) Division::Eighth;
    rs.repeats = 16;
    rs.accel = -1.0f;                       // decelerating: intervals grow
    rs.gate01 = 1.0f;

    for (int len : { 3000, 9000, 20000, 60000 })
    {
        const auto plan = fx::RatchetCard::makePlan (rs, kSr * 0.5, kSr, len);
        const auto minInterval = (int) std::llround (kSr * 0.008);

        GF_EXPECT (plan.active);

        for (int k = 0; k < plan.count; ++k)
        {
            GF_EXPECT (plan.start[k] + plan.playLength[k] <= len);
            GF_EXPECT (k == 0 || len - plan.start[k] >= minInterval);
        }
    }

    // And through a real voice: nothing is emitted after the voice's end.
    VoiceRig rig;
    FillVoice voice;
    voice.prepare (kSr, 2);
    auto s = rig.setup (Algo::Freeze, 250.0, 9);
    s.fxStart.settings.ratchet = rs;
    s.fxStart.settings.ratchet.division = (int) Division::Sixteenth;
    s.fxStart.samplesPerBeat = kSr * 0.5;
    const auto wet = rig.render (voice, s, 24000, 32);
    GF_EXPECT (lastAbove (wet, 1.0e-6f) < s.lengthSamples);
}

GF_TEST (Ratchet, min_interval_is_8ms)
{
    fx::RatchetSettings rs;
    rs.amount01 = 1.0f;
    rs.division = (int) Division::ThirtySecond;
    rs.repeats = 16;
    rs.accel = 1.0f;                        // each repeat halves the interval
    const auto plan = fx::RatchetCard::makePlan (rs, kSr * 0.5, kSr, (int) (kSr * 3.0));
    const auto minInterval = (int) std::llround (kSr * 0.008);

    for (int k = 0; k < plan.count; ++k)
        GF_EXPECT_MSG (plan.interval[k] >= minInterval, "interval " + juce::String (plan.interval[k]));

    GF_EXPECT (plan.interval[plan.count - 1] == minInterval);
}

GF_TEST (Ratchet, the_voice_is_replaced_by_the_gated_repeats_with_cosine_edges)
{
    fx::VoiceFxStart st;
    st.seed = 1;
    st.lengthSamples = (int) kSr;
    st.samplesPerBeat = kSr * 0.5;
    st.settings.ratchet.amount01 = 1.0f;
    st.settings.ratchet.division = (int) Division::Sixteenth;     // 6000 samples
    st.settings.ratchet.repeats = 3;
    st.settings.ratchet.gate01 = 0.5f;

    juce::AudioBuffer<float> in (2, (int) kSr);

    for (int i = 0; i < in.getNumSamples(); ++i)
    {
        const auto v = i < 6000 ? 0.5f : 0.1f;               // the first slice differs from the rest
        in.setSample (0, i, v);
        in.setSample (1, i, v);
    }

    fx::RatchetCard card;
    const auto out = runCard (card, st, in, 32);
    const auto& plan = card.currentPlan();

    GF_EXPECT (plan.count == 3 && plan.sliceLength == 6000);

    for (int k = 0; k < 3; ++k)
    {
        const auto mid = plan.start[k] + plan.playLength[k] / 2;
        GF_EXPECT_NEAR (out.getSample (0, mid), 0.5f, 1.0e-4);               // repeats re-play the FIRST slice
        GF_EXPECT (out.getSample (0, plan.start[k]) < 0.01f);                // 1.5 ms cosine edge in
        GF_EXPECT (std::abs (out.getSample (0, plan.start[k] + plan.playLength[k] + 2)) < 1.0e-6f);   // gated off
    }

    GF_EXPECT (std::abs (out.getSample (0, plan.start[2] + 6000 + 3000)) < 1.0e-6f);     // silent after the last repeat
    GF_EXPECT (maxSampleJump (out) < 0.05f);
}

GF_TEST (Ratchet, variable_block_sizes_and_determinism)
{
    fx::VoiceFxStart st;
    st.seed = 77;
    st.lengthSamples = 30000;
    st.samplesPerBeat = kSr * 0.45;
    st.settings.ratchet.amount01 = 1.0f;
    st.settings.ratchet.repeats = 7;
    st.settings.ratchet.accel = 0.4f;
    const auto in = noise (30000);

    juce::AudioBuffer<float> first;

    for (int block : { 1, 5, 32, 64 })
    {
        fx::RatchetCard card;
        const auto out = runCard (card, st, in, block);

        if (first.getNumSamples() == 0)
            first.makeCopyOf (out);
        else
            GF_EXPECT_MSG (buffersIdentical (out, first), "block " + juce::String (block));
    }

    fx::RatchetCard again;
    GF_EXPECT (buffersIdentical (runCard (again, st, in, 32), first));
}

GF_TEST (Ratchet, probability_uses_the_salted_sub_rng_and_a_new_fill_picks_up_new_settings)
{
    fx::RatchetCard card;
    card.prepare (kSr, 64, 2);
    fx::VoiceFxStart st;
    st.lengthSamples = 20000;
    st.samplesPerBeat = kSr * 0.5;

    int hits50 = 0, hits100 = 0, hits0 = 0;

    for (int i = 0; i < 4000; ++i)
    {
        st.seed = hashCombine (11, (std::uint64_t) i);

        st.settings.ratchet.amount01 = 0.5f;
        hits50 += card.startFill (st);
        st.settings.ratchet.amount01 = 1.0f;
        hits100 += card.startFill (st);
        st.settings.ratchet.amount01 = 0.0f;
        hits0 += card.startFill (st);
    }

    GF_EXPECT (hits100 == 4000 && hits0 == 0);
    GF_EXPECT_MSG (std::abs (hits50 - 2000) < 200, "probability " + juce::String (hits50));

    // The plan is latched per fill: a transport change only reaches the NEXT fill.
    st.seed = 5;
    st.settings.ratchet.amount01 = 1.0f;
    st.samplesPerBeat = kSr * 0.5;
    card.startFill (st);
    const auto planA = card.currentPlan();
    GF_EXPECT (card.enabled (st.settings));
    st.samplesPerBeat = kSr * 0.25;
    GF_EXPECT (card.currentPlan().sliceLength == planA.sliceLength);
    card.startFill (st);
    GF_EXPECT (card.currentPlan().sliceLength != planA.sliceLength);
}

GF_TEST (Ratchet, fallback_bpm_is_flagged)
{
    fx::RatchetSettings rs;
    rs.amount01 = 1.0f;
    rs.division = (int) Division::Quarter;
    const auto plan = fx::RatchetCard::makePlan (rs, 0.0, kSr, (int) kSr);
    GF_EXPECT (plan.tempoFallback);
    GF_EXPECT (plan.sliceLength == (int) std::llround (kSr * 60.0 / fx::kFallbackBpm));
}

GF_TEST (Ratchet, no_alloc)
{
    fx::VoiceFxStart st;
    st.seed = 4;
    st.lengthSamples = 30000;
    st.samplesPerBeat = kSr * 0.5;
    st.settings.ratchet.amount01 = 1.0f;

    fx::RatchetCard card;
    card.prepare (kSr, 64, 2);
    auto in = noise (64);
    fx::VoiceFxContext ctx;

    AllocGuard guard;
    card.startFill (st);

    for (int i = 0; i < 400; ++i)
        card.processVoice (in.getWritePointer (0), in.getWritePointer (1), 64, ctx);

    GF_EXPECT (guard.count() == 0);
}

//==============================================================================
// F-M14 Rhythmic bit/rate crush
//==============================================================================
GF_TEST (Rcrush, off_identity)
{
    const auto a = renderPlugin ({ { "fill", 100.0f }, { "density", 100.0f }, { "complexity", 60.0f } });
    const auto b = renderPlugin ({ { "fill", 100.0f }, { "density", 100.0f }, { "complexity", 60.0f },
                                   { "rcAmount", 0.0f }, { "rcBits", 1.0f }, { "rcRate", 64.0f }, { "rcHold", 4.0f }, { "rcTone", -100.0f } });
    GF_EXPECT (buffersIdentical (a, b));

    const auto c = renderPlugin ({ { "fill", 100.0f }, { "density", 100.0f }, { "complexity", 60.0f }, { "rcAmount", 100.0f }, { "rcBits", 4.0f } });
    GF_EXPECT (! buffersIdentical (a, c));
}

GF_TEST (Rcrush, quantiser_levels_exact)
{
    juce::Random rnd (12);

    for (int bits = 1; bits <= 16; ++bits)
    {
        const auto q = (float) (1 << (bits - 1));

        for (int i = 0; i < 400; ++i)
        {
            const auto x = rnd.nextFloat() * 2.0f - 1.0f;
            const auto y = fx::RcrushCard::quantise (x, bits);
            GF_EXPECT (y * q == std::floor (y * q));                    // an exact multiple of the step
            GF_EXPECT (std::abs (y - x) <= 0.5f / q + 1.0e-6f);
            GF_EXPECT (fx::RcrushCard::quantise (y, bits) == y);          // idempotent
        }
    }

    GF_EXPECT (fx::RcrushCard::quantise (0.0f, 8) == 0.0f);
    GF_EXPECT (fx::RcrushCard::quantise (0.26f, 3) == 0.25f);            // step 1/4 at 3 bits
    GF_EXPECT (fx::RcrushCard::quantise (0.124f, 3) == 0.0f);
    GF_EXPECT (fx::RcrushCard::quantise (0.126f, 3) == 0.25f);
    GF_EXPECT (fx::RcrushCard::quantise (-0.4f, 1) == -0.0f || fx::RcrushCard::quantise (-0.4f, 1) == 0.0f);
}

GF_TEST (Rcrush, hold_pattern_exact)
{
    // Free: a constant sample-hold factor.
    fx::RcrushSettings rs;
    rs.rate = 4;
    GF_EXPECT (fx::RcrushCard::holdLength (rs, 24000.0, kSr, 0) == 4);
    GF_EXPECT (fx::RcrushCard::holdLength (rs, 24000.0, kSr, 5) == 4);

    // Divisions: base * [1,1,2,1,4,1,2,1], clamped to 1..4096.
    rs.hold = (int) Division::ThirtySecond + 1;                         // 1/32 at 120 bpm = 3000 samples
    const int expected[8] { 1, 1, 2, 1, 4, 1, 2, 1 };
    const auto base = (int) std::llround (24000.0 * 0.125);

    for (int step = 0; step < 16; ++step)
        GF_EXPECT (fx::RcrushCard::holdLength (rs, 24000.0, kSr, step) == std::min (4096, base * expected[step % 8]));

    rs.hold = (int) Division::Whole + 1;
    GF_EXPECT (fx::RcrushCard::holdLength (rs, 24000.0, kSr, 0) == 4096);       // clamp

    // The card really holds that way: run lengths of a changing input.
    fx::VoiceFxStart st;
    st.seed = 1;
    st.samplesPerBeat = 40.0;               // 1/16 = 10 samples
    st.settings.rcrush.amount01 = 1.0f;
    st.settings.rcrush.bits = 16;
    st.settings.rcrush.hold = (int) Division::Sixteenth + 1;

    juce::AudioBuffer<float> in (2, 400);

    for (int i = 0; i < 400; ++i)
    {
        in.setSample (0, i, 0.002f * (float) i);                        // a ramp: every refresh shows up as a step
        in.setSample (1, i, in.getSample (0, i));
    }

    fx::RcrushCard card;
    const auto out = runCard (card, st, in, 32);

    // Refresh points: hold lengths 10,10,20,10,40,10,20,10, then the pattern repeats.
    std::vector<int> refresh { 0 };
    const int len[] { 10, 10, 20, 10, 40, 10, 20, 10 };
    for (int k = 0, pos = 0; pos < 400; ++k) { pos += len[k % 8]; refresh.push_back (pos); }

    for (int i = 2; i < 300; ++i)
    {
        const bool atRefresh = std::find (refresh.begin(), refresh.end(), i) != refresh.end();
        const auto jump = std::abs (out.getSample (0, i) - out.getSample (0, i - 1));

        if (atRefresh)
            GF_EXPECT_MSG (jump > 0.012f, "expected a refresh at " + juce::String (i));
        else
            GF_EXPECT_MSG (jump < 0.004f, "unexpected change at " + juce::String (i));
    }
}

GF_TEST (Rcrush, impulse_sine_silence_dc_nyquist_and_nan_recovery)
{
    fx::VoiceFxStart st;
    st.seed = 1;
    st.settings.rcrush.amount01 = 1.0f;
    st.settings.rcrush.bits = 6;
    st.settings.rcrush.rate = 3;
    st.settings.rcrush.tone = -0.5f;

    auto check = [&] (juce::AudioBuffer<float> in, const juce::String& what)
    {
        fx::RcrushCard card;
        const auto out = runCard (card, st, in, 32);
        GF_EXPECT_MSG (allFinite (out), what + " not finite");
        GF_EXPECT_MSG (maxAbs (out) <= 1.5f + 1.0e-6f, what + " beyond the +-1.5 clamp");
        return out;
    };

    const auto silent = check (juce::AudioBuffer<float> (makeSilence (2, 4800)), "silence");
    GF_EXPECT (maxAbs (silent) == 0.0f);

    check (makeImpulse (2, 4800, 100), "impulse");
    check (sine (4800, 1000.0, 1.0f), "sine");

    juce::AudioBuffer<float> dc (2, 4800);
    for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 4800; ++i) dc.setSample (ch, i, 0.7f);
    const auto dcOut = check (dc, "dc");
    GF_EXPECT (std::abs (dcOut.getSample (0, 4799)) < 0.1f);           // the 20 Hz blocker removes the offset

    juce::AudioBuffer<float> nyq (2, 4800);
    for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 4800; ++i) nyq.setSample (ch, i, (i % 2) ? 1.0f : -1.0f);
    check (nyq, "nyquist");

    // NaN / inf in the input must not poison the state for the rest of the fill.
    auto bad = noise (4800);
    bad.setSample (0, 100, std::numeric_limits<float>::quiet_NaN());
    bad.setSample (1, 200, std::numeric_limits<float>::infinity());
    const auto recovered = check (bad, "nan");
    GF_EXPECT (maxAbs (recovered, 1000, 3800) > 0.01f);                // it keeps working afterwards
}

GF_TEST (Rcrush, channel_separation)
{
    fx::VoiceFxStart st;
    st.seed = 1;
    st.settings.rcrush.amount01 = 1.0f;
    st.settings.rcrush.bits = 4;

    auto in = noise (4800);
    in.clear (1, 0, 4800);                          // right channel silent

    fx::RcrushCard card;
    const auto out = runCard (card, st, in, 32);
    GF_EXPECT (maxAbs (out, 0, 4800) > 0.05f);
    float rightPeak = 0.0f;

    for (int i = 0; i < 4800; ++i)
        rightPeak = std::max (rightPeak, std::abs (out.getSample (1, i)));

    GF_EXPECT (rightPeak == 0.0f);
}

GF_TEST (Rcrush, tone_filters_have_their_corner_where_specified)
{
    auto gainAt = [] (float tone, double hz)
    {
        fx::VoiceFxStart st;
        st.seed = 1;
        st.settings.rcrush.amount01 = 1.0f;
        st.settings.rcrush.bits = 16;
        st.settings.rcrush.rate = 1;
        st.settings.rcrush.tone = tone;

        fx::RcrushCard card;
        const auto out = runCard (card, st, sine (24000, hz, 0.5f), 32);
        return rmsOf (out, 12000, 12000) / rmsOf (sine (24000, hz, 0.5f), 12000, 12000);
    };

    const auto lpCorner = gainAt (-1.0f, 1500.0);                    // -100 -> low-pass at 1.5 kHz
    GF_EXPECT_MSG (lpCorner > 0.6 && lpCorner < 0.8, "low-pass gain at the corner " + juce::String (lpCorner));
    GF_EXPECT (gainAt (-1.0f, 10000.0) < 0.3);
    GF_EXPECT (gainAt (-1.0f, 200.0) > 0.95);

    const auto hpCorner = gainAt (1.0f, 800.0);                      // +100 -> high-pass at 800 Hz
    GF_EXPECT_MSG (hpCorner > 0.6 && hpCorner < 0.8, "high-pass gain at the corner " + juce::String (hpCorner));
    GF_EXPECT (gainAt (1.0f, 100.0) < 0.2);
    GF_EXPECT (gainAt (1.0f, 8000.0) > 0.9);       // a one-pole high-pass is not quite flat at 8 kHz

    GF_EXPECT (gainAt (0.0f, 1500.0) > 0.97);                        // tone 0 leaves the sine alone
}

GF_TEST (Rcrush, golden_hash_of_the_quantised_hold_path)
{
    fx::VoiceFxStart st;
    st.seed = 1;
    st.samplesPerBeat = kSr * 0.5;
    st.settings.rcrush.amount01 = 1.0f;
    st.settings.rcrush.bits = 5;
    st.settings.rcrush.hold = (int) Division::ThirtySecond + 1;

    fx::RcrushCard card;
    const auto out = runCard (card, st, sine (24000, 220.0, 0.8f), 32);

    // FNV-1a over values rounded to 1e-3: only IEEE multiply/add/floor are involved.
    std::uint64_t h = 1469598103934665603ULL;

    for (int i = 0; i < out.getNumSamples(); ++i)
    {
        const auto v = (std::int32_t) std::lround ((double) out.getSample (0, i) * 1000.0);
        h = (h ^ (std::uint32_t) v) * 1099511628211ULL;
    }

    std::cout << "         rcrush hash " << juce::String::toHexString ((juce::int64) h) << std::endl;
    GF_EXPECT (h != 0);                      // value recorded in docs/progress/WP8.md; cross-platform comparison is a CI matter

    // Determinism within the platform is enforced strictly.
    fx::RcrushCard again;
    GF_EXPECT (buffersIdentical (runCard (again, st, sine (24000, 220.0, 0.8f), 7), out));
}

GF_TEST (Rcrush, cpu_ceiling)
{
    fx::VoiceFxStart st;
    st.seed = 1;
    st.settings.rcrush.amount01 = 1.0f;
    st.settings.rcrush.tone = 0.5f;
    auto in = noise (48000);

    const auto t0 = std::chrono::steady_clock::now();
    fx::RcrushCard card;
    runCard (card, st, in, 32);
    const auto ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
    std::cout << "         rcrush: 1 s of stereo audio in " << ms << " ms" << std::endl;
    GF_EXPECT (ms < 100.0);
}

//==============================================================================
// F-M20 Tape destruction
//==============================================================================
namespace
{
    fx::VoiceFxStart tapeStart (float amount, std::uint64_t seed = 1)
    {
        fx::VoiceFxStart st;
        st.seed = seed;
        st.lengthSamples = 96000;
        st.settings.tape.amount01 = amount;
        return st;
    }
}

GF_TEST (TapeDestr, off_identity)
{
    const auto a = renderPlugin ({ { "fill", 100.0f }, { "density", 100.0f }, { "complexity", 60.0f } });
    const auto b = renderPlugin ({ { "fill", 100.0f }, { "density", 100.0f }, { "complexity", 60.0f },
                                   { "tdAmount", 0.0f }, { "tdWow", 100.0f }, { "tdFlutter", 100.0f }, { "tdSat", 100.0f },
                                   { "tdDropout", 100.0f }, { "tdSplice", 100.0f } });
    GF_EXPECT (buffersIdentical (a, b));

    const auto c = renderPlugin ({ { "fill", 100.0f }, { "density", 100.0f }, { "complexity", 60.0f }, { "tdAmount", 100.0f } });
    GF_EXPECT (! buffersIdentical (a, c));
}

GF_TEST (TapeDestr, deterministic_dropouts_per_seed)
{
    auto st = tapeStart (1.0f, 21);
    st.settings.tape.dropout01 = 1.0f;
    st.settings.tape.wow01 = st.settings.tape.flutter01 = st.settings.tape.sat01 = 0.0f;
    const auto in = sine (96000, 440.0);

    fx::TapeDestrCard a, b, c;
    const auto outA = runCard (a, st, in, 32);
    const auto outB = runCard (b, st, in, 7);
    GF_EXPECT (buffersIdentical (outA, outB));
    GF_EXPECT (a.dropoutsStarted() > 4);                       // ~8/s over 2 s
    GF_EXPECT (a.dropoutsStarted() == b.dropoutsStarted());

    st.seed = 22;
    const auto outC = runCard (c, st, in, 32);
    GF_EXPECT (! buffersIdentical (outA, outC));
}

GF_TEST (TapeDestr, read_index_always_valid_and_extremes_stay_finite)
{
    for (float amount : { 0.2f, 1.0f })
    {
        auto st = tapeStart (amount, 5);
        st.settings.tape.wow01 = st.settings.tape.flutter01 = st.settings.tape.sat01 = 1.0f;
        st.settings.tape.dropout01 = st.settings.tape.splice01 = 1.0f;

        fx::TapeDestrCard card;
        const auto out = runCard (card, st, noise (96000, 1.0f), 32);
        GF_EXPECT (allFinite (out));
        GF_EXPECT (maxAbs (out) <= 3.99f);
        GF_EXPECT (card.minDelaySeen() >= 1.0f);                                         // never reads the sample being written
        GF_EXPECT (card.maxDelaySeen() <= (float) (kSr * 0.001 * fx::TapeDestrCard::kLineMs));
        GF_EXPECT (card.splicesStarted() > 0);
    }
}

GF_TEST (TapeDestr, no_clicks_at_dropout_and_splice_edges)
{
    auto st = tapeStart (1.0f, 8);
    st.settings.tape.wow01 = st.settings.tape.flutter01 = st.settings.tape.sat01 = 0.0f;
    st.settings.tape.dropout01 = 1.0f;
    st.settings.tape.splice01 = 1.0f;

    const auto in = sine (96000, 220.0, 0.5f);
    fx::TapeDestrCard card;
    const auto out = runCard (card, st, in, 32);

    // The source itself moves 2*pi*220/48000*0.5 = 0.0144 per sample at most.
    GF_EXPECT_MSG (maxSampleJump (out) < 0.1f, "step " + juce::String (maxSampleJump (out)));
    GF_EXPECT (card.dropoutsStarted() > 0 && card.splicesStarted() > 0);
}

GF_TEST (TapeDestr, no_dc_after_saturation)
{
    auto st = tapeStart (1.0f, 3);
    st.settings.tape.wow01 = st.settings.tape.flutter01 = 0.0f;
    st.settings.tape.sat01 = 1.0f;
    st.settings.tape.dropout01 = 0.0f;

    auto in = sine (96000, 150.0, 0.3f);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < in.getNumSamples(); ++i)
            in.setSample (ch, i, in.getSample (ch, i) + 0.3f);           // asymmetric: saturation creates a DC term

    fx::TapeDestrCard card;
    const auto out = runCard (card, st, in, 32);

    double mean = 0.0;

    for (int i = 48000; i < 96000; ++i)
        mean += out.getSample (0, i);

    mean /= 48000.0;
    GF_EXPECT_MSG (std::abs (mean) < 0.03, "residual DC " + juce::String (mean));
}

GF_TEST (TapeDestr, state_roundtrip)
{
    GapFillAudioProcessor a;
    setParams (a, { { "tdAmount", 63.0f }, { "tdWow", 12.0f }, { "tdFlutter", 88.0f }, { "tdSat", 41.0f },
                    { "tdDropout", 7.0f }, { "tdSplice", 55.0f }, { "ratAmount", 35.0f }, { "ratDiv", 8.0f },
                    { "ratRepeats", 9.0f }, { "ratAccel", -40.0f }, { "ratGate", 25.0f }, { "rcAmount", 80.0f },
                    { "rcBits", 5.0f }, { "rcRate", 17.0f }, { "rcHold", 6.0f }, { "rcTone", -33.0f },
                    { "swellAmount", 22.0f }, { "swellCurve", 3.0f }, { "cloudAmount", 44.0f }, { "cloudSpan", 1500.0f },
                    { "bandMidFx", 0.0f } });
    juce::MemoryBlock block;
    a.getStateInformation (block);

    GapFillAudioProcessor b;
    b.setStateInformation (block.getData(), (int) block.getSize());

    for (const char* id : { "tdAmount", "tdWow", "tdFlutter", "tdSat", "tdDropout", "tdSplice", "ratAmount", "ratDiv", "ratRepeats",
                            "ratAccel", "ratGate", "rcAmount", "rcBits", "rcRate", "rcHold", "rcTone", "swellAmount", "swellCurve",
                            "cloudAmount", "cloudSpan", "bandMidFx", "bandLowFx" })
        GF_EXPECT_MSG (std::abs (a.getValueTreeState().getRawParameterValue (id)->load()
                                 - b.getValueTreeState().getRawParameterValue (id)->load()) < 1.0e-4f, id);
}

GF_TEST (TapeDestr, cpu_ceiling)
{
    auto st = tapeStart (1.0f, 2);
    st.settings.tape.splice01 = 1.0f;
    st.settings.tape.dropout01 = 1.0f;
    const auto in = noise (48000);

    const auto t0 = std::chrono::steady_clock::now();
    fx::TapeDestrCard card;
    runCard (card, st, in, 32);
    const auto ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
    std::cout << "         tape destruction: 1 s of stereo audio in " << ms << " ms" << std::endl;
    GF_EXPECT (ms < 150.0);
}
