// F-P04 catalog and UI localization contracts.
#include "../support/TestRig.h"
#include "features/i18n/Catalog.h"
#include "features/i18n/Strings.h"

using namespace gapfill::i18n;

GF_TEST (I18n, missing_key_falls_back)
{
    ScopedLanguage language ("pseudo");
    GF_EXPECT (tr ("common.ok").isNotEmpty());
    GF_EXPECT (text ("Unknown user data") == "Unknown user data");
    GF_EXPECT (currentLanguage() == "pseudo");
}

GF_TEST (I18n, json_rejects_oversize_malformed)
{
    Catalog catalog;
    juce::String error;
    const juce::String valid = R"({"hello":"Hello"})";
    GF_EXPECT (catalog.load (valid.toRawUTF8(), valid.getNumBytesAsUTF8(), error));
    const juce::String invalid = R"({"hello":123})";
    GF_EXPECT (! catalog.load (invalid.toRawUTF8(), invalid.getNumBytesAsUTF8(), error));
    GF_EXPECT (catalog.get ("hello") == "Hello");
    juce::MemoryBlock tooLarge (maxCatalogBytes + 1, true);
    GF_EXPECT (! catalog.load (tooLarge.getData(), tooLarge.getSize(), error));
    const char badUtf8[] = { '{', '"', 'a', '"', ':', '"', (char) 0xff, '"', '}' };
    GF_EXPECT (! catalog.load (badUtf8, sizeof (badUtf8), error));
}

GF_TEST (I18n, placeholders_balanced_and_arguments_verbatim)
{
    std::set<int> slots;
    GF_EXPECT (Catalog::placeholders ("{0} s ago: {1}", slots));
    GF_EXPECT (slots == std::set<int> ({ 0, 1 }));
    GF_EXPECT (! Catalog::placeholders ("{0} {unclosed", slots));
    GF_EXPECT (! Catalog::placeholders ("unmatched }", slots));
    GF_EXPECT (Catalog::expand ("{1}: {0}", { "My {preset}", "Name" }) == "Name: My {preset}");
}

GF_TEST (I18n, translation_cannot_drop_or_add_placeholders)
{
    Catalog english, translated;
    juce::String error;
    const juce::String source = R"({"message":"Saved {0}."})";
    const juce::String invalid = R"({"message":"Saved."})";
    GF_EXPECT (english.load (source.toRawUTF8(), source.getNumBytesAsUTF8(), error));
    GF_EXPECT (! translated.load (invalid.toRawUTF8(), invalid.getNumBytesAsUTF8(), error, &english));
}

GF_TEST (I18n, default_english_and_language_changes_never_touch_processor_state)
{
    ScopedLanguage language ("en");
    gapfill::GapFillAudioProcessor proc;
    juce::MemoryBlock before, after;
    proc.getStateInformation (before);
    GF_EXPECT (setLanguage ("pseudo", false));
    proc.getStateInformation (after);
    GF_EXPECT (before == after);
    GF_EXPECT (! setLanguage ("../../outside", false));
    GF_EXPECT (currentLanguage() == "pseudo");
}
