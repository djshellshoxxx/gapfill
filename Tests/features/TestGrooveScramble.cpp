#include "../support/AllocGuard.h"
#include "../support/TestRig.h"
#include "../support/VoiceRig.h"
#include "features/context/ContextHooks.h"
#include "features/groove/GrooveWarp.h"
#include "dsp/ScrambleTiming.h"

using namespace gapfill;
using namespace gapfill::groove;
using namespace gftest;

namespace
{
    GrooveShifts shiftedBoundaries()
    {
        GrooveSnapshot snapshot;
        snapshot.valid = true;
        snapshot.confidence = 1.0f;
        for (int i = 0; i < kSlots; ++i) snapshot.offsetSteps[i] = i % 2 ? 0.3f : 0.0f;
        GrooveShifts shifts;
        makeShifts (snapshot, 1, 0.0, 0.25, 24000.0, 1.0f, shifts);
        return shifts;
    }

    VoiceSetup scrambleSetup (const VoiceRig& rig, const GrooveShifts& shifts)
    {
        auto setup = rig.setup (Algo::Scramble, 600.0, 31);
        setup.modSyncSeconds = 0.125f;
    setup.sinceEventSamples = setup.lengthSamples;
        setup.grooveShifts = shifts;
        return setup;
    }
}

GF_TEST (GrooveScramble, model_two_context_latches_synced_shifts_and_legacy_stays_straight)
{
    EngineSettings settings;
    auto& groove = settings.perf.groove;
    groove.amount01 = 1.0f;
    groove.syncOn = true;
    groove.snap.valid = true;
    groove.snap.confidence = 1.0f;
    for (int i = 0; i < kSlots; ++i) groove.snap.offsetSteps[i] = i % 2 ? 0.3f : 0.0f;
    settings.transportValid = true;
    settings.samplesPerBeat = 24000.0;
    GapContext ctx;
    FillPlan plan;
    VoiceSetup setup;
    setup.algo = Algo::Scramble;
    setup.sampleRate = 48000.0;
    setup.lengthSamples = 28800;
    setup.sinceEventSamples = 28800;
    setup.modSyncSeconds = 0.125f;

    groove.modelVersion = 2;
    context::prepareVoiceSetup (setup, plan, ctx, settings, SourceHistory(), nullptr);
    GF_EXPECT (setup.grooveShifts.on && setup.grooveShifts.at (1) == 1800);
    for (int mode = 0; mode < 4; ++mode)
    {
        auto s = settings;
        auto voice = setup;
        voice.grooveShifts = {};
        if (mode == 0) s.perf.groove.modelVersion = 1;
        if (mode == 1) s.perf.groove.syncOn = false;
        if (mode == 2) s.perf.groove.amount01 = 0.0f;
        if (mode == 3) voice.modSyncSeconds = 0.0f;
        context::prepareVoiceSetup (voice, plan, ctx, s, SourceHistory(), nullptr);
        GF_EXPECT (! voice.grooveShifts.on);
    }
}

GF_TEST (GrooveScramble, measured_slice_starts_follow_shifted_boundaries_without_moving_fill_start)
{
    VoiceRig rig;
    rig.fillDc (1.0f);
    auto shifts = shiftedBoundaries();
    FillVoice straight, moved;
    straight.prepare (48000.0, 2);
    moved.prepare (48000.0, 2);
    const auto plainSetup = scrambleSetup (rig, {});
    const auto shiftedSetup = scrambleSetup (rig, shifts);
    const auto plain = rig.render (straight, plainSetup, plainSetup.lengthSamples + 100, 64);
    const auto wet = rig.render (moved, shiftedSetup, shiftedSetup.lengthSamples + 100, 64);
    const auto firstAudible = [] (const juce::AudioBuffer<float>& audio)
    {
        for (int i = 0; i < audio.getNumSamples(); ++i)
            if (std::abs (audio.getSample (0, i)) > 1.0e-5f) return i;
        return -1;
    };
    GF_EXPECT (firstAudible (plain) == firstAudible (wet));
    GF_EXPECT (std::abs (plain.getSample (0, 6000)) < 1.0e-6f);
    GF_EXPECT (std::abs (wet.getSample (0, 6000)) > 0.1f);
    for (int k = 1; k <= 4; ++k)
    {
        const auto boundary = k * 6000 + shifts.at (k);
        GF_EXPECT (std::abs (wet.getSample (0, boundary)) < 1.0e-6f);
        GF_EXPECT (std::abs (wet.getSample (0, boundary + 100)) > 0.1f);
    }
    GF_EXPECT (lastAbove (wet, 1.0e-5f) < shiftedSetup.lengthSamples);
}

