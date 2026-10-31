// Scene schema v2: complete recall, portable payloads and bounded lossless persistence.

#include "../support/LiveRig.h"

#include "features/bank/BankFile.h"
#include "features/scenes/SceneOverlay.h"
#include "features/state/StateSubtrees.h"

#include <algorithm>
#include <cstring>

using namespace gapfill;
using namespace gapfill::scenes;
using namespace gftest;

namespace
{
    juce::ValueTree legacySceneTree (const char* id = nullptr, float value = 0.5f)
    {
        juce::ValueTree root ("Scenes"), slot ("S");
        root.setProperty ("v", 1, nullptr);
        root.setProperty ("q", (int) Quant::Bar, nullptr);
        slot.setProperty ("n", "Released", nullptr);
        slot.setProperty ("sc", (int) kDefaultScope, nullptr);
        slot.setProperty ("q", (int) Quant::Inherit, nullptr);

        if (id != nullptr)
        {
            std::uint8_t data[6] {};
            const auto index = (std::uint16_t) paramIndexOf (id);
            std::memcpy (data, &index, sizeof (index));
            std::memcpy (data + 2, &value, sizeof (value));
            slot.setProperty ("d", juce::Base64::toBase64 (data, sizeof (data)), nullptr);
        }

        root.appendChild (slot, nullptr);
        return root;
    }

    bool resolvedContains (const std::vector<ResolvedParam>& values, const char* id)
    {
        const auto index = paramIndexOf (id);
        return std::any_of (values.begin(), values.end(), [index] (const auto& p) { return p.index == index; });
    }

    Scene denseScene (const juce::String& name)
    {
        Scene out;
        out.name = name;
        out.scope = kAllScopes;

        for (const auto& id : idsForScope (kAllScopes))
            out.params.push_back ({ (std::uint16_t) paramIndexOf (id), 0.375f });

        return out;
    }
}

GF_TEST (SceneFormat, new_scenes_capture_and_overlay_every_harmonic_parameter)
{
    GapFillAudioProcessor p;
    setParams (p, { { "hgAmount", 72.0f }, { "hgVoices", 5.0f }, { "hgMotion", 63.0f }, { "hgTone", -48.0f } });
    const auto captured = captureScene (p.getValueTreeState(), "Harmonic", kDefaultScope);
    std::vector<ResolvedParam> values;
    juce::String why;
    GF_EXPECT (resolveScene (p.getValueTreeState(), captured, values, why));

    Overlay o;
    o.id = 1;
    o.quant = (std::uint8_t) Quant::Immediate;
    o.n = (std::uint16_t) values.size();

    for (size_t i = 0; i < values.size(); ++i)
    {
        o.idx[i] = values[i].index;
        o.real[i] = values[i].real;
    }

    for (const char* id : { "hgAmount", "hgVoices", "hgMotion", "hgTone" })
        GF_EXPECT_MSG (resolvedContains (values, id), id);

    initOverlayTargets();
    TripleBuffer<Overlay> pub;
    pub.publish (o);
    SceneEngine engine;
    TransportSnap transport;
    engine.beginChunk (pub, transport);
    EngineSettings settings;
    engine.view().applyTo (settings);
    GF_EXPECT_NEAR (settings.perf.harmonic.amount01, 0.72, 1e-6);
    GF_EXPECT (settings.perf.harmonic.voices == 5);
    GF_EXPECT_NEAR (settings.perf.harmonic.motion01, 0.63, 1e-6);
    GF_EXPECT_NEAR (settings.perf.harmonic.tone, -0.48, 1e-6);
}

