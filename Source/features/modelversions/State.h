#pragma once

#include "ModelVersions.h"
#include <juce_data_structures/juce_data_structures.h>

namespace gapfill::models
{
inline constexpr const char* kTreeType = "ModelVersions";

/** Message-thread codec. The canonical child belongs directly to the APVTS
    root, so presets, Undo and A/B copy it with the parameter tree. */
juce::ValueTree toModelVersionsTree (ModelVersions);

/** Strict bounded parser. Missing, malformed or unsupported metadata selects
    all legacy models; partial upgrades must never silently mix versions. */
ModelVersions fromModelVersionsTree (const juce::ValueTree&) noexcept;
} // namespace gapfill::models
