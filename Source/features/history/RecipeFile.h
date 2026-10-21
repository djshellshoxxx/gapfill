#pragma once

// U-1 Save recipe / Load recipe: the `.gfrecipe` file
// (docs/dev/05-SPEC-USABILITY-VALUE.md U-1).
//
//   <GFRECIPE name="Stutter 1/32 - High - 180 ms" recipe="base64" appVersion="0.1.0" stateVersion="2"/>
//
// <= 1 KB written; the loader refuses anything over 4 KiB, malformed XML, a wrong
// root, a bad checksum or any out-of-range field, with user-facing text, and never
// touches the current sound. Message thread only (file I/O).

#include "../../dsp/FillRecipe.h"

#include <juce_core/juce_core.h>

namespace gapfill::history
{

inline constexpr std::int64_t kMaxRecipeFileBytes = 4096;
inline constexpr int kMaxRecipeNameLength = 48;

struct RecipeFile
{
    bool ok = false;
    juce::String error;          ///< user-facing reason when !ok
    FillRecipe recipe;
    juce::String name;
};

/** Printable, length-capped, no path separators (safe as a file name and a label). */
juce::String sanitiseRecipeName (const juce::String& name);

/** <Documents>/GapFill/Recipes (not created). */
juce::File defaultRecipeDirectory();

/** The XML text for `recipe`. */
juce::String recipeFileText (const FillRecipe& recipe, const juce::String& name);

/** Writes the file (creating the parent directory). False + `error` on failure. */
bool writeRecipeFile (const juce::File&, const FillRecipe&, const juce::String& name, juce::String& error);

RecipeFile parseRecipeText (const juce::String& text);
RecipeFile readRecipeFile (const juce::File&);

} // namespace gapfill::history
