#include "RecipePresentation.h"
#include "Strings.h"
#include "../history/Describe.h"
#include "../abcompare/MatchLevels.h"

namespace gapfill::i18n
{
juce::String describeRecipe (const FillRecipe& recipe)
{
    const auto algorithm = text (algoName ((Algo) juce::jlimit (0, numAlgos - 1, (int) recipe.algo)));
    const auto duration = juce::String ((int) recipe.lengthMs);
    const auto division = recipe.division != FillRecipe::noDivision && recipe.division < (int) Division::NumDivisions
                            ? juce::String (divisionNames()[(int) recipe.division]) : juce::String();
    const auto band = recipe.band >= 0 && recipe.band <= 2 ? text (history::strings().bands[recipe.band]) : juce::String();
    const bool response = (recipe.flags & FillRecipe::flagResponse) != 0;
    if (division.isNotEmpty() && band.isNotEmpty())
        return response ? format ("{0} {1} - {2} - {3} ms - Response", { algorithm, division, band, duration })
                        : format ("{0} {1} - {2} - {3} ms", { algorithm, division, band, duration });
    if (division.isNotEmpty())
        return response ? format ("{0} {1} - {2} ms - Response", { algorithm, division, duration })
                        : format ("{0} {1} - {2} ms", { algorithm, division, duration });
    if (band.isNotEmpty())
        return response ? format ("{0} - {1} - {2} ms - Response", { algorithm, band, duration })
                        : format ("{0} - {1} - {2} ms", { algorithm, band, duration });
    return response ? format ("{0} - {1} ms - Response", { algorithm, duration })
                    : format ("{0} - {1} ms", { algorithm, duration });
}

juce::String matchTrim (float decibels)
{
    const auto canonical = abcompare::trimText (decibels);
    return canonical.isEmpty() ? canonical : format ("{0} dB match", { canonical.upToFirstOccurrenceOf (" dB match", false, false) });
}
}
