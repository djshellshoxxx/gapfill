// WP9 bus cards: F-M15 comb resonator, F-L19 frequency-shifted delay, F-L15 dub feedback
// network, F-L20 formant shifting, F-L21 transient-derived convolution, F-X02 spectral
// freeze and morph (docs/dev/12 section 7). Cards are driven directly (exact figures) and
// through the chain / processor (neutral contract).

#include "../support/AllocGuard.h"
#include "../support/TestRig.h"

#include "dsp/DspUtils.h"
#include "features/fx/FxChain.h"
#include "features/fx/FxUiState.h"
#include "features/fx/bus/BusCards.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

using namespace gapfill;
using namespace gapfill::fx;
using namespace gapfill::fx::bus;
using namespace gftest;

namespace
{
    constexpr double kSr = 48000.0;
    constexpr double kPi = 3.141592653589793;

    juce::AudioBuffer<float> silence (int n)
    {
        juce::AudioBuffer<float> b (2, n);
        b.clear();
        return b;
    }

    juce::AudioBuffer<float> noiseBuf (int n, float amp = 0.5f, int seed = 3)
    {
        juce::AudioBuffer<float> b (2, n);
        juce::Random rnd (seed);

        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
                b.setSample (c, i, (rnd.nextFloat() * 2.0f - 1.0f) * amp);

        return b;
    }

    juce::AudioBuffer<float> sineBuf (int n, double hz, float amp = 0.5f, double sr = kSr)
    {
        juce::AudioBuffer<float> b (2, n);

        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
                b.setSample (c, i, amp * (float) std::sin (2.0 * kPi * hz * (double) i / sr));

        return b;
    }

    juce::AudioBuffer<float> impulseBuf (int n, int pos, float amp = 1.0f)
    {
        auto b = silence (n);
        b.setSample (0, pos, amp);
        b.setSample (1, pos, amp);
        return b;
    }

    struct Params
    {
        FxSettings s;
        double spb = 0.0;                 ///< 0 = no transport
        std::uint16_t scaleMask = 0;
        int scaleRoot = 0;
        const float* duck = nullptr;
        const SourceHistory* history = nullptr;
        std::int64_t historyOffset = 0;
        std::vector<FillStartInfo> starts;
    };

    /** Runs a card over `out` in place, in blocks. The card must be prepared. Allocation-free (the allocation
        tests call this one so that copying the input buffer is not counted as the card's allocation). */
    void runCardInPlace (BusFx& card, Params& p, juce::AudioBuffer<float>& out, int block = 512)
    {
        for (int pos = 0; pos < out.getNumSamples(); pos += block)
        {
            const auto len = std::min (block, out.getNumSamples() - pos);
            BusFxContext ctx;
            ctx.sampleRate = kSr;
            ctx.settings = &p.s;
            ctx.samplesPerBeat = p.spb;
            ctx.scaleMask = p.scaleMask;
            ctx.scaleRoot = p.scaleRoot;
            ctx.duck = p.duck;
            ctx.history = p.history;
            ctx.historyOffset = p.historyOffset;
            ctx.fillStarts = p.starts.data();
            ctx.numStarts = pos == 0 ? (int) p.starts.size() : 0;
            ctx.blockStartPos = pos;
            card.processBus (out, pos, len, ctx);
        }
    }

    /** Runs a card over `in` in blocks. The card must be prepared. */
    juce::AudioBuffer<float> runCard (BusFx& card, Params& p, const juce::AudioBuffer<float>& in, int block = 512)
    {
        auto out = in;
        runCardInPlace (card, p, out, block);
        return out;
    }

    float peakAbs (const juce::AudioBuffer<float>& b, int from, int to, int ch = -1)
    {
        float m = 0.0f;

        for (int c = 0; c < b.getNumChannels(); ++c)
            if (ch < 0 || ch == c)
                for (int i = from; i < std::min (to, b.getNumSamples()); ++i)
                    m = std::max (m, std::abs (b.getSample (c, i)));

        return m;
    }

    bool allFiniteBuf (const juce::AudioBuffer<float>& b)
    {
        for (int c = 0; c < b.getNumChannels(); ++c)
            for (int i = 0; i < b.getNumSamples(); ++i)
                if (! std::isfinite (b.getSample (c, i)))
                    return false;

        return true;
    }

    bool identical (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        if (a.getNumSamples() != b.getNumSamples())
            return false;

        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < a.getNumSamples(); ++i)
                if (a.getSample (c, i) != b.getSample (c, i))
                    return false;

        return true;
    }

    float maxJump (const juce::AudioBuffer<float>& b, int from, int to, int ch = 0)
    {
        float m = 0.0f;

        for (int i = std::max (1, from); i < std::min (to, b.getNumSamples()); ++i)
            m = std::max (m, std::abs (b.getSample (ch, i) - b.getSample (ch, i - 1)));

        return m;
    }

    /** Frequency of the strongest spectral peak of x[from, from + n) within [lo, hi] Hz
        (Hann window, zero padding, parabolic interpolation on the log magnitude). */
    double peakHz (const juce::AudioBuffer<float>& b, int ch, int from, int n, double lo, double hi, double sr = kSr, double* peakMag = nullptr)
    {
        int order = 1;

        while ((1 << order) < n * 8)
            ++order;

        const int size = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> data ((size_t) size * 2, 0.0f);

        for (int i = 0; i < n; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / (double) n);
            data[(size_t) i] = (float) (w * b.getSample (ch, from + i));
        }

        fft.performRealOnlyForwardTransform (data.data(), true);

        auto magAt = [&] (int k) { return std::hypot (data[(size_t) (2 * k)], data[(size_t) (2 * k + 1)]); };
        const int kLo = std::max (2, (int) (lo / sr * size)), kHi = std::min (size / 2 - 2, (int) (hi / sr * size));
        int best = kLo;

        for (int k = kLo; k <= kHi; ++k)
            if (magAt (k) > magAt (best))
                best = k;

        const double a = std::log (magAt (best - 1) + 1.0e-20), bb = std::log (magAt (best) + 1.0e-20), c = std::log (magAt (best + 1) + 1.0e-20);
        const double denom = a - 2.0 * bb + c;
        const double off = std::abs (denom) < 1.0e-12 ? 0.0 : 0.5 * (a - c) / denom;

        if (peakMag != nullptr)
            *peakMag = magAt (best);

        return ((double) best + off) * sr / (double) size;
    }

    double magnitudeAt (const juce::AudioBuffer<float>& b, int ch, int from, int n, double hz, double sr = kSr)
    {
        double re = 0.0, im = 0.0;

        for (int i = 0; i < n; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / (double) n);
            const double x = w * b.getSample (ch, from + i);
            const double ph = 2.0 * kPi * hz * (double) i / sr;
            re += x * std::cos (ph);
            im -= x * std::sin (ph);
        }

        return std::hypot (re, im);
    }

    double cents (double a, double b) { return 1200.0 * std::log2 (a / b); }

    /** Writes `in` into a fresh SourceHistory (6 s, 2 ch). */
    void fillHistory (SourceHistory& h, const juce::AudioBuffer<float>& in)
    {
        h.prepare (2, (int) (kSr * 6.0));
        h.write (in, 0, in.getNumSamples());
    }

    template <class Card>
    void prep (Card& c, double sr = kSr, int block = 512)
    {
        c.prepare (sr, block, 2);
    }

    template <class T>
    double timeMs (T&& fn)
    {
        const auto t0 = std::chrono::steady_clock::now();
        fn();
        return std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
    }
}

//==============================================================================
// Neutral contract (all cards)
//==============================================================================
GF_TEST (BusFx, defaults_chain_identity_and_cards_never_called)
{
    FxChain chain;
    chain.prepare (kSr, 512, 2);
    FxSettings s;
    GF_EXPECT (! s.anyBusFx());
    GF_EXPECT (chain.numBusCards() == 6);

    auto buf = noiseBuf (4096);
    const auto ref = buf;
    auto io = chain.beginBlock (s, 4096);
    GF_EXPECT (io.duck == nullptr && io.plainWet == nullptr);
    chain.processBus (buf, 0, 4096, s, io, 0);
    GF_EXPECT (identical (buf, ref));

    for (int i = 0; i < chain.numBusCards(); ++i)
        GF_EXPECT (chain.busCard (i)->callCount() == 0);
}

GF_TEST (BusFx, chain_order_is_permanent)
{
    FxChain chain;
    const CardId expected[] { CardId::Formant, CardId::Resonator, CardId::FsDelay, CardId::Dub, CardId::Convo, CardId::SpFreeze };

    for (int i = 0; i < 6; ++i)
        GF_EXPECT (chain.busCard (i)->id() == expected[i]);
}

