#include "../TestFramework.h"
#include "../../Source/features/i18n/Strings.h"

GF_TEST (I18n, language_metadata_is_stable_across_selection_and_owned_by_each_caller)
{
    struct RestoreLanguage
    {
        juce::String previous = gapfill::i18n::currentLanguage();
        ~RestoreLanguage() { gapfill::i18n::setLanguage (previous, false); }
    } restore;
    auto original = gapfill::i18n::availableLanguages();
    GF_EXPECT (! original.empty());
    bool english = false, pseudo = false;
    for (const auto& language : original)
    {
        GF_EXPECT (language.name.isNotEmpty());
        english = english || language.code == "en";
        pseudo = pseudo || language.code == "pseudo";
    }
    GF_EXPECT (english && pseudo);
    GF_EXPECT (gapfill::i18n::setLanguage ("pseudo", false));
    const auto afterSelection = gapfill::i18n::availableLanguages();
    GF_EXPECT (afterSelection.size() == original.size());
    for (std::size_t index = 0; index < original.size(); ++index)
    {
        GF_EXPECT (afterSelection[index].code == original[index].code);
        GF_EXPECT (afterSelection[index].name == original[index].name);
    }
    original.front().code = "caller.changed";
    original.front().name = "Caller-owned text";
    const auto independent = gapfill::i18n::availableLanguages();
    GF_EXPECT (independent.front().code == afterSelection.front().code);
    GF_EXPECT (independent.front().name == afterSelection.front().name);
    GF_EXPECT (gapfill::i18n::setLanguage ("en", false));
    const auto englishSelection = gapfill::i18n::availableLanguages();
    GF_EXPECT (englishSelection.front().name == afterSelection.front().name);
}
