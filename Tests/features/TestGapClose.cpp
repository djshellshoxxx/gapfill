// Gap-closing pass after the completeness audit of 2026-10-21 (docs/audits/2026-10-21-completeness-audit.md, "Closed
// after the audit"): F-M17 Mix-panel selector + footer chip, F-M21 curve editor, F-X05 Skip in the History drawer,
// F-S11 Ctrl+D / favourites-only stepping, F-M18 header A/B feed, F-S12 chip target. Each test names its gap.
//
// No bit-exact comparisons of mathematically equivalent expressions: curve samples are compared with a tolerance.

#include "../support/TestRig.h"
#include "UiTestUtil.h"

#include "PluginEditor.h"
#include "features/battle/Battle.h"
#include "features/context/ContextState.h"
#include "features/presetbrowser/GlobalSettings.h"
#include "features/presetbrowser/PresetCatalog.h"
#include "features/tension/CurveEdit.h"
#include "gui/Panels.h"
#include "gui/SlotHost.h"
#include "gui/slots/TensionEditor.h"

using namespace gapfill;
using namespace gftest;

namespace
{
    float rawParam (GapFillAudioProcessor& p, const char* id)
    {
        return p.getValueTreeState().getRawParameterValue (id)->load();
    }

    void setNormalised (GapFillAudioProcessor& p, const char* id, float v01)
    {
        p.getValueTreeState().getParameter (id)->setValueNotifyingHost (v01);
    }

    void setReal (GapFillAudioProcessor& p, const char* id, float real)
    {
        auto* prm = p.getValueTreeState().getParameter (id);
        prm->setValueNotifyingHost (prm->convertTo0to1 (real));
    }

    std::unique_ptr<juce::Component> makeSlot (Slot slot, int order, GapFillAudioProcessor& proc)
    {
        for (const auto& f : getSlotFactories (slot))
            if (f.order == order)
                return f.make (proc);

        return nullptr;
    }

    /** A left click delivered the way a mouse does (triggerClick() would post an async message). */
    void click (juce::Button& b)
    {
        const auto at = b.getLocalBounds().getCentre().toFloat();
        const auto event = [&] (bool down)
        {
            return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at,
                                     down ? juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier) : juce::ModifierKeys(),
                                     0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &b, &b, juce::Time::getCurrentTime(), at,
                                     juce::Time::getCurrentTime(), 1, false);
        };
        auto& component = static_cast<juce::Component&> (b);              // Button's overrides are protected
        component.mouseDown (event (true));
        component.mouseUp (event (false));
    }

    void runBlock (GapFillAudioProcessor& proc)
    {
        juce::AudioBuffer<float> block (2, 512);
        juce::MidiBuffer midi;
        block.clear();
        proc.processBlock (block, midi);
    }

    template <typename T>
    T* findButton (juce::Component& root, const juce::String& text)
    {
        std::vector<juce::Component*> all;
        collectAll (root, all);

        for (auto* c : all)
            if (auto* b = dynamic_cast<T*> (c))
                if (b->getButtonText() == text)
                    return b;

        return nullptr;
    }

    struct TempSettings
    {
        TempSettings()
        {
            file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("gapfill-gapclose-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()) + ".xml");
            settings::GlobalSettings::useFileForTests (file);
        }

        ~TempSettings()
        {
            settings::GlobalSettings::useFileForTests (juce::File::getSpecialLocation (juce::File::tempDirectory)
                                                           .getChildFile ("gapfill-gapclose-after.xml"));
            file.deleteFile();
        }

        juce::File file;
    };
}