GF_TEST (BusFx, plugin_with_every_bus_amount_zero_is_bit_identical)
{
    auto render = [] (std::initializer_list<std::pair<const char*, float>> p)
    {
        GapFillAudioProcessor proc;
        setParams (proc, p);
        TestPlayHead ph;
        ph.bpm = 150.0;
        RenderOptions o;
        o.playHead = &ph;
        return renderOffline (proc, makeDrumLoop (kSr, 150.0, 3), 512, kSr, o).out;
    };

    const auto a = render ({ { "fill", 100.0f }, { "density", 100.0f } });
    const auto b = render ({ { "fill", 100.0f }, { "density", 100.0f },
                             { "resAmount", 0.0f }, { "resNote", 90.0f }, { "resDecay", 100.0f },
                             { "fsdAmount", 0.0f }, { "fsdShift", 1500.0f }, { "fsdFeedback", 85.0f },
                             { "dubAmount", 0.0f }, { "dubFeedback", 90.0f }, { "dubSat", 100.0f },
                             { "fmtAmount", 0.0f }, { "fmtShift", 12.0f }, { "fmtVowel", 3.0f },
                             { "sfzAmount", 0.0f }, { "sfzMorph", 100.0f }, { "sfzPitch", 12.0f },
                             { "cvAmount", 0.0f }, { "cvSize", 500.0f } });
    GF_EXPECT (identical (a, b));
}

GF_TEST (BusFx, every_card_changes_the_plugin_output_when_on)
{
    auto render = [] (const char* id)
    {
        GapFillAudioProcessor proc;
        setParams (proc, { { "fill", 100.0f }, { "density", 100.0f }, { id, 100.0f } });
        TestPlayHead ph;
        ph.bpm = 150.0;
        RenderOptions o;
        o.playHead = &ph;
        return renderOffline (proc, makeDrumLoop (kSr, 150.0, 3), 512, kSr, o).out;
    };

    GapFillAudioProcessor base;
    setParams (base, { { "fill", 100.0f }, { "density", 100.0f } });
    TestPlayHead ph;
    ph.bpm = 150.0;
    RenderOptions o;
    o.playHead = &ph;
    const auto ref = renderOffline (base, makeDrumLoop (kSr, 150.0, 3), 512, kSr, o).out;

    for (const char* id : { "resAmount", "fsdAmount", "dubAmount" })
    {
        const auto out = render (id);
        GF_EXPECT_MSG (! identical (ref, out), id);
        GF_EXPECT (allFinite (out));
    }
}

GF_TEST (BusFx, panic_clears_all_cards)
{
    for (int which = 0; which < 5; ++which)
    {
        FxChain chain;
        chain.prepare (kSr, 512, 2);
        Params p;
        p.s.resonator.amount01 = which == 0 ? 1.0f : 0.0f;
        p.s.fsdelay.amount01 = which == 1 ? 1.0f : 0.0f;
        p.s.dub.amount01 = which == 2 ? 1.0f : 0.0f;
        p.s.formant.amount01 = which == 3 ? 1.0f : 0.0f;
        p.s.convo.amount01 = 0.0f;
        p.s.spfreeze.amount01 = which == 4 ? 1.0f : 0.0f;
        p.s.fsdelay.feedback01 = 0.8f;
        p.s.dub.feedback01 = 0.8f;
        p.s.resonator.decay01 = 1.0f;

        auto buf = noiseBuf((int) kSr);
        auto io = chain.beginBlock (p.s, 512);
        auto work = buf;

        // The chain indexes its duck buffer from the start of the buffer it is given (the processor hands it a
        // block-sized wet buffer), so feed it 512-sample blocks at offset 0, not a long buffer at growing offsets.
        auto runBlocks = [&] (juce::AudioBuffer<float>& buffer, const EngineBlockIO& blockIo)
        {
            juce::AudioBuffer<float> blk (2, 512);

            for (int pos = 0; pos + 512 <= buffer.getNumSamples(); pos += 512)
            {
                for (int ch = 0; ch < 2; ++ch)
                    blk.copyFrom (ch, 0, buffer, ch, pos, 512);

                chain.processBus (blk, 0, 512, p.s, blockIo, pos);

                for (int ch = 0; ch < 2; ++ch)
                    buffer.copyFrom (ch, pos, blk, ch, 0, 512);
            }
        };

        runBlocks (work, io);

        chain.fadeOut ((int) (0.004 * kSr));
        auto tail = silence ((int) (kSr * 0.5));
        auto io2 = chain.beginBlock (p.s, 512);

        runBlocks (tail, io2);

        // after the 4 ms fade (plus the latency of the spectral cards) nothing but silence remains
        const auto lat = chain.maxLatencySamples();
        GF_EXPECT_MSG (peakAbs (tail, (int) (0.01 * kSr) + lat, tail.getNumSamples()) < 1.0e-3f, "card " + juce::String (which));
    }
}

//==============================================================================
// F-M15 Comb resonator
//==============================================================================
GF_TEST (Resonator, off_identity)
{
    ResonatorCard card;
    prep (card);
    Params p;
    GF_EXPECT (! card.enabled (p.s));
    GF_EXPECT (card.isIdle());
}

GF_TEST (Resonator, tuning_error_under_5_cents)
{
    double worst = 0.0;

    for (int note = 24; note <= 96; note += 4)
    {
        ResonatorCard card;
        prep (card);
        Params p;
        p.s.resonator.amount01 = 1.0f;
        p.s.resonator.note = note;
        p.s.resonator.decay01 = 1.0f;
        p.s.resonator.damp01 = 0.2f;
        p.s.resonator.spread01 = 0.0f;

        const int n = (int) (kSr * 1.5);
        const auto out = runCard (card, p, impulseBuf (n, 4000));
        const auto expected = 440.0 * std::pow (2.0, (note - 69) / 12.0);
        const auto got = peakHz (out, 0, 6000, (int) (kSr * 0.8), expected * 0.9, expected * 1.1);
        const auto err = std::abs (cents (got, expected));
        worst = std::max (worst, err);
        GF_EXPECT_MSG (err < 5.0, "note " + juce::String (note) + " error " + juce::String (err) + " cents");
    }

    std::printf ("      [F-M15] worst tuning error %.2f cents\n", worst);
}

GF_TEST (Resonator, feedback_stable_extremes)
{
    ResonatorCard card;
    prep (card);
    Params p;
    p.s.resonator = { 1.0f, 24, 0, 1.0f, 0.0f, 1.0f };
    const auto out = runCard (card, p, noiseBuf ((int) (kSr * 5), 0.9f));
    GF_EXPECT (allFiniteBuf (out));
    GF_EXPECT (peakAbs (out, 0, out.getNumSamples()) < 20.0f);
}

GF_TEST (Resonator, scale_mode_notes_in_scale)
{
    const std::uint16_t major = 0b101010110101;              // 0 2 4 5 7 9 11
    const int root = 2;                                       // D major

    for (std::uint32_t seed = 0; seed < 300; ++seed)
    {
        const auto n = ResonatorCard::pickScaleNote (60, major, root, seed);
        GF_EXPECT (n >= 48 && n <= 72);
        GF_EXPECT ((major >> (((n - root) % 12 + 12) % 12)) & 1);
        GF_EXPECT (n == ResonatorCard::pickScaleNote (60, major, root, seed));        // deterministic
    }

    GF_EXPECT (ResonatorCard::pickScaleNote (60, 0, 0, 7) == 60);                    // Scale Lock off: Manual

    // card level: a fill start retunes to an in-scale note, a missing scale behaves as Manual
    ResonatorCard card;
    prep (card);
    Params p;
    p.s.resonator.amount01 = 1.0f;
    p.s.resonator.mode = 1;
    p.s.resonator.note = 61;                                  // not in D major
    p.scaleMask = major;
    p.scaleRoot = root;
    FillStartInfo st;
    st.recipe.seed = 1234;
    p.starts.push_back (st);
    runCard (card, p, silence (1024));
    const auto note = (int) card.currentNote();
    GF_EXPECT ((major >> (((note - root) % 12 + 12) % 12)) & 1);

    ResonatorCard manual;
    prep (manual);
    Params q;
    q.s.resonator.amount01 = 1.0f;
    q.s.resonator.mode = 1;
    q.s.resonator.note = 61;
    runCard (manual, q, silence (1024));
    GF_EXPECT ((int) manual.currentNote() == 61);
}

GF_TEST (Resonator, retune_click_free)
{
    ResonatorCard card;
    prep (card);
    Params p;
    p.s.resonator.amount01 = 1.0f;
    p.s.resonator.note = 57;
    p.s.resonator.decay01 = 0.6f;
    const auto in = sineBuf ((int) (kSr * 1.0), 220.0, 0.3f);
    auto head = in;
    head = runCard (card, p, in);
    p.s.resonator.note = 64;
    const auto after = runCard (card, p, in);

    // the retune happens at the start of the second run: compare the jump with the signal's own slope
    const auto jump = maxJump (after, 0, 2000);
    const auto reference = maxJump (head, 20000, 22000);
    GF_EXPECT_MSG (jump < reference * 3.0f + 0.05f, "jump " + juce::String (jump) + " vs " + juce::String (reference));
}

