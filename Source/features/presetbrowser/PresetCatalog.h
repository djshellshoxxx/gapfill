#pragma once

// U-4 preset browser model (docs/dev/05 U-4): pure logic, no GUI, no processor.
//
//  - one flat list of entries: the factory presets (index order = host program order,
//    which is permanent) followed by the user presets found on disk;
//  - search (name + description, case-insensitive), category and Favourites filters;
//  - wrap-around previous / next;
//  - user preset files: `<root>/<Category>/<Name>.gapfill`, flat files stay visible
//    as "Uncategorised"; scanning is capped (2000 files, 1 MB each) and meant to run on
//    a background thread; it never touches the audio thread.
//
// Preset identity (the favourites key): factory `F:<name>`, user `U:<relative path under the root>` (F-S11 ids).

#include <juce_core/juce_core.h>

#include <vector>

namespace gapfill::presets
{

inline constexpr int kMaxNameLength = 48;
inline constexpr int kMaxDescriptionLength = 160;
inline constexpr int kMaxAuthorLength = 48;
inline constexpr int kMaxScanFiles = 2000;
inline constexpr const char* kKeyStepScope = "presets.stepScope";   ///< GlobalSettings int: 0 all presets, 1 favourites only (F-S11)
inline constexpr juce::int64 kMaxPresetFileBytes = 1024 * 1024;

/** Fixed browser categories (the "Starting Points" group is the eighth). */
const juce::StringArray& presetCategories();

inline constexpr const char* kFavourites = "Favourites";
inline constexpr const char* kUncategorised = "Uncategorised";
inline constexpr const char* kAllCategories = "";

struct PresetEntry
{
    enum class Origin { Factory, User };

    Origin origin = Origin::Factory;
    juce::String name, description, category, author;
    juce::String identity;          ///< favourites key
    int factoryIndex = -1;
    juce::File file;                ///< user presets
    juce::File root;                ///< the root the file was found under (user presets)
    juce::String rootTag;           ///< "doc" or "app"
};

//--- text hygiene ------------------------------------------------------------------
/** Printable text only, control characters removed, trimmed, capped at `maxLength`. */
juce::String cleanMetaText (const juce::String& raw, int maxLength);

/** A file-system-safe preset name: <= 48 characters, no path separators or reserved
    characters, no leading dots. Empty when nothing usable is left. */
juce::String sanitisePresetName (const juce::String& raw);

/** `category` if it is one of the fixed categories (case-insensitive), else "Uncategorised". */
juce::String normaliseCategory (const juce::String& raw);

//--- sources -------------------------------------------------------------------------
std::vector<PresetEntry> factoryEntries();

struct ScanRoot
{
    juce::String tag;
    juce::File folder;
};

struct ScanResult
{
    std::vector<PresetEntry> entries;
    int skippedOversize = 0;
    bool capped = false;
};

/** Finds `*.gapfill` in each root (flat, and one level of category folders). */
ScanResult scanUserPresets (const std::vector<ScanRoot>& roots,
                            int maxFiles = kMaxScanFiles,
                            juce::int64 maxBytes = kMaxPresetFileBytes);

//--- catalog ---------------------------------------------------------------------------
class PresetCatalog
{
public:
    void setFactory (std::vector<PresetEntry>);
    void setUser (std::vector<PresetEntry>);

    const std::vector<PresetEntry>& entries() const noexcept   { return all; }

    /** Indices into entries() matching `search` (name or description contains it,
        case-insensitive; blank = everything) and `category` ("" = all, "Favourites" =
        the favourites list, else the exact category). Order = entries() order. */
    std::vector<int> filter (const juce::String& search,
                             const juce::String& category,
                             const juce::StringArray& favourites) const;

    int indexOfIdentity (const juce::String& identity) const noexcept;

    /** Previous / next with wrap-around. `current` < 0 (a modified "Custom" sound)
        steps to the first entry going forward and the last going back. */
    int step (int current, int delta) const noexcept;

    /** F-S11: indices (ascending) of the entries whose identity is in `favourites`. */
    std::vector<int> favouriteIndices (const juce::StringArray& favourites) const;

    /** F-S11 "Favourites only" stepping: like step(), but only among `pool` (ascending entry indices).
        A `current` outside the pool (a non-favourite, or < 0 for a modified sound) goes to the next
        pool entry after it (previous one before it when going back), wrapping. -1 when the pool is empty. */
    static int stepWithin (const std::vector<int>& pool, int current, int delta) noexcept;

private:
    std::vector<PresetEntry> factory, user, all;
    void rebuild();
};

} // namespace gapfill::presets
