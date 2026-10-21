// FX-0 effect-card framework and F-L14 multiband effect routing
// (docs/dev/12 section 3 FX-0, section 7 F-L14).

#include "../support/AllocGuard.h"
#include "../support/TestRig.h"
#include "../support/VoiceRig.h"

#include "features/fx/FxChain.h"
#include "features/fx/FxUiState.h"
#include "features/fx/VoiceFxChain.h"

#include <algorithm>
#include <tuple>

using namespace gapfill;
using namespace gftest;

namespace
{
    /** A bus card that records what the chain hands it. */
    class StubBus : public fx::BusFx
    {
    public:
        bool on = true;
        bool sawDuck = false;
        float minDuck = 1.0f;
        double inputEnergy = 0.0;
        int fadeCalls = 0;
        int maxStarts = 0;
        std::int64_t firstStartPos = -1;

        fx::CardId id() const noexcept override { return fx::CardId::Resonator; }
        void prepare (double, int, int) override {}
        void reset() override {}
        void fadeOut (int) override { ++fadeCalls; }
        bool enabled (const fx::FxSettings&) const noexcept override { return on; }

        void processBus (juce::AudioBuffer<float>& wet, int start, int n, const fx::BusFxContext& ctx) noexcept override
        {
            noteCall();

            for (int ch = 0; ch < wet.getNumChannels(); ++ch)
            {
                const auto* d = wet.getReadPointer (ch, start);

                for (int i = 0; i < n; ++i)
                    inputEnergy += (double) d[i] * (double) d[i];
            }

            if (ctx.duck != nullptr)
            {
                sawDuck = true;

                for (int i = 0; i < n; ++i)
                    minDuck = std::min (minDuck, ctx.duck[start + i]);
            }

            maxStarts = std::max (maxStarts, ctx.numStarts);

            if (ctx.numStarts > 0 && firstStartPos < 0)
                firstStartPos = ctx.fillStarts[0].outputPos;
        }
    };

    constexpr double kSr = 48000.0;
    constexpr double kBpm = 150.0;

    juce::AudioBuffer<float> renderWith (std::initializer_list<std::pair<const char*, float>> p,
                                         fx::BusFx* stub = nullptr, int* fills = nullptr)
    {
        GapFillAudioProcessor proc;

        if (stub != nullptr)
            proc.getFxChain().addBusCard (stub);

        setParams (proc, p);
        TestPlayHead ph;
        ph.bpm = kBpm;
        RenderOptions o;
        o.playHead = &ph;
        auto r = renderOffline (proc, drumLoop (kSr, kBpm, 3), 512, kSr, o);

        if (fills != nullptr)
            *fills = r.fills;

        return std::move (r.out);
    }

    const std::initializer_list<std::pair<const char*, float>> kBusyFill
        = { { "fill", 100.0f }, { "density", 100.0f }, { "complexity", 70.0f }, { "intensity", 70.0f } };
}

//==============================================================================
GF_TEST (Fx, chain_empty_is_identity)
{
    fx::FxChain chain;
    chain.prepare (kSr, 512, 2);

    juce::AudioBuffer<float> wet (2, 512);
    juce::Random rnd (5);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            wet.setSample (ch, i, rnd.nextFloat() * 2.0f - 1.0f);

    const juce::AudioBuffer<float> before (wet);
    const fx::FxSettings s;
    auto io = chain.beginBlock (s, 512);

    GF_EXPECT (io.duck == nullptr);               // nothing published while no bus card is enabled
    GF_EXPECT (io.plainWet == nullptr);

    chain.processBus (wet, 0, 512, s, io, 0);
    GF_EXPECT (buffersIdentical (wet, before));
    GF_EXPECT (chain.numBusCards() == 0);
}

GF_TEST (Fx, chain_order_fixed)
{
    fx::VoiceFxChain vc;
    GF_EXPECT (vc.numCards() == 3);
    GF_EXPECT (vc.card (0)->id() == fx::CardId::Ratchet);
    GF_EXPECT (vc.card (1)->id() == fx::CardId::TapeDestr);
    GF_EXPECT (vc.card (2)->id() == fx::CardId::RCrush);

    // The enum is the permanent order: voice side first, then the bus side.
    GF_EXPECT ((int) fx::CardId::Swell < (int) fx::CardId::Cloud);
    GF_EXPECT ((int) fx::CardId::Cloud < (int) fx::CardId::Ratchet);
    GF_EXPECT ((int) fx::CardId::Ratchet < (int) fx::CardId::TapeDestr);
    GF_EXPECT ((int) fx::CardId::TapeDestr < (int) fx::CardId::Formant);
    GF_EXPECT ((int) fx::CardId::Formant < (int) fx::CardId::RCrush);
    GF_EXPECT ((int) fx::CardId::RCrush < (int) fx::CardId::Resonator);
    GF_EXPECT ((int) fx::CardId::Resonator < (int) fx::CardId::FsDelay);
    GF_EXPECT ((int) fx::CardId::FsDelay < (int) fx::CardId::Dub);
    GF_EXPECT ((int) fx::CardId::Dub < (int) fx::CardId::Convo);
    GF_EXPECT ((int) fx::CardId::Convo < (int) fx::CardId::SpFreeze);
}

