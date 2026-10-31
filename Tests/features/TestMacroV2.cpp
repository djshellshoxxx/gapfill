#include "../support/AllocGuard.h"
#include "../support/LiveRig.h"
#include "features/macros/MacroModel.h"
#include "features/scenes/SceneOverlay.h"
#include "features/state/StateSubtrees.h"
#include "features/bank/BankFile.h"

using namespace gapfill;
using namespace gapfill::macros;
using namespace gftest;

namespace
{
    MacroConfig onlyTarget (int target, std::uint8_t schema = 2)
    {
        auto c = defaultConfig();
        for (auto& count : c.count) count = 0;
        c.targetSchemaVersion = schema;
        c.count[0] = 1;
        c.map[0][0] = {};
        c.map[0][0].target = (std::uint8_t) target;
        return c;
    }

    float targetValue (const EngineSettings& s, int target)
    {
        switch (target)
        {
            case 21: return s.fx.swell.amount01; case 22: return s.fx.cloud.amount01;
            case 23: return s.fx.ratchet.amount01; case 24: return s.fx.rcrush.amount01;
            case 25: return s.fx.tape.amount01; case 26: return s.fx.resonator.amount01;
            case 27: return s.fx.fsdelay.amount01; case 28: return s.fx.dub.amount01;
            case 29: return s.fx.formant.amount01; case 30: return s.fx.spfreeze.amount01;
            case 31: return s.fx.convo.amount01; case 32: return s.perf.tension.depth[0];
            case 33: return s.perf.energy.adapt01; case 34: return s.scaleLock.amount01;
            case 35: return s.perf.groove.amount01; default: return -1;
        }
    }

    juce::ValueTree legacyTree()
    {
        juce::ValueTree tree (MacroModel::legacySubtreeName);
        tree.setProperty ("v", 1, nullptr);
        juce::ValueTree child ("M");
        child.setProperty ("i", 0, nullptr);
        child.setProperty ("n", "Old swell", nullptr);
        child.setProperty ("m", "21,0,1000,0,0,0", nullptr);
        tree.appendChild (child, nullptr);
        return tree;
    }
}

GF_TEST (MacroV2, all_shipped_advanced_targets_drive_their_real_fields_and_claim_distinct_bits)
{
    initTargets();
    for (int target = 21; target < kNumTargets; ++target)
    {
        MacroRuntime rt;
        rt.cfg = onlyTarget (target);
        EngineSettings s;
        s.perf.tension.depth[0] = 1.0f;
        std::uint32_t owned = 0;
        const float knobs[kNumMacros] = { 0.75f, 0, 0, 0 };
        rt.evaluateBlock (s, knobs, owned);
        GF_EXPECT_MSG (std::abs (targetValue (s, target) - 0.75f) < 1e-6f, targetInfo (target).id);
        GF_EXPECT (owned == 0);
        GF_EXPECT (s.perf.advancedMacroOwnedMask == (1u << (target - kNumC2Targets)));
    }
}

GF_TEST (MacroV2, legacy_restore_resave_and_explicit_upgrade_preserve_old_advanced_noop)
{
    initTargets();
    MacroModel model;
    model.restore (legacyTree());
    GF_EXPECT (model.get().targetSchemaVersion == 1);
    auto saved = model.toValueTree();
    GF_EXPECT (saved.hasType (MacroModel::subtreeName));
    GF_EXPECT ((int) saved["v"] == 2 && (int) saved["targets"] == 1);
    MacroModel reopened;
    reopened.restore (saved);
    GF_EXPECT (reopened.get().targetSchemaVersion == 1);
    MacroRuntime rt;
    rt.cfg = reopened.get();
    EngineSettings s;
    s.fx.swell.amount01 = 0.2f;
    std::uint32_t owned = 0;
    const float knobs[kNumMacros] = { 0.75f, 0, 0, 0 };
    rt.evaluateBlock (s, knobs, owned);
    GF_EXPECT_NEAR (s.fx.swell.amount01, 0.2, 1e-6);
    GF_EXPECT (s.perf.advancedMacroOwnedMask == 0);
    GF_EXPECT (reopened.upgradeTargetSchema());
    GF_EXPECT (! reopened.upgradeTargetSchema());
    GF_EXPECT (juce::String (reopened.get().names[0]) == "Old swell");
    rt.cfg = reopened.get();
    rt.evaluateBlock (s, knobs, owned);
    GF_EXPECT_NEAR (s.fx.swell.amount01, 0.75, 1e-6);
}

