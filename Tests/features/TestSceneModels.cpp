#include "../support/LiveRig.h"
#include "features/bank/BankFile.h"
#include "features/modelversions/State.h"
#include "features/effective/Effective.h"

using namespace gapfill;
using namespace gapfill::scenes;
using namespace gftest;

namespace
{
    void setVersions (juce::AudioProcessorValueTreeState& apvts, models::ModelVersions versions)
    {
        apvts.state.removeChild (apvts.state.getChildWithName (models::kTreeType), nullptr);
        apvts.state.appendChild (models::toModelVersionsTree (versions), nullptr);
    }
}

GF_TEST (SceneModels, cloud_model_is_captured_with_fx_scope_and_missing_legacy_metadata_selects_one)
{
    GapFillAudioProcessor p;
    auto& state = p.getValueTreeState();
    setVersions (state, { 2, 2, 1 });
    const auto captured = captureScene (state, "New cloud", ScopeFx);
    GF_EXPECT (captured.cloudModelVersion == 2);
    const auto excluded = captureScene (state, "Creative only", ScopeCreative);
    GF_EXPECT (excluded.cloudModelVersion == 1);
    SceneBank bank;
    GF_EXPECT (bank.store (0, captured));
    auto tree = bank.toValueTree (kMaxSceneStateBytes);
    tree.getChild (0).removeProperty ("cm", nullptr);
    SceneBank missing;
    missing.restore (tree);
    GF_EXPECT (missing.size() == 1 && missing.at (0).cloudModelVersion == 1);
    tree.getChild (0).setProperty ("cm", 3, nullptr);
    GF_EXPECT (! SceneBank::validateTree (tree));
    missing.restore (tree);
    GF_EXPECT (missing.size() == 1 && missing.at (0).cloudModelVersion == 1);
}

GF_TEST (SceneModels, project_and_portable_bank_roundtrip_and_digest_cover_cloud_model)
{
    Scene scene;
    scene.name = "Cloud 2";
    scene.scope = ScopeFx;
    scene.cloudModelVersion = 2;
    SceneBank bank;
    GF_EXPECT (bank.store (0, scene));
    SceneBank restored;
    restored.restore (bank.toValueTree (kMaxSceneStateBytes));
    GF_EXPECT (restored.size() == 1 && restored.at (0) == scene);
    const auto text = bank::exportToString (bank, {}, "test", "2026-10-31");
    auto report = bank::parseString (text);
    GF_EXPECT (report.status == bank::ParseStatus::Ok && ! report.digestMismatch);
    GF_EXPECT (report.accepted[0].cloudModelVersion == 2);
    report = bank::parseString (text.replace ("cloudModel=\"2\"", "cloudModel=\"1\""));
    GF_EXPECT (report.status == bank::ParseStatus::Ok && report.digestMismatch);
}

GF_TEST (SceneModels, fixed_boundary_projects_cloud_version_and_keeps_unrelated_domains)
{
    initTables(); initOverlayTargets();
    TripleBuffer<Overlay> pub;
    Overlay overlay;
    overlay.id = 1;
    overlay.fixedBoundary = 1;
    overlay.targetPpq = 8;
    overlay.modelDomains = ModelCloud;
    overlay.cloudModelVersion = 2;
    pub.publish (overlay);
    SceneEngine engine;
    TransportSnap t;
    t.valid = true; t.ppq = 7.99; t.samplesPerBeat = 24000; t.numSamples = 512;
    engine.stagePublished (pub, t);
    EngineSettings s;
    s.fx.cloud.modelVersion = 1;
    GF_EXPECT (! engine.view().appliesAt (t.ppq, true));
    t.numSamples = engine.samplesUntilBoundary (t);
    GF_EXPECT (engine.beginChunk (pub, t).appliedId == 0);
    t.ppq = 8; t.numSamples = 272;
    GF_EXPECT (engine.beginChunk (pub, t).appliedId == 1);
    GF_EXPECT (engine.view().appliesAt (t.ppq, true));
    engine.view().applyFxTo (s);
    GF_EXPECT (s.fx.cloud.modelVersion == 2);
}

GF_TEST (SceneModels, message_commit_changes_only_cloud_and_notifies_the_owner_once)
{
    GapFillAudioProcessor p;
    auto& state = p.getValueTreeState();
    setVersions (state, { 2, 2, 1 });
    SceneController controller (state);
    GF_EXPECT (controller.saveCurrent (0, "Cloud", ScopeFx));
    setVersions (state, { 1, 1, 2 });
    int callbacks = 0;
    controller.setModelVersionsCommittedCallback ([&] { ++callbacks; });
    GF_EXPECT (controller.gotoScene (0));
    const auto request = controller.getPendingId();
    controller.onApplied (request);
    const auto got = models::fromModelVersionsTree (state.state.getChildWithName (models::kTreeType));
    GF_EXPECT (got.cloud == 2 && got.taste == 1 && got.arrangement == 2);
    GF_EXPECT (callbacks == 1);
    controller.onApplied (request);
    GF_EXPECT (callbacks == 1);
}

