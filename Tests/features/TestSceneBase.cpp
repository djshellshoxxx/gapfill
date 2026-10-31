#include "../support/AllocGuard.h"
#include "../support/LiveRig.h"
#include "features/scenes/SceneOverlay.h"
#include "features/FeatureHooks.h"
#include "features/common/PerformanceTargets.h"

#include <algorithm>

using namespace gapfill;
using namespace gapfill::scenes;
using namespace gftest;

namespace
{
    bool contains (const std::vector<juce::String>& ids, const char* id)
    { return std::find (ids.begin(), ids.end(), id) != ids.end(); }

    void add (OverlayView& view, const char* id, float real)
    {
        const auto i = view.n++;
        view.refs[i] = targets::sceneRefForId (id);
        view.real[i] = real;
    }
}

GF_TEST (SceneBase, released_scope_lists_are_frozen_and_new_shaping_does_not_expand_old_recall)
{
    GF_EXPECT (idsForScope (kDefaultScope, 1).size() == 128);
    GF_EXPECT (idsForScope (kAllScopes, 1).size() == 158);
    for (const char* id : { "pingPong", "fl01_lock", "personality", "mistakes", "fillShape", "fillShapeAmount", "evolve" })
    {
        GF_EXPECT_MSG (contains (idsForScope (ScopeCreative), id), id);
        GF_EXPECT_MSG (! contains (idsForScope (kAllScopes, 1), id), id);
    }
    for (const char* id : { "bandLowType", "bandMidType", "bandHighType" })
    {
        GF_EXPECT (contains (idsForScope (ScopeBands), id));
        GF_EXPECT (! contains (idsForScope (kAllScopes, 1), id));
    }
    for (const char* id : { "dry", "wet", "output", "bypass", "lookahead", "seed", "wetSolo", "wetSafety", "qualityMode" })
        GF_EXPECT (! contains (idsForScope (kAllScopes), id));
}

GF_TEST (SceneBase, every_admitted_scene_id_has_a_destination_in_its_actual_dsp_stage)
{
    for (const auto& id : idsForScope (kAllScopes))
        GF_EXPECT_MSG ((bool) targets::sceneRefForId (id.toRawUTF8()), "No scene destination: " + id);
}

GF_TEST (SceneBase, raw_scene_precedes_personality_and_macros_and_is_not_reapplied_at_decision)
{
    macros::initTargets();
    macros::MacroRuntime runtime;
    runtime.cfg = macros::defaultConfig();
    OverlayView view;
    view.active = view.immediate = true;
    add (view, "fill", 20);
    add (view, "density", 40);
    add (view, "personality", 3);
    add (view, "macrosOn", 1);
    add (view, "macroActivity", 100);
    EngineSettings s;
    Effective::ModulationInputs inputs;
    inputs.macros.runtime = &runtime;
    view.applyBaseTo (s, inputs);
    GF_EXPECT (s.perf.sceneBaseApplied && inputs.personality == 3 && inputs.macros.on);
    Effective::MacroOutputs out;
    Effective::applyBlock (s, inputs, out);
    GF_EXPECT_NEAR (s.fill01, .75, 1e-6);
    GF_EXPECT_NEAR (s.density01, .90, 1e-6);
    s.perf.sceneView = &view;
    Effective::TransportInfo transport;
    transport.valid = true;
    Effective::applyScene (s, 8, transport);
    GF_EXPECT_NEAR (s.fill01, .75, 1e-6);
    GF_EXPECT_NEAR (s.density01, .90, 1e-6);
    view.scopeVersion = 1;
    EngineSettings legacy;
    Effective::ModulationInputs unchanged;
    view.applyBaseTo (legacy, unchanged);
    GF_EXPECT (! legacy.perf.sceneBaseApplied && unchanged.personality == 0 && ! unchanged.macros.on);
}