GF_TEST (SceneFormat, released_v1_scenes_restore_without_resetting_new_scope_members)
{
    GapFillAudioProcessor p;
    SceneBank bank;
    const auto released = legacySceneTree ("fill", 0.25f);
    GF_EXPECT (SceneBank::validateTree (released));
    bank.restore (released);
    GF_EXPECT (bank.size() == 1);
    std::vector<ResolvedParam> values;
    juce::String why;
    GF_EXPECT (resolveScene (p.getValueTreeState(), bank.at (0), values, why));
    GF_EXPECT (resolvedContains (values, "fill"));

    for (const char* id : { "hgAmount", "hgVoices", "hgMotion", "hgTone" })
        GF_EXPECT_MSG (! resolvedContains (values, id), id);

    // Saving a migrated scene must preserve its original recall domain.
    const auto saved = bank.toValueTree (256 * 1024);
    SceneBank again;
    again.restore (saved);
    values.clear();
    GF_EXPECT (resolveScene (p.getValueTreeState(), again.at (0), values, why));
    GF_EXPECT (! resolvedContains (values, "hgAmount"));
}

GF_TEST (SceneFormat, current_payload_has_fixed_little_endian_bytes)
{
    SceneBank bank;
    Scene s;
    s.name = "Half";
    const auto index = (std::uint16_t) paramIndexOf ("hgAmount");
    s.params.push_back ({ index, 0.5f });
    GF_EXPECT (bank.store (0, s));
    const auto tree = bank.toValueTree (256 * 1024);
    GF_EXPECT ((int) tree.getProperty ("v") == 2);
    juce::MemoryOutputStream decoded;
    GF_EXPECT (juce::Base64::convertFromBase64 (decoded, tree.getChild (0).getProperty ("d").toString()));
    GF_EXPECT (decoded.getDataSize() == 6);
    const auto* bytes = static_cast<const std::uint8_t*> (decoded.getData());
    GF_EXPECT (bytes[0] == (std::uint8_t) index && bytes[1] == (std::uint8_t) (index >> 8));
    GF_EXPECT (bytes[2] == 0 && bytes[3] == 0 && bytes[4] == 0 && bytes[5] == 0x3f);
}

GF_TEST (SceneFormat, every_dense_scene_survives_project_and_portable_bank_roundtrips)
{
    GapFillAudioProcessor p;
    auto& bank = p.getLiveControl().scenes().getBank();

    for (int i = 0; i < kMaxScenes; ++i)
        GF_EXPECT (bank.store (i, denseScene ("Scene " + juce::String (i + 1))));

    const auto tree = bank.toValueTree (256 * 1024);
    GF_EXPECT (tree.isValid() && tree.getNumChildren() == kMaxScenes);
    GF_EXPECT (SceneBank::validateTree (tree));
    GF_EXPECT (state::serialisedSize (tree) <= 256 * 1024);

    juce::MemoryBlock state;
    p.getStateInformation (state);
    GF_EXPECT (state.getSize() < 1024 * 1024);
    GapFillAudioProcessor restored;
    restored.setStateInformation (state.getData(), (int) state.getSize());
    const auto& savedBank = restored.getLiveControl().scenes().getBank();
    GF_EXPECT (savedBank.size() == kMaxScenes);
    GF_EXPECT (savedBank.canonicalPayload() == bank.canonicalPayload());

    const auto text = bank::exportToString (bank, {}, "test", "2026-10-31T00:00:00Z");
    const auto report = bank::parseString (text);
    GF_EXPECT (report.status == bank::ParseStatus::Ok);
    GF_EXPECT (report.accepted.size() == (size_t) kMaxScenes && report.skipped.empty());
    GF_EXPECT (! report.digestMismatch);
}

GF_TEST (SceneFormat, cap_failure_is_explicit_and_never_returns_a_partial_success)
{
    SceneBank bank;
    GF_EXPECT (bank.store (0, denseScene ("First")));
    GF_EXPECT (bank.store (1, denseScene ("Second")));
    bool overflow = false;
    const auto result = bank.toValueTree (128, &overflow);
    GF_EXPECT (overflow);
    GF_EXPECT (! result.isValid());
    GF_EXPECT (bank.size() == 2);
}