GF_TEST (GrooveScramble, off_zero_warp_block_sizes_and_allocation_contracts)
{
    VoiceRig rig;
    auto shifts = shiftedBoundaries();
    GrooveShifts disabled = shifts;
    disabled.on = false;
    GrooveShifts zero;
    zero.on = true;
    zero.count = kMaxShifts;
    auto render = [&] (const GrooveShifts& warp, int block)
    {
        FillVoice voice;
        voice.prepare (48000.0, 2);
        const auto setup = scrambleSetup (rig, warp);
        return rig.render (voice, setup, setup.lengthSamples, block);
    };
    const auto plain = render ({}, 64);
    GF_EXPECT (buffersIdentical (plain, render (disabled, 64)));
    GF_EXPECT (buffersIdentical (plain, render (zero, 64)));
    const auto shifted = render (shifts, 64);
    for (const auto block : { 1, 13, 1000 })
        GF_EXPECT (buffersIdentical (shifted, render (shifts, block)));

    FillVoice voice;
    voice.prepare (48000.0, 2);
    const auto setup = scrambleSetup (rig, shifts);
    juce::AudioBuffer<float> wet (2, setup.lengthSamples);
    wet.clear();
    AllocGuard guard;
    GF_EXPECT (voice.start (setup, rig.history, &rig.spectral));
    voice.render (wet, 0, wet.getNumSamples(), rig.history);
    GF_EXPECT (guard.count() == 0);
}

GF_TEST (GrooveScramble, clamped_period_and_both_early_and_late_boundaries_follow_actual_timing)
{
    VoiceRig rig;
    rig.fillDc (1.0f);
    for (const auto offset : { -0.3f, 0.3f })
    {
        EngineSettings settings;
        settings.transportValid = true;
        settings.samplesPerBeat = 24000.0;
        auto& groove = settings.perf.groove;
        groove.modelVersion = 2;
        groove.amount01 = 1.0f;
        groove.snap.valid = true;
        groove.snap.confidence = 1.0f;
        for (int i = 0; i < kSlots; ++i) groove.snap.offsetSteps[i] = i % 2 ? offset : 0.0f;
        auto setup = rig.setup (Algo::Scramble, 600.0, 31);
        setup.modSyncSeconds = 0.125f;
        const auto period = voiceTiming::syncedScrambleSliceForPlan (setup.lengthSamples, setup.sinceEventSamples,
                                                                    setup.modSyncSeconds, setup.sampleRate);
        GF_EXPECT (period == 2976); // requested125ms, source-limited to62ms
        GapContext ctx;
        FillPlan plan;
        context::prepareVoiceSetup (setup, plan, ctx, settings, rig.history, nullptr);
        GF_EXPECT (setup.grooveShifts.on);
        GF_EXPECT (std::abs (setup.grooveShifts.at (1) - (offset > 0.0f ? 893 : -893)) <= 1);
        FillVoice voice;
        voice.prepare (48000.0, 2);
        const auto wet = rig.render (voice, setup, setup.lengthSamples, 64);
        GF_EXPECT (std::abs (wet.getSample (0, period)) > 0.1f);
        for (int k = 1; k <= 4; ++k)
        {
            const auto boundary = k * period + setup.grooveShifts.at (k);
            GF_EXPECT (std::abs (wet.getSample (0, boundary)) < 1.0e-6f);
            GF_EXPECT (std::abs (wet.getSample (0, boundary + 100)) > 0.1f);
        }
    }
}
