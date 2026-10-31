// State versioning and fail-soft subtrees (docs/dev/02-FOUNDATIONS.md F0-6).

#include "../support/TestRig.h"

#include "features/state/StateSubtrees.h"

using namespace gapfill;
using namespace gftest;

namespace
{
    int appliedCount = 0, resetCount = 0;
    juce::String lastApplied;

    bool validateKeepsFlag (const juce::ValueTree& t)   { return (bool) t.getProperty ("ok", false); }
    void applyRecord (GapFillAudioProcessor&, const juce::ValueTree& t)   { ++appliedCount; lastApplied = t.getType().toString(); }
    void resetRecord (GapFillAudioProcessor&)           { ++resetCount; }

    std::vector<state::SubtreeDesc> testTable()
    {
        return { { "Good",  256, validateKeepsFlag, applyRecord, nullptr, resetRecord },
                 { "Small", 64,  validateKeepsFlag, applyRecord, nullptr, resetRecord },
                 { "Wide",  4096, nullptr, applyRecord, nullptr, resetRecord } };
    }

    juce::ValueTree makeTree (const char* name, bool ok, int padding = 0)
    {
        juce::ValueTree t (name);
        t.setProperty ("ok", ok, nullptr);

        if (padding > 0)
            t.setProperty ("pad", juce::String::repeatedString ("x", padding), nullptr);

        return t;
    }

    juce::MemoryBlock savedState (GapFillAudioProcessor& p)
    {
        juce::MemoryBlock b;
        p.getStateInformation (b);
        return b;
    }
}

GF_TEST (State, saved_state_carries_the_current_version)
{
    GapFillAudioProcessor p;
    const auto xml = juce::AudioProcessor::getXmlFromBinary (savedState (p).getData(), (int) savedState (p).getSize());
    GF_EXPECT (xml != nullptr);
    const auto root = juce::ValueTree::fromXml (*xml);
    GF_EXPECT (state::readStateVersion (root) == state::kStateVersion);
    GF_EXPECT (state::kStateVersion == 2);
}

GF_TEST (State, absent_or_bogus_version_reads_as_one)
{
    juce::ValueTree t ("GAPFILL_STATE");
    GF_EXPECT (state::readStateVersion (t) == 1);
    t.setProperty ("stateVersion", "banana", nullptr);
    GF_EXPECT (state::readStateVersion (t) >= 1);
    t.setProperty ("stateVersion", -5, nullptr);
    GF_EXPECT (state::readStateVersion (t) == 1);
}

GF_TEST (State, migrate_from_version_one_changes_nothing)
{
    juce::ValueTree t ("GAPFILL_STATE");
    t.setProperty ("program", 3, nullptr);
    t.appendChild (juce::ValueTree ("GAPFILL"), nullptr);
    const auto before = t.createCopy();
    state::migrate (t, 1);
    GF_EXPECT (t.isEquivalentTo (before));
}

GF_TEST (State, reserved_subtree_caps_fit_the_total_budget)
{
    std::size_t sum = 0;

    for (const auto& d : state::reservedSubtrees())
        sum += d.maxBytes;

    GF_EXPECT (sum <= state::kTotalSubtreeBudget);
    GF_EXPECT (state::reservedSubtrees().size() == 17);   // existing owners + ScenesV2/MacrosV2/V3; legacy rows stay read-only
}

GF_TEST (State, bad_oversized_and_absent_subtrees_fail_soft)
{
    GapFillAudioProcessor p;
    const auto table = testTable();

    juce::ValueTree root ("GAPFILL_STATE");
    root.appendChild (makeTree ("Good", true), nullptr);
    root.appendChild (makeTree ("Small", false), nullptr);           // validator says no
    // "Wide" absent

    appliedCount = resetCount = 0;
    const auto plan = state::planSubtrees (root, table);
    GF_EXPECT (plan.toApply.size() == 1);
    GF_EXPECT (plan.dropped.size() == 1);
    GF_EXPECT (plan.toReset.size() == 2);                             // Small (bad) + Wide (absent)

    state::applyPlan (p, plan);
    GF_EXPECT (appliedCount == 1 && resetCount == 2 && lastApplied == "Good");

    juce::ValueTree oversized ("GAPFILL_STATE");
    oversized.appendChild (makeTree ("Good", true, 1000), nullptr);   // > 256 bytes
    const auto plan2 = state::planSubtrees (oversized, table);
    GF_EXPECT (plan2.toApply.empty() && plan2.dropped.size() == 1);
}