//==============================================================================
// F-M17: Mix-panel OUTPUT selector (bound to wetSolo) that dims Dry / Wet.
GF_TEST (GapClose, mix_panel_output_selector_follows_and_drives_wet_only)
{
    GapFillAudioProcessor proc;
    MixPanel panel (proc.getValueTreeState());
    panel.setSize (620, 124);

    auto& button = panel.getOutputModeButton();
    GF_EXPECT (button.getButtonText() == "OUTPUT: NORMAL");
    GF_EXPECT (! panel.isWetOnlyShown());
    GF_EXPECT (button.getTitle().isNotEmpty());                          // accessible name
    GF_EXPECT (button.getDescription().containsIgnoreCase ("Dry and Wet are ignored"));
    GF_EXPECT (button.getBounds().getHeight() >= 24);                    // a real hit target
    GF_EXPECT (panel.getLocalBounds().contains (button.getParentComponent()->getBounds()));

    // The control writes the parameter ...
    click (button);
    GF_EXPECT (rawParam (proc, "wetSolo") > 0.5f);

    // ... and the panel follows the parameter (host automation, MIDI learn, scenes) on the next UI tick.
    panel.setLevels (0.0f, 0.0f, 0.0f);
    GF_EXPECT (panel.isWetOnlyShown());
    GF_EXPECT (button.getButtonText() == "OUTPUT: WET ONLY");
    GF_EXPECT (button.getTitle().containsIgnoreCase ("wet only"));

    // Dry and Wet are shown dimmed but not hidden; Output keeps full strength.
    std::vector<juce::Component*> all;
    collectAll (panel, all);
    int dimmed = 0, full = 0;

    for (auto* c : all)
        if (auto* k = dynamic_cast<GapKnob*> (c))
        {
            if (k->getAlpha() < 0.99f)
                ++dimmed;
            else
                ++full;

            GF_EXPECT (k->isVisible());
        }

    GF_EXPECT (dimmed == 2 && full == 1);

    setNormalised (proc, "wetSolo", 0.0f);
    panel.setLevels (0.0f, 0.0f, 0.0f);
    GF_EXPECT (! panel.isWetOnlyShown() && button.getButtonText() == "OUTPUT: NORMAL");

    for (auto* c : all)
        if (auto* k = dynamic_cast<GapKnob*> (c))
            GF_EXPECT (k->getAlpha() > 0.99f);
}

GF_TEST (GapClose, mix_panel_selector_does_not_overlap_the_other_readouts)
{
    GapFillAudioProcessor proc;

    for (const int width : { 520, 760, 1000 })
    {
        MixPanel panel (proc.getValueTreeState());
        panel.setSize (width, 124);

        auto* selector = panel.getOutputModeButton().getParentComponent();
        const auto sel = selector->getBounds();
        GF_EXPECT (sel.getHeight() >= 24 && sel.getWidth() >= 100);

        for (auto* c : panel.getChildren())
        {
            if (c == selector || ! c->isVisible() || c->getBounds().isEmpty())
                continue;

            GF_EXPECT_MSG (! sel.intersects (c->getBounds()), "selector overlaps a mix-panel control at width " + juce::String (width));
        }

        GF_EXPECT (sel.getBottom() <= panel.getHeight());
    }
}

//==============================================================================
// F-M17 footer chip and F-S12 hit target.
GF_TEST (GapClose, footer_chip_shows_wet_only_and_is_a_full_footer_height_target)
{
    GapFillAudioProcessor proc;
    auto chip = makeSlot (Slot::FooterStatus, 90, proc);
    GF_EXPECT (chip != nullptr);

    if (chip == nullptr)
        return;

    chip->setSize (480, 22);                                             // the footer height (kFooterHeight)

    // PANIC chip: 150 px wide, the full 22 px height is the target (the footer is not grown for it).
    GF_EXPECT (chip->hitTest (10, 0) && chip->hitTest (10, 21) && chip->hitTest (149, 11));
    GF_EXPECT (! chip->hitTest (10, 22));

    // WET ONLY chip: absent while Normal, a second 22 px target next to PANIC while wet only.
    GF_EXPECT (! chip->hitTest (200, 11));
    setNormalised (proc, "wetSolo", 1.0f);
    GF_EXPECT (chip->hitTest (200, 11) && chip->hitTest (200, 0) && chip->hitTest (200, 21));
    GF_EXPECT (! chip->hitTest (330, 11));                               // beyond the chip: the status line's area
    setNormalised (proc, "wetSolo", 0.0f);
    GF_EXPECT (! chip->hitTest (200, 11));
}

//==============================================================================
// F-M21 curve editor: the pure edit rules.
GF_TEST (GapClose, tension_edit_operations_always_leave_a_valid_lane)
{
    using namespace tension;
    juce::Random rng (4242);
    TensionLane lane;

    for (int i = 0; i < 20000; ++i)
    {
        switch (rng.nextInt (5))
        {
            case 0: edit::addPoint (lane, rng.nextInt (1001), rng.nextInt (1001)); break;
            case 1: edit::removePoint (lane, rng.nextInt (kMaxPoints + 2) - 1); break;
            case 2: edit::movePoint (lane, rng.nextInt (kMaxPoints + 2) - 1, rng.nextInt (1201) - 100, rng.nextInt (1201) - 100); break;
            case 3: edit::nudgePoint (lane, rng.nextInt (kMaxPoints), rng.nextInt (321) - 160, rng.nextInt (201) - 100); break;
            default: if (rng.nextInt (20) == 0) edit::copyShape (lane, rng.nextInt (NumShapes)); break;
        }

        TensionCurves c;
        c.lane[0] = lane;
        GF_EXPECT_MSG (validate (c), "edit " + juce::String (i) + " left an invalid lane");

        if (! validate (c))
            return;
    }
}

