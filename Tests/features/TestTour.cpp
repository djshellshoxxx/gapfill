// U-7 / F-S09 first-run guide.

#include "../support/TestRig.h"

#include "PluginEditor.h"
#include "features/tour/Tour.h"

using namespace gapfill;
using namespace gftest;

namespace
{
    struct TempSettings
    {
        TempSettings()
        {
            dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getChildFile ("gapfill-tour-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));
            dir.createDirectory();
            file = dir.getChildFile ("settings.xml");
            tour::shownInProcess() = false;
        }

        ~TempSettings()
        {
            tour::shownInProcess() = false;
            settings::GlobalSettings::useFileForTests (juce::File::getSpecialLocation (juce::File::tempDirectory)
                                                         .getChildFile ("gapfill-tour-reset.xml"));
            dir.deleteRecursively();
        }

        juce::File dir, file;
    };

    juce::Rectangle<int> centreOf (juce::Rectangle<int> r) { return juce::Rectangle<int> (1, 1).withCentre (r.getCentre()); }
}

GF_TEST (Tour, shows_once_per_user)
{
    TempSettings t;

    {
        settings::GlobalSettings s (t.file);
        tour::TourModel m (s, 3);
        GF_EXPECT (s.wasMissing());
        GF_EXPECT (m.shouldAutoShow (false));
        m.finish();
    }

    settings::GlobalSettings again (t.file);                 // a later launch
    tour::TourModel m2 (again, 3);
    GF_EXPECT (! m2.shouldAutoShow (false));
}

GF_TEST (Tour, skip_sets_flag_and_dont_show_again_suppresses)
{
    TempSettings t;
    settings::GlobalSettings s (t.file);
    tour::TourModel m (s, 3);
    m.skip();
    GF_EXPECT (s.getInt (tour::kKeyDoneVersion, 0) == tour::kTourVersion);
    GF_EXPECT (! m.shouldAutoShow (false));

    TempSettings t2;
    settings::GlobalSettings s2 (t2.file);
    tour::TourModel m2 (s2, 3);
    m2.dontShowAgain();
    GF_EXPECT (s2.getBool (tour::kKeySuppress, false));
    GF_EXPECT (! m2.shouldAutoShow (false));
    GF_EXPECT (! m2.shouldShowWhatsNewHint());
}

GF_TEST (Tour, not_shown_when_settings_unreadable_is_safe)
{
    TempSettings t;
    t.file.replaceWithText ("{ not xml");
    settings::GlobalSettings s (t.file);
    tour::TourModel m (s, 3);
    GF_EXPECT_MSG (! s.isUsable() && ! m.shouldAutoShow (false), "unreadable settings default to not showing, never looping");

    // A settings path that can never be created (its parent is a regular file).
    TempSettings t2;
    const auto blocker = t2.dir.getChildFile ("blocker");
    blocker.replaceWithText ("x");
    settings::GlobalSettings s2 (blocker.getChildFile ("settings.xml"));
    tour::TourModel m2 (s2, 3);
    GF_EXPECT (! m2.shouldAutoShow (false));
}

GF_TEST (Tour, not_shown_second_instance_same_process)
{
    TempSettings t;
    settings::GlobalSettings s (t.file);
    tour::TourModel m (s, 3);
    GF_EXPECT (m.shouldAutoShow (tour::shownInProcess()));
    tour::shownInProcess() = true;
    GF_EXPECT (! m.shouldAutoShow (tour::shownInProcess()));
}

GF_TEST (Tour, step3_falls_back_without_audition)
{
    const auto with = tour::defaultSteps (true);
    const auto without = tour::defaultSteps (false);
    GF_EXPECT (with.size() == 3 && without.size() == 3);
    GF_EXPECT (with[2].id == "audition" && without[2].id == "undo");
    GF_EXPECT (with[1].offersExample && ! with[0].offersExample);
}

GF_TEST (Tour, overlay_accessible_names_and_keyboard_walkthrough)
{
    TempSettings t;
    settings::GlobalSettings s (t.file);
    tour::TourModel m (s, 3);
    tour::TourOverlay overlay (tour::defaultSteps (false), m);
    overlay.setBounds (0, 0, 1020, 620);

    GF_EXPECT (overlay.getCardTitle() == "Quick tour, step 1 of 3");

    bool closed = false;
    overlay.onClosed = [&closed] { closed = true; };

    overlay.keyPressed (juce::KeyPress (juce::KeyPress::rightKey));
    GF_EXPECT (overlay.getCardTitle() == "Quick tour, step 2 of 3");
    overlay.keyPressed (juce::KeyPress (juce::KeyPress::leftKey));
    GF_EXPECT (overlay.getStepIndex() == 0);
    overlay.keyPressed (juce::KeyPress (juce::KeyPress::rightKey));
    overlay.keyPressed (juce::KeyPress (juce::KeyPress::rightKey));
    GF_EXPECT (overlay.getStepIndex() == 2 && ! closed);
    overlay.keyPressed (juce::KeyPress (juce::KeyPress::rightKey));      // DONE
    GF_EXPECT (closed && s.getInt (tour::kKeyDoneVersion, 0) == tour::kTourVersion);
}

GF_TEST (Tour, esc_counts_as_skip)
{
    TempSettings t;
    settings::GlobalSettings s (t.file);
    tour::TourModel m (s, 3);
    tour::TourOverlay overlay (tour::defaultSteps (false), m);
    overlay.setBounds (0, 0, 1020, 620);

    bool closed = false;
    overlay.onClosed = [&closed] { closed = true; };
    GF_EXPECT (overlay.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
    GF_EXPECT (closed && s.getInt (tour::kKeyDoneVersion, 0) == tour::kTourVersion);
}

GF_TEST (Tour, missing_target_centres_card)
{
    TempSettings t;
    settings::GlobalSettings s (t.file);
    tour::TourModel m (s, 3);
    tour::TourOverlay overlay (tour::defaultSteps (false), m);
    overlay.findTarget = [] (const juce::String&) -> juce::Component* { return nullptr; };
    overlay.setBounds (0, 0, 1020, 620);

    GF_EXPECT (overlay.getHighlights().empty());
    const auto card = overlay.getCardBounds();
    GF_EXPECT (std::abs (card.getCentreX() - 510) <= 1 && std::abs (card.getCentreY() - 310) <= 1);
    GF_EXPECT (overlay.getLocalBounds().contains (card));
    juce::ignoreUnused (centreOf);
}

GF_TEST (Tour, card_never_covers_the_highlighted_target_and_survives_resize)
{
    TempSettings t;
    settings::GlobalSettings::useFileForTests (t.file);

    GapFillAudioProcessor proc;
    GapFillAudioProcessorEditor editor (proc);
    editor.setSize (1020, 620);
    editor.showTour (true);

    auto* overlay = editor.getTourOverlayForTests();
    GF_EXPECT (overlay != nullptr && overlay->getBounds() == editor.getLocalBounds());

    for (int step = 0; step < 3; ++step)
    {
        for (const auto size : { juce::Point<int> (1020, 620), juce::Point<int> (1400, 800) })
        {
            editor.setSize (size.x, size.y);
            GF_EXPECT (overlay->getBounds() == editor.getLocalBounds());
            GF_EXPECT (editor.getLocalBounds().contains (overlay->getCardBounds()));

            for (const auto& h : overlay->getHighlights())
                GF_EXPECT_MSG (! h.intersects (overlay->getCardBounds()), "step " + juce::String (step));
        }

        overlay->pressNext();
    }
}

GF_TEST (Tour, load_example_is_single_undo_step_and_not_applied_on_skip)
{
    TempSettings t;
    settings::GlobalSettings::useFileForTests (t.file);

    GapFillAudioProcessor proc;
    GapFillAudioProcessorEditor editor (proc);
    editor.setSize (1020, 620);
    editor.showTour (true);

    auto* overlay = editor.getTourOverlayForTests();
    const auto depth = proc.getUndoDepth();
    const auto before = proc.getCurrentPresetName();

    overlay->pressSkip();
    GF_EXPECT_MSG (proc.getUndoDepth() == depth && proc.getCurrentPresetName() == before, "skipping changes nothing");

    editor.showTour (true);
    overlay = editor.getTourOverlayForTests();
    overlay->pressNext();                                            // step 2 offers the example
    GF_EXPECT (overlay->getStep().offersExample);
    overlay->onLoadExample();
    GF_EXPECT (proc.getUndoDepth() == depth + 1);
    GF_EXPECT (proc.getCurrentPresetName() == "Drum Gap Glitch");
    GF_EXPECT (proc.undo());
    GF_EXPECT (proc.getCurrentPresetName() == before || proc.getCurrentPresetName() == "Custom");
}

GF_TEST (Tour, not_shown_below_min_size_and_after_done)
{
    TempSettings t;
    settings::GlobalSettings::useFileForTests (t.file);

    GapFillAudioProcessor proc;
    {
        GapFillAudioProcessorEditor tiny (proc);
        tiny.setSize (700, 400);
        GF_EXPECT (! tiny.maybeAutoShowTourForTests());
    }

    GapFillAudioProcessorEditor editor (proc);
    editor.setSize (1020, 620);
    GF_EXPECT (editor.maybeAutoShowTourForTests());                  // first run, writable settings
    GF_EXPECT (tour::shownInProcess());

    GapFillAudioProcessorEditor second (proc);
    second.setSize (1020, 620);
    GF_EXPECT_MSG (! second.maybeAutoShowTourForTests(), "only the first editor in a process shows the tour");
}
