#include "StateSubtrees.h"

#include "../explain/ExplainState.h"

namespace gapfill::state
{

namespace
{
    constexpr std::size_t Ki = 1024;
}

const std::vector<SubtreeDesc>& reservedSubtrees()
{
    // One row per feature-owned subtree. The owning ticket fills in its row
    // (and only its row): validate / apply / serialise / reset.
    static const std::vector<SubtreeDesc> table =
    {
        { "Macros",  2 * Ki,  nullptr, nullptr, nullptr, nullptr },   // C-2
        { "Lock",    1 * Ki,  nullptr, nullptr, nullptr, nullptr },   // C-5
        { "History", 1 * Ki,  nullptr, nullptr, nullptr, nullptr },   // U-1
        { "Perf",    4 * Ki,  nullptr, nullptr, nullptr, nullptr },   // U prefs
        { "Scenes",  24 * Ki, nullptr, nullptr, nullptr, nullptr },   // F-L07 / N-VAL
        { "Cmd",     3 * Ki,  nullptr, nullptr, nullptr, nullptr },   // X-1
        { "Tension", 3 * Ki,  nullptr, nullptr, nullptr, nullptr },   // F-M21
        { "Groove",  1 * Ki,  nullptr, nullptr, nullptr, nullptr },   // F-L09
        { "Fx",      1 * Ki,  nullptr, nullptr, nullptr, nullptr },   // FX-0
        { "Health",  1 * Ki,  explain::validateSubtree, explain::applySubtree, explain::serialiseSubtree, explain::resetSubtree },   // N-USE (WP3)
        { "Learn",   4 * Ki,  nullptr, nullptr, nullptr, nullptr },   // F-X05
    };

    return table;
}

int readStateVersion (const juce::ValueTree& root)
{
    const auto v = root.getProperty ("stateVersion", 1);
    return v.isInt() || v.isInt64() || v.isDouble() || v.isString() ? juce::jmax (1, (int) v) : 1;
}

void migrate (juce::ValueTree& root, int fromVersion)
{
    // v1 -> v2: nothing to change (all new parameters default to neutral).
    // v2 -> v3: nothing to change (doc-12 subtrees are optional).
    for (int version = fromVersion; version < kStateVersion; ++version)
    {
        juce::ignoreUnused (root);
    }
}

std::size_t serialisedSize (const juce::ValueTree& tree)
{
    return tree.isValid() ? (std::size_t) tree.toXmlString().getNumBytesAsUTF8() : 0u;
}

SubtreePlan planSubtrees (const juce::ValueTree& root, const std::vector<SubtreeDesc>& table)
{
    SubtreePlan plan;
    std::size_t budget = kTotalSubtreeBudget;

    for (const auto& desc : table)
    {
        const auto tree = root.getChildWithName (juce::Identifier (desc.name));

        if (! tree.isValid())
        {
            plan.toReset.push_back (&desc);          // absent => default
            continue;
        }

        const auto size = serialisedSize (tree);
        const bool ok = size <= desc.maxBytes && size <= budget
                         && (desc.validate == nullptr || desc.validate (tree));

        if (ok)
        {
            budget -= size;
            plan.toApply.emplace_back (&desc, tree);
        }
        else
        {
            plan.toReset.push_back (&desc);
            plan.dropped.push_back (&desc);
        }
    }

    return plan;
}

void applyPlan (GapFillAudioProcessor& proc, const SubtreePlan& plan)
{
    for (const auto* desc : plan.toReset)
        if (desc->reset != nullptr)
            desc->reset (proc);

    for (const auto& [desc, tree] : plan.toApply)
        if (desc->apply != nullptr)
            desc->apply (proc, tree);
}

void writeSubtrees (const GapFillAudioProcessor& proc, juce::ValueTree& root, const std::vector<SubtreeDesc>& table)
{
    std::size_t budget = kTotalSubtreeBudget;

    for (const auto& desc : table)
    {
        if (desc.serialise == nullptr)
            continue;

        auto tree = desc.serialise (proc);

        if (! tree.isValid() || ! tree.hasType (juce::Identifier (desc.name)))
            continue;

        const auto size = serialisedSize (tree);

        if (size > desc.maxBytes || size > budget)
            continue;                                // never write what the loader would drop

        budget -= size;
        root.appendChild (tree, nullptr);
    }
}

} // namespace gapfill::state
