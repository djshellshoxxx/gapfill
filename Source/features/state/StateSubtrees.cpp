#include "StateSubtrees.h"

#include "../../PluginProcessor.h"

namespace gapfill::state
{

namespace
{
    constexpr std::size_t Ki = 1024;

    // ---- WP6 rows: <Cmd> (X-1), <Perf> (panic options), <Scenes> (F-L07 / N-VAL) ----
    juce::ValueTree cmdSerialise (const GapFillAudioProcessor& p)
    {
        const auto& m = p.getLiveControl().bindings();
        const auto t = m.toValueTree();
        const bool empty = m.size() == 0 && m.getTable().pcSceneEnabled == 0 && m.getFeedbackDevice().empty();
        return empty ? juce::ValueTree() : t;
    }

    void cmdApply (GapFillAudioProcessor& p, const juce::ValueTree& t)  { p.getLiveControl().bindings().restore (t); }
    void cmdReset (GapFillAudioProcessor& p)                            { p.getLiveControl().bindings().restore ({}); }

    juce::ValueTree macrosSerialise (const GapFillAudioProcessor& p)
    {
        const auto& m = p.getLiveControl().macroMappings();
        macros::MacroModel factory;
        const auto tree = m.toValueTree();
        return tree.isEquivalentTo (factory.toValueTree()) ? juce::ValueTree() : tree;     // defaults write nothing
    }

    void macrosApply (GapFillAudioProcessor& p, const juce::ValueTree& t)  { p.getLiveControl().macroMappings().restore (t); }
    void macrosReset (GapFillAudioProcessor& p)                            { p.getLiveControl().macroMappings().resetAll(); }

    juce::ValueTree perfSerialise (const GapFillAudioProcessor& p)
    {
        const auto o = p.getLiveControl().getPerfOptions();
        const bool isDefault = o.panic.recovery == panic::RecoveryMode::Manual && o.panic.panicToggles;
        return isDefault ? juce::ValueTree() : p.getLiveControl().perfToValueTree();
    }

    void perfApply (GapFillAudioProcessor& p, const juce::ValueTree& t) { p.getLiveControl().perfRestore (t); }
    void perfReset (GapFillAudioProcessor& p)                           { p.getLiveControl().perfRestore ({}); }

    juce::ValueTree scenesSerialise (const GapFillAudioProcessor& p)
    {
        const auto& bank = p.getLiveControl().scenes().getBank();
        return bank.size() == 0 && bank.getName().isEmpty() ? juce::ValueTree()
                                                              : bank.toValueTree (24 * Ki - 64);
    }

    void scenesApply (GapFillAudioProcessor& p, const juce::ValueTree& t)
    {
        scenes::SceneBank bank;
        bank.restore (t);
        p.getLiveControl().scenes().replaceBank (bank);
    }

    void scenesReset (GapFillAudioProcessor& p)                         { p.getLiveControl().scenes().reset(); }
}

const std::vector<SubtreeDesc>& reservedSubtrees()
{
    // One row per feature-owned subtree. The owning ticket fills in its row
    // (and only its row): validate / apply / serialise / reset.
    static const std::vector<SubtreeDesc> table =
    {
        { "Macros",  2 * Ki,  &macros::MacroModel::validateTree, &macrosApply, &macrosSerialise, &macrosReset },   // C-2
        { "Lock",    1 * Ki,  nullptr, nullptr, nullptr, nullptr },   // C-5
        { "History", 1 * Ki,  nullptr, nullptr, nullptr, nullptr },   // U-1
        { "Perf",    4 * Ki,  &live::LiveControl::perfValidate, &perfApply, &perfSerialise, &perfReset },   // U prefs + panic options (WP6)
        { "Scenes",  24 * Ki, &scenes::SceneBank::validateTree, &scenesApply, &scenesSerialise, &scenesReset },   // F-L07 / N-VAL
        { "Cmd",     3 * Ki,  &commands::BindingModel::validateTree, &cmdApply, &cmdSerialise, &cmdReset },   // X-1
        { "Tension", 3 * Ki,  nullptr, nullptr, nullptr, nullptr },   // F-M21
        { "Groove",  1 * Ki,  nullptr, nullptr, nullptr, nullptr },   // F-L09
        { "Fx",      1 * Ki,  nullptr, nullptr, nullptr, nullptr },   // FX-0
        { "Health",  1 * Ki,  nullptr, nullptr, nullptr, nullptr },   // N-USE
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