GF_TEST (SceneBase, shaping_musical_tempo_and_post_wet_values_reach_their_real_units)
{
    OverlayView view;
    add (view, "pingPong", 0); add (view, "fl01_lock", 1); add (view, "mistakes", 75);
    add (view, "fillShape", 4); add (view, "fillShapeAmount", 65); add (view, "evolve", 3);
    add (view, "bandLowType", 12); add (view, "phraseBars", 4); add (view, "phraseOffset", 7);
    add (view, "densityBreath", 1); add (view, "densityBreathBars", 3); add (view, "densityBreathDepth", 60);
    add (view, "fm21_bars", 4); add (view, "fm21_depthFx", 42); add (view, "fl10_calls", 4);
    add (view, "fl09_grid", 0); add (view, "fl09_hold", 1); add (view, "fx03_window", 24);
    add (view, "fillBudget", 6); add (view, "battleEvery", 4);
    add (view, "maxFillSync", 1); add (view, "maxFillDiv", (float) Division::Half);
    add (view, "protLow", 250); add (view, "protCenter", 55);
    add (view, "scaleRoot", 2); add (view, "scaleType", 2);
    EngineSettings s;
    Effective::ModulationInputs inputs;
    view.applyBaseTo (s, inputs);
    GF_EXPECT (! s.pingPong && s.perf.lockOn && s.perf.envShape == 4 && s.perf.evolve.periodBars == 32);
    GF_EXPECT_NEAR (s.perf.mistakes01, .75, 1e-6); GF_EXPECT_NEAR (s.perf.envAmount01, .65, 1e-6);
    GF_EXPECT (s.perf.bandType[0] == 12 && s.perf.phrase.bars == 16 && s.perf.phrase.offsetBars == 7);
    GF_EXPECT (s.perf.breath.on && s.perf.breath.bars == 8);
    GF_EXPECT_NEAR (s.perf.breath.depth01, .6, 1e-6);
    GF_EXPECT (s.perf.tension.bars == 32 && s.perf.call.calls == 4);
    GF_EXPECT_NEAR (s.perf.tension.depth[tension::FxSend], .42, 1e-6);
    GF_EXPECT (s.perf.groove.grid == 0 && s.perf.groove.hold && s.perf.key.windowSeconds == 24);
    GF_EXPECT (s.perf.budget.perBar == 8 && s.perf.battleEvery == 8);
    GF_EXPECT (s.perf.sceneMaxFillSync == 1 && s.perf.sceneMaxFillDivision == (int) Division::Half);
    GF_EXPECT_NEAR (s.perf.sceneProtectLowHz, 250, 1e-6);
    GF_EXPECT_NEAR (s.perf.sceneProtectCenter01, .55, 1e-6);
    const auto expectedScale = scale::makeScaleSet (2, 2);
    GF_EXPECT (s.scaleLock.set.root == expectedScale.root && s.scaleLock.set.type == expectedScale.type && s.scaleLock.set.mask == expectedScale.mask);
}

GF_TEST (SceneBase, detector_and_crossover_projection_happens_before_analysis_and_is_legacy_inert)
{
    OverlayView view;
    add (view, "detMode", 2); add (view, "detSource", 1); add (view, "sensitivity", 77);
    add (view, "gapThresh", 43); add (view, "gapRelease", 120); add (view, "retrigGuard", 99);
    add (view, "protectHold", 88); add (view, "xoverLM", 2000); add (view, "xoverMH", 2500);
    AnalysisSettings analysis;
    view.applyAnalysisTo (analysis);
    GF_EXPECT ((int) analysis.mode == 2 && (int) analysis.source == 1);
    GF_EXPECT_NEAR (analysis.sensitivity01, .77, 1e-6); GF_EXPECT_NEAR (analysis.gapThresh01, .43, 1e-6);
    GF_EXPECT_NEAR (analysis.gapReleaseMs, 120, 1e-6); GF_EXPECT_NEAR (analysis.retrigGuardMs, 99, 1e-6);
    GF_EXPECT_NEAR (analysis.protectHoldMs, 88, 1e-6);
    GF_EXPECT_NEAR (analysis.xoverLowMid, 2000, 1e-6); GF_EXPECT_NEAR (analysis.xoverMidHigh, 3000, 1e-6);
    view.scopeVersion = 1;
    AnalysisSettings legacy;
    view.applyAnalysisTo (legacy);
    GF_EXPECT_NEAR (legacy.sensitivity01, .5, 1e-6);
}

GF_TEST (SceneBase, cached_stage_projections_allocate_nothing)
{
    OverlayView view;
    add (view, "fm21_bars", 4); add (view, "detMode", 2); add (view, "macroActivity", 75);
    EngineSettings settings;
    Effective::ModulationInputs inputs;
    AnalysisSettings analysis;
    AllocGuard guard;
    for (int i = 0; i < 256; ++i) { view.applyBaseTo (settings, inputs); view.applyAnalysisTo (analysis); }
    GF_EXPECT (guard.count() == 0);
}
