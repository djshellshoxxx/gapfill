#include "LockSubtree.h"
#include "../history/RecipeCodec.h"
#include "../../PluginProcessor.h"

namespace gapfill::lock
{

bool validate (const juce::ValueTree& tree)
{
    if (tree.getProperty ("v", 1).toString().trim() != "1")
        return false;                                   // unknown subtree version: drop it, keep the rest

    const auto valid = tree.getProperty ("valid", 0).toString().trim();

    if (valid != "0" && valid != "1")
        return false;

    if (valid == "0")
        return true;

    FillRecipe r;
    return recipecodec::decode (tree.getProperty ("recipe").toString(), r);
}

void apply (GapFillAudioProcessor& proc, const juce::ValueTree& tree)
{
    FillRecipe r;

    if (tree.getProperty ("valid", 0).toString().trim() == "1"
         && recipecodec::decode (tree.getProperty ("recipe").toString(), r))
    {
        RecipeExtras extras;
        recipecodec::decodeExtras (tree.getProperty ("extras").toString(), extras);
        extras = normalizeRecipeExtrasForAlgorithm (r.algo, extras);
        proc.getRecipes().lockPublication.beginRestore ({ true, r, extras });
    }
    else
    {
        reset (proc);
    }
    if (auto* messages = juce::MessageManager::getInstanceWithoutCreating(); messages != nullptr && messages->isThisTheMessageThread())
        proc.getRecipes().pollLockPublication();
}

juce::ValueTree serialise (const GapFillAudioProcessor& proc)
{
    LockSnapshot mirror;
    if (! proc.getRecipes().lockPublication.tryLatest (mirror))
    {
        juce::ValueTree error ("Lock");
        error.setProperty ("__serialiseError", "The Fill Lock state changed while it was being captured. Try saving again.", nullptr);
        return error;
    }

    if (! mirror.valid)
        return {};

    juce::ValueTree tree ("Lock");
    tree.setProperty ("v", 1, nullptr);
    tree.setProperty ("valid", 1, nullptr);
    tree.setProperty ("recipe", recipecodec::encode (mirror.recipe), nullptr);
    const auto extra = recipecodec::encodeExtras (mirror.extras);
    if (extra.isNotEmpty())
        tree.setProperty ("extras", extra, nullptr);
    return tree;
}

void reset (GapFillAudioProcessor& proc)
{
    proc.getRecipes().lockPublication.beginRestore ({});
    if (auto* messages = juce::MessageManager::getInstanceWithoutCreating(); messages != nullptr && messages->isThisTheMessageThread())
        proc.getRecipes().pollLockPublication();
}

} // namespace gapfill::lock
