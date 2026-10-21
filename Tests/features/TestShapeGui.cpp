// WP5 GUI: SHAPE panel, band-lane fill types, fallback-tempo option, ParamInfo coverage.

#include "../support/TestRig.h"
#include "UiTestUtil.h"

#include "PluginEditor.h"
#include "features/info/ParamInfo.h"
#include "features/shaping/Gui.h"
#include "gui/Panels.h"
#include "gui/SlotHost.h"

using namespace gapfill;
using namespace gftest;

namespace
{
    const char* const kMyIds[] = { "personality", "captureLength", "fillShape", "fillShapeAmount", "densityBreath", "densityBreathBars",
                                   "densityBreathDepth", "densityBreathCurve", "phraseBars", "phraseBias", "phraseOffset", "mistakes",
                                   "fillQuant", "modSync", "bandLowType", "bandMidType", "bandHighType", "evolve", "fillBudget", "quietZone" };

    void setView (GapFillAudioProcessor& proc, ui::ViewMode m)   { proc.getUiState().setViewMode (m); }

    std::vector<juce::Component*> visibleControls (shapegui::ShapePanel& panel)
    {
        std::vector<juce::Component*> all, out;
        collectAll (panel, all);

        for (auto* c : all)
            if ((dynamic_cast<GapKnob*> (c) || dynamic_cast<GapCombo*> (c) || dynamic_cast<GapToggle*> (c)) && visibleInTree (*c, panel))
                out.push_back (c);

        return out;
    }
}

GF_TEST (ShapeGui, every_wp5_parameter_has_info_and_a_short_line)
{
    const auto missing = info::idsMissingInfo();

    for (auto* id : kMyIds)
    {
        GF_EXPECT_MSG (! missing.contains (id), juce::String (id) + " has no ParamInfo entry");
        const auto* e = info::ParamInfoRegistry::get().find (id);
        GF_EXPECT (e != nullptr);

        if (e != nullptr)
            GF_EXPECT_MSG (juce::String (e->oneLine).length() <= info::kMaxOneLineLength, id);
    }
}

GF_TEST (ShapeGui, advanced_panel_has_every_control_inside_its_bounds_without_overlap)
{
    GapFillAudioProcessor proc;
    setView (proc, ui::ViewMode::Advanced);
    shapegui::ShapePanel panel (proc);

    int cells = 0;
    for (const auto& g : shapegui::groups())
        for (const auto& c : g.cells)
        {
            ++cells;
            GF_EXPECT_MSG (proc.getValueTreeState().getParameter (c.paramId) != nullptr, c.paramId);
        }

    const auto controls = visibleControls (panel);
    GF_EXPECT ((int) controls.size() == cells);

    const auto h = panel.getPreferredHeight();
    GF_EXPECT_MSG (h > 100 && h <= 560, "preferred height " + juce::String (h) + " must fit the 620 px minimum editor");
    GF_EXPECT (panel.getWidth() == shapegui::ShapePanel::kContentWidth && panel.getHeight() == h);

    std::vector<juce::Component*> all;
    collectAll (panel, all);
    juce::Array<juce::Rectangle<int>> boxes;

    for (auto* c : all)
    {
        if (c->getParentComponent() != &panel || ! c->isVisible())
            continue;

        GF_EXPECT_MSG (panel.getLocalBounds().contains (c->getBounds()), "control outside the panel: " + c->getTitle());
        GF_EXPECT_MSG (c->getTitle().isNotEmpty() || dynamic_cast<GapKnob*> (c) || dynamic_cast<GapCombo*> (c)
                        || dynamic_cast<GapToggle*> (c) || dynamic_cast<juce::Label*> (c), "missing accessible title");

        for (const auto& other : boxes)
            GF_EXPECT_MSG (! other.intersects (c->getBounds()), "overlapping controls: " + c->getTitle());

        boxes.add (c->getBounds());
    }
}