GF_TEST (Resonator, silence_denormal_clean)
{
    ResonatorCard card;
    prep (card);
    Params p;
    p.s.resonator.amount01 = 1.0f;
    p.s.resonator.decay01 = 1.0f;
    auto in = silence ((int) (kSr * 6));
    const auto burst = noiseBuf (4800, 0.5f);

    for (int c = 0; c < 2; ++c)
        in.copyFrom (c, 0, burst, c, 0, 4800);

    const auto out = runCard (card, p, in);
    GF_EXPECT (allFiniteBuf (out));

    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < out.getNumSamples(); ++i)
            GF_EXPECT (std::fpclassify (out.getSample (c, i)) != FP_SUBNORMAL);
}

GF_TEST (Resonator, stereo_spread_range)
{
    ResonatorCard card;
    prep (card);
    Params p;
    p.s.resonator.amount01 = 1.0f;
    p.s.resonator.note = 69;
    p.s.resonator.decay01 = 1.0f;
    p.s.resonator.damp01 = 0.1f;
    p.s.resonator.spread01 = 1.0f;
    const auto out = runCard (card, p, impulseBuf ((int) (kSr * 1.5), 4000));
    const auto l = peakHz (out, 0, 6000, (int) (kSr * 0.8), 400.0, 480.0);
    const auto r = peakHz (out, 1, 6000, (int) (kSr * 0.8), 400.0, 480.0);
    const auto spread = cents (r, l);
    GF_EXPECT_MSG (std::abs (spread - 30.0) < 6.0, "spread " + juce::String (spread) + " cents");
    GF_EXPECT_MSG (std::abs (cents (l, 440.0) + 15.0) < 5.0 && std::abs (cents (r, 440.0) - 15.0) < 5.0, "L/R centred on the note");
}

GF_TEST (Resonator, tail_cap)
{
    ResonatorCard card;
    prep (card);
    Params p;
    p.s.resonator.amount01 = 1.0f;
    p.s.resonator.decay01 = 1.0f;
    p.s.resonator.damp01 = 0.0f;
    auto in = silence ((int) (kSr * 3.0));
    const auto burst = noiseBuf (4800, 0.5f, 6);

    for (int c = 0; c < 2; ++c)
        in.copyFrom (c, 2000, burst, c, 0, 4800);

    const auto out = runCard (card, p, in);
    GF_EXPECT (peakAbs (out, (int) (kSr * 0.5), (int) (kSr * 1.0)) > 1.0e-3f);          // still ringing
    GF_EXPECT (peakAbs (out, (int) (kSr * 2.5), out.getNumSamples()) < 1.0e-3f);        // capped at 2 s (+ 0.25 s fade)
    GF_EXPECT (card.tailCapMs() == 2000);
}

GF_TEST (Resonator, duck_applied)
{
    ResonatorCard card;
    prep (card);
    Params p;
    p.s.resonator.amount01 = 1.0f;
    p.s.resonator.decay01 = 1.0f;
    const int n = (int) (kSr * 1.0);
    std::vector<float> duck ((size_t) n, 1.0f);
    std::fill (duck.begin() + 20000, duck.begin() + 30000, 0.0f);
    p.duck = duck.data();
    const auto out = runCard (card, p, impulseBuf (n, 2000));
    GF_EXPECT (peakAbs (out, 15000, 20000) > 1.0e-3f);
    GF_EXPECT (peakAbs (out, 20000, 30000) == 0.0f);
}

//==============================================================================
// F-L19 Frequency-shifted delay
//==============================================================================
GF_TEST (FsDelay, off_identity)
{
    FsDelayCard card;
    prep (card);
    Params p;
    GF_EXPECT (! card.enabled (p.s));
}

GF_TEST (FsDelay, zero_shift_equals_plain_delay_reference)
{
    FsDelayCard card;
    prep (card);
    Params p;
    p.s.fsdelay = { 1.0f, 0.0f, (int) Division::Eighth, 0.5f, 0.3f, 0.0f };
    const int n = 60000;
    const auto in = noiseBuf (n, 0.3f, 11);
    const auto out = runCard (card, p, in);

    // reference: plain damped feedback delay, 120 BPM fallback, 1/8 = 12000 samples
    const int D = 12000;
    std::vector<float> line ((size_t) D, 0.0f);
    float lp[2][4] {};
    float dcx[2] {}, dcy[2] {};
    const float a = onePoleCoeff (16000.0f + (2500.0f - 16000.0f) * 0.3f, kSr);
    double maxErr = 0.0;
    std::vector<float> lineR ((size_t) D, 0.0f);

    for (int i = 0; i < n; ++i)
    {
        for (int c = 0; c < 2; ++c)
        {
            auto& ln = c == 0 ? line : lineR;
            float v = ln[(size_t) (i % D)];

            for (auto& s : lp[c])
            {
                s += a * (v - s);
                v = s;
            }

            const float y = v - dcx[c] + 0.995f * dcy[c];
            dcx[c] = v;
            dcy[c] = y;
            ln[(size_t) (i % D)] = in.getSample (c, i) + 0.5f * y;

            if (i > 3000)                                        // after the 20 ms Amount ramp
                maxErr = std::max (maxErr, (double) std::abs (out.getSample (c, i) - (in.getSample (c, i) + y)));
        }
    }

    GF_EXPECT_MSG (maxErr < 2.0e-4, "max error " + juce::String (maxErr));
}

GF_TEST (FsDelay, shift_accuracy_within_0_5Hz)
{
    for (float shift : { 123.4f, 500.0f, 1500.0f })
    {
        FsDelayCard card;
        prep (card);
        Params p;
        p.s.fsdelay = { 1.0f, shift, (int) Division::Eighth, 0.0f, 0.0f, 0.0f };
        const auto out = runCard (card, p, sineBuf ((int) (kSr * 1.2), 1000.0, 0.4f));

        // the single echo starts after 0.25 s; the dry sine is at 1000 Hz, the echo at 1000 + shift
        const auto hz = peakHz (out, 0, (int) (kSr * 0.4), (int) (kSr * 0.5), 1000.0 + shift - 60.0, 1000.0 + shift + 60.0);
        GF_EXPECT_MSG (std::abs (hz - (1000.0 + shift)) < 0.5, "shift " + juce::String (shift) + " got " + juce::String (hz));
    }
}

GF_TEST (FsDelay, negative_shift_direction)
{
    FsDelayCard card;
    prep (card);
    Params p;
    p.s.fsdelay = { 1.0f, -300.0f, (int) Division::Eighth, 0.0f, 0.0f, 0.0f };
    const auto out = runCard (card, p, sineBuf ((int) (kSr * 1.2), 1000.0, 0.4f));
    const auto hz = peakHz (out, 0, (int) (kSr * 0.4), (int) (kSr * 0.5), 600.0, 940.0);
    GF_EXPECT_MSG (std::abs (hz - 700.0) < 0.5, "got " + juce::String (hz));
}

GF_TEST (FsDelay, alias_30dB_below_at_8k_plus2k)
{
    FsDelayCard card;
    prep (card);
    Params p;
    p.s.fsdelay = { 1.0f, 2000.0f, (int) Division::Eighth, 0.0f, 0.0f, 0.0f };
    const auto out = runCard (card, p, sineBuf ((int) (kSr * 1.2), 8000.0, 0.4f));
    const auto wanted = magnitudeAt (out, 0, (int) (kSr * 0.4), (int) (kSr * 0.5), 10000.0);
    const auto image = magnitudeAt (out, 0, (int) (kSr * 0.4), (int) (kSr * 0.5), 6000.0);
    const auto db = 20.0 * std::log10 (wanted / std::max (1.0e-12, image));
    GF_EXPECT_MSG (db > 30.0, "image rejection " + juce::String (db) + " dB");
}

GF_TEST (FsDelay, feedback_stable_max)
{
    FsDelayCard card;
    prep (card);
    Params p;
    p.s.fsdelay = { 1.0f, 777.0f, (int) Division::Sixteenth, 1.0f, 0.0f, 1.0f };       // feedback request 100 % -> 85 % cap
    const auto out = runCard (card, p, noiseBuf ((int) (kSr * 8), 0.9f));
    GF_EXPECT (allFiniteBuf (out));
    GF_EXPECT (peakAbs (out, 0, out.getNumSamples()) < 12.0f);
}

GF_TEST (FsDelay, tempo_change_slew)
{
    FsDelayCard card;
    prep (card);
    Params p;
    p.s.fsdelay = { 1.0f, 0.0f, (int) Division::Eighth, 0.5f, 0.0f, 0.0f };
    p.spb = 24000.0;
    const auto in = sineBuf ((int) (kSr * 1.0), 330.0, 0.3f);
    const auto first = runCard (card, p, in);
    p.spb = 30000.0;                                          // 96 BPM: the delay target moves by 20 %
    const auto second = runCard (card, p, in);
    GF_EXPECT (allFiniteBuf (second));
    GF_EXPECT (maxJump (second, 0, second.getNumSamples()) < maxJump (first, 24000, 48000) * 2.5f + 0.05f);
}

