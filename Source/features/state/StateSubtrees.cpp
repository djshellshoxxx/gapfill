#include "StateSubtrees.h"
#include "../sync/SyncState.h"
#include "../recipes/RecipeServices.h"
#include "../lock/LockSubtree.h"
#include "../history/HistorySubtree.h"
#include "../fx/FxUiState.h"

#include "../explain/ExplainState.h"

namespace gapfill::state
{

namespace
{
    constexpr std::size_t Ki = 1024;

    // `<Perf>` is shared: WP4 stores the audition trigger and A/B match switch, E-2 the fallback tempo.
    // Every attribute is optional and owned by exactly one feature, so the handlers simply run in turn.
    bool perfValidate (const juce::ValueTree& t)
    {
        return recipes::validatePerf (t) && sync::validatePerf (t);
    }

    void perfApply (GapFillAudioProcessor& p, const juce::ValueTree& t)
    {
        recipes::applyPerf (p, t);
        sync::applyPerf (p, t);
    }

    juce::ValueTree perfSerialise (const GapFillAudioProcessor& p)
    {
        auto a = recipes::serialisePerf (p);
        const auto b = sync::serialisePerf (p);

        if (! b.isValid())
            return a;

        if (! a.isValid())
            return b;

        for (int i = 0; i < b.getNumProperties(); ++i)
            a.setProperty (b.getPropertyName (i), b.getProperty (b.getPropertyName (i)), nullptr);

        return a;
    }

    void perfReset (GapFillAudioProcessor& p)
    {
        recipes::resetPerf (p);
        sync::resetPerf (p);
    }
}

const std::vector<SubtreeDesc>& reservedSubtrees()
{
    // One row per feature-owned subtree. The owning ticket fills in its row
    // (and only its row): validate / apply / serialise / reset.
    static const std::vector<SubtreeDesc> table =
    {
        { "Macros",  2 * Ki,  nullptr, nullptr, nullptr, nullptr },   // C-2
        { "Lock",    1 * Ki,  lock::validate, lock::apply, lock::serialise, lock::reset },   // C-5
        { "History", 1 * Ki,  history::validate, history::apply, history::serialise, history::reset },   // U-1
        { "Perf",    4 * Ki,  perfValidate, perfApply, perfSerialise, perfReset },   // U prefs (WP4: trigger, match)
        { "Scenes",  24 * Ki, nullptr, nullptr, nullptr, nullptr },   // F-L07 / N-VAL
        { "Cmd",     3 * Ki,  nullptr, nullptr, nullptr, nullptr },   // X-1
        { "Tension", 3 * Ki,  nullptr, nullptr, nullptr, nullptr },   // F-M21
        { "Groove",  1 * Ki,  nullptr, nullptr, nullptr, nullptr },   // F-L09
        { "Fx",      1 * Ki,  fx::validateFxTree, fx::applyFxTree, fx::serialiseFxTree, fx::resetFxTree },   // FX-0
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
