#include "../support/TestRig.h"

#include "features/health/Gui.h"
#include "features/i18n/Strings.h"
#include "features/tour/Tour.h"

using namespace gapfill;

namespace
{
    void buttonsFit (juce::Component& root)
    {
        for (auto* child : root.getChildren())
        {
            if (auto* label = dynamic_cast<TextLabel*> (child); label != nullptr && label->isVisible() && label->getText().isNotEmpty())
                GF_EXPECT_MSG (label->getHeight() >= label->heightForWidth (label->getWidth()), label->getText());
            if (auto* button = dynamic_cast<juce::TextButton*> (child); button != nullptr && button->isVisible())
            {
                GF_EXPECT_MSG (root.getLocalBounds().contains (button->getBounds()), button->getButtonText());
                const auto font = button->getLookAndFeel().getTextButtonFont (*button, button->getHeight());
                const auto width = juce::GlyphArrangement::getStringWidth (font, button->getButtonText()) + 20.0f;
                GF_EXPECT_MSG ((float) button->getWidth() + 0.5f >= width, button->getButtonText());
                for (auto* other : root.getChildren())
                    if (other != button && dynamic_cast<juce::TextButton*> (other) != nullptr && other->isVisible())
                        GF_EXPECT_MSG (! button->getBounds().intersects (other->getBounds()), button->getButtonText());
            }
            buttonsFit (*child);
        }
    }
}

GF_TEST (PresentationLayout, tour_actions_fit_without_truncation_in_english_and_pseudo)
{
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("gapfill-tour-layout-" + juce::Uuid().toString() + ".xml");
    settings::GlobalSettings preferences (file);
    for (const auto* locale : { "en", "pseudo" })
    {
        i18n::ScopedLanguage language (locale);
        tour::TourModel model (preferences, 3);
        tour::TourOverlay overlay (tour::defaultSteps (false), model);
        for (const auto size : { juce::Point<int> (1020, 620), { 1100, 700 }, { 1800, 1000 } })
        {
            overlay.setSize (size.x, size.y);
            model.start();
            // refresh through navigation, preserving the same public path as the UI.
            overlay.keyPressed (juce::KeyPress (juce::KeyPress::leftKey));
            for (int step = 0; step < 3; ++step)
            {
                GF_EXPECT (overlay.getLocalBounds().contains (overlay.getCardBounds()));
                buttonsFit (overlay);
                if (step < 2) overlay.pressNext();
            }
        }
    }
    file.deleteFile();
}

GF_TEST (PresentationLayout, health_confirmation_actions_wrap_inside_the_status_details)
{
    for (const auto* locale : { "en", "pseudo" })
    {
        i18n::ScopedLanguage language (locale);
        GapFillAudioProcessor proc;
        gftest::setParams (proc, { { "fill", 50.0f }, { "protNext", 80.0f } });
        fixit::Evidence evidence;
        evidence.numGaps = 8;
        evidence.reasons[(size_t) RejectReason::GuardEaten] = 8;
        fixit::ApvtsParamAccess access (proc.getValueTreeState());
        auto& controller = proc.getExplainState().fixitController;
        controller.tick (100, evidence, access, true, 0);
        controller.tick (4200, evidence, access, true, 0);
        GF_EXPECT (controller.getChip().kind == fixit::Chip::Kind::Suggestion);
        GF_EXPECT (controller.getChip().proposal.needsConfirm);
        const auto original = access.getNormalised ("protNext");
        health::DetailsPanel details (proc);
        buttonsFit (details);
        GF_EXPECT (access.getNormalised ("protNext") == original);
    }
}
