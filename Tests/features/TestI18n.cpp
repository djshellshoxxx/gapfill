// F-P04 catalog and UI localization contracts.
#include "../support/TestRig.h"
#include "features/i18n/Catalog.h"
#include "features/i18n/Strings.h"
#include "features/i18n/ComponentTranslator.h"
#include "../support/AllocGuard.h"
#include <array>

using namespace gapfill::i18n;

GF_TEST (I18n, missing_key_falls_back)
{
    Catalog english, partial;
    juce::String error;
    const juce::String base = R"({"greeting":"Hello","farewell":"Goodbye"})";
    const juce::String overlay = R"({"greeting":"Bonjour"})";
    GF_EXPECT (english.load (base.toRawUTF8(), base.getNumBytesAsUTF8(), error));
    GF_EXPECT (partial.load (overlay.toRawUTF8(), overlay.getNumBytesAsUTF8(), error, &english));
    GF_EXPECT (partial.get ("farewell", &english) == "Goodbye");
    ScopedLanguage language ("pseudo");
    GF_EXPECT (tr ("common.ok").isNotEmpty());
    GF_EXPECT (text ("Unknown user data") == "Unknown user data");
    GF_EXPECT (currentLanguage() == "pseudo");
}

GF_TEST (I18n, all_keys_used_exist_and_pseudo_expands_complete_strings)
{
    Catalog english, pseudo;
    juce::String error;
    const auto languages = gftest::testDataDir().getParentDirectory().getChildFile ("Resources/lang");
    GF_EXPECT (english.loadFile (languages.getChildFile ("en.json"), error));
    GF_EXPECT (pseudo.loadFile (languages.getChildFile ("pseudo.json"), error, &english));
    ScopedLanguage language ("en");
    for (const auto& entry : english.entries())
    {
        GF_EXPECT (tr (entry.first) == entry.second);
        if (entry.first != "locale.nativeName")
            GF_EXPECT (pseudo.get (entry.first).length() >= (int) std::ceil (entry.second.length() * 1.4));
    }
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

GF_TEST (I18n, component_translation_preserves_ids_user_data_and_english_originals)
{
    ScopedLanguage language ("en");
    juce::Component root;
    juce::Label label, userName;
    juce::ComboBox choices, userChoices;
    label.setText ("Language", juce::dontSendNotification);
    userName.setText ("Language", juce::dontSendNotification); // intentional collision with a catalog entry
    markUserContent (userName);
    choices.addItem ("OK", 10);
    choices.addItem ("Language", 20);
    choices.setSelectedId (20, juce::dontSendNotification);
    userChoices.addItem ("Language", 51);
    userChoices.setSelectedId (51, juce::dontSendNotification);
    markUserContent (userChoices);
    for (auto* c : std::array<juce::Component*, 4> { &label, &userName, &choices, &userChoices })
        root.addAndMakeVisible (*c);
    ComponentTranslator translate (root);
    GF_EXPECT (setLanguage ("pseudo", false));
    translate.refresh();
    const auto once = label.getText();
    GF_EXPECT (once != "Language");
    GF_EXPECT (userName.getText() == "Language");
    GF_EXPECT (userChoices.getItemText (0) == "Language");
    GF_EXPECT (choices.getSelectedId() == 20);
    translate.refresh();
    GF_EXPECT (label.getText() == once);
    GF_EXPECT (setLanguage ("en", false));
    translate.refresh();
    GF_EXPECT (label.getText() == "Language");
    GF_EXPECT (choices.getItemText (0) == "OK");
}

GF_TEST (I18n, unchanged_pseudo_refresh_does_not_allocate)
{
    ScopedLanguage language ("pseudo");
    juce::Component root;
    juce::Label label;
    label.setText ("Language", juce::dontSendNotification);
    root.addAndMakeVisible (label);
    ComponentTranslator translate (root);
    translate.refresh();
    gftest::AllocGuard guard;
    translate.refresh();
    GF_EXPECT (guard.count() == 0);
}

GF_TEST (I18n, new_components_and_choices_translate_without_changing_model_ids)
{
    ScopedLanguage language ("pseudo");
    juce::Component root;
    ComponentTranslator translate (root);
    juce::Label late;
    late.setText ("Language", juce::dontSendNotification);
    root.addAndMakeVisible (late);
    translate.refresh();
    GF_EXPECT (late.getText() == text ("Language"));
    juce::ComboBox choices;
    root.addAndMakeVisible (choices);
    translate.refresh();
    choices.addItem ("OK", 73);
    choices.setSelectedId (73, juce::dontSendNotification);
    translate.refresh();
    GF_EXPECT (choices.getItemText (0) == text ("OK"));
    GF_EXPECT (choices.getSelectedId() == 73);
}