GF_TEST (Fx, card_off_never_called)
{
    fx::VoiceFxChain vc;
    vc.prepare (kSr, fx::kMaxVoiceBlock, 2);

    fx::VoiceFxStart st;
    st.seed = 3;
    st.lengthSamples = 4800;
    vc.start (st);
    GF_EXPECT (! vc.isActive());

    float l[32] {}, r[32] {};
    vc.process (l, r, 32, 4000, 4800);

    for (int i = 0; i < vc.numCards(); ++i)
        GF_EXPECT_MSG (vc.card (i)->callCount() == 0, "card " + juce::String (i) + " was called while off");

    // Only the rate crush on: the others must stay untouched.
    st.settings.rcrush.amount01 = 1.0f;
    vc.start (st);
    GF_EXPECT (vc.isActive());
    vc.process (l, r, 32, 4000, 4800);
    GF_EXPECT (vc.rcrushCard().callCount() == 1);
    GF_EXPECT (vc.ratchetCard().callCount() == 0);
    GF_EXPECT (vc.tapeCard().callCount() == 0);
}

GF_TEST (Fx, voice_fx_state_per_voice)
{
    VoiceRig rig;
    FillVoice plain, crushed, reference;

    for (auto* v : { &plain, &crushed, &reference })
        v->prepare (kRigSr, 2);

    auto sPlain = rig.setup (Algo::Freeze, 300.0, 5);
    auto sCrush = sPlain;
    sCrush.fxStart.settings.rcrush.amount01 = 1.0f;
    sCrush.fxStart.settings.rcrush.bits = 3;

    const auto refOut = rig.render (reference, sPlain, 20000, 32);

    // Run both voices interleaved, block by block, as the engine does.
    VoiceRig rig2;
    rig2.spectral.release();
    GF_EXPECT (plain.start (sPlain, rig2.history, &rig2.spectral));
    GF_EXPECT (crushed.start (sCrush, rig2.history, &rig2.spectral));

    juce::AudioBuffer<float> a (2, 20000), b (2, 20000);
    a.clear();
    b.clear();

    for (int pos = 0; pos < 20000; pos += 32)
    {
        plain.render (a, pos, 32, rig2.history);
        crushed.render (b, pos, 32, rig2.history);
    }

    GF_EXPECT (buffersIdentical (a, refOut));           // the neighbour's cards did not leak into it
    GF_EXPECT (! buffersIdentical (b, refOut));
}

GF_TEST (Fx, enable_disable_is_click_free_for_every_voice_card)
{
    VoiceRig rig;

    for (int card = 0; card < 3; ++card)
    {
        FillVoice voice;
        voice.prepare (kRigSr, 2);
        auto s = rig.setup (Algo::Freeze, 300.0, 21 + (std::uint64_t) card);

        if (card == 0) { s.fxStart.settings.ratchet.amount01 = 1.0f; s.fxStart.settings.ratchet.division = 5; }
        if (card == 1) { s.fxStart.settings.tape.amount01 = 1.0f; s.fxStart.settings.tape.dropout01 = 1.0f; s.fxStart.settings.tape.splice01 = 1.0f; }
        if (card == 2) { s.fxStart.settings.rcrush.amount01 = 1.0f; s.fxStart.settings.rcrush.bits = 4; }

        bool started = false;
        const auto wet = rig.render (voice, s, 30000, 32, &started);
        GF_EXPECT (started);
        GF_EXPECT (allFinite (wet));
        GF_EXPECT (maxAbs (wet) < 4.0f);
        GF_EXPECT_MSG (std::abs (wet.getSample (0, 0)) < 1.0e-3f, "non-silent first sample, card " + juce::String (card));
        GF_EXPECT_MSG (lastAbove (wet, 1.0e-4f) < s.lengthSamples, "output past the voice end, card " + juce::String (card));
        GF_EXPECT_MSG (maxSampleJump (wet) < 4.0f, "click, card " + juce::String (card));
    }
}