GF_TEST (GapClose, tension_edit_rules_pin_the_end_points_and_the_limits)
{
    using namespace tension;
    TensionLane lane;

    GF_EXPECT (! edit::removePoint (lane, 0) && ! edit::removePoint (lane, 1));    // never below two points
    const int mid = edit::addPoint (lane, 500, 900);
    GF_EXPECT (mid == 1 && lane.numPoints == 3 && lane.pt[1].x == 500 && lane.pt[1].y == 900);

    // End points keep x, only y moves.
    edit::movePoint (lane, 0, 300, 100);
    edit::movePoint (lane, 2, 600, 950);
    GF_EXPECT (lane.pt[0].x == 0 && lane.pt[0].y == 100 && lane.pt[2].x == 1000 && lane.pt[2].y == 950);

    // An inner point cannot cross or touch its neighbours.
    edit::movePoint (lane, 1, 5000, 2000);
    GF_EXPECT (lane.pt[1].x == 1000 - kMinSpacing && lane.pt[1].y == 1000);
    edit::movePoint (lane, 1, -5, -5);
    GF_EXPECT (lane.pt[1].x == kMinSpacing && lane.pt[1].y == 0);

    // Full lane: no 17th point, and no room between two touching points.
    TensionLane full;

    for (int i = 0; i < 40; ++i)
        edit::addPoint (full, 20 + i * 24, 500);

    GF_EXPECT (full.numPoints == kMaxPoints);
    GF_EXPECT (edit::addPoint (full, 500, 500) == -1);

    GF_EXPECT (edit::removePoint (full, 3) && full.numPoints == kMaxPoints - 1);

    edit::resetLane (full);
    GF_EXPECT (full.numPoints == 2 && full.pt[0].y == 500 && full.pt[1].y == 500);
}

GF_TEST (GapClose, tension_copy_shape_samples_the_built_in_shape)
{
    using namespace tension;

    for (int shape : { Build, BuildDrop, Breakdown, Swell })
    {
        TensionLane lane;
        edit::copyShape (lane, shape);
        TensionCurves c;
        c.lane[0] = lane;
        GF_EXPECT (validate (c));
        GF_EXPECT (lane.numPoints == kMaxPoints);

        // The 16 sampled points sit on the shape; between them a straight line follows it within a small error.
        double worst = 0.0;

        for (int i = 0; i <= 64; ++i)
        {
            const float ph = (float) i / 64.0f;
            const double err = std::abs ((double) evalLane (lane, ph) - (double) shapeValue (shape, ph));
            worst = std::max (worst, err);
        }

        // Build+Drop has a jump at 7/8; a straight line across one sample interval (1/15) is the worst case there.
        GF_EXPECT_MSG (worst < (shape == BuildDrop ? 0.5 : 0.06), "shape " + juce::String (shape) + " error " + juce::String (worst));
    }

    TensionLane lane;
    edit::copyShape (lane, Custom);                                      // Custom = flat reset
    GF_EXPECT (lane.numPoints == 2);
}

GF_TEST (GapClose, tension_nearest_point_picks_by_distance)
{
    using namespace tension;
    TensionLane lane;
    edit::addPoint (lane, 300, 200);
    edit::addPoint (lane, 700, 800);
    GF_EXPECT (edit::nearestPoint (lane, 20, 480) == 0);
    GF_EXPECT (edit::nearestPoint (lane, 310, 210) == 1);
    GF_EXPECT (edit::nearestPoint (lane, 690, 760) == 2);
    GF_EXPECT (edit::nearestPoint (lane, 990, 500) == 3);
}

