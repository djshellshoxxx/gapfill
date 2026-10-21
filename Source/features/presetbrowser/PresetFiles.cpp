#include "PresetFiles.h"

#include "../../PluginProcessor.h"

namespace gapfill::presets
{

juce::File userPresetRoot()
{
    // Decision D11 (docs/dev/features/DECISIONS.md): keep the AppData root; categories are subfolders.
    return legacyPresetRoot();
}

juce::File legacyPresetRoot()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
             .getChildFile ("GapFill").getChildFile ("Presets");
}

std::vector<ScanRoot> defaultScanRoots()
{
    return { { "app", userPresetRoot() } };
}

juce::File presetPathFor (const juce::File& root, const juce::String& category, const juce::String& name)
{
    const auto cat = normaliseCategory (category);

    if (cat == kUncategorised)
        return root.getChildFile (name + ".gapfill");

    return root.getChildFile (cat).getChildFile (name + ".gapfill");
}

bool writeUserPreset (GapFillAudioProcessor& proc, const juce::File& file, const PresetMeta& meta, juce::String& error)
{
    error.clear();

    const auto name = sanitisePresetName (meta.name);

    if (file == juce::File() || file.isDirectory() || name.isEmpty())
    {
        error = "Please choose a preset name that contains valid filename characters.";
        return false;
    }

    auto tree = proc.getValueTreeState().copyState();

    // Always (re)write the four attributes so stale values never leak from a previous load.
    for (const char* key : { "name", "category", "description", "author" })
        tree.removeProperty (key, nullptr);

    tree.setProperty ("name", name, nullptr);

    const auto category = normaliseCategory (meta.category);

    if (category != kUncategorised)
        tree.setProperty ("category", category, nullptr);

    if (const auto d = cleanMetaText (meta.description, kMaxDescriptionLength); d.isNotEmpty())
        tree.setProperty ("description", d, nullptr);

    if (const auto a = cleanMetaText (meta.author, kMaxAuthorLength); a.isNotEmpty())
        tree.setProperty ("author", a, nullptr);

    auto xml = tree.createXml();

    if (xml == nullptr)
    {
        error = "GapFill could not create preset data.";
        return false;
    }

    if (! file.getParentDirectory().exists() && ! file.getParentDirectory().createDirectory().wasOk())
    {
        error = "GapFill could not create the user preset folder.";
        return false;
    }

    // Atomic: the old file stays intact if writing fails half-way.
    juce::TemporaryFile temp (file);

    if (! xml->writeTo (temp.getFile()) || ! temp.overwriteTargetFileWithTemporary())
    {
        error = "GapFill could not write the preset file. Check the folder permissions and available disk space.";
        return false;
    }

    return true;
}

bool loadPreset (GapFillAudioProcessor& proc, const PresetEntry& entry, juce::String& error)
{
    error.clear();

    if (entry.origin == PresetEntry::Origin::Factory)
    {
        if (entry.factoryIndex < 0 || entry.factoryIndex >= proc.getNumPrograms())
        {
            error = "That factory preset no longer exists.";
            return false;
        }

        proc.selectPreset (entry.factoryIndex);            // pushes an Undo snapshot
        return true;
    }

    if (! proc.loadParameterStateFromFile (entry.file, error))
        return false;

    // The metadata attributes belong to the file, not to the running state.
    auto& state = proc.getValueTreeState().state;

    for (const char* key : { "name", "category", "description", "author" })
        state.removeProperty (key, nullptr);

    return true;
}

bool renameUserPreset (const PresetEntry& entry, const juce::String& newName, juce::String& error, juce::File* newFile)
{
    error.clear();
    const auto name = sanitisePresetName (newName);

    if (entry.origin != PresetEntry::Origin::User || ! entry.file.existsAsFile())
    {
        error = "Only user presets can be renamed.";
        return false;
    }

    if (name.isEmpty())
    {
        error = "Please choose a preset name that contains valid filename characters.";
        return false;
    }

    const auto target = entry.file.getParentDirectory().getChildFile (name + ".gapfill");

    if (target.existsAsFile() && target != entry.file)
    {
        error = "A preset with that name already exists.";
        return false;
    }

    auto xml = juce::XmlDocument::parse (entry.file);

    if (xml == nullptr)
    {
        error = "That preset file is not valid GapFill XML.";
        return false;
    }

    xml->setAttribute ("name", name);

    juce::TemporaryFile temp (target);

    if (! xml->writeTo (temp.getFile()) || ! temp.overwriteTargetFileWithTemporary())
    {
        error = "GapFill could not write the renamed preset.";
        return false;
    }

    if (target != entry.file)
        entry.file.deleteFile();

    if (newFile != nullptr)
        *newFile = target;

    return true;
}

bool deleteUserPreset (const PresetEntry& entry, juce::String& error)
{
    error.clear();

    if (entry.origin != PresetEntry::Origin::User || ! entry.file.existsAsFile())
    {
        error = "Only user presets can be deleted.";
        return false;
    }

    if (! entry.file.moveToTrash() && ! entry.file.deleteFile())
    {
        error = "GapFill could not delete that preset file.";
        return false;
    }

    return true;
}

} // namespace gapfill::presets
