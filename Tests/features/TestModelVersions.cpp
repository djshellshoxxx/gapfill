#include "../TestFramework.h"
#include "features/modelversions/State.h"

using namespace gapfill::models;

GF_TEST (ModelVersions, fresh_defaults_and_atomic_packing_roundtrip)
{
    GF_EXPECT (ModelVersions {} == (ModelVersions { 2, 2, 2 }));
    for (std::uint8_t c = 1; c <= 2; ++c)
        for (std::uint8_t t = 1; t <= 2; ++t)
            for (std::uint8_t a = 1; a <= 2; ++a)
            {
                const ModelVersions v { c, t, a };
                GF_EXPECT (ModelVersions::fromPacked (v.packed()) == v);
                GF_EXPECT (fromModelVersionsTree (toModelVersionsTree (v)) == v);
            }
    GF_EXPECT (ModelVersions::fromPacked (0) == ModelVersions::legacy());
    GF_EXPECT (ModelVersions::fromPacked (0xff020202u) == ModelVersions::legacy());
}

GF_TEST (ModelVersions, absent_or_malformed_metadata_retains_all_legacy_models)
{
    GF_EXPECT (fromModelVersionsTree ({}) == ModelVersions::legacy());
    const auto latest = toModelVersionsTree ({});
    for (const auto* field : { "v", "cloud", "taste", "arrangement" })
    {
        for (const auto* bad : { "", "0", "3", "2junk", "-1", "1.0", " 1" })
        {
            auto tree = latest.createCopy();
            tree.setProperty (field, bad, nullptr);
            GF_EXPECT (fromModelVersionsTree (tree) == ModelVersions::legacy());
        }
        auto tree = latest.createCopy();
        tree.removeProperty (field, nullptr);
        GF_EXPECT (fromModelVersionsTree (tree) == ModelVersions::legacy());
    }
    auto extra = latest.createCopy();
    extra.setProperty ("unknown", 1, nullptr);
    GF_EXPECT (fromModelVersionsTree (extra) == ModelVersions::legacy());
    extra = latest.createCopy();
    extra.appendChild (juce::ValueTree ("child"), nullptr);
    GF_EXPECT (fromModelVersionsTree (extra) == ModelVersions::legacy());
}

GF_TEST (ModelVersions, parameter_snapshot_copy_preserves_explicit_models)
{
    juce::ValueTree parameters ("GAPFILL");
    parameters.appendChild (toModelVersionsTree ({ 1, 2, 1 }), nullptr);
    const auto xml = parameters.createXml();
    GF_EXPECT (xml != nullptr);
    const auto restored = juce::ValueTree::fromXml (*xml);
    GF_EXPECT (fromModelVersionsTree (restored.getChildWithName (kTreeType)) == (ModelVersions { 1, 2, 1 }));
    GF_EXPECT (fromModelVersionsTree (parameters.createCopy().getChildWithName (kTreeType)) == (ModelVersions { 1, 2, 1 }));
}