//==============================================================================
// F-M21 curve editor popover: publishes curves, switches Shape to Custom, keyboard works.
GF_TEST (GapClose, tension_editor_publishes_edits_and_switches_to_custom)
{
    GapFillAudioProcessor proc;
    auto& svc = proc.getContextState().tensionService();
    GF_EXPECT (svc.isDefault());
    GF_EXPECT (rawParam (proc, "fm21_shape") < 3.5f);                    // default Build+Drop

    TensionCurveEditor ed (proc);
    GF_EXPECT (ed.getWidth() == TensionCurveEditor::kWidth && ed.getHeight() == TensionCurveEditor::kHeight);
    GF_EXPECT (ed.getCanvasBounds().getHeight() == TensionCurveEditor::kCanvasHeight && ed.getCanvasBounds().getWidth() >= 440);
    GF_EXPECT (ed.getTitle().containsIgnoreCase ("Density point 1"));    // announced selection

    ed.addPointAt (500, 900);
    GF_EXPECT (ed.getSelectedPoint() == 1);
    GF_EXPECT (svc.curves().lane[0].numPoints == 3 && svc.curves().lane[0].pt[1].y == 900);
    GF_EXPECT (! svc.isDefault());
    GF_EXPECT (rawParam (proc, "fm21_shape") > 3.5f);                    // Custom: the edit is audible

    // Keyboard: arrows move (1/64 length, 1 %), Shift = 10x, PageUp / PageDown select, Delete removes.
    const auto before = svc.curves().lane[0].pt[1];
    GF_EXPECT (ed.keyPressed (juce::KeyPress (juce::KeyPress::upKey, juce::ModifierKeys::noModifiers, 0)));
    GF_EXPECT (svc.curves().lane[0].pt[1].y == before.y + 10 || before.y + 10 > 1000);
    GF_EXPECT (ed.keyPressed (juce::KeyPress (juce::KeyPress::rightKey, juce::ModifierKeys::shiftModifier, 0)));
    GF_EXPECT (svc.curves().lane[0].pt[1].x == before.x + 160);
    GF_EXPECT (ed.keyPressed (juce::KeyPress (juce::KeyPress::pageDownKey, juce::ModifierKeys::noModifiers, 0)));
    GF_EXPECT (ed.getSelectedPoint() == 0);
    GF_EXPECT (ed.keyPressed (juce::KeyPress (juce::KeyPress::endKey, juce::ModifierKeys::noModifiers, 0)));
    GF_EXPECT (ed.getSelectedPoint() == 2);
    GF_EXPECT (ed.keyPressed (juce::KeyPress (juce::KeyPress::pageDownKey, juce::ModifierKeys::noModifiers, 0)));
    GF_EXPECT (ed.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey, juce::ModifierKeys::noModifiers, 0)));
    GF_EXPECT (svc.curves().lane[0].numPoints == 2);
    GF_EXPECT (ed.keyPressed (juce::KeyPress (juce::KeyPress::insertKey, juce::ModifierKeys::noModifiers, 0)));
    GF_EXPECT (svc.curves().lane[0].numPoints == 3);

    // Lanes are independent; shape buttons copy into the selected lane; reset flattens it.
    ed.setLane (2);
    GF_EXPECT (ed.getLane() == 2 && ed.getTitle().containsIgnoreCase ("Width point 1"));
    ed.applyShape (tension::Swell);
    GF_EXPECT (svc.curves().lane[2].numPoints == tension::kMaxPoints && svc.curves().lane[0].numPoints == 3);
    ed.resetCurrentLane();
    GF_EXPECT (svc.curves().lane[2].numPoints == 2);

    GF_EXPECT (tension::validate (svc.curves()));
}

