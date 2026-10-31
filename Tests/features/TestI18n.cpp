// F-P04 catalog and UI localization contracts.
#include "../support/TestRig.h"
#include "features/i18n/Catalog.h"
#include "features/i18n/Strings.h"
#include "features/i18n/ComponentTranslator.h"
#include "features/i18n/RecipePresentation.h"
#include "features/history/Describe.h"
#include "features/presetbrowser/PresetBrowser.h"
#include "features/info/ParamInfo.h"
#include "features/info/StatusBoard.h"
#include "features/coop/Coop.h"
#include "gui/HelpView.h"
#include "../support/AllocGuard.h"
#include <array>
#include <bit>

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
    int changes = 0;
    choices.onChange = [&changes] { ++changes; };
    translate.refresh();
    GF_EXPECT (choices.getItemText (0) == text ("OK"));
    GF_EXPECT (choices.getSelectedId() == 73);
    GF_EXPECT (changes == 0);
    GF_EXPECT (setLanguage ("en", false));
    translate.refresh();
    GF_EXPECT (choices.getSelectedId() == 73 && choices.getText() == "OK" && changes == 0);
}

GF_TEST (I18n, file_error_templates_preserve_paths_as_arguments)
{
    ScopedLanguage language ("pseudo");
    const juce::String path = "/user/Language/OK/{0}.wav";
    const auto displayed = errorText ("Couldn't open " + path + " for writing.");
    GF_EXPECT (displayed == format ("Couldn't open {0} for writing.", { path }));
    GF_EXPECT (displayed.contains (path));
    GF_EXPECT (errorText (path) == path);
}

GF_TEST (I18n, recipe_display_translation_preserves_canonical_name_and_data)
{
    gapfill::FillRecipe recipe;
    recipe.algo = 0;
    recipe.band = 1;
    recipe.lengthMs = 180;
    const auto original = gapfill::history::describeRecipe (recipe, 0.0);
    {
        ScopedLanguage language ("en");
        GF_EXPECT (describeRecipe (recipe) == original);
    }
    {
        ScopedLanguage language ("pseudo");
        GF_EXPECT (describeRecipe (recipe) != original);
        GF_EXPECT (describeRecipe (recipe).contains ("180"));
        GF_EXPECT (gapfill::history::describeRecipe (recipe, 0.0) == original);
        GF_EXPECT (recipe.lengthMs == 180 && recipe.band == 1 && recipe.algo == 0);
    }
}

GF_TEST (I18n, translated_preset_category_chips_keep_canonical_filter_identity)
{
    ScopedLanguage language ("pseudo");
    gapfill::GapFillAudioProcessor processor;
    juce::TemporaryFile temporary (".xml");
    auto settings = std::make_shared<gapfill::settings::GlobalSettings> (temporary.getFile());
    gapfill::presets::PresetBrowser browser (processor, settings, {});
    browser.refreshNow();
    const auto all = browser.getNumVisible();
    browser.setCategory ("Rhythmic");
    for (auto* child : browser.getChildren())
        if (auto* button = dynamic_cast<juce::TextButton*> (child))
            if (button->getProperties()["gf.presetCategory"].toString() == "All")
            {
                GF_EXPECT (button->getButtonText() == text ("All"));
                button->onClick();
                GF_EXPECT (browser.getNumVisible() == all);
                GF_EXPECT (button->getToggleState());
                return;
            }
    GF_EXPECT (false);
}

GF_TEST (I18n, registered_parameter_tooltips_translate_as_presentation_only)
{
    const auto* metadata = gapfill::info::ParamInfoRegistry::get().find ("density");
    GF_EXPECT (metadata != nullptr);
    if (metadata == nullptr) return;
    const auto canonical = gapfill::info::tooltipFor (*metadata);
    ScopedLanguage language ("pseudo");
    const auto displayed = text (canonical);
    GF_EXPECT (displayed != canonical);
    GF_EXPECT (displayed.contains (metadata->typicalRange));
    GF_EXPECT (gapfill::info::tooltipFor (*metadata) == canonical);
    GF_EXPECT (text ("GAP CONFIRM") == text ("Gap Confirm").toUpperCase());
}

GF_TEST (I18n, layout_no_clip_longest_string)
{
    // Help renders every registered prose entry, including the longest sentence,
    // with real shaped fonts. Verify its actual glyph bounds at narrow/default/
    // wide content widths, rather than estimating from character counts.
    for (const auto* code : { "en", "pseudo" })
    {
        ScopedLanguage language (code);
        gapfill::HelpContent content;
        for (const auto width : { 320, 700, 1250 })
        {
            content.setSize (width, 100);
            content.setSize (width, content.idealHeight());
            GF_EXPECT (! content.hasHorizontalOverflow());
        }
    }
}

GF_TEST (I18n, hover_parameter_readout_localizes_only_presentation)
{
    gapfill::GapFillAudioProcessor processor;
    auto& state = processor.getValueTreeState();
    const auto value = state.getParameter ("density")->getValue();
    juce::String english;
    {
        ScopedLanguage language ("en");
        english = gapfill::info::describeParameter (state, "density");
    }
    {
        ScopedLanguage language ("pseudo");
        GF_EXPECT (gapfill::info::describeParameter (state, "density") != english);
        GF_EXPECT (std::bit_cast<std::uint32_t> (state.getParameter ("density")->getValue()) == std::bit_cast<std::uint32_t> (value));
    }
}

GF_TEST (I18n, cooperative_status_localization_preserves_membership_and_slot)
{
    gapfill::coop::CoopMember member;
    member.beginChunk (gapfill::coop::GroupH, gapfill::coop::AvoidOverlap, 48000.0, 1.0, 1);
    const auto group = member.getGroup();
    const auto id = member.getId();
    juce::String english;
    {
        ScopedLanguage language ("en");
        english = member.describe();
    }
    {
        ScopedLanguage language ("pseudo");
        GF_EXPECT (member.describe() != english);
        GF_EXPECT (member.getGroup() == group && member.getId() == id);
    }
    gapfill::coop::DeterministicConfig settings;
    settings.group = gapfill::coop::GroupH;
    settings.slot = 7;
    member.beginDeterministicChunk (settings, {}); // invalid transport pauses; language cannot supply timing
    const auto slot = member.assignedSlot();
    {
        ScopedLanguage language ("en");
        GF_EXPECT (member.describe() == "Cooperation paused: no host transport");
    }
    {
        ScopedLanguage language ("pseudo");
        GF_EXPECT (member.describe() == text ("Cooperation paused: no host transport"));
        GF_EXPECT (member.assignedSlot() == slot);
    }
}