GF_TEST (ShapeGui, simple_view_shows_personality_only_and_never_changes_values)
{
    GapFillAudioProcessor proc;
    setParams (proc, { { "mistakes", 40.0f }, { "evolve", 2.0f }, { "personality", 3.0f } });
    setView (proc, ui::ViewMode::Simple);
    shapegui::ShapePanel panel (proc);

    const auto controls = visibleControls (panel);
    GF_EXPECT_MSG (controls.size() == 1, "Simple shows " + juce::String ((int) controls.size()) + " controls");
    GF_EXPECT (panel.getPreferredHeight() < 120);

    // Hidden is not reset.
    GF_EXPECT_NEAR (pv (proc.getValueTreeState().getRawParameterValue ("mistakes")), 40.0, 0.01);
    GF_EXPECT (pi (proc.getValueTreeState().getRawParameterValue ("evolve")) == 2);

    setView (proc, ui::ViewMode::Advanced);
    panel.applyViewMode();
    GF_EXPECT (visibleControls (panel).size() > 10);
}

GF_TEST (ShapeGui, capture_buttons_post_commands_and_release_switches_capture_off)
{
    GapFillAudioProcessor proc;
    setParams (proc, { { "capture", 1.0f }, { "fill", 80.0f } });
    setView (proc, ui::ViewMode::Advanced);
    shapegui::ShapePanel panel (proc);

    juce::TextButton* recapture = nullptr;
    juce::TextButton* release = nullptr;
    std::vector<juce::Component*> all;
    collectAll (panel, all);

    for (auto* c : all)
        if (auto* b = dynamic_cast<juce::TextButton*> (c))
        {
            if (b->getButtonText() == "RECAPTURE") recapture = b;
            if (b->getButtonText() == "RELEASE")   release = b;
        }

    GF_EXPECT (recapture != nullptr && release != nullptr);
    GF_EXPECT (recapture->getTitle().isNotEmpty() && release->getTitle().isNotEmpty());

    // Recapture: render with Capture on, press, keep rendering: a snapshot is produced.
    RenderOptions o;
    o.onBlock = [&] (int block, int) { if (block == 150) recapture->triggerClick(); };
    renderOffline (proc, drumLoop (48000.0, 120.0, 6), 512, 48000.0, o);
    GF_EXPECT (proc.getShaping().capture.getSnapshotCount() >= 1);
    GF_EXPECT (panel.getCaptureStatusText().startsWith ("Capture"));

    release->triggerClick();
    GF_EXPECT (pi (proc.getValueTreeState().getRawParameterValue ("capture")) == 0);
}

GF_TEST (ShapeGui, evolve_label_follows_the_engine_waypoint)
{
    GapFillAudioProcessor proc;
    setView (proc, ui::ViewMode::Advanced);
    setParams (proc, { { "evolve", 1.0f }, { "fill", 60.0f } });
    shapegui::ShapePanel panel (proc);
    GF_EXPECT (panel.getEvolveText().contains ("transport"));

    TestPlayHead ph;
    ph.bpm = 150.0; ph.tsNum = 4; ph.tsDen = 4;
    RenderOptions o;
    o.playHead = &ph;
    renderOffline (proc, drumLoop (48000.0, 150.0, 2), 512, 48000.0, o);

    const auto profile = proc.getFillEngine().getDecisionTelemetry().evolveProfile.load();
    GF_EXPECT (profile >= 0);

    // The panel polls on a timer; a fresh panel reads the value at construction.
    shapegui::ShapePanel fresh (proc);
    GF_EXPECT (fresh.getEvolveText() == juce::String ("-> ") + surpriseTargets (profile).name);
}