GF_TEST (SceneFormat, unsupported_versions_and_malformed_records_preserve_existing_bank)
{
    SceneBank bank;
    GF_EXPECT (bank.store (0, denseScene ("Existing")));
    const auto before = bank.canonicalPayload();

    for (int version : { 0, -1, 3, 999 })
    {
        auto bad = legacySceneTree ("fill");
        bad.setProperty ("v", version, nullptr);
        GF_EXPECT (! SceneBank::validateTree (bad));
        bank.restore (bad);
        GF_EXPECT (bank.canonicalPayload() == before);
    }

    auto bad = legacySceneTree ("fill");
    bad.getChild (0).setProperty ("d", "AAAA", nullptr); // three bytes, not a whole six-byte record
    GF_EXPECT (! SceneBank::validateTree (bad));
    bank.restore (bad);
    GF_EXPECT (bank.canonicalPayload() == before);
}

GF_TEST (SceneFormat, duplicate_parameter_indices_are_rejected)
{
    auto s = denseScene ("Duplicate");
    s.params.push_back (s.params.front());
    juce::String why;
    GF_EXPECT (! isSceneValid (s, why));
}

GF_TEST (SceneFormat, state_serialisation_reports_failure_without_partially_mutating_the_root)
{
    GapFillAudioProcessor p;
    auto& bank = p.getLiveControl().scenes().getBank();
    GF_EXPECT (bank.store (0, denseScene ("Corrupted after admission")));
    bank.at (0).params[0].value01 = 2.0f;
    juce::ValueTree root ("GAPFILL_STATE");
    root.setProperty ("marker", "untouched", nullptr);
    const auto before = root.createCopy();
    const auto result = state::writeSubtrees (p, root, state::reservedSubtrees());
    GF_EXPECT (! result.ok && result.error.isNotEmpty());
    GF_EXPECT (root.isEquivalentTo (before));
}

GF_TEST (SceneFormat, valid_new_schema_wins_and_invalid_new_schema_falls_back_to_legacy)
{
    GapFillAudioProcessor p;
    juce::ValueTree root ("GAPFILL_STATE");
    root.appendChild (legacySceneTree ("fill", 0.25f), nullptr);
    SceneBank modern;
    GF_EXPECT (modern.store (0, denseScene ("Modern")));
    root.appendChild (modern.toValueTree (256 * 1024), nullptr);

    auto plan = state::planSubtrees (root, state::reservedSubtrees());
    state::applyPlan (p, plan);
    GF_EXPECT (p.getLiveControl().scenes().getBank().at (0).name == "Modern");

    root.getChildWithName (SceneBank::subtreeName).setProperty ("v", 999, nullptr);
    plan = state::planSubtrees (root, state::reservedSubtrees());
    state::applyPlan (p, plan);
    GF_EXPECT (p.getLiveControl().scenes().getBank().at (0).name == "Released");

    // A successful new save removes stale legacy data and cannot make an old
    // reader interpret the new payload as its native-format Scenes subtree.
    const auto write = state::writeSubtrees (p, root, state::reservedSubtrees());
    GF_EXPECT (write.ok);
    GF_EXPECT (! root.getChildWithName (SceneBank::legacySubtreeName).isValid());
    GF_EXPECT (root.getChildWithName (SceneBank::subtreeName).isValid());
}

