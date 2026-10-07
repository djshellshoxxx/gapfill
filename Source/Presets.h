#pragma once

#include "Parameters.h"
#include <vector>

namespace gapfill
{

/** One parameter override inside a factory preset (value in real units). */
struct PresetValue
{
    const char* id;
    float value;
};

struct Preset
{
    const char* name;
    const char* description;
    std::vector<PresetValue> values;
};

/** The built-in presets. Index 0 is always "Init" (plain defaults). */
const std::vector<Preset>& getFactoryPresets();

/** Restores every parameter to its default value. */
void resetToDefaults (juce::AudioProcessorValueTreeState& state);

/** Applies a factory preset (defaults first, then the preset's overrides). */
void applyPreset (juce::AudioProcessorValueTreeState& state, int index);

/** Randomises the creative parameters. When `everything` is true a wider but
    still safe set is touched; mix, output, lookahead and bypass are never
    randomised. Returns the seed that was used. */
int randomiseParameters (juce::AudioProcessorValueTreeState& state,
                         bool everything,
                         juce::Random& random);

/** Number of curated "Surprise Me" profiles. */
int getNumSurpriseProfiles() noexcept;

/** Applies one curated, coherent creative profile with seeded variation.
    Only creative settings change: detector, protection, latency, spectral
    lanes and mix are preserved. Never picks `previousProfile` again, so
    consecutive presses always change character. Returns the profile index
    and writes its display name into `name`. */
int surpriseParameters (juce::AudioProcessorValueTreeState& state,
                        juce::Random& random,
                        int previousProfile,
                        juce::String& name);

} // namespace gapfill