GF_TEST (FsDelay, sr_change_reset_and_192k_limits)
{
    for (double sr : { 44100.0, 96000.0, 192000.0 })
    {
        FsDelayCard card;
        card.prepare (sr, 512, 2);
        Params p;
        p.s.fsdelay = { 1.0f, -900.0f, (int) Division::Whole, 0.85f, 0.2f, 1.0f };
        p.spb = sr * 60.0 / 30.0;                             // 30 BPM: a whole note is 8 s, far beyond the line
        juce::AudioBuffer<float> in (2, (int) (sr * 3.0));
        in.clear();
        in.setSample (0, 100, 1.0f);
        auto out = in;

        for (int pos = 0; pos < in.getNumSamples(); pos += 512)
        {
            BusFxContext ctx;
            ctx.sampleRate = sr;
            ctx.settings = &p.s;
            ctx.samplesPerBeat = p.spb;
            card.processBus (out, pos, std::min (512, in.getNumSamples() - pos), ctx);
        }

        GF_EXPECT (allFiniteBuf (out));
        GF_EXPECT (card.maxDelaySamples() == (int) (2.0 * sr));
    }
}

GF_TEST (FsDelay, tail_cap_and_panic_clear)
{
    FsDelayCard card;
    prep (card);
    Params p;
    p.s.fsdelay = { 1.0f, 100.0f, (int) Division::Sixteenth, 0.85f, 0.0f, 0.0f };
    const auto out = runCard (card, p, impulseBuf ((int) (kSr * 6.0), 2000));
    GF_EXPECT (peakAbs (out, (int) (kSr * 0.5), (int) (kSr * 1.0)) > 1.0e-3f);
    GF_EXPECT (peakAbs (out, (int) (kSr * 4.5), out.getNumSamples()) < 1.0e-3f);          // 4 s cap + 0.25 s fade
    GF_EXPECT (card.tailCapMs() == 4000);

    FsDelayCard b;
    prep (b);
    auto first = runCard (b, p, sineBuf ((int) (kSr * 1.0), 200.0, 0.3f));
    b.fadeOut (192);
    p.s.fsdelay.amount01 = 1.0f;
    const auto tail = runCard (b, p, silence ((int) (kSr * 0.5)));
    GF_EXPECT (peakAbs (tail, 400, tail.getNumSamples()) < 1.0e-4f);
    GF_EXPECT (maxJump (tail, 0, 400) < 0.1f);
}

GF_TEST (FsDelay, duck_applied)
{
    FsDelayCard card;
    prep (card);
    Params p;
    p.s.fsdelay = { 1.0f, 0.0f, (int) Division::Sixteenth, 0.5f, 0.0f, 0.0f };
    const int n = (int) (kSr * 1.0);
    std::vector<float> duck ((size_t) n, 1.0f);
    std::fill (duck.begin() + 20000, duck.begin() + 30000, 0.0f);
    p.duck = duck.data();
    const auto out = runCard (card, p, impulseBuf (n, 2000));
    GF_EXPECT (peakAbs (out, 20000, 30000) == 0.0f);
    GF_EXPECT (peakAbs (out, 2500, 20000) > 1.0e-3f);
}

//==============================================================================
// F-L15 Dynamic dub feedback network
//==============================================================================
namespace
{
    DubSettings dubSettings (float fb = 0.5f, float move = 0.0f, float sat = 0.0f, bool ping = true)
    {
        DubSettings d;
        d.amount01 = 1.0f;
        d.div = (int) Division::Sixteenth;
        d.feedback01 = fb;
        d.lowHz = 20.0f;
        d.highHz = 18000.0f;
        d.move01 = move;
        d.sat01 = sat;
        d.ping = ping;
        return d;
    }
}

GF_TEST (Dub, off_identity)
{
    DubCard card;
    prep (card);
    Params p;
    GF_EXPECT (! card.enabled (p.s));
}

GF_TEST (Dub, impulse_decay_monotonic)
{
    DubCard card;
    prep (card);
    Params p;
    p.s.dub = dubSettings (0.6f, 0.0f, 0.0f, false);
    const auto out = runCard (card, p, impulseBuf ((int) (kSr * 3.0), 2000));
    const int D = 6000;                                           // 1/16 at 120 BPM
    float prev = 1.0e9f;

    for (int k = 1; k <= 8; ++k)
    {
        const auto pk = peakAbs (out, 2000 + k * D - 40, 2000 + k * D + 40, 0);
        GF_EXPECT_MSG (pk > 0.0f && pk < prev, "echo " + juce::String (k));
        prev = pk;
    }
}

GF_TEST (Dub, max_feedback_bounded)
{
    DubCard card;
    prep (card);
    Params p;
    p.s.dub = dubSettings (1.0f, 1.0f, 1.0f, true);               // request 100 % -> capped at 90 %
    const auto out = runCard (card, p, noiseBuf ((int) (kSr * 10), 0.9f));
    GF_EXPECT (allFiniteBuf (out));
    GF_EXPECT (peakAbs (out, 0, out.getNumSamples()) < 4.0f);
}

GF_TEST (Dub, ping_pong_routing)
{
    DubCard card;
    prep (card);
    Params p;
    p.s.dub = dubSettings (0.7f, 0.0f, 0.0f, true);
    const auto out = runCard (card, p, impulseBuf ((int) (kSr * 1.0), 2000));
    const int D = 6000;
    // first echo on the left only, second on the right only
    GF_EXPECT (peakAbs (out, 2000 + D - 40, 2000 + D + 40, 0) > 0.05f);
    GF_EXPECT (peakAbs (out, 2000 + D - 40, 2000 + D + 40, 1) < 1.0e-4f);
    GF_EXPECT (peakAbs (out, 2000 + 2 * D - 40, 2000 + 2 * D + 40, 1) > 0.02f);
    GF_EXPECT (peakAbs (out, 2000 + 2 * D - 40, 2000 + 2 * D + 40, 0) < 1.0e-4f);
}

GF_TEST (Dub, no_transport_lfo_fallback)
{
    // no transport = 120 BPM tempo and a 4 s LFO period, i.e. exactly the 120 BPM transport result
    DubCard a, b;
    prep (a);
    prep (b);
    Params pa, pb;
    pa.s.dub = pb.s.dub = dubSettings (0.6f, 1.0f, 0.3f, true);
    pa.spb = 0.0;
    pb.spb = kSr * 0.5;
    const auto in = noiseBuf ((int) (kSr * 2.0), 0.4f);
    GF_EXPECT (identical (runCard (a, pa, in), runCard (b, pb, in)));
    GF_EXPECT_NEAR (DubCard::lfoShape (0.25f), 1.0, 1.0e-6);
    GF_EXPECT_NEAR (DubCard::lfoShape (0.0f), DubCard::lfoShape (1.0f), 1.0e-5);
}

GF_TEST (Dub, automation_ramps_smooth)
{
    DubCard card;
    prep (card);
    Params p;
    p.s.dub = dubSettings (0.6f, 0.0f, 0.0f, true);
    p.s.dub.highHz = 12000.0f;
    const auto in = noiseBuf ((int) (kSr * 1.0), 0.3f);
    auto out1 = runCard (card, p, in);
    p.s.dub.highHz = 600.0f;
    p.s.dub.lowHz = 800.0f;
    const auto out2 = runCard (card, p, in);
    GF_EXPECT (allFiniteBuf (out2));
    GF_EXPECT (maxJump (out2, 0, out2.getNumSamples()) < 2.0f * maxJump (out1, 20000, 48000) + 0.2f);
}

GF_TEST (Dub, tail_cap_forced_decay)
{
    DubCard card;
    prep (card);
    GF_EXPECT (card.tailCapMs() == 8000);
    Params p;
    p.s.dub = dubSettings (0.9f, 0.0f, 0.0f, false);
    p.s.dub.div = (int) Division::Eighth;
    const auto out = runCard (card, p, impulseBuf ((int) (kSr * 9.2), 1000));
    GF_EXPECT (peakAbs (out, (int) (kSr * 1.0), (int) (kSr * 2.0)) > 1.0e-3f);
    GF_EXPECT (peakAbs (out, (int) (kSr * 8.6), out.getNumSamples()) < 1.0e-3f);

    DubCard shortCap;
    shortCap.setTailCapMs (2000);
    prep (shortCap);
    GF_EXPECT (shortCap.tailCapMs() == 2000);
    const auto o2 = runCard (shortCap, p, impulseBuf ((int) (kSr * 3.0), 1000));
    GF_EXPECT (peakAbs (o2, (int) (kSr * 2.5), o2.getNumSamples()) < 1.0e-3f);
}

GF_TEST (Dub, panic_clear_no_discontinuity)
{
    DubCard card;
    prep (card);
    Params p;
    p.s.dub = dubSettings (0.8f, 0.5f, 0.5f, true);
    runCard (card, p, sineBuf ((int) (kSr * 1.0), 200.0, 0.3f));
    card.fadeOut (192);
    const auto tail = runCard (card, p, silence ((int) (kSr * 0.5)));
    GF_EXPECT (maxJump (tail, 0, tail.getNumSamples()) < 0.1f);
    GF_EXPECT (peakAbs (tail, 400, tail.getNumSamples()) < 1.0e-4f);
}