GF_TEST (SceneModels, a_manual_model_upgrade_removes_only_the_pending_model_overlay)
{
    GapFillAudioProcessor p;
    auto& state = p.getValueTreeState();
    setVersions (state, { 1, 1, 1 });
    SceneController controller (state);
    GF_EXPECT (controller.saveCurrent (0, "Old cloud", ScopeFx));
    GF_EXPECT (controller.gotoScene (0));
    setVersions (state, { 2, 2, 2 });
    controller.poll();
    Overlay published;
    GF_EXPECT (controller.publisher().consume (published));
    GF_EXPECT (published.modelDomains == 0 && published.n > 0);
    controller.onApplied (controller.getPendingId());
    const auto got = models::fromModelVersionsTree (state.state.getChildWithName (models::kTreeType));
    GF_EXPECT (got.cloud == 2 && got.taste == 2 && got.arrangement == 2);
}

GF_TEST (SceneModels, groove_domain_roundtrips_and_relinquishes_only_its_manual_change)
{
    GapFillAudioProcessor p;
    auto& state = p.getValueTreeState();
    setVersions (state, { 2, 1, 2, 2 });
    const auto scene = captureScene (state, "Both", ScopeFx | ScopeMusical);
    GF_EXPECT (scene.cloudModelVersion == 2 && scene.grooveModelVersion == 2);
    GF_EXPECT (captureScene (state, "FX", ScopeFx).grooveModelVersion == 1);
    SceneBank saved;
    GF_EXPECT (saved.store (0, scene));
    const auto text = bank::exportToString (saved, {}, "test", "2026-10-31");
    auto report = bank::parseString (text);
    GF_EXPECT (report.status == bank::ParseStatus::Ok && ! report.digestMismatch);
    GF_EXPECT (report.accepted[0] == scene);
    report = bank::parseString (text.replace ("grooveModel=\"2\"", "grooveModel=\"1\""));
    GF_EXPECT (report.status == bank::ParseStatus::Ok && report.digestMismatch);
    auto tree = saved.toValueTree (kMaxSceneStateBytes);
    tree.getChild (0).removeProperty ("gm", nullptr);
    SceneBank old;
    old.restore (tree);
    GF_EXPECT (old.size() == 1 && old.at (0).grooveModelVersion == 1);
    tree.getChild (0).setProperty ("gm", 3, nullptr);
    GF_EXPECT (! SceneBank::validateTree (tree));
    SceneController controller (state);
    GF_EXPECT (controller.saveCurrent (0, "Both", ScopeFx | ScopeMusical));
    setVersions (state, { 1, 2, 1, 1 });
    GF_EXPECT (controller.gotoScene (0));
    setVersions (state, { 1, 2, 1, 2 });
    controller.poll();
    Overlay published;
    GF_EXPECT (controller.publisher().consume (published));
    GF_EXPECT (published.modelDomains == ModelCloud);
    int callbacks = 0;
    controller.setModelVersionsCommittedCallback ([&] { ++callbacks; });
    controller.onApplied (controller.getPendingId());
    GF_EXPECT ((models::fromModelVersionsTree (state.state.getChildWithName (models::kTreeType)) == models::ModelVersions { 2, 2, 1, 2 }));
    GF_EXPECT (callbacks == 1);
}

GF_TEST (SceneModels, legacy_scope_projects_legacy_groove_at_the_exact_boundary_before_modulation)
{
    TripleBuffer<Overlay> pub;
    Overlay overlay;
    overlay.id = 9;
    overlay.fixedBoundary = 1;
    overlay.targetPpq = 8;
    overlay.scopeVersion = 1;
    overlay.modelDomains = ModelGroove;
    overlay.grooveModelVersion = 1;
    pub.publish (overlay);
    SceneEngine engine;
    TransportSnap t;
    t.valid = true; t.ppq = 7.99; t.samplesPerBeat = 24000; t.numSamples = 512;
    engine.stagePublished (pub, t);
    GF_EXPECT (! engine.view().appliesAt (t.ppq, true));
    t.numSamples = engine.samplesUntilBoundary (t);
    GF_EXPECT (engine.beginChunk (pub, t).appliedId == 0);
    t.ppq = 8; t.numSamples = 272;
    GF_EXPECT (engine.beginChunk (pub, t).appliedId == 9);
    EngineSettings settings;
    settings.perf.groove.modelVersion = 2;
    Effective::ModulationInputs inputs;
    engine.view().applyBaseTo (settings, inputs);
    GF_EXPECT (settings.perf.groove.modelVersion == 1);
    GF_EXPECT (! settings.perf.sceneBaseApplied);
}