GF_TEST (Fx, tail_flushed_within_bound_and_panic_stops_every_card)
{
    VoiceRig rig;
    FillVoice voice;
    voice.prepare (kRigSr, 2);

    auto s = rig.setup (Algo::Freeze, 600.0, 31);
    s.fxStart.settings.ratchet.amount01 = 1.0f;
    s.fxStart.settings.tape.amount01 = 1.0f;
    s.fxStart.settings.rcrush.amount01 = 1.0f;

    const auto stopAt = 4800;
    const auto fade = (int) (kRigSr * 0.004);
    const auto wet = rig.render (voice, s, 30000, 32, nullptr, stopAt, fade);

    GF_EXPECT (! voice.isActive());
    GF_EXPECT (allFinite (wet));
    GF_EXPECT_MSG (lastAbove (wet, 1.0e-5f) <= stopAt + fade + 8, "fx cards kept sounding after the panic fade");
    GF_EXPECT (maxSampleJump (wet) < 4.0f);
}

GF_TEST (Fx, bus_chain_publishes_duck_and_fill_starts_only_while_a_card_is_enabled)
{
    StubBus stub;
    int fills = 0;
    renderWith (kBusyFill, &stub, &fills);

    GF_EXPECT (fills > 0);
    GF_EXPECT (stub.callCount() > 0);
    GF_EXPECT (stub.sawDuck);
    GF_EXPECT (stub.minDuck >= 0.0f && stub.minDuck < 0.99f);    // protection engaged around the hits
    GF_EXPECT (stub.maxStarts >= 1);                             // BusFxContext carries the fills started this block
    GF_EXPECT (stub.firstStartPos >= 0);

    StubBus off;
    off.on = false;
    renderWith (kBusyFill, &off, nullptr);
    GF_EXPECT (off.callCount() == 0);
    GF_EXPECT (! off.sawDuck);
}

GF_TEST (Fx, chain_fadeOut_reaches_every_bus_card)
{
    fx::FxChain chain;
    StubBus a, b;
    chain.addBusCard (&a);
    chain.addBusCard (&b);
    chain.prepare (kSr, 512, 2);
    chain.fadeOut (200);
    GF_EXPECT (a.fadeCalls == 1 && b.fadeCalls == 1);
}

GF_TEST (Fx, worst_case_cpu_is_recorded)
{
    GapFillAudioProcessor proc;
    setParams (proc, { { "fill", 100.0f }, { "density", 100.0f }, { "ratAmount", 100.0f },
                       { "tdAmount", 100.0f }, { "rcAmount", 100.0f } });
    TestPlayHead ph;
    ph.bpm = kBpm;
    RenderOptions o;
    o.playHead = &ph;
    renderOffline (proc, drumLoop (kSr, kBpm, 4), 512, kSr, o);

    float worst = 0.0f;

    for (int i = 0; i < fx::kNumCards; ++i)
        worst = std::max (worst, proc.getFxMeters().cpuWorstMicros[i].load());

    GF_EXPECT (worst > 0.0f);
    GF_EXPECT (worst < 20000.0f);        // a 32-sample voice block never takes 20 ms
}

GF_TEST (Fx, audio_thread_does_not_allocate_with_every_voice_effect_on)
{
    GapFillAudioProcessor proc;
    TestPlayHead playHead;
    playHead.bpm = kBpm;
    setParams (proc, { { "fill", 90.0f }, { "density", 100.0f }, { "complexity", 80.0f },
                       { "swellAmount", 100.0f }, { "cloudAmount", 100.0f },
                       { "ratAmount", 100.0f }, { "tdAmount", 100.0f }, { "tdSplice", 80.0f },
                       { "rcAmount", 100.0f }, { "rcHold", 5.0f }, { "bandLowFx", 0.0f } });

    const auto input = drumLoop (kSr, kBpm, 3);
    prepareProcessor (proc, kSr, 512, false);
    proc.setPlayHead (&playHead);

    juce::AudioBuffer<float> work (2, 512);
    juce::MidiBuffer midi;
    long allocations = 0;
    int guarded = 0;

    for (int pos = 0; pos + 512 <= input.getNumSamples(); pos += 512)
    {
        for (int ch = 0; ch < 2; ++ch)
            work.copyFrom (ch, 0, input, ch, pos, 512);

        if (pos >= (int) kSr)
        {
            AllocGuard guard;
            proc.processBlock (work, midi);
            allocations += guard.count();
            ++guarded;
        }
        else
        {
            proc.processBlock (work, midi);
        }

        playHead.advance (512, kSr);
    }

    proc.setPlayHead (nullptr);
    GF_EXPECT (guarded > 20);
    GF_EXPECT_MSG (allocations == 0, "audio thread allocated " + juce::String (allocations) + " times");
}