GF_TEST (ShapeGui, band_lanes_have_a_fill_type_box_each)
{
    GapFillAudioProcessor proc;
    SpectralPanel spectral (proc.getValueTreeState());
    spectral.setBounds (0, 0, 480, 220);                           // small but legal panel size

    std::vector<juce::Component*> all;
    collectAll (spectral, all);
    int boxes = 0;
    juce::Array<juce::Rectangle<int>> rects;

    for (auto* c : all)
        if (auto* box = dynamic_cast<ParamComboBox*> (c))
        {
            const auto id = box->getProperties()[kParamIdProperty].toString();

            if (id.endsWith ("Type"))
            {
                ++boxes;
                GF_EXPECT (box->getTitle().contains ("fill type"));
                GF_EXPECT (box->getNumItems() == 1 + numAlgos);
                GF_EXPECT (box->getHeight() >= 14 && spectral.getLocalBounds().contains (box->getBounds()));
                rects.add (box->getBounds());
            }
        }

    GF_EXPECT (boxes == 3);

    for (int i = 0; i < rects.size(); ++i)
        for (int j = i + 1; j < rects.size(); ++j)
            GF_EXPECT (! rects[i].intersects (rects[j]));

    // The box edits the parameter.
    for (auto* c : all)
        if (auto* box = dynamic_cast<ParamComboBox*> (c))
            if (box->getProperties()[kParamIdProperty].toString() == "bandMidType")
                box->setSelectedItemIndex (4, juce::sendNotificationSync);

    GF_EXPECT (pi (proc.getValueTreeState().getRawParameterValue ("bandMidType")) == 4);
}

GF_TEST (ShapeGui, slots_register_the_shape_button_and_the_tempo_option)
{
    bool shape = false, tempo = false;

    for (const auto& f : getSlotFactories (Slot::CreativeExtra))
        shape = shape || f.order == 110;

    for (const auto& f : getSlotFactories (Slot::OptionsExtra))
        tempo = tempo || f.order == 200;

    GF_EXPECT (shape && tempo);

    GapFillAudioProcessor proc;

    for (const auto& f : getSlotFactories (Slot::OptionsExtra))
    {
        if (f.order != 200)
            continue;

        auto comp = f.make (proc);
        GF_EXPECT (comp->getTitle() == "Tempo when host provides none");

        std::vector<juce::Component*> all;
        collectAll (*comp, all);

        for (auto* c : all)
            if (auto* s = dynamic_cast<juce::Slider*> (c))
            {
                s->setValue (90.0, juce::sendNotificationSync);
                GF_EXPECT_NEAR (proc.getShaping().tempo.getFallbackBpm(), 90.0, 1.0e-9);
                s->setValue (500.0, juce::sendNotificationSync);          // clamped by the slider range
                GF_EXPECT_NEAR (proc.getShaping().tempo.getFallbackBpm(), 300.0, 1.0e-9);
            }
    }
}

GF_TEST (ShapeGui, editor_shows_shape_next_to_capture_at_the_minimum_size_in_both_views)
{
    for (auto mode : { ui::ViewMode::Simple, ui::ViewMode::Advanced })
    {
        GapFillAudioProcessor proc;
        proc.getUiState().setViewMode (mode);
        GapFillAudioProcessorEditor editor (proc);
        editor.setSize (1020, 620);

        juce::TextButton* shape = nullptr;
        juce::Component* capture = nullptr;

        for (auto* c : editor.getChildren())
        {
            if (auto* b = dynamic_cast<juce::TextButton*> (c))
                if (b->getButtonText() == "SHAPE")
                    shape = b;

            if (dynamic_cast<GapToggle*> (c) != nullptr)
                capture = c;
        }

        GF_EXPECT (shape != nullptr && capture != nullptr);

        if (shape != nullptr && capture != nullptr)
        {
            GF_EXPECT (shape->isVisible() && shape->getWidth() >= 44 && shape->getHeight() >= 20);
            GF_EXPECT (capture->getWidth() >= 60);
            GF_EXPECT (! shape->getBounds().intersects (capture->getBounds()));
            GF_EXPECT (editor.getLocalBounds().contains (shape->getBounds()));
        }
    }
}