GF_TEST (GapClose, tension_editor_mouse_adds_drags_and_removes)
{
    GapFillAudioProcessor proc;
    auto& svc = proc.getContextState().tensionService();
    TensionCurveEditor ed (proc);
    const auto cv = ed.getCanvasBounds();

    const auto at = [&] (int xPerMille, int yPerMille)
    {
        const int w = cv.getWidth() - 20, h = cv.getHeight() - 20;
        return juce::Point<int> (cv.getX() + 10 + xPerMille * w / 1000, cv.getBottom() - 10 - yPerMille * h / 1000);
    };

    const auto mouse = [&] (juce::Point<int> p, bool popup = false)
    {
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), p.toFloat(),
                                 popup ? juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier) : juce::ModifierKeys(),
                                 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &ed, &ed, juce::Time::getCurrentTime(), p.toFloat(),
                                 juce::Time::getCurrentTime(), 1, false);
    };

    ed.mouseDoubleClick (mouse (at (400, 700)));
    GF_EXPECT (svc.curves().lane[0].numPoints == 3);

    // Drag it: pick near it, then move.
    ed.mouseDown (mouse (at (400, 700)));
    GF_EXPECT (ed.getSelectedPoint() == 1);
    ed.mouseDrag (mouse (at (600, 300)));
    const auto moved = svc.curves().lane[0].pt[1];
    GF_EXPECT (std::abs ((int) moved.x - 600) <= 12 && std::abs ((int) moved.y - 300) <= 12);

    // Right click on it removes it; right click on an end point keeps it.
    ed.mouseDown (mouse (at (600, 300), true));
    GF_EXPECT (svc.curves().lane[0].numPoints == 2);
    ed.mouseDown (mouse (at (0, 500), true));
    GF_EXPECT (svc.curves().lane[0].numPoints == 2);
}

GF_TEST (GapClose, tension_strip_has_the_edit_button_and_the_help_names_it)
{
    GapFillAudioProcessor proc;
    auto strip = makeSlot (Slot::PerformanceTab, 470, proc);
    GF_EXPECT (strip != nullptr);

    if (strip == nullptr)
        return;

    auto* b = findButton<juce::TextButton> (*strip, "EDIT CURVES...");
    GF_EXPECT (b != nullptr);

    if (b != nullptr)
    {
        GF_EXPECT (b->getTitle().isNotEmpty() && b->getTooltip().isNotEmpty());
        GF_EXPECT (b->getHeight() >= 24 || strip->getHeight() == 0);
    }
}

