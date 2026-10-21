// U-4 preset browser: catalog, user preset files, global settings, browser component.

#include "../support/TestRig.h"

#include "PluginEditor.h"
#include "features/presetbrowser/GlobalSettings.h"
#include "features/presetbrowser/PresetBrowser.h"
#include "features/presetbrowser/PresetCatalog.h"
#include "features/presetbrowser/PresetFiles.h"

using namespace gapfill;
using namespace gapfill::presets;
using namespace gftest;

namespace
{
    struct TempDir
    {
        TempDir()
        {
            dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getChildFile ("gapfill-test-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));
            dir.createDirectory();
        }

        ~TempDir() { dir.deleteRecursively(); }

        juce::File dir;
    };

    void writeFile (const juce::File& f, const juce::String& text)
    {
        f.getParentDirectory().createDirectory();
        f.replaceWithText (text);
    }

    const char* const kValidPreset = "<PARAMETERS/>";   // a parameter-free tree is a valid (neutral) preset
}

GF_TEST (PresetBrowser, factory_presets_have_valid_unique_categories_and_stable_order)
{
    const auto entries = factoryEntries();
    GF_EXPECT (entries.size() >= 17);

    // The first 17 are the beta1 programs: their order is the permanent host program index.
    const char* const expected[] = { "Init", "Subtle Ear Candy", "Drum Gap Glitch", "DnB Micro Fills", "Breakcore Destroyer",
                                     "Ghost Reverses", "Industrial Debris", "Vocal Ghosts", "Vocal Delay Fills",
                                     "Bass Between Kicks", "Granular Spaces", "Trance Transitions", "Trip Hop Dust",
                                     "Spectral High Fill", "Spectral Mid Mutation", "Sparse Texture", "Extreme Chaos" };

    for (int i = 0; i < 17; ++i)
        GF_EXPECT_MSG (entries[(size_t) i].name == expected[i], expected[i]);

    juce::StringArray names;

    for (const auto& e : entries)
    {
        GF_EXPECT_MSG (presetCategories().contains (e.category), e.name + " -> " + e.category);
        GF_EXPECT_MSG (! names.contains (e.name), "duplicate " + e.name);
        names.add (e.name);
    }
}

GF_TEST (PresetBrowser, search_filters_name_and_description)
{
    PresetCatalog c;
    c.setFactory (factoryEntries());

    const auto ghosts = c.filter ("ghost", kAllCategories, {});
    GF_EXPECT (ghosts.size() >= 2);                                  // "Ghost Reverses", "Vocal Ghosts"

    for (const auto i : ghosts)
        GF_EXPECT (c.entries()[(size_t) i].name.containsIgnoreCase ("ghost")
                   || c.entries()[(size_t) i].description.containsIgnoreCase ("ghost"));

    // A word that only appears in a description ("shrapnel" - Industrial Debris).
    const auto byDescription = c.filter ("SHRAPNEL", kAllCategories, {});
    GF_EXPECT (byDescription.size() == 1 && c.entries()[(size_t) byDescription[0]].name == "Industrial Debris");

    GF_EXPECT (c.filter ("   ", kAllCategories, {}).size() == c.entries().size());
    GF_EXPECT (c.filter ("zzzzzz-nothing", kAllCategories, {}).empty());
}

GF_TEST (PresetBrowser, category_filter)
{
    PresetCatalog c;
    c.setFactory (factoryEntries());

    for (const auto& cat : presetCategories())
        for (const auto i : c.filter ({}, cat, {}))
            GF_EXPECT (c.entries()[(size_t) i].category == cat);

    size_t total = 0;
    for (const auto& cat : presetCategories())
        total += c.filter ({}, cat, {}).size();
    GF_EXPECT_MSG (total + c.filter ({}, kUncategorised, {}).size() == c.entries().size(), "every preset is in exactly one category");

    juce::StringArray fav { "f:Init", "f:Extreme Chaos", "u:doc/gone.gapfill" };
    const auto favourites = c.filter ({}, kFavourites, fav);
    GF_EXPECT (favourites.size() == 2);
    GF_EXPECT (c.filter ("extreme", kFavourites, fav).size() == 1);   // search and category combine
}

GF_TEST (PresetBrowser, next_prev_wraps)
{
    PresetCatalog c;
    c.setFactory (factoryEntries());
    const auto n = (int) c.entries().size();

    GF_EXPECT (c.step (0, -1) == n - 1);
    GF_EXPECT (c.step (n - 1, +1) == 0);
    GF_EXPECT (c.step (3, +1) == 4 && c.step (3, -1) == 2);
    GF_EXPECT_MSG (c.step (-1, +1) == 0 && c.step (-1, -1) == n - 1, "a modified (Custom) sound steps to the ends");

    PresetCatalog empty;
    GF_EXPECT (empty.step (0, 1) == -1);

    // A full lap returns to the start.
    int i = 5;
    for (int k = 0; k < n; ++k)
        i = c.step (i, +1);
    GF_EXPECT (i == 5);
}

GF_TEST (PresetBrowser, favourites_roundtrip)
{
    TempDir tmp;
    const auto file = tmp.dir.getChildFile ("settings.xml");

    {
        settings::GlobalSettings s (file);
        GF_EXPECT (s.getFavourites().isEmpty());
        GF_EXPECT (s.setFavourite ("f:Init", true));
        GF_EXPECT (s.setFavourite ("u:doc/Dub/My Dub.gapfill", true));
        GF_EXPECT (s.setFavourite ("f:Init", true));                  // idempotent
        GF_EXPECT (s.getFavourites().size() == 2);
        GF_EXPECT (! s.setFavourite ("f:Init", false));
        GF_EXPECT (! s.isFavourite ("f:Init") && s.isFavourite ("u:doc/Dub/My Dub.gapfill"));
        GF_EXPECT (! s.setFavourite ("", true));
        GF_EXPECT (! s.setFavourite (juce::String::repeatedString ("x", 300), true));
        s.setInt (settings::GlobalSettings::keyUiScale, 150);
        s.flush();
    }

    settings::GlobalSettings reread (file);
    GF_EXPECT (reread.isUsable());
    GF_EXPECT (reread.getFavourites() == juce::StringArray { "u:doc/Dub/My Dub.gapfill" });
    GF_EXPECT (reread.getInt (settings::GlobalSettings::keyUiScale, 100) == 150);
}

GF_TEST (PresetBrowser, settings_file_unreadable_defaults)
{
    TempDir tmp;
    const auto file = tmp.dir.getChildFile ("settings.xml");
    file.replaceWithText ("this is { not xml");

    settings::GlobalSettings s (file);
    GF_EXPECT_MSG (! s.isUsable(), "an unparsable file is reported as unusable");
    GF_EXPECT (s.getFavourites().isEmpty());
    GF_EXPECT (s.getInt (settings::GlobalSettings::keyUiScale, 100) == 100);
    GF_EXPECT (! s.getBool (settings::GlobalSettings::keyFirstRun, false));
    s.setFavourite ("f:Init", true);                                  // must not throw or crash
    s.flush();
    GF_EXPECT_MSG (file.loadFileAsString() == "this is { not xml", "an unreadable file is never overwritten");
}

GF_TEST (PresetBrowser, sanitise_preset_name)
{
    GF_EXPECT (sanitisePresetName ("My Preset") == "My Preset");
    GF_EXPECT (sanitisePresetName ("  a/b\\c:d*e?f\"g<h>i|j  ") == "abcdefghij");
    GF_EXPECT (sanitisePresetName ("../../etc/passwd") == "etcpasswd");
    GF_EXPECT (sanitisePresetName ("...hidden...") == "hidden");
    GF_EXPECT (sanitisePresetName ("///").isEmpty());
    GF_EXPECT (sanitisePresetName ("   ").isEmpty());
    GF_EXPECT (sanitisePresetName ("CON").isEmpty());
    GF_EXPECT (sanitisePresetName (juce::String::repeatedString ("a", 100)).length() == kMaxNameLength);
    GF_EXPECT (sanitisePresetName (juce::String ("tab\there\nnew")) == "tabherenew");

    GF_EXPECT (normaliseCategory ("dub") == "Dub");
    GF_EXPECT (normaliseCategory ("nonsense") == kUncategorised);
    GF_EXPECT (cleanMetaText (juce::String ("a") + juce::String::charToString (7) + "b", 10) == "ab");
}

GF_TEST (PresetBrowser, user_scan_caps_and_skips_oversize)
{
    TempDir tmp;

    for (int i = 0; i < 30; ++i)
        writeFile (tmp.dir.getChildFile ("Glitch").getChildFile ("p" + juce::String (i) + ".gapfill"), kValidPreset);

    juce::MemoryBlock big;
    big.setSize ((size_t) kMaxPresetFileBytes + 10, true);
    tmp.dir.getChildFile ("Glitch").getChildFile ("huge.gapfill").replaceWithData (big.getData(), big.getSize());

    auto all = scanUserPresets ({ { "doc", tmp.dir } });
    GF_EXPECT (all.entries.size() == 30);
    GF_EXPECT (all.skippedOversize == 1);
    GF_EXPECT (! all.capped);

    auto capped = scanUserPresets ({ { "doc", tmp.dir } }, 10);
    GF_EXPECT (capped.capped && capped.entries.size() <= 10);

    // An empty / missing root is fine.
    GF_EXPECT (scanUserPresets ({ { "doc", tmp.dir.getChildFile ("nope") } }).entries.empty());
}

GF_TEST (PresetBrowser, legacy_flat_files_listed)
{
    TempDir tmp;
    writeFile (tmp.dir.getChildFile ("Old Flat.gapfill"), kValidPreset);
    writeFile (tmp.dir.getChildFile ("Dub").getChildFile ("Deep.gapfill"), kValidPreset);

    const auto r = scanUserPresets ({ { "app", tmp.dir } });
    GF_EXPECT (r.entries.size() == 2);

    const PresetEntry* flat = nullptr;
    const PresetEntry* deep = nullptr;

    for (const auto& e : r.entries)
    {
        if (e.name == "Old Flat") flat = &e;
        if (e.name == "Deep")     deep = &e;
    }

    GF_EXPECT (flat != nullptr && flat->category == kUncategorised && flat->identity == "u:app/Old Flat.gapfill");
    GF_EXPECT (deep != nullptr && deep->category == "Dub" && deep->identity == "u:app/Dub/Deep.gapfill");
}

GF_TEST (PresetBrowser, corrupt_properties_ignored_not_fatal)
{
    TempDir tmp;
    const auto bad = juce::String ("<PARAMETERS name=\"") + juce::String::repeatedString ("N", 500) + "\" category=\"bogus\" description=\""
                   + juce::String::charToString (3) + "x\" author=\"" + juce::String::repeatedString ("A", 400) + "\"/>";
    writeFile (tmp.dir.getChildFile ("Weird.gapfill"), bad);
    writeFile (tmp.dir.getChildFile ("Broken.gapfill"), "<<< not xml");

    const auto r = scanUserPresets ({ { "doc", tmp.dir } });
    GF_EXPECT (r.entries.size() == 2);

    for (const auto& e : r.entries)
    {
        GF_EXPECT (e.name.length() <= kMaxNameLength);
        GF_EXPECT (e.author.length() <= kMaxAuthorLength);
        GF_EXPECT (e.description.length() <= kMaxDescriptionLength);
        GF_EXPECT (e.category == kUncategorised);              // unknown category falls back
        GF_EXPECT (e.description.containsOnly ("x") || e.description.isEmpty());
    }

    // The broken file is listed (by file name); loading it reports the normal error.
    GapFillAudioProcessor proc;
    juce::String error;
    PresetEntry broken;

    for (const auto& e : r.entries)
        if (e.name == "Broken")
            broken = e;

    GF_EXPECT (broken.file.existsAsFile());
    GF_EXPECT (! loadPreset (proc, broken, error) && error.isNotEmpty());
}

GF_TEST (PresetBrowser, user_preset_save_load_rename_delete)
{
    TempDir tmp;
    GapFillAudioProcessor proc;
    setParams (proc, { { "fill", 77.0f }, { "chaos", 61.0f } });

    const auto file = presetPathFor (tmp.dir, "Dub", "Deep Space");
    GF_EXPECT (file == tmp.dir.getChildFile ("Dub").getChildFile ("Deep Space.gapfill"));
    GF_EXPECT (presetPathFor (tmp.dir, "Uncategorised", "X") == tmp.dir.getChildFile ("X.gapfill"));

    juce::String error;
    GF_EXPECT (writeUserPreset (proc, file, { "Deep Space", "Dub", "Echoes in the dark", "me" }, error));
    GF_EXPECT (! writeUserPreset (proc, file, { "///", "Dub", {}, {} }, error) && error.isNotEmpty());

    const auto found = scanUserPresets ({ { "doc", tmp.dir } });
    GF_EXPECT (found.entries.size() == 1);
    const auto entry = found.entries[0];
    GF_EXPECT (entry.name == "Deep Space" && entry.category == "Dub" && entry.description == "Echoes in the dark" && entry.author == "me");

    // Loading: new values applied, one Undo step pushed, metadata not kept in the running state.
    setParams (proc, { { "fill", 5.0f }, { "chaos", 5.0f } });
    const auto depth = proc.getUndoDepth();
    GF_EXPECT (loadPreset (proc, entry, error));
    GF_EXPECT (proc.getUndoDepth() == depth + 1);
    GF_EXPECT_NEAR (*proc.getValueTreeState().getRawParameterValue ("fill"), 77.0f, 0.01);
    GF_EXPECT (! proc.getValueTreeState().state.hasProperty ("name") && ! proc.getValueTreeState().state.hasProperty ("category"));
    GF_EXPECT (proc.undo());
    GF_EXPECT_NEAR (*proc.getValueTreeState().getRawParameterValue ("fill"), 5.0f, 0.01);

    // Saving again from the loaded state must not inherit stale metadata.
    const auto second = presetPathFor (tmp.dir, "Glitch", "Second");
    GF_EXPECT (writeUserPreset (proc, second, { "Second", "Glitch", {}, {} }, error));
    const auto secondEntry = scanUserPresets ({ { "doc", tmp.dir } });
    for (const auto& e : secondEntry.entries)
        if (e.name == "Second")
            GF_EXPECT (e.description.isEmpty() && e.author.isEmpty() && e.category == "Glitch");

    // Rename refuses to overwrite, then renames.
    juce::File renamed;
    GF_EXPECT (renameUserPreset (entry, "Deeper Space", error, &renamed));
    GF_EXPECT (renamed.existsAsFile() && ! entry.file.existsAsFile());

    auto renamedEntry = entry;
    renamedEntry.file = renamed;
    GF_EXPECT (deleteUserPreset (renamedEntry, error));
    GF_EXPECT (! renamed.existsAsFile());
}

GF_TEST (PresetBrowser, load_pushes_undo)
{
    GapFillAudioProcessor proc;
    const auto entries = factoryEntries();
    const auto depth = proc.getUndoDepth();

    juce::String error;
    GF_EXPECT (loadPreset (proc, entries[3], error));
    GF_EXPECT (proc.getUndoDepth() == depth + 1);
    GF_EXPECT (proc.getCurrentPresetName() == entries[3].name);

    PresetEntry gone = entries[3];
    gone.factoryIndex = 9999;
    GF_EXPECT (! loadPreset (proc, gone, error) && error.isNotEmpty());
}

GF_TEST (PresetBrowser, browser_component_filters_loads_and_stars)
{
    TempDir tmp;
    TempDir settingsDir;
    auto settingsFile = std::make_shared<settings::GlobalSettings> (settingsDir.dir.getChildFile ("s.xml"));

    writeFile (tmp.dir.getChildFile ("Dub").getChildFile ("Deep.gapfill"), kValidPreset);

    GapFillAudioProcessor proc;
    PresetBrowser browser (proc, settingsFile, { { "doc", tmp.dir } });
    browser.refreshNow();

    const auto total = browser.getNumVisible();
    GF_EXPECT (total == (int) factoryEntries().size() + 1);

    browser.setSearch ("ghost");
    GF_EXPECT (browser.getNumVisible() >= 2 && browser.getNumVisible() < total);
    browser.setSearch ({});

    browser.setCategory ("Dub");
    bool sawUser = false;
    for (int i = 0; i < browser.getNumVisible(); ++i)
        sawUser = sawUser || browser.getVisibleEntry (i).origin == PresetEntry::Origin::User;
    GF_EXPECT (sawUser);

    browser.setCategory ({});
    browser.toggleFavouriteRow (1);
    const auto id = browser.getVisibleEntry (1).identity;
    GF_EXPECT (settingsFile->isFavourite (id));
    browser.setCategory (kFavourites);
    GF_EXPECT (browser.getNumVisible() == 1 && browser.getVisibleEntry (0).identity == id);

    PresetEntry loaded;
    browser.onLoaded = [&loaded] (const PresetEntry& e) { loaded = e; };
    browser.setBounds (0, 0, 540, 460);
    GF_EXPECT (browser.loadVisibleRow (0));
    GF_EXPECT (loaded.identity == id);

    browser.setSuggestion ({ "Rhythmic", "Dub", "Subtle" }, "6.2 hits/s, median gap 74 ms");
    GF_EXPECT (browser.getSuggestedCategories().size() == 3);
}

GF_TEST (PresetBrowser, editor_steps_through_presets_with_wrap)
{
    GapFillAudioProcessor proc;
    const auto n = (int) factoryEntries().size();

    proc.getUiState().requestPresetStep (+1);
    proc.getUiState().requestPresetStep (+1);
    GF_EXPECT (proc.getUiState().takePresetStep() == 2);
    GF_EXPECT (proc.getUiState().takePresetStep() == 0);

    for (int i = 0; i < 200; ++i)
        proc.getUiState().requestPresetStep (+1);
    GF_EXPECT (proc.getUiState().takePresetStep() == 64);              // clamped

    juce::ignoreUnused (n);
}