GF_TEST (MacroV2, unsupported_versions_and_malformed_tuples_are_rejected_atomically)
{
    MacroModel model;
    model.rename (0, "Keep");
    auto tree = model.toValueTree();
    tree.setProperty ("v", 3, nullptr);
    GF_EXPECT (! MacroModel::validateTree (tree));
    model.restore (tree);
    GF_EXPECT (juce::String (model.get().names[0]) == "Keep");
    tree = model.toValueTree();
    tree.setProperty ("targets", 3, nullptr);
    GF_EXPECT (! MacroModel::validateTree (tree));
    auto old = legacyTree();
    old.setProperty ("v", 2, nullptr);
    GF_EXPECT (! MacroModel::validateTree (old));
    for (const char* bad : { "21,--1,1000,0", "21,0,1000,-0-1", "21,0,1000,0;" })
    {
        tree = model.toValueTree();
        tree.getChild (0).setProperty ("m", bad, nullptr);
        GF_EXPECT_MSG (! MacroModel::validateTree (tree), bad);
    }
}

GF_TEST (MacroV2, tension_amount_scales_configured_depths_without_cumulative_decision_scaling)
{
    initTargets();
    MacroRuntime rt;
    rt.cfg = onlyTarget (32);
    rt.cfg.lfo[0] = { 1, 3, 1, (std::uint8_t) Division::Quarter, 1, 1, 0 };
    EngineSettings s;
    const float nominal[4] = { .8f, .6f, .4f, .2f };
    for (int lane = 0; lane < 4; ++lane) s.perf.tension.depth[lane] = nominal[lane];
    const float knobs[kNumMacros] = { 0, 0, 0, 0 };
    std::uint32_t owned = 0;
    rt.evaluateBlock (s, knobs, owned);
    for (double ppq : { .75, .25, .75, .25 })
    {
        rt.evaluateDecision (s, ppq);
        const float factor = ppq < .5 ? 1.0f : 0.0f;
        for (int lane = 0; lane < 4; ++lane)
            GF_EXPECT_NEAR (s.perf.tension.depth[lane], nominal[lane] * factor, 1e-6);
    }
}

GF_TEST (MacroV2, advanced_lfo_and_scenes_respect_macro_ownership_until_the_mapping_is_removed)
{
    initTargets();
    MacroRuntime rt;
    rt.cfg = onlyTarget (21);
    rt.cfg.lfo[0] = { 1, 3, 1, (std::uint8_t) Division::Quarter, 1, 1, 0 };
    EngineSettings s;
    std::uint32_t owned = 0;
    const float knobs[kNumMacros] = { .5f, 0, 0, 0 };
    rt.evaluateBlock (s, knobs, owned);
    scenes::OverlayView scene;
    scene.n = 1;
    scene.refs[0] = targets::sceneRefForId ("swellAmount");
    scene.real[0] = 25;
    rt.evaluateDecision (s, .25);
    scene.applyTo (s);
    GF_EXPECT_NEAR (s.fx.swell.amount01, 1.0, 1e-6);
    rt.cfg.count[0] = 0;
    rt.evaluateBlock (s, knobs, owned);
    scene.applyTo (s);
    GF_EXPECT_NEAR (s.fx.swell.amount01, .25, 1e-6);
    GF_EXPECT (s.perf.advancedMacroOwnedMask == 0);
}

GF_TEST (MacroV2, virtual_target_availability_resolves_real_registered_controls)
{
    GapFillAudioProcessor proc;
    for (int target = 0; target < kNumTargets; ++target)
    {
        GF_EXPECT (proc.getValueTreeState().getParameter (targetParameterId (target)) != nullptr);
        GF_EXPECT_MSG (proc.getLiveControl().macroMappings().isTargetAvailable (target), targetInfo (target).id);
    }
}

GF_TEST (MacroV2, cached_audio_evaluation_remains_allocation_free)
{
    initTargets();
    MacroRuntime rt;
    rt.cfg = onlyTarget (29);
    EngineSettings s;
    std::uint32_t owned = 0;
    const float knobs[kNumMacros] = { .75f, 0, 0, 0 };
    AllocGuard guard;
    for (int i = 0; i < 200; ++i)
    {
        rt.evaluateBlock (s, knobs, owned);
        rt.evaluateDecision (s, i * .25);
    }
    GF_EXPECT (guard.count() == 0);
}

