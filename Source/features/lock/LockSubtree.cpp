#include "LockSubtree.h"
#include "../history/RecipeCodec.h"
#include "../../PluginProcessor.h"

namespace gapfill::lock
{

bool validate (const juce::ValueTree& tree)
{
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
    auto& mirror = proc.getRecipes().lockMirror;
    FillRecipe r;

    if (tree.getProperty ("valid", 0).toString().trim() == "1"
         && recipecodec::decode (tree.getProperty ("recipe").toString(), r))
    {
        mirror = { true, r };

        Command c;
        c.type = CommandType::LockRecipe;
        c.recipe = r;
        proc.postCommand (c);
    }
    else
    {
        reset (proc);
    }
}

juce::ValueTree serialise (const GapFillAudioProcessor& proc)
{
    const auto& mirror = proc.getRecipes().lockMirror;

    if (! mirror.valid)
        return {};

    juce::ValueTree tree ("Lock");
    tree.setProperty ("valid", 1, nullptr);
    tree.setProperty ("recipe", recipecodec::encode (mirror.recipe), nullptr);
    return tree;
}

void reset (GapFillAudioProcessor& proc)
{
    proc.getRecipes().lockMirror = {};

    Command c;
    c.type = CommandType::Unlock;
    proc.postCommand (c);
}

} // namespace gapfill::lock
