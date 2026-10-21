#pragma once

// U-4 file adapter: saving, loading, renaming and deleting user presets.
//
// A `.gapfill` file stays the plain APVTS parameter tree (the format shipped in
// beta1); name / category / description / author are four OPTIONAL root attributes
// that parameter validation ignores. Loading is always the single validated path
// (`GapFillAudioProcessor::loadParameterStateFromFile`): every load pushes an Undo
// snapshot and rejections are reported exactly as before.

#include "PresetCatalog.h"

namespace gapfill
{
class GapFillAudioProcessor;
}

namespace gapfill::presets
{

struct PresetMeta
{
    juce::String name, category, description, author;
};

/** `<AppData>/GapFill/Presets` (decision D11) - where user presets live, categories as subfolders. */
juce::File userPresetRoot();

/** Same folder; kept as a name for callers that predate decision D11. */
juce::File legacyPresetRoot();

/** Roots handed to scanUserPresets (new root first). */
std::vector<ScanRoot> defaultScanRoots();

/** `<root>/<Category>/<Name>.gapfill`; "Uncategorised" or an unknown category writes flat. */
juce::File presetPathFor (const juce::File& root, const juce::String& category, const juce::String& sanitisedName);

/** Writes the current parameters plus the metadata. False + `error` on failure. */
bool writeUserPreset (GapFillAudioProcessor&, const juce::File& file, const PresetMeta&, juce::String& error);

/** Loads `entry` (factory or user) through the validated path. */
bool loadPreset (GapFillAudioProcessor&, const PresetEntry& entry, juce::String& error);

/** Renames a user preset (file name and `name` attribute). Refuses to overwrite. */
bool renameUserPreset (const PresetEntry& entry, const juce::String& newName, juce::String& error, juce::File* newFile = nullptr);

/** Moves a user preset file to the trash / deletes it. */
bool deleteUserPreset (const PresetEntry& entry, juce::String& error);

} // namespace gapfill::presets
