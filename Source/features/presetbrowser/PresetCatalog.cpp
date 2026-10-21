#include "PresetCatalog.h"

#include "../../Presets.h"

#include <juce_data_structures/juce_data_structures.h>

#include <algorithm>

namespace gapfill::presets
{

const juce::StringArray& presetCategories()
{
    static const juce::StringArray categories { "Subtle", "Rhythmic", "Glitch", "Ambient", "Dub",
                                                "Destructive", "Utility", "Starting Points" };
    return categories;
}

//==============================================================================
juce::String cleanMetaText (const juce::String& raw, int maxLength)
{
    juce::String out;

    for (auto c = raw.getCharPointer(); ! c.isEmpty(); ++c)
    {
        const auto ch = *c;

        if (ch >= 32 && ch != 127 && ! (ch >= 0x80 && ch < 0xa0))      // printable only
            out << juce::String::charToString (ch);

        if (out.length() >= maxLength)
            break;
    }

    return out.trim();
}

juce::String sanitisePresetName (const juce::String& raw)
{
    // Characters that are illegal in a file name on at least one supported platform.
    auto name = cleanMetaText (raw, 4 * kMaxNameLength);

    juce::String out;

    for (auto c = name.getCharPointer(); ! c.isEmpty(); ++c)
    {
        const auto ch = *c;

        if (juce::String ("\\/:*?\"<>|").containsChar (ch))
            continue;

        out << juce::String::charToString (ch);
    }

    out = out.trim();

    while (out.startsWithChar ('.'))
        out = out.substring (1).trim();

    while (out.endsWithChar ('.'))
        out = out.dropLastCharacters (1).trim();

    if (out.length() > kMaxNameLength)
        out = out.substring (0, kMaxNameLength).trim();

    // Windows reserved device names.
    const auto upper = out.toUpperCase();

    for (const char* reserved : { "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "LPT1", "LPT2", "LPT3" })
        if (upper == reserved)
            return {};

    return out;
}

juce::String normaliseCategory (const juce::String& raw)
{
    const auto trimmed = raw.trim();

    for (const auto& c : presetCategories())
        if (c.equalsIgnoreCase (trimmed))
            return c;

    return kUncategorised;
}

//==============================================================================
std::vector<PresetEntry> factoryEntries()
{
    std::vector<PresetEntry> out;
    const auto& factory = getFactoryPresets();

    for (int i = 0; i < (int) factory.size(); ++i)
    {
        const auto& p = factory[(size_t) i];
        PresetEntry e;
        e.origin = PresetEntry::Origin::Factory;
        e.name = p.name;
        e.description = p.description;
        e.category = normaliseCategory (p.category != nullptr ? p.category : "");
        e.identity = "f:" + e.name;
        e.factoryIndex = i;
        out.push_back (std::move (e));
    }

    return out;
}

namespace
{
    PresetEntry makeUserEntry (const ScanRoot& root, const juce::File& file, const juce::String& folderCategory)
    {
        PresetEntry e;
        e.origin = PresetEntry::Origin::User;
        e.file = file;
        e.root = root.folder;
        e.rootTag = root.tag;
        e.name = cleanMetaText (file.getFileNameWithoutExtension(), kMaxNameLength);
        e.category = folderCategory.isEmpty() ? juce::String (kUncategorised) : normaliseCategory (folderCategory);
        e.identity = "u:" + root.tag + "/" + file.getRelativePathFrom (root.folder).replaceCharacter ('\\', '/');

        // Optional root attributes override the file-name defaults. Anything malformed is ignored.
        if (auto xml = juce::XmlDocument::parse (file))
        {
            const auto name = cleanMetaText (xml->getStringAttribute ("name"), kMaxNameLength);
            const auto category = xml->getStringAttribute ("category");

            if (name.isNotEmpty())
                e.name = name;

            if (category.trim().isNotEmpty())
                e.category = normaliseCategory (category);

            e.description = cleanMetaText (xml->getStringAttribute ("description"), kMaxDescriptionLength);
            e.author = cleanMetaText (xml->getStringAttribute ("author"), kMaxAuthorLength);
        }

        if (e.name.isEmpty())
            e.name = "Preset";

        return e;
    }
}

ScanResult scanUserPresets (const std::vector<ScanRoot>& roots, int maxFiles, juce::int64 maxBytes)
{
    ScanResult result;
    int seen = 0;

    for (const auto& root : roots)
    {
        if (! root.folder.isDirectory())
            continue;

        const auto consider = [&] (const juce::File& file, const juce::String& folderCategory) -> bool
        {
            if (seen >= maxFiles)
            {
                result.capped = true;
                return false;
            }

            ++seen;

            if (file.getSize() > maxBytes)
            {
                ++result.skippedOversize;
                return true;
            }

            result.entries.push_back (makeUserEntry (root, file, folderCategory));
            return true;
        };

        // Flat legacy files first (they appear under "Uncategorised").
        auto flat = root.folder.findChildFiles (juce::File::findFiles, false, "*.gapfill");
        flat.sort();

        for (const auto& f : flat)
            if (! consider (f, {}))
                break;

        auto folders = root.folder.findChildFiles (juce::File::findDirectories, false);
        folders.sort();

        for (const auto& folder : folders)
        {
            auto files = folder.findChildFiles (juce::File::findFiles, false, "*.gapfill");
            files.sort();

            for (const auto& f : files)
                if (! consider (f, folder.getFileName()))
                    break;
        }
    }

    std::stable_sort (result.entries.begin(), result.entries.end(),
                      [] (const PresetEntry& a, const PresetEntry& b)
                      {
                          if (a.category != b.category)
                              return a.category.compareIgnoreCase (b.category) < 0;

                          return a.name.compareIgnoreCase (b.name) < 0;
                      });

    return result;
}

//==============================================================================
void PresetCatalog::setFactory (std::vector<PresetEntry> e)
{
    factory = std::move (e);
    rebuild();
}

void PresetCatalog::setUser (std::vector<PresetEntry> e)
{
    user = std::move (e);
    rebuild();
}

void PresetCatalog::rebuild()
{
    all = factory;
    all.insert (all.end(), user.begin(), user.end());
}

std::vector<int> PresetCatalog::filter (const juce::String& search,
                                        const juce::String& category,
                                        const juce::StringArray& favourites) const
{
    std::vector<int> out;
    const auto needle = search.trim();

    for (int i = 0; i < (int) all.size(); ++i)
    {
        const auto& e = all[(size_t) i];

        if (category.isNotEmpty())
        {
            if (category == kFavourites)
            {
                if (! favourites.contains (e.identity))
                    continue;
            }
            else if (e.category != category)
            {
                continue;
            }
        }

        if (needle.isNotEmpty() && ! e.name.containsIgnoreCase (needle) && ! e.description.containsIgnoreCase (needle))
            continue;

        out.push_back (i);
    }

    return out;
}

int PresetCatalog::indexOfIdentity (const juce::String& identity) const noexcept
{
    for (int i = 0; i < (int) all.size(); ++i)
        if (all[(size_t) i].identity == identity)
            return i;

    return -1;
}

int PresetCatalog::step (int current, int delta) const noexcept
{
    const auto n = (int) all.size();

    if (n == 0)
        return -1;

    if (current < 0 || current >= n)
        return delta >= 0 ? 0 : n - 1;

    return ((current + delta) % n + n) % n;
}

} // namespace gapfill::presets