GF_TEST (Dub, worst_case_cpu)
{
    DubCard card;
    prep (card);
    Params p;
    p.s.dub = dubSettings (0.9f, 1.0f, 1.0f, true);
    const auto in = noiseBuf ((int) (kSr * 5.0), 0.5f);
    const auto ms = timeMs ([&] { runCard (card, p, in); });
    std::printf ("      [F-L15] 5 s stereo worst case: %.1f ms (%.1f %% of real time)\n", ms, ms / 50.0);
    GF_EXPECT (ms < 2500.0);
}

GF_TEST (Dub, duck_applied)
{
    DubCard card;
    prep (card);
    Params p;
    p.s.dub = dubSettings (0.7f, 0.0f, 0.0f, false);
    const int n = (int) (kSr * 1.0);
    std::vector<float> duck ((size_t) n, 1.0f);
    std::fill (duck.begin() + 12000, duck.begin() + 20000, 0.0f);
    p.duck = duck.data();
    const auto out = runCard (card, p, impulseBuf (n, 2000));
    GF_EXPECT (peakAbs (out, 12000, 20000) == 0.0f);
    GF_EXPECT (peakAbs (out, 7900, 8100) > 0.05f);
}

//==============================================================================
// F-L20 Formant shifting
//==============================================================================
namespace
{
    /** Sawtooth-like harmonic tone (all harmonics, 1/n amplitude). */
    juce::AudioBuffer<float> harmonicTone (int n, double f0, double envelopeHz = 0.0, double q = 4.0, float gain = 0.3f)
    {
        juce::AudioBuffer<float> b (2, n);
        b.clear();

        for (int h = 1; h * f0 < 10000.0; ++h)
        {
            double a = 1.0 / h;

            if (envelopeHz > 0.0)
            {
                const double x = ((h * f0) - envelopeHz) / (envelopeHz / (2.0 * q));
                a = 1.0 / (1.0 + x * x) + 0.003;
            }

            for (int i = 0; i < n; ++i)
            {
                const auto v = (float) (gain * a * std::sin (2.0 * kPi * h * f0 * (double) i / kSr));
                b.setSample (0, i, b.getSample (0, i) + v);
                b.setSample (1, i, b.getSample (1, i) + v);
            }
        }

        return b;
    }
}

GF_TEST (Formant, off_identity)
{
    FormantCard card;
    prep (card);
    Params p;
    GF_EXPECT (! card.enabled (p.s));
    p.s.formant.amount01 = 1.0f;
    p.s.lite = true;
    GF_EXPECT (! card.enabled (p.s));                             // lite_disabled
}

GF_TEST (Formant, pitch_preserved_within_5_cents)
{
    FormantCard card;
    prep (card);
    Params p;
    p.s.formant = { 1.0f, 7.0f, 0.0f, 0 };
    const auto in = harmonicTone ((int) (kSr * 1.5), 200.0);
    const auto out = runCard (card, p, in);
    const auto hz = peakHz (out, 0, (int) (kSr * 0.4), (int) (kSr * 0.8), 150.0, 260.0);
    GF_EXPECT_MSG (std::abs (cents (hz, 200.0)) < 5.0, "f0 " + juce::String (hz));
}

GF_TEST (Formant, formant_peak_moves_by_ratio)
{
    // harmonics of 120 Hz shaped by a resonance at 800 Hz; +12 st moves the resonance to ~1600 Hz
    auto strongest = [] (const juce::AudioBuffer<float>& b, int from, int n)
    {
        int best = 1;
        double bm = 0.0;

        for (int h = 2; h < 40; ++h)
        {
            const auto m = magnitudeAt (b, 0, from, n, 120.0 * h);

            if (m > bm)
            {
                bm = m;
                best = h;
            }
        }

        return 120.0 * best;
    };

    const auto in = harmonicTone ((int) (kSr * 1.5), 120.0, 800.0, 3.0);
    FormantCard card;
    prep (card);
    Params p;
    p.s.formant = { 1.0f, 12.0f, 0.0f, 0 };
    const auto out = runCard (card, p, in);

    const auto before = strongest (in, (int) (kSr * 0.4), (int) (kSr * 0.5));
    const auto after = strongest (out, (int) (kSr * 0.6), (int) (kSr * 0.5));
    GF_EXPECT_MSG (std::abs (before - 800.0) <= 120.0, "input peak " + juce::String (before));
    GF_EXPECT_MSG (std::abs (after - 1600.0) <= 240.0, "shifted peak " + juce::String (after));
}

GF_TEST (Formant, stereo_phase_coherent)
{
    FormantCard card;
    prep (card);
    Params p;
    p.s.formant = { 1.0f, -5.0f, 0.4f, 2 };
    const auto in = noiseBuf ((int) (kSr * 1.0), 0.4f);               // L == R? make them equal
    auto same = in;
    same.copyFrom (1, 0, in, 0, 0, in.getNumSamples());
    const auto out = runCard (card, p, same);

    for (int i = 0; i < out.getNumSamples(); ++i)
        GF_EXPECT (out.getSample (0, i) == out.getSample (1, i));

    // an inter-channel delay of 5 samples survives (phases are never modified)
    auto delayed = same;

    for (int i = 0; i < delayed.getNumSamples(); ++i)
        delayed.setSample (1, i, i >= 5 ? same.getSample (0, i - 5) : 0.0f);

    FormantCard card2;
    prep (card2);
    const auto o2 = runCard (card2, p, delayed);
    int bestLag = 0;
    double bestC = -1.0e30;

    for (int lag = -20; lag <= 20; ++lag)
    {
        double c = 0.0;

        for (int i = 30000; i < 40000; ++i)
            c += (double) o2.getSample (0, i) * (double) o2.getSample (1, i + lag);

        if (c > bestC)
        {
            bestC = c;
            bestLag = lag;
        }
    }

    GF_EXPECT_MSG (bestLag == 5, "lag " + juce::String (bestLag));
}

GF_TEST (Formant, transient_timing_within_latency)
{
    for (float shift : { 0.0f, 3.0f })
    {
        FormantCard card;
        prep (card);
        Params p;
        p.s.formant = { 1.0f, shift, 0.0f, shift == 0.0f ? 1 : 0 };
        const int pos = 20000;
        const auto out = runCard (card, p, impulseBuf (pos * 2, pos, 0.8f));
        int argmax = 0;

        for (int i = 0; i < out.getNumSamples(); ++i)
            if (std::abs (out.getSample (0, i)) > std::abs (out.getSample (0, argmax)))
                argmax = i;

        GF_EXPECT_MSG (std::abs (argmax - (pos + card.latencySamples())) <= 2, "shift " + juce::String (shift) + " peak at " + juce::String (argmax));
    }
}

GF_TEST (Formant, silence_noise_stable_and_extremes_finite)
{
    for (int vowel = 0; vowel <= 5; ++vowel)
        for (float shift : { -12.0f, 0.0f, 12.0f })
            for (float tilt : { -1.0f, 0.0f, 1.0f })
            {
                FormantCard card;
                prep (card);
                Params p;
                p.s.formant = { 1.0f, shift, tilt, vowel };
                auto in = noiseBuf ((int) (kSr * 0.6), 0.8f);

                for (int i = 20000; i < 26000; ++i)
                {
                    in.setSample (0, i, 0.0f);                         // silence section
                    in.setSample (1, i, 1.0f);                         // DC section
                }

                const auto out = runCard (card, p, in);
                GF_EXPECT_MSG (allFiniteBuf (out), "vowel " + juce::String (vowel));
                GF_EXPECT (peakAbs (out, 0, out.getNumSamples()) < 20.0f);
            }

    FormantCard card;
    prep (card);
    Params p;
    p.s.formant = { 1.0f, 7.0f, 0.5f, 3 };
    const auto out = runCard (card, p, silence ((int) (kSr * 1.0)));
    GF_EXPECT (peakAbs (out, 0, out.getNumSamples()) < 1.0e-6f);
}

GF_TEST (Formant, latency_declared_and_duck_aligned)
{
    FormantCard card;
    prep (card);
    GF_EXPECT (card.latencySamples() == 1023);
    GF_EXPECT (FormantCard::snapShift (3.4f, 0) == 3.4f);
    GF_EXPECT (FormantCard::snapShift (3.4f, 0b101010110101) == 4.0f);       // 3 is not in the major scale, 4 is

    Params p;
    p.s.formant = { 1.0f, 4.0f, 0.0f, 0 };
    const int n = (int) (kSr * 1.0);
    std::vector<float> duck ((size_t) n, 1.0f);
    std::fill (duck.begin() + 20000, duck.begin() + 30000, 0.0f);
    p.duck = duck.data();
    const auto out = runCard (card, p, noiseBuf (n, 0.5f));
    GF_EXPECT (peakAbs (out, 20000, 30000) == 0.0f);
    GF_EXPECT (peakAbs (out, 31000, 40000) > 0.01f);

    FormantCard hi;
    hi.prepare (96000.0, 512, 2);
    GF_EXPECT (hi.latencySamples() == 2047);
}