GF_TEST (Fx, every_effect_off_renders_exactly_like_the_defaults)
{
    int fills = 0;
    const auto reference = renderWith (kBusyFill, nullptr, &fills);
    GF_EXPECT (fills > 0);

    // All Amounts at 0, every other effect control moved: nothing may change.
    const auto moved = renderWith({ { "fill", 100.0f }, { "density", 100.0f }, { "complexity", 70.0f }, { "intensity", 70.0f },
                                    { "swellAmount", 0.0f }, { "swellCurve", 3.0f }, { "swellFilter", 100.0f }, { "swellTarget", 2.0f },
                                    { "cloudAmount", 0.0f }, { "cloudDensity", 80.0f }, { "cloudSize", 250.0f }, { "cloudPitch", 1200.0f },
                                    { "ratAmount", 0.0f }, { "ratDiv", 8.0f }, { "ratRepeats", 16.0f }, { "ratAccel", 100.0f },
                                    { "rcAmount", 0.0f }, { "rcBits", 1.0f }, { "rcRate", 64.0f }, { "rcHold", 12.0f }, { "rcTone", 100.0f },
                                    { "tdAmount", 0.0f }, { "tdWow", 100.0f }, { "tdFlutter", 100.0f }, { "tdSplice", 100.0f } });

    GF_EXPECT (buffersIdentical (reference, moved));
}

GF_TEST (Fx, fx_subtree_roundtrips_and_a_default_state_writes_nothing)
{
    GapFillAudioProcessor fresh;
    juce::MemoryBlock block;
    fresh.getStateInformation (block);

    auto hasFx = [] (const juce::MemoryBlock& b)
    {
        if (auto xml = juce::AudioProcessor::getXmlFromBinary (b.getData(), (int) b.getSize()))
            return xml->getChildByName ("Fx") != nullptr;

        return false;
    };

    GF_EXPECT (! hasFx (block));

    GapFillAudioProcessor a;
    a.getFxUiState().folded[(int) fx::CardId::Ratchet].store (false);
    a.getFxUiState().rememberedAmount[(int) fx::CardId::Ratchet].store (62.5f);
    a.getFxUiState().rememberedAmount[(int) fx::CardId::RCrush].store (30.0f);
    juce::MemoryBlock saved;
    a.getStateInformation (saved);
    GF_EXPECT (hasFx (saved));
    GF_EXPECT (saved.getSize() < 64 * 1024);

    GapFillAudioProcessor b;
    b.setStateInformation (saved.getData(), (int) saved.getSize());
    GF_EXPECT (! b.getFxUiState().folded[(int) fx::CardId::Ratchet].load());
    GF_EXPECT_NEAR (b.getFxUiState().rememberedAmount[(int) fx::CardId::Ratchet].load(), 62.5f, 1.0e-4);
    GF_EXPECT_NEAR (b.getFxUiState().rememberedAmount[(int) fx::CardId::RCrush].load(), 30.0f, 1.0e-4);
    GF_EXPECT (b.getFxUiState().folded[(int) fx::CardId::Swell].load());

    // Loading a default state resets the fold state again.
    b.setStateInformation (block.getData(), (int) block.getSize());
    GF_EXPECT (b.getFxUiState().isDefault());

    // Fail-soft: a damaged <Fx> subtree is rejected, everything else loads.
    juce::ValueTree bad ("Fx");
    juce::ValueTree card ("Card");
    card.setProperty ("id", "nonsense", nullptr);
    bad.appendChild (card, nullptr);
    GF_EXPECT (! fx::validateFxTree (bad));

    juce::ValueTree range ("Fx");
    juce::ValueTree c2 ("Card");
    c2.setProperty ("id", "ratchet", nullptr);
    c2.setProperty ("amount", 900.0, nullptr);
    range.appendChild (c2, nullptr);
    GF_EXPECT (! fx::validateFxTree (range));
}

//==============================================================================
// F-L14 Multiband effect routing
//==============================================================================
GF_TEST (BandFx, all_on_is_the_default_and_changes_nothing)
{
    const auto a = renderWith (kBusyFill);
    const auto b = renderWith({ { "fill", 100.0f }, { "density", 100.0f }, { "complexity", 70.0f }, { "intensity", 70.0f },
                                { "bandLowFx", 1.0f }, { "bandMidFx", 1.0f }, { "bandHighFx", 1.0f } });
    GF_EXPECT (buffersIdentical (a, b));
}

