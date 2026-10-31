#include "../support/TestRig.h"
#include "../support/VoiceRig.h"
#include "features/formant/FormantPool.h"

using namespace gapfill;
using namespace gapfill::formant;
using namespace gftest;

namespace
{
    FormantSetup active (float shift = 0.0f)
    {
        FormantSetup setup;
        setup.enabled = true;
        setup.amount01 = 1.0f;
        setup.shiftSt = shift;
        return setup;
    }
}

GF_TEST (Formant, off_path_keeps_plugin_audio_bit_identical_and_claims_nothing)
{
    auto baseline = std::make_unique<GapFillAudioProcessor>();
    auto disabled = std::make_unique<GapFillAudioProcessor>();
    setParams (*disabled, { { "fl20_shift", 12 }, { "fl20_tilt", -6 }, { "fl20_vowel", 5 }, { "fl20_amount", 0 } });
    const auto input = drumLoop (48000.0, 150.0, 2);
    const auto a = renderOffline (*baseline, input, 512, 48000.0).out;
    const auto b = renderOffline (*disabled, input, 512, 48000.0).out;
    GF_EXPECT (buffersIdentical (a, b));
    GF_EXPECT (disabled->getFillEngine().getFormantPool().claimCount() == 0);
}

GF_TEST (Formant, enabled_stage_is_scheduled_without_changing_plugin_latency)
{
    auto plain = std::make_unique<GapFillAudioProcessor>();
    auto processed = std::make_unique<GapFillAudioProcessor>();
    for (auto* proc : { plain.get(), processed.get() })
        setParams (*proc, { { "fill", 100 }, { "density", 100 }, { "complexity", 0 }, { "chaos", 0 } });
    setParams (*processed, { { "fl20_amount", 100 }, { "fl20_shift", 7 } });
    const auto input = drumLoop (48000.0, 150.0, 3);
    const auto a = renderOffline (*plain, input, 512, 48000.0).out;
    const auto b = renderOffline (*processed, input, 512, 48000.0).out;
    GF_EXPECT (plain->getActiveLatencySamples() == processed->getActiveLatencySamples());
    GF_EXPECT (processed->getFillEngine().getFormantPool().claimCount() > 0);
    GF_EXPECT (allFinite (b) && ! buffersIdentical (a, b));
}

GF_TEST (Formant, voice_drains_inside_planned_deadline_and_panic_releases_pool)
{
    VoiceRig rig;
    FormantPool pool;
    auto voice = std::make_unique<FillVoice>();
    voice->prepare (48000.0, 2);
    pool.prepare (48000.0);
    auto setup = rig.setup (Algo::Stutter, 300.0);
    setup.formantSetup = active (7.0f);
    auto* processor = pool.request (setup.formantSetup, setup.lengthSamples, 1920);
    GF_EXPECT (voice->start (setup, rig.history, &rig.spectral, processor));
    GF_EXPECT (voice->getFormantInputSamples() == setup.lengthSamples - 512);
    auto wet = makeSilence (2, setup.lengthSamples + 1024);
    voice->render (wet, 0, 4096, rig.history);
    voice->requestStop (192);
    voice->render (wet, 4096, wet.getNumSamples() - 4096, rig.history);
    GF_EXPECT (! voice->isActive() && ! processor->isClaimed());
    GF_EXPECT (lastAbove (wet, 1.0e-6f) < 4096 + 192);
}

GF_TEST (Formant, voice_prepare_and_destruction_release_pool_ownership)
{
    VoiceRig rig;
    FormantPool pool;
    pool.prepare (48000.0);
    auto voice = std::make_unique<FillVoice>();
    voice->prepare (48000.0, 2);
    auto setup = rig.setup (Algo::Stutter, 300.0);
    setup.formantSetup = active (7.0f);
    auto* first = pool.request (setup.formantSetup, setup.lengthSamples, 1920);
    GF_EXPECT (voice->start (setup, rig.history, &rig.spectral, first));
    voice->prepare (96000.0, 2);
    GF_EXPECT (! first->isClaimed());
    pool.prepare (96000.0);
    setup.sampleRate = 96000.0;
    auto* second = pool.request (setup.formantSetup, setup.lengthSamples, 1920);
    GF_EXPECT (voice->start (setup, rig.history, &rig.spectral, second));
    voice.reset();
    GF_EXPECT (! second->isClaimed());
}

GF_TEST (Formant, parameter_state_round_trips_without_changing_bus_card)
{
    auto source = std::make_unique<GapFillAudioProcessor>();
    auto restored = std::make_unique<GapFillAudioProcessor>();
    setParams (*source, { { "fmtAmount", 37 }, { "fmtShift", -4 }, { "fl20_amount", 80 },
                         { "fl20_shift", 7 }, { "fl20_tilt", 2 }, { "fl20_vowel", 3 },
                         { "fl20_preserve", 24 }, { "fl20_detail", 2 } });
    juce::MemoryBlock saved;
    source->getStateInformation (saved);
    restored->setStateInformation (saved.getData(), (int) saved.getSize());
    for (const char* id : { "fmtAmount", "fmtShift", "fl20_amount", "fl20_shift", "fl20_tilt",
                            "fl20_vowel", "fl20_preserve", "fl20_detail" })
        GF_EXPECT_NEAR (source->getValueTreeState().getRawParameterValue (id)->load(),
                        restored->getValueTreeState().getRawParameterValue (id)->load(), 1.0e-6);
}