GF_TEST (SceneFormat, portable_v2_preserves_small_float_values_and_legacy_v1_still_imports)
{
    SceneBank bank;
    Scene s;
    s.name = "Precision";
    s.params.push_back ({ (std::uint16_t) paramIndexOf ("fill"), 1.2345678e-12f });
    GF_EXPECT (bank.store (0, s));
    const auto text = bank::exportToString (bank, {}, "test", "2026-10-31");
    const auto parsed = bank::parseString (text);
    GF_EXPECT (parsed.status == bank::ParseStatus::Ok && ! parsed.digestMismatch);
    GF_EXPECT (std::memcmp (&parsed.accepted[0].params[0].value01, &s.params[0].value01, sizeof (float)) == 0);

    const auto legacy = bank::parseString ("<GapBank formatVersion=\"1\" name=\"Legacy\"><Scene name=\"Old\" "
                                           "scope=\"creative\"><P id=\"fill\" v=\"0.25\"/></Scene></GapBank>");
    GF_EXPECT (legacy.status == bank::ParseStatus::Ok && legacy.accepted.size() == 1);
    GF_EXPECT (legacy.accepted[0].scopeVersion == 1);

    for (const char* malformed : { "1e", "++0.5", "0.2-0.1", ".", "nan", "1.1" })
    {
        const auto broken = bank::parseString ("<GapBank formatVersion=\"1\"><Scene name=\"Bad\">"
                                               "<P id=\"fill\" v=\"" + juce::String (malformed) + "\"/></Scene></GapBank>");
        GF_EXPECT (broken.status == bank::ParseStatus::Empty && broken.skipped.size() == 1);
    }
}

GF_TEST (SceneFormat, group_scene_request_uses_the_published_absolute_boundary)
{
    GapFillAudioProcessor p;
    auto& ctl = p.getLiveControl().scenes();
    GF_EXPECT (ctl.getBank().store (0, denseScene ("Group")));
    GF_EXPECT (ctl.gotoSceneAt (0, 8.0));
    SceneEngine engine;
    TransportSnap transport;
    transport.valid = true;
    transport.ppq = 7.99;
    transport.samplesPerBeat = 24000.0;
    transport.numSamples = 512;
    const auto result = engine.beginChunk (ctl.publisher(), transport);
    GF_EXPECT (result.appliedId == ctl.getPendingId() && result.missedDeadlineId == 0);
    GF_EXPECT_NEAR (engine.view().boundaryPpq, 8.0, 0.0);
    GF_EXPECT (! engine.view().appliesAt (7.999, true));
    GF_EXPECT (engine.view().appliesAt (8.0, true));
}

GF_TEST (SceneFormat, group_scene_late_or_invalid_transport_cancels_without_applying)
{
    for (bool valid : { false, true })
    {
        GapFillAudioProcessor p;
        auto& ctl = p.getLiveControl().scenes();
        GF_EXPECT (ctl.getBank().store (0, denseScene ("Group")));
        GF_EXPECT (ctl.gotoSceneAt (0, 8.0));
        const auto id = ctl.getPendingId();
        SceneEngine engine;
        TransportSnap transport;
        transport.valid = valid;
        transport.ppq = 8.01;
        transport.samplesPerBeat = 24000.0;
        transport.numSamples = 512;
        const auto result = engine.beginChunk (ctl.publisher(), transport);
        GF_EXPECT (result.appliedId == 0 && result.missedDeadlineId == id);
        GF_EXPECT (! engine.hasPending() && ! engine.view().active);
        ctl.rejectPending (id, "Missed group boundary");
        GF_EXPECT (ctl.getPendingId() == 0 && ctl.getStatus().contains ("Missed"));
    }
}

GF_TEST (SceneFormat, group_scene_pending_transport_stop_or_seek_cancels_its_generation)
{
    for (int action = 0; action < 2; ++action)
    {
        GapFillAudioProcessor p;
        auto& ctl = p.getLiveControl().scenes();
        GF_EXPECT (ctl.getBank().store (0, denseScene ("Group")));
        GF_EXPECT (ctl.gotoSceneAt (0, 8.0));
        const auto id = ctl.getPendingId();
        SceneEngine engine;
        TransportSnap transport;
        transport.valid = true;
        transport.ppq = 6.0;
        transport.samplesPerBeat = 24000.0;
        transport.numSamples = 512;
        GF_EXPECT (engine.beginChunk (ctl.publisher(), transport).appliedId == 0);
        transport.valid = action != 0;
        transport.ppq = action == 0 ? 6.0 : 2.0;
        const auto result = engine.beginChunk (ctl.publisher(), transport);
        GF_EXPECT (result.appliedId == 0 && result.missedDeadlineId == id);
        GF_EXPECT (! engine.hasPending());
    }
}

