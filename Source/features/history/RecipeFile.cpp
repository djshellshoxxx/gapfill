#include "RecipeFile.h"
#include "RecipeCodec.h"
#include "../state/StateSubtrees.h"

#ifndef GAPFILL_VERSION
 #define GAPFILL_VERSION "0.0.0"
#endif

namespace gapfill::history
{

juce::String sanitiseRecipeName (const juce::String& name)
{
    juce::String out;

    for (auto c : name.trim())
    {
        if (out.length() >= kMaxRecipeNameLength)
            break;

        const bool printable = c >= 32 && c != 127;
        const bool separator = c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"'
                               || c == '<' || c == '>' || c == '|';

        if (printable && ! separator)
            out << juce::String::charToString (c);
        else if (printable)
            out << "_";
    }

    return out.trim();
}

juce::File defaultRecipeDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
               .getChildFile ("GapFill").getChildFile ("Recipes");
}

juce::String recipeFileText (const FillRecipe& recipe, const juce::String& name)
{
    juce::XmlElement root ("GFRECIPE");
    root.setAttribute ("name", sanitiseRecipeName (name));
    root.setAttribute ("recipe", recipecodec::encode (recipe));
    root.setAttribute ("appVersion", juce::String (GAPFILL_VERSION));
    root.setAttribute ("stateVersion", state::kStateVersion);
    return root.toString (juce::XmlElement::TextFormat().singleLine());
}

bool writeRecipeFile (const juce::File& file, const FillRecipe& recipe, const juce::String& name, juce::String& error)
{
    if (! isValid (recipe))
    {
        error = "That recipe is not valid and was not saved.";
        return false;
    }

    if (! file.getParentDirectory().createDirectory())
    {
        error = "Couldn't create the folder " + file.getParentDirectory().getFullPathName();
        return false;
    }

    if (! file.replaceWithText (recipeFileText (recipe, name)))
    {
        error = "Couldn't write " + file.getFullPathName();
        return false;
    }

    return true;
}

RecipeFile parseRecipeText (const juce::String& text)
{
    RecipeFile result;

    if (text.getNumBytesAsUTF8() > (size_t) kMaxRecipeFileBytes)
    {
        result.error = "That file is too large to be a GapFill recipe.";
        return result;
    }

    const auto xml = juce::XmlDocument::parse (text);

    if (xml == nullptr || ! xml->hasTagName ("GFRECIPE"))
    {
        result.error = "That file is not a GapFill recipe.";
        return result;
    }

    if (! recipecodec::decode (xml->getStringAttribute ("recipe"), result.recipe))
    {
        result.error = "That recipe file is damaged or has values GapFill does not understand.";
        return result;
    }

    result.name = sanitiseRecipeName (xml->getStringAttribute ("name"));
    result.ok = true;
    return result;
}

RecipeFile readRecipeFile (const juce::File& file)
{
    RecipeFile result;

    if (! file.existsAsFile())
    {
        result.error = "That file does not exist.";
        return result;
    }

    if (file.getSize() > kMaxRecipeFileBytes)
    {
        result.error = "That file is too large to be a GapFill recipe.";
        return result;
    }

    return parseRecipeText (file.loadFileAsString());
}

} // namespace gapfill::history