GF_TEST (MacroV2, current_valid_subtree_wins_and_invalid_current_falls_back_to_valid_legacy)
{
    MacroModel current;
    current.rename (0, "New engine");
    juce::ValueTree root ("GAPFILL");
    root.appendChild (legacyTree(), nullptr);
    root.appendChild (current.toValueTree(), nullptr);
    GapFillAudioProcessor proc;
    state::applyPlan (proc, state::planSubtrees (root, state::reservedSubtrees()));
    GF_EXPECT (juce::String (proc.getLiveControl().macroMappings().get().names[0]) == "New engine");
    root.getChildWithName (MacroModel::subtreeName).setProperty ("v", 99, nullptr);
    state::applyPlan (proc, state::planSubtrees (root, state::reservedSubtrees()));
    GF_EXPECT (juce::String (proc.getLiveControl().macroMappings().get().names[0]) == "Old swell");
    GF_EXPECT (proc.getLiveControl().macroMappings().get().targetSchemaVersion == 1);
    root.removeChild (root.getChildWithName (MacroModel::subtreeName), nullptr);
    GF_EXPECT (state::writeSubtrees (proc, root, state::reservedSubtrees()).ok);
    GF_EXPECT (! root.getChildWithName (MacroModel::legacySubtreeName).isValid());
    GF_EXPECT ((int) root.getChildWithName (MacroModel::subtreeName)["targets"] == 1);
}

GF_TEST (MacroV2, optional_portable_bank_macros_keep_their_engine_schema_and_validation)
{
    scenes::initTables();
    scenes::SceneBank scenes;
    scenes::Scene scene;
    scene.name = "One";
    scene.params.push_back ({ (std::uint16_t) scenes::paramIndexOf ("fill"), .5f });
    GF_EXPECT (scenes.store (0, scene));
    MacroModel model;
    model.restore (legacyTree());
    bank::ExportOptions options;
    options.includeMacros = true;
    options.macros = model.toValueTree();
    const auto text = bank::exportToString (scenes, options, "test", "2026-10-31");
    const auto parsed = bank::parseString (text);
    GF_EXPECT (parsed.status == bank::ParseStatus::Ok);
    GF_EXPECT (parsed.macros.hasType (MacroModel::subtreeName));
    MacroModel reopened;
    reopened.restore (parsed.macros);
    GF_EXPECT (reopened.get().targetSchemaVersion == 1);
    options.macros.setProperty ("v", 3, nullptr);
    GF_EXPECT (bank::exportToString (scenes, options, "test", "2026-10-31").isEmpty());
}

GF_TEST (MacroV2, maximum_escaped_names_mappings_and_lfos_survive_project_save_and_reload)
{
    GapFillAudioProcessor proc;
    auto& model = proc.getLiveControl().macroMappings();
    for (int macro = 0; macro < kNumMacros; ++macro)
        while (model.get().count[macro] > 0) GF_EXPECT (model.removeMapping (macro, 0));
    for (int macro = 0; macro < kNumMacros; ++macro)
    {
        GF_EXPECT (model.rename (macro, juce::String::repeatedString ("\"", kNameBytes)));
        GF_EXPECT (model.setLfo (macro, { 1, 4, 1, (std::uint8_t) ((int) Division::NumDivisions - 1), 20, 1, 360 }));
        for (int mapping = 0; mapping < kMaxMappings; ++mapping)
        {
            Mapping target;
            target.target = (std::uint8_t) (4 + macro * kMaxMappings + mapping);
            target.min01 = target.max01 = 1;
            target.curve = 3;
            target.smoothMs = 500;
            target.bipolar = 1;
            GF_EXPECT (model.addMapping (macro, target) == MacroModel::AddResult::Ok);
        }
    }
    const auto expected = model.toValueTree();
    GF_EXPECT (MacroModel::validateTree (expected));
    GF_EXPECT_MSG (state::serialisedSize (expected) <= 2 * 1024, "escaped maximum macro XML exceeds its budget");
    juce::ValueTree tree ("GAPFILL");
    GF_EXPECT (state::writeSubtrees (proc, tree, state::reservedSubtrees()).ok);
    GF_EXPECT (tree.getChildWithName (MacroModel::subtreeName).isEquivalentTo (expected));
    juce::MemoryBlock saved;
    proc.getStateInformation (saved);
    GF_EXPECT (saved.getSize() > 0);
    GapFillAudioProcessor restored;
    restored.setStateInformation (saved.getData(), (int) saved.getSize());
    GF_EXPECT (restored.getLiveControl().macroMappings().toValueTree().isEquivalentTo (expected));
}