GF_TEST (Formant, cpu_memory_budget)
{
    FormantCard card;
    prep (card);
    Params p;
    p.s.formant = { 1.0f, 5.0f, 0.3f, 1 };
    const auto in = noiseBuf ((int) (kSr * 5.0), 0.5f);
    const auto ms = timeMs ([&] { runCard (card, p, in); });
    std::printf ("      [F-L20] 5 s stereo: %.1f ms (%.1f %% of real time); preallocated %zu KiB\n",
                 ms, ms / 50.0, card.preallocatedBytes() / 1024);
    GF_EXPECT (ms < 1500.0);
    GF_EXPECT (card.preallocatedBytes() < 2u * 1024u * 1024u);
}

GF_TEST (Formant, no_alloc)
{
    FormantCard card;
    prep (card);
    Params p;
    p.s.formant = { 1.0f, 5.0f, 0.3f, 1 };
    auto buf = noiseBuf (4096, 0.5f);
    runCard (card, p, buf);
    auto work = buf;                         // the copy is the test's, not the card's
    AllocGuard guard;
    runCardInPlace (card, p, work);
    GF_EXPECT (guard.count() == 0);
}

//==============================================================================
// F-L21 Transient-derived convolution
//==============================================================================
namespace
{
    bool waitFor (const std::function<bool()>& cond, int ms = 4000)
    {
        for (int t = 0; t < ms; t += 5)
        {
            if (cond())
                return true;

            std::this_thread::sleep_for (std::chrono::milliseconds (5));
        }

        return cond();
    }

    /** A dry history with a decaying noise hit at `pos`. */
    juce::AudioBuffer<float> hitSignal (int n, int pos)
    {
        auto b = silence (n);
        juce::Random rnd (77);

        for (int i = 0; i < 24000 && pos + i < n; ++i)
        {
            const auto env = std::exp (-(float) i / 4000.0f);

            for (int c = 0; c < 2; ++c)
                b.setSample (c, pos + i, env * (rnd.nextFloat() * 2.0f - 1.0f) * 0.8f);
        }

        return b;
    }

    /** Captures an IR from `signal` and waits until it is loaded. */
    bool captureIr (ConvoCard& card, const juce::AudioBuffer<float>& signal, SourceHistory& hist, float sizeMs = 120.0f, float damp = 0.4f)
    {
        fillHistory (hist, signal);
        Params p;
        p.s.convo = { 1.0f, sizeMs, damp };
        p.history = &hist;
        const auto before = card.irSequence();
        card.requestCapture();
        runCard (card, p, silence (1024));
        return waitFor ([&] { return card.irSequence() != before && card.hasIr(); });
    }

    juce::AudioBuffer<float> readWav (const juce::File& f, double& rate)
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatReader> r (wav.createReaderFor (new juce::FileInputStream (f), true));
        juce::AudioBuffer<float> b ((int) r->numChannels, (int) r->lengthInSamples);
        r->read (&b, 0, (int) r->lengthInSamples, 0, true, true);
        rate = r->sampleRate;
        return b;
    }
}

GF_TEST (Convo, off_and_noir_identity)
{
    ConvoCard card;
    prep (card);
    Params p;
    GF_EXPECT (! card.enabled (p.s));
    p.s.convo = { 1.0f, 120.0f, 0.4f };
    GF_EXPECT (! card.hasIr());
    const auto in = noiseBuf (8192, 0.5f);
    GF_EXPECT (identical (runCard (card, p, in), in));              // no IR: passes dry
    p.s.lite = true;
    GF_EXPECT (! card.enabled (p.s));                               // lite_disabled
}

GF_TEST (Convo, ir_extraction_known_signal_and_normalisation_level)
{
    ConvoCard card;
    prep (card);
    SourceHistory hist;
    const int hitPos = (int) (kSr * 2.0);
    GF_EXPECT (captureIr (card, hitSignal ((int) (kSr * 3.0), hitPos), hist, 150.0f, 0.0f));
    GF_EXPECT (card.capturesDone() == 1);

    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("gapfill_ir_test.wav");
    GF_EXPECT (card.saveIrToFile (file));
    double rate = 0.0;
    const auto ir = readWav (file, rate);
    file.deleteFile();

    GF_EXPECT_NEAR (rate, kSr, 0.1);
    GF_EXPECT_NEAR (ir.getNumSamples(), 0.002 * kSr + 0.150 * kSr, 4.0);

    int peak = 0;

    for (int i = 0; i < ir.getNumSamples(); ++i)
        if (std::abs (ir.getSample (0, i)) > std::abs (ir.getSample (0, peak)))
            peak = i;

    GF_EXPECT_MSG (peak < 0.006 * kSr, "peak at " + juce::String (peak));

    double sum = 0.0;

    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < ir.getNumSamples(); ++i)
            sum += (double) ir.getSample (c, i) * ir.getSample (c, i);

    const auto rmsDb = 10.0 * std::log10 (sum / (2.0 * ir.getNumSamples()));
    GF_EXPECT_MSG (std::abs (rmsDb + 24.0) < 0.3, "rms " + juce::String (rmsDb) + " dBFS");
}

GF_TEST (Convo, oversize_nan_rejected)
{
    ConvoCard card;
    prep (card);
    juce::AudioBuffer<float> bad (2, 4800);
    bad.clear();
    bad.setSample (1, 100, std::numeric_limits<float>::quiet_NaN());
    GF_EXPECT (! card.loadIrFromBuffer (bad, kSr));

    const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("gapfill_ir_long.wav");
    {
        juce::WavAudioFormat wav;
        tmp.deleteFile();
        std::unique_ptr<juce::FileOutputStream> os (tmp.createOutputStream());
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), kSr, 1, 16, {}, 0));
        os.release();
        juce::AudioBuffer<float> longIr (1, (int) (kSr * 2.0));
        longIr.clear();
        longIr.setSample (0, 0, 0.5f);
        w->writeFromAudioSampleBuffer (longIr, 0, longIr.getNumSamples());
    }
    GF_EXPECT (! card.loadIrFromFile (tmp));                       // longer than 1 s
    tmp.deleteFile();
    GF_EXPECT (card.rejected() >= 2);
    GF_EXPECT (! card.hasIr());
}

GF_TEST (Convo, zero_latency_and_delta_ir)
{
    ConvoCard card;
    prep (card);
    GF_EXPECT (card.latencySamples() == 0);
    juce::AudioBuffer<float> delta (2, 256);
    delta.clear();
    delta.setSample (0, 0, 1.0f);
    delta.setSample (1, 0, 1.0f);
    GF_EXPECT (card.loadIrFromBuffer (delta, kSr));
    GF_EXPECT (waitFor ([&] { return card.hasIr(); }));
    std::this_thread::sleep_for (std::chrono::milliseconds (300));                    // JUCE loads in the background

    Params p;
    p.s.convo = { 1.0f, 120.0f, 0.0f };
    runCard (card, p, silence (4096));                                                  // let the Amount ramp settle and the IR swap in
    const auto out = runCard (card, p, impulseBuf (4096, 1000, 0.5f));
    // dry 0.5 + wet 0.5 x 1 at the SAME sample
    GF_EXPECT_NEAR (out.getSample (0, 1000), 1.0, 0.02);
    GF_EXPECT (peakAbs (out, 0, 1000) < 1.0e-4f);
}

GF_TEST (Convo, swap_crossfade_no_click)
{
    ConvoCard card;
    prep (card);
    SourceHistory hist;
    GF_EXPECT (captureIr (card, hitSignal ((int) (kSr * 3.0), (int) (kSr * 1.5)), hist, 100.0f, 0.0f));
    std::this_thread::sleep_for (std::chrono::milliseconds (200));

    Params p;
    p.s.convo = { 0.8f, 100.0f, 0.0f };
    p.history = &hist;
    const auto in = sineBuf ((int) (kSr * 1.0), 220.0, 0.3f);
    runCard (card, p, in);
    const auto ref = runCard (card, p, in);
    card.requestCapture();
    const auto swapped = runCard (card, p, in);
    waitFor ([&] { return card.capturesDone() >= 2; });
    std::this_thread::sleep_for (std::chrono::milliseconds (100));
    const auto after = runCard (card, p, in);

    GF_EXPECT (allFiniteBuf (swapped) && allFiniteBuf (after));
    GF_EXPECT (maxJump (after, 0, after.getNumSamples()) < maxJump (ref, 20000, 48000) * 3.0f + 0.1f);
}