GF_TEST (State, total_budget_drops_later_subtrees)
{
    auto table = testTable();
    table[0].maxBytes = table[2].maxBytes = state::kTotalSubtreeBudget;

    juce::ValueTree root ("GAPFILL_STATE");
    const auto each = (int) (state::kTotalSubtreeBudget * 3 / 4);
    root.appendChild (makeTree ("Good", true, each), nullptr);
    root.appendChild (makeTree ("Wide", true, each), nullptr);
    GF_EXPECT (state::serialisedSize (root.getChild (0)) <= state::kTotalSubtreeBudget);
    GF_EXPECT (state::serialisedSize (root.getChild (0)) + state::serialisedSize (root.getChild (1)) > state::kTotalSubtreeBudget);

    const auto plan = state::planSubtrees (root, table);
    GF_EXPECT (plan.toApply.size() == 1 && plan.dropped.size() == 1);
    GF_EXPECT (juce::String (plan.toApply[0].first->name) == "Good" && juce::String (plan.dropped[0]->name) == "Wide");
}

GF_TEST (State, corrupt_optional_subtree_never_blocks_the_parameters)
{
    GapFillAudioProcessor a, b;
    setParams (a, { { "wet", 37.0f }, { "fill", 71.0f } });

    const auto block = savedState (a);
    auto xml = juce::AudioProcessor::getXmlFromBinary (block.getData(), (int) block.getSize());
    GF_EXPECT (xml != nullptr);
    auto root = juce::ValueTree::fromXml (*xml);

    juce::ValueTree junk ("Lock");
    junk.setProperty ("blob", juce::String::repeatedString ("A", 5000), nullptr);   // far above the 1 KiB cap
    root.appendChild (junk, nullptr);

    juce::MemoryBlock damaged;
    juce::AudioProcessor::copyXmlToBinary (*root.createXml(), damaged);
    b.setStateInformation (damaged.getData(), (int) damaged.getSize());

    GF_EXPECT_NEAR (b.getValueTreeState().getRawParameterValue ("wet")->load(), 37.0, 1.0e-3);
    GF_EXPECT_NEAR (b.getValueTreeState().getRawParameterValue ("fill")->load(), 71.0, 1.0e-3);
}

GF_TEST (State, bad_parameter_value_still_rejects_the_whole_state)
{
    GapFillAudioProcessor a, b;
    setParams (a, { { "wet", 37.0f } });
    setParams (b, { { "wet", 12.0f } });

    const auto block = savedState (a);
    auto xml = juce::AudioProcessor::getXmlFromBinary (block.getData(), (int) block.getSize());
    auto root = juce::ValueTree::fromXml (*xml);
    auto params = root.getChild (0);

    for (int i = 0; i < params.getNumChildren(); ++i)
        if (params.getChild (i).getProperty ("id").toString() == "wet")
            params.getChild (i).setProperty ("value", 9999.0, nullptr);

    juce::MemoryBlock damaged;
    juce::AudioProcessor::copyXmlToBinary (*root.createXml(), damaged);
    b.setStateInformation (damaged.getData(), (int) damaged.getSize());
    GF_EXPECT_NEAR (b.getValueTreeState().getRawParameterValue ("wet")->load(), 12.0, 1.0e-3);
}

GF_TEST (State, three_schema_supersession_prefers_valid_newest_and_falls_back_without_double_apply)
{
    GapFillAudioProcessor processor;
    const std::vector<state::SubtreeDesc> descriptors {
        { "Old", 256, validateKeepsFlag, applyRecord, nullptr, resetRecord },
        { "Middle", 256, validateKeepsFlag, applyRecord, nullptr, resetRecord, "Old" },
        { "Newest", 256, validateKeepsFlag, applyRecord, nullptr, resetRecord, "Middle", "Old" }
    };
    juce::ValueTree root ("Root");
    root.appendChild (makeTree ("Old", true), nullptr);
    root.appendChild (makeTree ("Middle", true), nullptr);
    root.appendChild (makeTree ("Newest", true), nullptr);
    for (const auto* expected : { "Newest", "Middle", "Old" })
    {
        appliedCount = resetCount = 0;
        const auto plan = state::planSubtrees (root, descriptors);
        GF_EXPECT (plan.toApply.size() == 1);
        state::applyPlan (processor, plan);
        GF_EXPECT (appliedCount == 1 && lastApplied == expected);
        root.getChildWithName (expected).setProperty ("ok", false, nullptr);
    }
}
