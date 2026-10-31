// Pure scene-boundary preflight. Production integration must split before
// source history/analysis and then process each segment exactly once.

#include "../support/TestRig.h"
#include "features/scenes/SceneOverlay.h"

#include <cmath>

using namespace gapfill;
using namespace gapfill::scenes;
using namespace gftest;

GF_TEST (SceneSplit, staging_does_not_apply_or_advance_and_offset_is_a_pure_query)
{
    initTables();
    initOverlayTargets();
    TripleBuffer<Overlay> pub;
    SceneEngine engine;
    Overlay o;
    o.id = 42;
    o.n = 1;
    o.idx[0] = (std::uint16_t) paramIndexOf ("fmtAmount");
    o.real[0] = 50.0f;
    o.fixedBoundary = 1;
    o.targetPpq = 8.0;
    pub.publish (o);
    TransportSnap t;
    t.valid = true;
    t.ppq = 7.99;
    t.samplesPerBeat = 24000.0;
    t.numSamples = 512;
    const auto staged = engine.stagePublished (pub, t);
    GF_EXPECT (staged.appliedId == 0 && staged.missedDeadlineId == 0 && engine.hasPending());
    const auto split = engine.samplesUntilBoundary (t);
    GF_EXPECT (split == 240);
    GF_EXPECT (engine.samplesUntilBoundary (t) == split && engine.hasPending());
    t.numSamples = split;
    GF_EXPECT (engine.beginChunk (pub, t).appliedId == 0);
    t.ppq = 8.0;
    t.numSamples = 512 - split;
    GF_EXPECT (engine.beginChunk (pub, t).appliedId == 42);
    EngineSettings s;
    engine.view().applyTo (s);
    GF_EXPECT_NEAR (s.fx.formant.amount01, 0.5, 1e-6);
}

GF_TEST (SceneSplit, split_boundary_matches_the_same_absolute_sample_for_variable_host_blocks)
{
    for (int block : { 32, 64, 512, 8192 })
    {
        TripleBuffer<Overlay> pub;
        SceneEngine engine;
        Overlay o;
        o.id = 1;
        o.fixedBoundary = 1;
        o.targetPpq = 4.0;
        pub.publish (o);
        int splitSample = -1;
        for (int pos = 0; pos <= 96000; pos += block)
        {
            TransportSnap t;
            t.valid = true;
            t.ppq = (double) pos / 24000.0;
            t.samplesPerBeat = 24000.0;
            t.numSamples = block;
            engine.stagePublished (pub, t);
            const auto offset = engine.samplesUntilBoundary (t);
            if (offset < block)
            {
                splitSample = pos + offset;
                break;
            }
            engine.beginChunk (pub, t);
        }
        GF_EXPECT_MSG (splitSample == 96000, "host block " + juce::String (block));
    }
}

GF_TEST (SceneSplit, preflight_keeps_the_generation_cancellable_until_its_first_boundary_sample)
{
    GapFillAudioProcessor p;
    auto& ctl = p.getLiveControl().scenes();
    Scene scene;
    scene.name = "Group";
    GF_EXPECT (ctl.getBank().store (0, scene));
    GF_EXPECT (ctl.gotoSceneAt (0, 8.0, 3, 17));
    SceneEngine engine;
    TransportSnap t;
    t.valid = true;
    t.ppq = std::nextafter (7.99, 8.0);
    t.samplesPerBeat = 24000.0;
    t.numSamples = 512;
    engine.stagePublished (ctl.publisher(), t);
    const auto offset = engine.samplesUntilBoundary (t);
    GF_EXPECT (offset == 240);
    t.numSamples = offset;
    GF_EXPECT (engine.beginChunk (ctl.publisher(), t).appliedId == 0);
    GF_EXPECT (engine.hasPending() && engine.pendingGroup() == 3 && engine.pendingCancelEpoch() == 17);
    engine.cancelPending(); // a newer group cancel epoch arrived between segments
    t.ppq += (double) offset / t.samplesPerBeat;
    t.numSamples = 512 - offset;
    engine.stagePublished (ctl.publisher(), t);
    GF_EXPECT (engine.beginChunk (ctl.publisher(), t).appliedId == 0);
}

GF_TEST (SceneSplit, effect_projection_preserves_all_unrelated_effective_settings)
{
    OverlayView view;
    view.n = 3;
    view.refs[0] = targets::sceneRefForId ("fill");
    view.real[0] = 90;
    view.refs[1] = targets::sceneRefForId ("fmtAmount");
    view.real[1] = 75;
    view.refs[2] = targets::sceneRefForId ("fl20_shift");
    view.real[2] = -7;
    EngineSettings s;
    s.fill01 = .2f;
    s.perf.energy.adapt01 = .3f;
    view.applyFxTo (s);
    GF_EXPECT_NEAR (s.fill01, .2, 1e-6);
    GF_EXPECT_NEAR (s.perf.energy.adapt01, .3, 1e-6);
    GF_EXPECT_NEAR (s.fx.formant.amount01, .75, 1e-6);
    GF_EXPECT_NEAR (s.voiceFormant.shiftSt, -7, 1e-6);
}