GF_TEST (Convo, state_has_no_audio)
{
    GapFillAudioProcessor proc;
    proc.prepareToPlay (kSr, 512);
    auto& card = proc.getFxChain().cardsRef().convo;
    juce::AudioBuffer<float> delta (2, 4000);
    delta.clear();
    delta.setSample (0, 10, 1.0f);
    card.loadIrFromBuffer (delta, kSr);
    waitFor ([&] { return card.hasIr(); });
    card.setAutoCapture (true);

    const auto tree = fx::serialiseFxTree (proc);
    const auto xml = tree.toXmlString();
    GF_EXPECT (xml.length() < 1024);
    GF_EXPECT (xml.contains ("auto"));

    juce::MemoryBlock block;
    proc.getStateInformation (block);
    GF_EXPECT (block.getSize() < 64 * 1024);                        // no IR audio in the project state
    GF_EXPECT (! fx::validateFxTree (juce::ValueTree ("Nope")));
    GF_EXPECT (fx::validateFxTree (tree));
}

GF_TEST (Convo, cpu_budget_500ms_and_tail_cap)
{
    ConvoCard card;
    prep (card);
    GF_EXPECT (card.tailCapMs() == 1500);
    juce::AudioBuffer<float> ir (2, (int) (kSr * 0.5));
    juce::Random rnd (4);

    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < ir.getNumSamples(); ++i)
            ir.setSample (c, i, (rnd.nextFloat() * 2.0f - 1.0f) * std::exp (-(float) i / 6000.0f) * 0.2f);

    GF_EXPECT (card.loadIrFromBuffer (ir, kSr));
    waitFor ([&] { return card.hasIr(); });
    std::this_thread::sleep_for (std::chrono::milliseconds (400));

    Params p;
    p.s.convo = { 1.0f, 500.0f, 0.0f };
    const auto in = noiseBuf ((int) (kSr * 5.0), 0.3f);
    const auto ms = timeMs ([&] { runCard (card, p, in); });
    std::printf ("      [F-L21] 5 s stereo with a 500 ms IR: %.1f ms (%.1f %% of real time)\n", ms, ms / 50.0);
    GF_EXPECT (ms < 2500.0);

    ConvoCard c2;
    prep (c2);
    c2.loadIrFromBuffer (ir, kSr);
    waitFor ([&] { return c2.hasIr(); });
    std::this_thread::sleep_for (std::chrono::milliseconds (400));
    const auto out = runCard (c2, p, impulseBuf ((int) (kSr * 3.0), 1000));
    GF_EXPECT (peakAbs (out, 2000, 20000) > 1.0e-3f);
    GF_EXPECT (peakAbs (out, (int) (kSr * 2.0), out.getNumSamples()) < 1.0e-3f);
}

GF_TEST (Convo, duck_applied_and_thread_stops_cleanly)
{
    const auto t0 = std::chrono::steady_clock::now();
    {
        ConvoCard card;
        prep (card);
        juce::AudioBuffer<float> ir (2, 2400);
        ir.clear();
        ir.setSample (0, 0, 1.0f);
        ir.setSample (1, 0, 1.0f);
        ir.setSample (0, 1000, 0.5f);
        ir.setSample (1, 1000, 0.5f);
        card.loadIrFromBuffer (ir, kSr);
        waitFor ([&] { return card.hasIr(); });
        std::this_thread::sleep_for (std::chrono::milliseconds (400));
        Params p;
        p.s.convo = { 1.0f, 120.0f, 0.0f };
        const int n = 20000;
        std::vector<float> duck ((size_t) n, 1.0f);
        std::fill (duck.begin() + 8000, duck.begin() + 12000, 0.0f);
        p.duck = duck.data();
        runCard (card, p, silence (4096));
        auto in = impulseBuf (n, 7500, 0.5f);
        in.setSample (0, 11500, 0.5f);
        const auto out = runCard (card, p, in);
        // the wet part (1000 samples after the impulse) falls inside the duck and is removed
        GF_EXPECT (std::abs (out.getSample (0, 8500)) < 1.0e-6f);
        GF_EXPECT (std::abs (out.getSample (0, 7500)) > 0.5f);
    }
    const auto ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
    GF_EXPECT (ms < 3000.0);
}

//==============================================================================
// F-X02 Spectral freeze and morph
//==============================================================================
namespace
{
    /** Captures `signal` as the next frozen frame (manual capture at the newest history position). */
    void captureFrame (SpFreezeCard& card, Params& p, SourceHistory& hist, const juce::AudioBuffer<float>& signal, int blocks = 4)
    {
        fillHistory (hist, signal);
        p.history = &hist;
        const auto before = card.captureSequence();
        card.requestCapture();
        runCard (card, p, silence (512 * blocks));
        GF_EXPECT (card.captureSequence() == before + 1);
    }

    SpFreezeSettings freezeSettings (float length = 3000.0f, float decay = 1.0f)
    {
        SpFreezeSettings f;
        f.amount01 = 1.0f;
        f.lengthMs = length;
        f.decay01 = decay;
        f.smear01 = 0.0f;
        f.morph01 = 0.0f;
        f.pitchSt = 0.0f;
        f.width01 = 0.0f;
        return f;
    }
}

GF_TEST (SpFreeze, off_identity)
{
    SpFreezeCard card;
    prep (card);
    Params p;
    GF_EXPECT (! card.enabled (p.s));
    GF_EXPECT (card.isIdle());
    p.s.spfreeze.amount01 = 1.0f;
    p.s.lite = true;
    GF_EXPECT (! card.enabled (p.s));
}

GF_TEST (SpFreeze, phase_continuity_and_pitch)
{
    SpFreezeCard card;
    prep (card);
    Params p;
    p.s.spfreeze = freezeSettings();
    SourceHistory hist;
    captureFrame (card, p, hist, sineBuf ((int) (kSr * 3.0), 1000.0, 0.5f), 1);
    const auto out = runCard (card, p, silence ((int) (kSr * 2.0)));

    GF_EXPECT (card.isSounding());
    GF_EXPECT (allFiniteBuf (out));

    const auto hz = peakHz (out, 0, (int) (kSr * 0.3), (int) (kSr * 0.5), 800.0, 1200.0);
    GF_EXPECT_MSG (std::abs (hz - 1000.0) < 4.0, "frozen pitch " + juce::String (hz));

    // amplitude steady apart from the programmed decay (RT60 4 s)
    const double perSecDb = -60.0 / 4.0;
    double lo = 1.0e9, hi = -1.0e9;

    for (int w = 0; w < 80; ++w)
    {
        const int from = (int) (kSr * 0.3) + w * 480;
        double e = 0.0;

        for (int i = 0; i < 480; ++i)
            e += out.getSample (0, from + i) * out.getSample (0, from + i);

        const double db = 10.0 * std::log10 (e / 480.0 + 1.0e-20) - perSecDb * (double) from / kSr;
        lo = std::min (lo, db);
        hi = std::max (hi, db);
    }

    GF_EXPECT_MSG (hi - lo < 1.5, "level wobble " + juce::String (hi - lo) + " dB");
    GF_EXPECT (maxJump (out, (int) (kSr * 0.3), (int) (kSr * 1.5)) < 0.12f);
}

GF_TEST (SpFreeze, capture_release_stress)
{
    SpFreezeCard card;
    prep (card, kSr, 64);
    Params p;
    p.s.spfreeze = freezeSettings (500.0f, 0.5f);
    SourceHistory hist;
    fillHistory (hist, noiseBuf ((int) (kSr * 3.0), 0.4f));
    p.history = &hist;

    for (int i = 0; i < 10000; ++i)
    {
        card.requestCapture();
        auto a = runCard (card, p, noiseBuf (64, 0.1f, i), 64);
        card.requestRelease();
        auto b = runCard (card, p, silence (64), 64);

        if (i % 1000 == 0)
            GF_EXPECT (allFiniteBuf (a) && allFiniteBuf (b));
    }

    GF_EXPECT (card.captureSequence() == 10000);
}

GF_TEST (SpFreeze, thread_race)
{
    SpFreezeCard card;
    prep (card);
    Params p;
    p.s.spfreeze = freezeSettings (300.0f, 0.3f);
    SourceHistory hist;
    fillHistory (hist, noiseBuf ((int) (kSr * 3.0), 0.4f));
    p.history = &hist;

    std::atomic<bool> stop { false };
    std::thread t ([&]
    {
        while (! stop.load())
        {
            card.requestCapture();
            card.requestRelease();
            card.setAutoCapture (card.getAutoCapture());
            std::this_thread::yield();
        }
    });

    bool finite = true;

    for (int i = 0; i < 400; ++i)
        finite = finite && allFiniteBuf (runCard (card, p, noiseBuf (512, 0.1f, i)));

    stop = true;
    t.join();
    GF_EXPECT (finite);
}

GF_TEST (SpFreeze, memory_ceiling_recorded)
{
    for (double sr : { 48000.0, 192000.0 })
    {
        SpFreezeCard card;
        card.prepare (sr, 512, 2);
        std::printf ("      [F-X02] %.0f Hz: N=%d, preallocated %zu KiB\n", sr, card.fftSize(), card.preallocatedBytes() / 1024);
        GF_EXPECT (card.fftSize() == (sr > 96000.0 ? 4096 : 2048));
        GF_EXPECT (card.preallocatedBytes() < 4u * 1024u * 1024u);
    }
}