//==============================================================================
// F-X05: Skip in the History drawer.
GF_TEST (GapClose, history_drawer_has_a_skip_button_that_rates_through_the_taste_service)
{
    TempSettings guard;
    GapFillAudioProcessor proc;
    prepareProcessor (proc, 48000.0, 512, false);
    auto& ctx = proc.getContextState();
    auto& svc = ctx.tasteService();
    svc.useLibraryFile (juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("gapfill-gapclose-ratings-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()) + ".xml"));

    GapFillAudioProcessorEditor editor (proc);
    editor.setSize (1100, 700);

    juce::Component* drawer = nullptr;

    for (auto* c : editor.getChildren())
        if (c->getTitle() == "Fill recipes")
            drawer = c;

    GF_EXPECT (drawer != nullptr);

    if (drawer == nullptr)
        return;

    juce::Button* skip = nullptr;
    std::vector<juce::Component*> all;
    collectAll (*drawer, all);

    for (auto* c : all)
        if (auto* b = dynamic_cast<juce::Button*> (c))
            if (b->getTitle() == "Skip this kind of fill")
                skip = b;

    GF_EXPECT (skip != nullptr);

    if (skip == nullptr)
        return;

    GF_EXPECT (skip->getButtonText() == "SKIP");
    GF_EXPECT (skip->getTooltip().containsIgnoreCase ("LEARN MY TASTE"));

    for (auto* c : all)
        if (c->getTitle() == "Recipes drawer")
            dynamic_cast<juce::Button*> (c)->setToggleState (true, juce::sendNotificationSync);

    GF_EXPECT (skip->isVisible() && skip->getHeight() >= 24);

    // Learning off: nothing is queued, the drawer says why.
    skip->onClick();
    runBlock (proc);
    ctx.timerTick (1000, true, 0.5f);
    GF_EXPECT (svc.counts().skips == 0);

    // Learning on, a fill has started: SKIP rates the latest fill.
    setNormalised (proc, "fx05_on", 1.0f);
    FillEvent started;
    started.type = FillEventType::Started;
    started.recipe.algo = (std::uint8_t) Algo::Granular;
    ctx.onFillEvent (started);

    skip->onClick();
    runBlock (proc);
    ctx.timerTick (2000, true, 0.5f);
    GF_EXPECT (svc.counts().skips == 1 && svc.counts().likes == 0);
}

//==============================================================================
// F-S11: Ctrl/Cmd+D, Ctrl/Cmd+Up/Down and the "favourites only" step scope.
GF_TEST (GapClose, catalog_step_within_a_pool_wraps_and_handles_non_members)
{
    using presets::PresetCatalog;
    const std::vector<int> pool { 2, 5, 9 };

    GF_EXPECT (PresetCatalog::stepWithin ({}, 3, 1) == -1);
    GF_EXPECT (PresetCatalog::stepWithin (pool, 2, 1) == 5);
    GF_EXPECT (PresetCatalog::stepWithin (pool, 9, 1) == 2);             // wraps forward
    GF_EXPECT (PresetCatalog::stepWithin (pool, 2, -1) == 9);            // wraps back
    GF_EXPECT (PresetCatalog::stepWithin (pool, 6, 1) == 9);             // not a member: the next favourite after it
    GF_EXPECT (PresetCatalog::stepWithin (pool, 6, -1) == 5);
    GF_EXPECT (PresetCatalog::stepWithin (pool, 12, 1) == 2);
    GF_EXPECT (PresetCatalog::stepWithin (pool, 0, -1) == 9);
    GF_EXPECT (PresetCatalog::stepWithin (pool, -1, 1) == 2);            // a modified sound goes to the edges
    GF_EXPECT (PresetCatalog::stepWithin (pool, -1, -1) == 9);

    // Visits every member exactly once per cycle.
    int at = 2, visited = 0;

    do { at = PresetCatalog::stepWithin (pool, at, 1); ++visited; } while (at != 2 && visited < 10);
    GF_EXPECT (visited == 3);
}

GF_TEST (GapClose, ctrl_d_toggles_the_favourite_and_ctrl_arrows_step)
{
    TempSettings guard;
    GapFillAudioProcessor proc;
    GapFillAudioProcessorEditor editor (proc);
    auto settingsFile = settings::GlobalSettings::shared();
    const auto entries = presets::factoryEntries();
    GF_EXPECT (entries.size() > 6);

    proc.selectPreset (2);
    const auto id = editor.currentPresetIdentityForTests();
    GF_EXPECT (id == entries[2].identity);

    const juce::KeyPress ctrlD ('d', juce::ModifierKeys::commandModifier, 0);
    GF_EXPECT (editor.keyPressed (ctrlD));
    GF_EXPECT (settingsFile->isFavourite (id));
    GF_EXPECT (editor.keyPressed (ctrlD));
    GF_EXPECT (! settingsFile->isFavourite (id));

    // Plain keys and Alt combinations are left alone.
    GF_EXPECT (! editor.keyPressed (juce::KeyPress ('d', juce::ModifierKeys::noModifiers, 0)));
    GF_EXPECT (! editor.keyPressed (juce::KeyPress ('d', juce::ModifierKeys::commandModifier | juce::ModifierKeys::altModifier, 0)));

    // Ctrl+Down = next, Ctrl+Up = previous (all presets).
    GF_EXPECT (editor.keyPressed (juce::KeyPress (juce::KeyPress::downKey, juce::ModifierKeys::commandModifier, 0)));
    GF_EXPECT (editor.currentPresetIdentityForTests() == entries[3].identity);
    GF_EXPECT (editor.keyPressed (juce::KeyPress (juce::KeyPress::upKey, juce::ModifierKeys::commandModifier, 0)));
    GF_EXPECT (editor.currentPresetIdentityForTests() == entries[2].identity);

    // A modified sound is handled without a crash (it may or may not still map to a preset).
    setReal (proc, "density", 3.0f);
    GF_EXPECT (editor.keyPressed (ctrlD));
}

GF_TEST (GapClose, favourites_only_stepping_visits_only_favourites_and_falls_back_when_none)
{
    TempSettings guard;
    GapFillAudioProcessor proc;
    GapFillAudioProcessorEditor editor (proc);
    auto settingsFile = settings::GlobalSettings::shared();
    const auto entries = presets::factoryEntries();
    GF_EXPECT (entries.size() > 8);

    settingsFile->setInt (presets::kKeyStepScope, 1);

    // No favourites yet: stepping falls back to all presets.
    proc.selectPreset (2);
    editor.stepPresetForTests (1);
    GF_EXPECT (editor.currentPresetIdentityForTests() == entries[3].identity);

    settingsFile->setFavourite (entries[2].identity, true);
    settingsFile->setFavourite (entries[7].identity, true);

    proc.selectPreset (2);
    editor.stepPresetForTests (1);
    GF_EXPECT (editor.currentPresetIdentityForTests() == entries[7].identity);
    editor.stepPresetForTests (1);
    GF_EXPECT (editor.currentPresetIdentityForTests() == entries[2].identity);      // wraps within the favourites
    editor.stepPresetForTests (-1);
    GF_EXPECT (editor.currentPresetIdentityForTests() == entries[7].identity);

    // From a non-favourite the next favourite is reached.
    proc.selectPreset (4);
    editor.stepPresetForTests (1);
    GF_EXPECT (editor.currentPresetIdentityForTests() == entries[7].identity);

    // Back to all presets.
    settingsFile->setInt (presets::kKeyStepScope, 0);
    editor.stepPresetForTests (1);
    GF_EXPECT (editor.currentPresetIdentityForTests() == entries[8].identity);
}

GF_TEST (GapClose, a_burst_of_preset_steps_leaves_one_undo_entry_and_undo_restores_the_start)
{
    TempSettings guard;
    GapFillAudioProcessor proc;
    GapFillAudioProcessorEditor editor (proc);

    proc.selectPreset (2);
    const auto startPreset = proc.getCurrentPresetName();
    const char* const probe[] = { "fill", "density", "complexity", "chaos", "intensity", "wet" };
    float startValues[6];

    for (int i = 0; i < 6; ++i)
        startValues[i] = rawParam (proc, probe[i]);

    const auto depth0 = proc.getUndoDepth();

    for (int i = 0; i < 12; ++i)
        editor.stepPresetForTests (1);

    GF_EXPECT_MSG (proc.getUndoDepth() == depth0 + 1, "12 steps pushed " + juce::String (proc.getUndoDepth() - depth0) + " undo entries");
    GF_EXPECT (proc.getCurrentPresetName() != startPreset);

    GF_EXPECT (proc.undo());                                              // back to the sound before the burst

    for (int i = 0; i < 6; ++i)
        GF_EXPECT_NEAR (rawParam (proc, probe[i]), startValues[i], 1.0e-3);

    // A step after the burst window (or after another undo-pushing action) starts a new entry.
    editor.forgetPresetBurstForTests();
    const auto depth1 = proc.getUndoDepth();
    editor.stepPresetForTests (1);
    editor.forgetPresetBurstForTests();
    editor.stepPresetForTests (1);
    GF_EXPECT (proc.getUndoDepth() == depth1 + 2);

    // Something else pushing an entry in between ends the burst.
    editor.stepPresetForTests (1);
    proc.pushUndoSnapshot();
    const auto depth2 = proc.getUndoDepth();
    editor.stepPresetForTests (1);
    GF_EXPECT (proc.getUndoDepth() == depth2 + 1);
}

//==============================================================================
// F-M18: Beat Battle styles can be taken from the header A/B snapshots.
GF_TEST (GapClose, beat_battle_styles_follow_the_header_ab_snapshots)
{
    TempSettings guard;
    GapFillAudioProcessor proc;
    GapFillAudioProcessorEditor editor (proc);
    auto& battles = proc.getLiveControl().battles();
    GF_EXPECT (! battles.hasStyle (0) && ! battles.hasStyle (1));

    auto* fromAb = findButton<juce::TextButton> (editor, "FROM A/B");
    GF_EXPECT (fromAb != nullptr);

    // A = density 20, B = density 80; the editor is on side A with density 20.
    setReal (proc, "density", 80.0f);
    const auto b = proc.getValueTreeState().copyState().createCopy();
    setReal (proc, "density", 20.0f);
    const auto a = proc.getValueTreeState().copyState().createCopy();
    editor.setHeaderSlotsForTests (a, b);

    GF_EXPECT (editor.feedBattleFromHeaderAbForTests());
    GF_EXPECT (battles.hasStyle (0) && battles.hasStyle (1) && battles.stylesDiffer());

    // Identical snapshots store two styles that do not differ (the card then says it needs two different styles).
    battles.clear();
    editor.setHeaderSlotsForTests (a, a.createCopy());
    GF_EXPECT (editor.feedBattleFromHeaderAbForTests());
    GF_EXPECT (battles.hasStyle (0) && battles.hasStyle (1) && ! battles.stylesDiffer());

    // The card's button goes through the same entry point.
    if (fromAb != nullptr)
    {
        battles.clear();
        editor.setHeaderSlotsForTests (a, b);
        fromAb->onClick();
        GF_EXPECT (battles.hasStyle (0) && battles.hasStyle (1) && battles.stylesDiffer());
        GF_EXPECT (fromAb->getTooltip().isNotEmpty() && fromAb->getTitle().isNotEmpty());
    }
}
