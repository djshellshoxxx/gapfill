#pragma once

#include "../../dsp/FillRecipe.h"
#include <juce_core/juce_core.h>

namespace gapfill::i18n
{
/** Message-thread display only. Recipe names/state/file basenames use the
    unchanged canonical history::describeRecipe instead. */
juce::String describeRecipe (const FillRecipe&);
juce::String matchTrim (float decibels);
}
