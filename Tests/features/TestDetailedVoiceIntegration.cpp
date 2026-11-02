#include "../support/TestRig.h"
#include "../support/VoiceRig.h"
#include "../support/AllocGuard.h"
#include "features/capture2/CaptureScan.h"
#include "features/harmonic/HarmonicHooks.h"
#include "features/harmonic/psola/PitchSourceRouter.h"
#include "features/formant/FormantPool.h"

using namespace gapfill;
using namespace gftest;
namespace ps = gapfill::harmonic::psola;

namespace
{
    void sineHistory (VoiceRig& rig, double sr)
    {
        rig.history.clear();
        juce::AudioBuffer<float> input (2, (int) sr);
        for (int i = 0; i < input.getNumSamples(); ++i)
            for (int ch = 0; ch < 2; ++ch)
                input.setSample (ch, i, 0.2f * std::sin ((float) (6.283185307179586 * 220 * i / sr)));
        rig.history.write (input, 0, input.getNumSamples());
        rig.anchor = rig.history.getWritePosition();
    }
}

GF_TEST (DetailedVoice, capture_scan_is_neutral_at_zero_and_stays_inside_the_real_window)
{
    for (const auto sr : { 8000.0, 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        VoiceRig rig (sr);
        const auto end = rig.history.getWritePosition();
        const int window = (int) (sr * 2), slice = (int) std::ceil (sr * 0.32);
        rig.history.setReadFloor (end - window);
        GF_EXPECT (capture::scannedAnchor (-17, rig.history, true, 0, window, slice, 1) == -17);
        GF_EXPECT (capture::scannedAnchor (end, rig.history, false, 1, window, slice, 1) == end);
        bool varied = false;
        const auto first = capture::scannedAnchor (end, rig.history, true, 1, window, slice, 0);
        for (std::uint64_t seed = 0; seed < 1000; ++seed)
        {
            const auto a = capture::scannedAnchor (end, rig.history, true, 1, window, slice, seed);
            GF_EXPECT (a == capture::scannedAnchor (end, rig.history, true, 1, window, slice, seed));
            GF_EXPECT (a <= end && a - slice >= rig.history.getReadFloor());
            varied = varied || a != first;
        }
        GF_EXPECT (varied);
    }
}

GF_TEST (DetailedVoice, pitch_hook_uses_source_coordinates_and_preserves_absent_legacy_replay)
{
    VoiceRig rig;
    sineHistory (rig, 48000);
    ps::PitchSourceRouter router;
    router.prepare (48000);
    router.setActive (true);
    float mono[512];
    for (int first = 0; first < 48000; first += 512)
    {
        const auto n = std::min (512, 48000 - first);
        for (int i = 0; i < n; ++i) mono[i] = 0.2f * std::sin ((float) (6.283185307179586 * 220 * (first + i) / 48000));
        router.push (mono, n, { first, first, true, false, {}, -1, 0 });
    }
    EngineSettings settings;
    settings.perf.pitchSync.enabled = true;
    settings.perf.pitchTracker = &router;
    auto setup = rig.setup (Algo::Granular, 250);
    FillPlan plan;
    GapContext ctx;
    harmonic::decide (setup, plan, ctx, settings, rig.history, nullptr);
    GF_EXPECT (setup.pitchSync.voiced && setup.recipeExtras.harmonic.outcome == ps::Outcome::Used);
    GF_EXPECT (std::abs (setup.pitchSync.f0Hz - 220) < 2);
    auto legacy = rig.setup (Algo::Granular, 250);
    legacy.replayRecipe = true;
    harmonic::decide (legacy, plan, ctx, settings, rig.history, nullptr);
    GF_EXPECT (! legacy.pitchSync.settings.enabled && legacy.recipeExtras.harmonic.version == 0);
    settings.perf.pitchSync.mix01 = 0;
    auto muted = rig.setup (Algo::Granular, 250);
    harmonic::decide (muted, plan, ctx, settings, rig.history, nullptr);
    GF_EXPECT (muted.recipeExtras.harmonic.outcome == ps::Outcome::Bypassed && ! muted.pitchSync.voiced);
    setup.replayRecipe = true;
    settings.perf.pitchSync.enabled = false;
    harmonic::decide (setup, plan, ctx, settings, rig.history, nullptr);
    GF_EXPECT (setup.pitchSync.settings.enabled && setup.recipeExtras.harmonic.outcome == ps::Outcome::Used);
}

GF_TEST (DetailedVoice, pitch_voice_borrows_its_private_capture_and_finishes_at_the_deadline)
{
    for (const auto sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        VoiceRig rig (sr);
        sineHistory (rig, sr);
        ps::PitchMarks marks;
        marks.prepare (sr);
        auto voice = std::make_unique<FillVoice>();
        voice->prepare (sr, 2);
        auto setup = rig.setup (Algo::Granular, 250, 71, sr);
        setup.sourceChannels = 1;
        setup.recipeSeed = 71;
        setup.pitchSync = { {}, {}, 220, 1, true };
        setup.pitchSync.settings.enabled = true;
        setup.pitchSync.settings.intervals = ps::IntervalSet::Major;
        setup.pitchSync.settings.voices = 4;
        setup.recipeExtras.harmonic = { 1, ps::Outcome::Used, (std::uint8_t) ps::IntervalSet::Major, 4, 220, 1 };
        auto wet = makeSilence (2, setup.lengthSamples + 1024);
        long allocations = -1;
        bool started = false;
        {
            AllocGuard guard;
            started = voice->start (setup, rig.history, &rig.spectral, nullptr, &marks);
            allocations = guard.count();
        }
        GF_EXPECT (started && allocations == 0);
        GF_EXPECT (voice->getRecipeExtras().harmonic.outcome == ps::Outcome::Used);
        GF_EXPECT (! voice->readsLiveHistory());
        // Overwrite the shared source after start: PSOLA must keep its own PCM.
        rig.history.clear();
        voice->render (wet, 0, wet.getNumSamples(), rig.history);
        GF_EXPECT (allFinite (wet) && regionEnergy (wet, 0, setup.lengthSamples) > 0.001f);
        GF_EXPECT (! voice->isActive() && lastAbove (wet, 1e-8f) < setup.lengthSamples);
    }
}

GF_TEST (DetailedVoice, detailed_zero_send_and_epoch_failure_use_plain_granular)
{
    VoiceRig rig;
    auto plain = std::make_unique<FillVoice>();
    auto detailed = std::make_unique<FillVoice>();
    plain->prepare (48000, 2);
    detailed->prepare (48000, 2);
    auto a = rig.setup (Algo::Granular, 250);
    auto b = a;
    b.pitchSync.settings.enabled = true;
    b.pitchSync.settings.mix01 = 0;
    b.recipeExtras.harmonic = { 1, ps::Outcome::Bypassed, 1, 2, 0, 0 };
    const auto expected = rig.render (*plain, a, 16000);
    const auto actual = rig.render (*detailed, b, 16000);
    GF_EXPECT (buffersIdentical (expected, actual));
    rig.fillDc (0);
    ps::PitchMarks marks;
    marks.prepare (48000);
    b.pitchSync.settings.mix01 = 1;
    b.pitchSync.voiced = true;
    b.pitchSync.f0Hz = 220;
    b.pitchSync.clarity = 1;
    b.recipeExtras.harmonic = { 1, ps::Outcome::Used, 1, 2, 220, 1 };
    GF_EXPECT (detailed->start (b, rig.history, &rig.spectral, nullptr, &marks));
    GF_EXPECT (detailed->getRecipeExtras().harmonic.outcome == ps::Outcome::EpochFallback);
}

GF_TEST (DetailedVoice, tape_is_inserted_inside_the_voice_and_drains_before_the_formant_deadline)
{
    for (const auto sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        VoiceRig rig (sr);
        auto voice = std::make_unique<FillVoice>();
        voice->prepare (sr, 2);
        formant::FormantPool pool;
        pool.prepare (sr);
        auto setup = rig.setup (Algo::Stutter, 300, 42, sr);
        setup.fxStart.settings.tapeExtra.amount01 = 1;
        setup.formantSetup.enabled = true;
        setup.formantSetup.amount01 = 1;
        setup.formantSetup.shiftSt = 7;
        auto* formant = pool.request (setup.formantSetup, setup.lengthSamples, (int) (sr * 0.04));
        GF_EXPECT (formant != nullptr);
        auto wet = makeSilence (2, setup.lengthSamples + 4096);
        bool started = false;
        long allocations = -1;
        {
            AllocGuard guard;
            started = voice->start (setup, rig.history, &rig.spectral, formant);
            voice->render (wet, 0, wet.getNumSamples(), rig.history);
            allocations = guard.count();
        }
        GF_EXPECT (started && allocations == 0 && allFinite (wet));
        GF_EXPECT (voice->getRecipeExtras().tape.chosen == 1);
        GF_EXPECT (! voice->isActive() && ! formant->isClaimed());
        GF_EXPECT (lastAbove (wet, 1e-8f) < setup.lengthSamples);
    }
}

GF_TEST (DetailedVoice, host_tail_reads_current_automation_without_an_audio_callback)
{
    auto proc = std::make_unique<GapFillAudioProcessor>();
    GF_EXPECT (proc->getTailLengthSeconds() == 2.5);
    setParams (*proc, { { "dubAmount", 100 }, { "fsdAmount", 100 } });
    GF_EXPECT (proc->getTailLengthSeconds() >= 2.5 + 16 + 4);
    prepareProcessor (*proc, 192000, 512, false);
    GF_EXPECT (proc->getTailLengthSeconds() >= 2.5 + 16 + 4);
    setParams (*proc, { { "dubAmount", 0 }, { "fsdAmount", 0 } });
    GF_EXPECT (proc->getTailLengthSeconds() == 2.5);
}

GF_TEST (DetailedVoice, recorded_tape_respects_current_send_and_band_routing_without_rewriting_the_recipe)
{
    VoiceRig rig;
    auto voice = std::make_unique<FillVoice>();
    auto plain = std::make_unique<FillVoice>();
    voice->prepare (48000, 2);
    plain->prepare (48000, 2);
    auto setup = rig.setup (Algo::Stutter, 300);
    auto selected = setup;
    selected.fxStart.settings.tapeExtra.amount01 = 1;
    selected.fxStart.settings.tapeExtra.mix01 = .7f;
    selected.recipeReplaySend01 = .5f;
    GF_EXPECT (voice->start (selected, rig.history, &rig.spectral));
    const auto recorded = voice->getRecipeExtras();
    GF_EXPECT (recorded.tape.chosen == 1 && recorded.tape.values[tapedamage::Mix] == 7000);
    for (const auto bandSkip : { false, true })
    {
        selected.replayRecipe = true;
        selected.recipeExtras = recorded;
        selected.recipeReplaySend01 = bandSkip ? 1 : 0;
        selected.fxStart.skipVoiceFx = bandSkip;
        const auto expected = rig.render (*plain, setup, 18000);
        const auto actual = rig.render (*voice, selected, 18000);
        GF_EXPECT (buffersIdentical (expected, actual));
        GF_EXPECT (voice->getRecipeExtras() == recorded);
    }
}

GF_TEST (DetailedVoice, production_tape_path_changes_audio_and_keeps_host_latency_and_fill_activity)
{
    auto plain = std::make_unique<GapFillAudioProcessor>();
    auto detailed = std::make_unique<GapFillAudioProcessor>();
    for (auto* proc : { plain.get(), detailed.get() })
        setParams (*proc, { { "fill", 100 }, { "density", 100 }, { "complexity", 0 }, { "chaos", 0 }, { "fillType", 2 } });
    setParams (*detailed, { { "fm20_amount", 100 }, { "fm20_wow", 100 }, { "fm20_mix", 100 } });
    const auto input = drumLoop (48000, 150, 3);
    const auto expected = renderOffline (*plain, input, 512, 48000).out;
    const auto actual = renderOffline (*detailed, input, 512, 48000).out;
    GF_EXPECT (allFinite (actual) && ! buffersIdentical (expected, actual));
    GF_EXPECT (plain->getFillEngine().getFillsStarted() == detailed->getFillEngine().getFillsStarted());
    GF_EXPECT (detailed->getFillEngine().getFillsStarted() > 0);
    GF_EXPECT (plain->getActiveLatencySamples() == detailed->getActiveLatencySamples());
}