GF_TEST (BandFx, flag_off_skips_voice_effects_for_that_band)
{
    const std::initializer_list<std::pair<const char*, float>> spectral
        = { { "detMode", 2.0f }, { "fill", 100.0f }, { "density", 100.0f }, { "complexity", 70.0f },
            { "bandLowProb", 100.0f }, { "bandMidProb", 100.0f }, { "bandHighProb", 100.0f } };

    int fills = 0;
    const auto off = renderWith (spectral, nullptr, &fills);
    GF_EXPECT (fills > 0);

    auto withRat = [&] (float low, float mid, float high)
    {
        return renderWith ({ { "detMode", 2.0f }, { "fill", 100.0f }, { "density", 100.0f }, { "complexity", 70.0f },
                             { "bandLowProb", 100.0f }, { "bandMidProb", 100.0f }, { "bandHighProb", 100.0f },
                             { "ratAmount", 100.0f }, { "rcAmount", 100.0f }, { "rcBits", 3.0f },
                             { "bandLowFx", low }, { "bandMidFx", mid }, { "bandHighFx", high } });
    };

    const auto allOn = withRat (1.0f, 1.0f, 1.0f);
    const auto allOff = withRat (0.0f, 0.0f, 0.0f);

    GF_EXPECT (! buffersIdentical (allOn, off));         // the effects did something
    GF_EXPECT (buffersIdentical (allOff, off));          // ...and every band voice bypassed them
}

GF_TEST (BandFx, two_bus_sum_equals_single_bus_when_no_bus_effect_runs)
{
    const auto single = renderWith (kBusyFill);
    const auto flagged = renderWith({ { "fill", 100.0f }, { "density", 100.0f }, { "complexity", 70.0f }, { "intensity", 70.0f },
                                      { "bandLowFx", 0.0f }, { "bandMidFx", 0.0f }, { "bandHighFx", 0.0f } });
    GF_EXPECT (buffersIdentical (single, flagged));
}

GF_TEST (BandFx, bus_input_contains_only_the_selected_bands)
{
    auto energy = [] (float lowFx)
    {
        StubBus stub;
        int fills = 0;
        const auto out = renderWith ({ { "detMode", 2.0f }, { "fill", 100.0f }, { "density", 100.0f }, { "complexity", 70.0f },
                                       { "bandLowProb", 100.0f }, { "bandMidOn", 0.0f }, { "bandHighOn", 0.0f },
                                       { "bandLowFx", lowFx } }, &stub, &fills);
        return std::make_tuple (stub.inputEnergy, (double) regionEnergy (out, 0, out.getNumSamples()), fills);
    };

    const auto [onBus, onOut, onFills] = energy (1.0f);
    const auto [offBus, offOut, offFills] = energy (0.0f);

    GF_EXPECT (onFills > 0 && offFills > 0);
    GF_EXPECT (onBus > 0.0);
    GF_EXPECT_MSG (offBus < onBus * 1.0e-6, "bus saw the bypassed band: " + juce::String (offBus));
    GF_EXPECT (offOut > 0.0);                            // the plain bus is still summed into the output
}

GF_TEST (BandFx, second_bus_is_used_only_when_needed)
{
    fx::FxChain chain;
    StubBus stub;
    chain.addBusCard (&stub);
    chain.prepare (kSr, 512, 2);

    fx::FxSettings s;
    GF_EXPECT (! chain.needsSplit (s));                       // all bands on
    GF_EXPECT (chain.beginBlock (s, 256).plainWet == nullptr);

    s.bandFx.low = false;
    GF_EXPECT (chain.needsSplit (s));                         // a band is off AND a bus card is enabled
    auto io = chain.beginBlock (s, 256);
    GF_EXPECT (io.plainWet != nullptr);

    stub.on = false;
    GF_EXPECT (! chain.needsSplit (s));                       // no bus card: single bus again
    GF_EXPECT (chain.beginBlock (s, 256).plainWet == nullptr);
    GF_EXPECT (chain.preallocatedBytes() >= (std::size_t) (2 * 512 * sizeof (float)));
}

GF_TEST (BandFx, crossover_parameters_and_routing_flags_are_independent)
{
    fx::BandFxSettings b;
    GF_EXPECT (b.allOn());
    GF_EXPECT (b.forBand (-1));                               // broadband voices always use effects
    b.low = false;
    GF_EXPECT (! b.forBand (0) && b.forBand (1) && b.forBand (2) && b.forBand (-1));
}
