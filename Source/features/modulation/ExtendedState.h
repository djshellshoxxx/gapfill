#pragma once

#include "ExtendedConfig.h"
#include <juce_data_structures/juce_data_structures.h>

namespace gapfill::modulation
{
/** Message-only exact float32 LE codec. Invalid input leaves the output intact.
    The nested legacy config uses its unchanged released codec. */
juce::ValueTree toExtendedTree (const ExtendedConfig&);
bool fromExtendedTree (const juce::ValueTree&, ExtendedConfig&);
bool validateExtendedTree (const juce::ValueTree&);
} // namespace gapfill::modulation
