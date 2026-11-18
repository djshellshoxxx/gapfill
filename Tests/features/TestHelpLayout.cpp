#include "../TestFramework.h"
#include "../../Source/gui/HelpView.h"
#include "../../Source/features/i18n/Strings.h"

namespace
{
    struct RestoreLanguage
    {
        juce::String previous = gapfill::i18n::currentLanguage();
        ~RestoreLanguage() { gapfill::i18n::setLanguage (previous, false); }
    };
}

GF_TEST (HelpLayout, zero_width_constructor_waits_for_real_text_geometry)
{
    RestoreLanguage restore;
    GF_EXPECT (gapfill::i18n::setLanguage ("en", false));
    gapfill::HelpContent content;
    GF_EXPECT (content.getWidth() == 0);
    GF_EXPECT (content.idealHeight() == 40); // no throwaway 120-pixel layout before the first real width
    content.setSize (700, 100);
    GF_EXPECT (content.getWidth() == 700 && content.idealHeight() > 100);
    GF_EXPECT (! content.hasHorizontalOverflow());
}

GF_TEST (HelpLayout, existing_width_rebuilds_full_help_when_language_changes)
{
    RestoreLanguage restore;
    GF_EXPECT (gapfill::i18n::setLanguage ("en", false));
    gapfill::HelpContent content;
    content.setSize (700, 100);
    const auto englishHeight = content.idealHeight();
    GF_EXPECT (englishHeight > 100);
    GF_EXPECT (gapfill::i18n::setLanguage ("pseudo", false));
    gapfill::i18n::languageChanges().sendSynchronousChangeMessage();
    GF_EXPECT (content.getWidth() == 700 && content.idealHeight() > englishHeight);
    GF_EXPECT (! content.hasHorizontalOverflow());
    GF_EXPECT (gapfill::i18n::setLanguage ("en", false));
    gapfill::i18n::languageChanges().sendSynchronousChangeMessage();
    GF_EXPECT (content.idealHeight() == englishHeight);
}

GF_TEST (HelpLayout, minimum_default_and_large_widths_keep_actual_shaped_glyphs_in_bounds)
{
    RestoreLanguage restore;
    for (const auto* language : { "en", "pseudo" })
    {
        GF_EXPECT (gapfill::i18n::setLanguage (language, false));
        gapfill::HelpContent content;
        for (const int width : { 320, 700, 1250 })
        {
            content.setSize (width, 100);
            GF_EXPECT (content.getWidth() == width && content.idealHeight() > 100);
            GF_EXPECT (! content.hasHorizontalOverflow());
        }
    }
}
