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
    const char* category;      ///< one of presets::presetCategories() (U-4); checked by validate_repo.py
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

/** The centre of one Surprise profile (C-8 Evolve waypoint). Read-only, derived from the
    same table Surprise uses, so the two can never drift apart. Values are 0..1 fractions
    (like EngineSettings); `weight[i]` is 0.80 for the profile's core algorithms and 0.06
    for the rest; `stutterDivision` is the profile's first candidate division. */
struct SurpriseTargets
{
    const char* name = "";
    float fill = 0.0f, intensity = 0.0f, complexity = 0.0f, chaos = 0.0f, density = 0.0f;
    float weight[numAlgos] {};
    int stutterDivision = 0;
};

/** `profile` is clamped into [0, getNumSurpriseProfiles()). */
const SurpriseTargets& surpriseTargets (int profile) noexcept;

} // namespace gapfill
