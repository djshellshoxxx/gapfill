#include "HistorySubtree.h"
#include "RecipeCodec.h"
#include "../../PluginProcessor.h"

namespace gapfill::history
{

namespace
{
    constexpr int kMaxEntriesAccepted = 16;      // generous: extra valid entries beyond 8 are ignored
}

bool validate (const juce::ValueTree& tree)
{
    // Structure only. Individual bad entries are dropped by apply(), not a reason to reject the rest.
    if (tree.getNumChildren() > kMaxEntriesAccepted)
        return false;

    for (int i = 0; i < tree.getNumChildren(); ++i)
        if (! tree.getChild (i).hasType ("R"))
            return false;

    return true;
}

void apply (GapFillAudioProcessor& proc, const juce::ValueTree& tree)
{
    FillRecipe recipes[RecipeHistory::capacity];
    RecipeExtras extras[RecipeHistory::capacity];
    int n = 0;

    for (int i = 0; i < tree.getNumChildren() && n < RecipeHistory::capacity; ++i)
    {
        FillRecipe r;

        if (recipecodec::decode (tree.getChild (i).getProperty ("b").toString(), r))
        {
            recipes[n] = r;
            recipecodec::decodeExtras (tree.getChild (i).getProperty ("x").toString(), extras[n]);
            ++n;
        }
    }

    proc.getRecipeHistory().restore (recipes, n, extras);
}

juce::ValueTree serialise (const GapFillAudioProcessor& proc)
{
    const auto& h = proc.getRecipeHistory();

    if (h.size() == 0)
        return {};

    juce::ValueTree tree ("History");

    for (int i = 0; i < h.size(); ++i)
    {
        juce::ValueTree entry ("R");
        entry.setProperty ("b", recipecodec::encode (h.get (i)), nullptr);
        const auto extra = recipecodec::encodeExtras (h.getExtras (i));
        if (extra.isNotEmpty())
            entry.setProperty ("x", extra, nullptr);
        tree.appendChild (entry, nullptr);
    }

    return tree;
}

void reset (GapFillAudioProcessor& proc)
{
    proc.getRecipeHistory().clear();
}

} // namespace gapfill::history
