#pragma once

// The per-user global settings file (docs/dev/05 U-4 "Engineering").
//
//   <userApplicationDataDirectory>/GapFill/settings.xml  (juce::PropertiesFile)
//
// Holds only per-USER choices that must not live in a project: favourites, interface
// size (U-8), first-run flag (U-7), contrast / control size / focus ring (F-D04).
// No network, no telemetry. Read once when the shared instance is created (message
// thread), written on change with the PropertiesFile's own 500 ms debounce. An
// unreadable file means defaults apply silently (and `isUsable()` is false so the
// first-run guide errs on the side of NOT showing).
//
// One instance is shared by every plug-in instance in the process (so two
// instances never overwrite each other's changes); tests redirect it with
// `useFileForTests`. Message thread only.

#include <juce_data_structures/juce_data_structures.h>

#include <memory>

namespace gapfill::settings
{

inline constexpr int kMaxFavourites = 500;
inline constexpr int kMaxFavouriteLength = 200;

class GlobalSettings
{
public:
    explicit GlobalSettings (const juce::File& file);
    ~GlobalSettings();

    /** The process-wide instance (created on first use at `defaultFile()`). */
    static std::shared_ptr<GlobalSettings> shared();

    /** `GAPFILL_SETTINGS_FILE` if set, else the per-user file. Unit-test builds
        default to a file in the temp directory so tests never touch real settings. */
    static juce::File defaultFile();

    /** Replaces the shared instance (tests). Existing shared_ptr holders keep the old one. */
    static void useFileForTests (const juce::File&);

    //--- state ------------------------------------------------------------------
    /** False when the file exists but could not be parsed, or its folder cannot be created. */
    bool isUsable() const noexcept     { return usable; }
    const juce::File& getFile() const noexcept { return file; }

    //--- typed values -----------------------------------------------------------
    int  getInt (const juce::String& key, int fallback) const;
    void setInt (const juce::String& key, int value);
    bool getBool (const juce::String& key, bool fallback) const;
    void setBool (const juce::String& key, bool value);

    //--- favourites (U-4 / F-S11) -----------------------------------------------
    juce::StringArray getFavourites() const;
    bool isFavourite (const juce::String& identity) const;
    /** Returns the new state. Ignored (false) for empty / over-long identities or a full list. */
    bool setFavourite (const juce::String& identity, bool on);

    //--- named values (keys used by the other WP11 tickets) -----------------------
    static constexpr const char* keyUiScale     = "uiScalePercent";   // U-8: 100..200
    static constexpr const char* keyFirstRun    = "firstRunDone";     // U-7
    static constexpr const char* keyContrast    = "contrast";         // F-D04: 0 standard, 1 high, 2 dim
    static constexpr const char* keyControlSize = "controlSize";      // F-D04: 0 standard, 1 large
    static constexpr const char* keyFocusRing   = "focusRingPx";      // F-D04: 2 or 3

    /** Writes pending changes now (also done by the destructor). */
    void flush();

private:
    juce::File file;
    std::unique_ptr<juce::PropertiesFile> props;
    bool usable = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlobalSettings)
};

} // namespace gapfill::settings