GF_TEST (SpFreeze, latency_declared_duck_aligned)
{
    SpFreezeCard card;
    prep (card);
    GF_EXPECT (card.latencySamples() == 2047);
    Params p;
    p.s.spfreeze = freezeSettings();
    SourceHistory hist;
    captureFrame (card, p, hist, sineBuf ((int) (kSr * 3.0), 700.0, 0.5f), 1);
    const int n = (int) (kSr * 1.0);
    std::vector<float> duck ((size_t) n, 1.0f);
    std::fill (duck.begin() + 20000, duck.begin() + 30000, 0.0f);
    p.duck = duck.data();
    const auto out = runCard (card, p, silence (n));
    GF_EXPECT (peakAbs (out, 20000, 30000) == 0.0f);
    GF_EXPECT (peakAbs (out, 31000, 40000) > 0.01f);
}

GF_TEST (SpFreeze, panic_fade)
{
    SpFreezeCard card;
    prep (card);
    Params p;
    p.s.spfreeze = freezeSettings();
    SourceHistory hist;
    captureFrame (card, p, hist, sineBuf ((int) (kSr * 3.0), 700.0, 0.5f), 1);
    runCard (card, p, silence ((int) (kSr * 0.5)));
    card.fadeOut ((int) (0.004 * kSr));
    const auto out = runCard (card, p, silence ((int) (kSr * 0.3)));
    GF_EXPECT (peakAbs (out, 0, 100) > 0.0f);
    GF_EXPECT (peakAbs (out, 400, out.getNumSamples()) < 1.0e-4f);
    GF_EXPECT (maxJump (out, 0, 400) < 0.1f);
}

GF_TEST (SpFreeze, release_fades_over_20ms)
{
    SpFreezeCard card;
    prep (card);
    Params p;
    p.s.spfreeze = freezeSettings();
    SourceHistory hist;
    captureFrame (card, p, hist, sineBuf ((int) (kSr * 3.0), 700.0, 0.5f), 1);
    runCard (card, p, silence ((int) (kSr * 0.3)));
    card.requestRelease();
    const auto out = runCard (card, p, silence ((int) (kSr * 0.3)));
    GF_EXPECT (peakAbs (out, 0, 400) > 0.01f);
    GF_EXPECT (peakAbs (out, 1500, out.getNumSamples()) < 1.0e-3f);
    GF_EXPECT (! card.isSounding());
}

GF_TEST (SpFreeze, morph_endpoints_exact)
{
    for (float morph : { 0.0f, 1.0f, 0.5f })
    {
        SpFreezeCard card;
        prep (card);
        Params p;
        p.s.spfreeze = freezeSettings();
        SourceHistory hist;
        captureFrame (card, p, hist, sineBuf ((int) (kSr * 3.0), 500.0, 0.5f), 1);
        card.requestRelease();
        runCard (card, p, silence (2048));
        captureFrame (card, p, hist, sineBuf ((int) (kSr * 3.0), 2000.0, 0.5f), 1);
        GF_EXPECT (card.hasSecondCapture());

        p.s.spfreeze.morph01 = morph;
        const auto out = runCard (card, p, silence ((int) (kSr * 0.6)));
        const auto low = magnitudeAt (out, 0, (int) (kSr * 0.2), 8192, 500.0);
        const auto high = magnitudeAt (out, 0, (int) (kSr * 0.2), 8192, 2000.0);

        if (morph == 0.0f)
            GF_EXPECT (high < low * 0.02);
        else if (morph == 1.0f)
            GF_EXPECT (low < high * 0.02);
        else
        {
            GF_EXPECT (low > high * 0.3 && high > low * 0.3);
        }
    }
}

GF_TEST (SpFreeze, pitch_and_width)
{
    SourceHistory hist;
    const auto src = sineBuf ((int) (kSr * 3.0), 1000.0, 0.5f);

    SpFreezeCard up;
    prep (up);
    Params p;
    p.s.spfreeze = freezeSettings();
    p.s.spfreeze.pitchSt = 12.0f;
    captureFrame (up, p, hist, src, 1);
    const auto o = runCard (up, p, silence ((int) (kSr * 1.0)));
    GF_EXPECT_MSG (std::abs (peakHz (o, 0, (int) (kSr * 0.3), (int) (kSr * 0.5), 1500.0, 2600.0) - 2000.0) < 12.0, "octave up");

    auto corr = [&] (float width)
    {
        SpFreezeCard c;
        prep (c);
        Params q;
        q.s.spfreeze = freezeSettings();
        q.s.spfreeze.width01 = width;
        auto mono = noiseBuf ((int) (kSr * 3.0), 0.4f, 21);
        mono.copyFrom (1, 0, mono, 0, 0, mono.getNumSamples());
        captureFrame (c, q, hist, mono, 1);
        const auto out = runCard (c, q, silence ((int) (kSr * 0.6)));
        double ab = 0.0, aa = 0.0, bb = 0.0;

        for (int i = 8000; i < 24000; ++i)
        {
            const double l = out.getSample (0, i), r = out.getSample (1, i);
            ab += l * r;
            aa += l * l;
            bb += r * r;
        }

        return ab / std::sqrt (aa * bb + 1.0e-20);
    };

    GF_EXPECT (corr (0.0f) > 0.99);
    GF_EXPECT (corr (1.0f) < 0.9);
}

GF_TEST (SpFreeze, auto_capture_at_fill_starts)
{
    SpFreezeCard card;
    prep (card);
    GF_EXPECT (card.getAutoCapture());                         // default on
    Params p;
    p.s.spfreeze = freezeSettings();
    SourceHistory hist;
    fillHistory (hist, sineBuf ((int) (kSr * 3.0), 900.0, 0.5f));
    p.history = &hist;
    p.historyOffset = 0;

    FillStartInfo preview;
    preview.recipe.flags = FillRecipe::flagPreview;
    preview.outputPos = 100000;
    p.starts = { preview };
    runCard (card, p, silence (2048));
    GF_EXPECT (card.captureSequence() == 0);                   // previews do not capture

    FillStartInfo natural;
    natural.outputPos = 120000;
    natural.recipe.seed = 5;
    p.starts = { natural };
    runCard (card, p, silence (2048));
    GF_EXPECT (card.captureSequence() == 1 && card.isSounding());

    SpFreezeCard off;
    prep (off);
    off.setAutoCapture (false);
    runCard (off, p, silence (2048));
    GF_EXPECT (off.captureSequence() == 0);
}

GF_TEST (SpFreeze, sr_change_reprepare_and_determinism)
{
    SpFreezeCard card;
    SourceHistory hist;

    for (double sr : { 48000.0, 96000.0, 192000.0, 44100.0 })
    {
        card.prepare (sr, 512, 2);
        Params p;
        p.s.spfreeze = freezeSettings();
        juce::AudioBuffer<float> src (2, (int) (sr * 2.0));
        juce::Random rnd (1);

        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < src.getNumSamples(); ++i)
                src.setSample (c, i, rnd.nextFloat() - 0.5f);

        hist.prepare (2, (int) (sr * 6.0));
        hist.write (src, 0, src.getNumSamples());
        p.history = &hist;
        card.requestCapture();
        auto out = silence ((int) (sr * 0.3));

        for (int pos = 0; pos < out.getNumSamples(); pos += 512)
        {
            BusFxContext ctx;
            ctx.sampleRate = sr;
            ctx.settings = &p.s;
            ctx.history = p.history;
            card.processBus (out, pos, std::min (512, out.getNumSamples() - pos), ctx);
        }

        GF_EXPECT (allFiniteBuf (out));
        GF_EXPECT (card.fftSize() == (sr > 96000.0 ? 4096 : 2048));
    }

    auto run1 = [&]
    {
        SpFreezeCard c;
        prep (c);
        Params p;
        p.s.spfreeze = freezeSettings();
        SourceHistory h;
        captureFrame (c, p, h, noiseBuf ((int) (kSr * 3.0), 0.4f, 8), 1);
        return runCard (c, p, silence ((int) (kSr * 0.4)));
    };

    GF_EXPECT (identical (run1(), run1()));                    // same platform; cross-platform tolerance is unverified
}

GF_TEST (SpFreeze, no_alloc)
{
    SpFreezeCard card;
    prep (card);
    Params p;
    p.s.spfreeze = freezeSettings();
    p.s.spfreeze.pitchSt = 3.0f;
    p.s.spfreeze.smear01 = 0.5f;
    SourceHistory hist;
    captureFrame (card, p, hist, noiseBuf ((int) (kSr * 3.0), 0.4f), 1);
    auto in = silence (4096);
    runCard (card, p, in);
    card.requestCapture();
    auto work = in;                          // the copy is the test's, not the card's
    AllocGuard guard;
    runCardInPlace (card, p, work);
    GF_EXPECT (guard.count() == 0);
}
