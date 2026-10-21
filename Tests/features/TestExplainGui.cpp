// WP3 component tests: legend, footer, detail view, timeline view, overlay slot.

#include "../support/TestRig.h"

#include "PluginEditor.h"
#include "features/explain/OverlaySlot.h"
#include "features/health/Gui.h"
#include "features/reasons/Gui.h"
#include "features/timeline/Gui.h"
#include "gui/SlotHost.h"

#include <set>

using namespace gapfill;
using namespace gftest;

namespace
{
    bool hasVisiblePixels (const juce::Image& img)
    {
        for (int y = 0; y < img.getHeight(); y += 2)
            for (int x = 0; x < img.getWidth(); x += 2)
                if (img.getPixelAt (x, y).getAlpha() > 8)
                    return true;

        return false;
    }

    juce::Image render (juce::Component& c, int w, int h)
    {
        c.setBounds (0, 0, w, h);
        juce::Image img (juce::Image::ARGB, w, h, true);
        juce::Graphics g (img);
        c.paintEntireComponent (g, true);

        // Optional: GF_SCREENSHOT_DIR=<dir> keeps the renders for a visual check.
        if (const auto dir = juce::SystemStats::getEnvironmentVariable ("GF_SCREENSHOT_DIR", {}); dir.isNotEmpty())
        {
            juce::File out (dir);
            out.createDirectory();
            auto f = out.getChildFile (c.getName().replaceCharacter (' ', '_') + "_" + juce::String (w) + "x" + juce::String (h) + ".png");
            f.deleteFile();
            juce::FileOutputStream stream (f);
            juce::PNGImageFormat().writeImageToStream (img, stream);
        }

        return img;
    }
}

GF_TEST (ReasonsGui, gui_legend_has_text_and_shape_for_every_state)
{
    const auto& entries = reasons::legendEntries();
    GF_EXPECT (entries.size() == 7);

    std::set<int> patterns;
    std::set<juce::String> labels;

    for (const auto& e : entries)
    {
        GF_EXPECT (juce::String (e.label).isNotEmpty() && juce::String (e.description).length() > 20);
        patterns.insert ((int) e.pattern);
        labels.insert (e.label);

        // Each state draws something visible by itself (a shape, not only a colour).
        juce::Image img (juce::Image::ARGB, 24, 14, true);
        juce::Graphics g (img);
        reasons::drawSwatch (g, { 2.0f, 2.0f, 20.0f, 10.0f }, e.pattern, reasons::swatchColour (e.pattern));
        GF_EXPECT_MSG (hasVisiblePixels (img), e.label);
    }

    GF_EXPECT (patterns.size() == 7 && labels.size() == 7);        // all patterns and names distinct
    for (const char* needed : { "EVENT", "GAP", "FILL", "PROTECTED", "LOOKAHEAD", "PREVIEW", "SKIPPED" })
        GF_EXPECT_MSG (labels.count (needed) == 1, needed);

    reasons::LegendStrip strip;
    strip.setName ("legend");
    GF_EXPECT (hasVisiblePixels (render (strip, strip.getPreferredWidth(), 18)));
    GF_EXPECT (strip.getDescription().isNotEmpty() && strip.getTitle().isNotEmpty());
}

GF_TEST (ReasonsGui, legend_collapses_and_the_choice_is_remembered_in_state)
{
    GapFillAudioProcessor proc;
    juce::Component parent;
    parent.setBounds (0, 0, 1000, 400);
    SlotHost host (proc, parent);
    host.setSlotBounds (Slot::VisualiserOverlay, { 0, 0, 1000, 400 });

    GF_EXPECT (host.getNumComponents (Slot::VisualiserOverlay) >= 1);   // WP3 legend + WP4 recipe drawer (order 200)
    auto* overlay = dynamic_cast<explain::VisualiserOverlayComponent*> (parent.getChildComponent (0));

    if (overlay == nullptr)
        for (auto* c : parent.getChildren())
            if (auto* o = dynamic_cast<explain::VisualiserOverlayComponent*> (c))
                overlay = o;

    GF_EXPECT (overlay != nullptr);
    GF_EXPECT (! overlay->getLegend().isCollapsed());
    GF_EXPECT (! overlay->isTimelineExpanded());                    // collapsed by default

    const auto wide = overlay->getLegend().getWidth();
    overlay->getLegend().setCollapsed (true);
    overlay->getLegend().onCollapsedChanged (true);          // what a click does
    GF_EXPECT (proc.getExplainState().legendCollapsed.load());
    GF_EXPECT (overlay->getLegend().getWidth() < wide);

    // Survives an editor round trip through the saved state.
    juce::MemoryBlock saved;
    proc.getStateInformation (saved);
    GapFillAudioProcessor other;
    other.setStateInformation (saved.getData(), (int) saved.getSize());
    juce::Component parent2;
    SlotHost host2 (other, parent2);

    for (auto* c : parent2.getChildren())
        if (auto* o = dynamic_cast<explain::VisualiserOverlayComponent*> (c))
            GF_EXPECT (o->getLegend().isCollapsed());
}

GF_TEST (ReasonsGui, overlay_passes_clicks_through_except_on_its_own_controls)
{
    GapFillAudioProcessor proc;
    explain::VisualiserOverlayComponent overlay (proc);
    overlay.setBounds (0, 0, 1000, 300);

    GF_EXPECT (! overlay.hitTest (500, 150));                       // the gap view underneath gets its hover tooltips
    GF_EXPECT (overlay.hitTest (20, 16));                           // the legend
    overlay.setTimelineExpanded (true);
    GF_EXPECT (overlay.hitTest (500, 150));                         // the expanded timeline takes input
}

GF_TEST (HealthGui, footer_shows_only_the_highest_priority_message)
{
    GapFillAudioProcessor proc;
    proc.getVisualisationFifo().setEnabled (true);                  // editor open
    auto& ex = proc.getExplainState();

    GF_EXPECT (health::computeFooterView (proc).kind == health::FooterKind::Nothing);       // healthy, nothing evaluated: silence

    // Real outcomes: Fill is at 0, so every gap is skipped for that reason.
    setParams (proc, { { "fill", 0.0f }, { "syncEnable", 0.0f } });
    renderGoldenScenarioA (proc);
    proc.drainFillEvents();

    auto v = health::computeFooterView (proc);
    GF_EXPECT (v.kind == health::FooterKind::LastGap);
    GF_EXPECT (v.text == "Last gap: skipped - Fill is at 0");

    // A suggestion outranks the last gap ...
    FillEvent newest[8];
    const auto& log = proc.getGapOutcomeLog();
    const auto n = juce::jmin (8, log.size());

    for (int i = 0; i < n; ++i)
        newest[i] = log.get (i);

    fixit::ApvtsParamAccess access (proc.getValueTreeState());
    const auto ev = fixit::buildEvidence (newest, n, 0);
    ex.fixitController.tick (1000, ev, access, true, 0);
    ex.fixitController.tick (5100, ev, access, true, 0);
    v = health::computeFooterView (proc);
    GF_EXPECT (v.kind == health::FooterKind::Fixit && v.text == "Suggestion: Fill 0 -> 50%");

    // ... and health outranks both.
    ex.healthTracker.update (health::bitOf (health::HealthCode::NoSidechain), 100);
    v = health::computeFooterView (proc);
    GF_EXPECT (v.kind == health::FooterKind::Health && v.text == "No sidechain - using input");

    // Muting the code hides it from the footer (the suggestion shows again).
    ex.muteHealthCode (health::HealthCode::NoSidechain, true);
    GF_EXPECT (health::computeFooterView (proc).kind == health::FooterKind::Fixit);

    // Critical messages cannot be muted.
    ex.healthTracker.update (health::bitOf (health::HealthCode::OverloadCritical), 200);
    ex.muteHealthCode (health::HealthCode::OverloadCritical, true);
    v = health::computeFooterView (proc);
    GF_EXPECT (v.kind == health::FooterKind::Health && v.healthIsCritical);

    // With the option off, suggestions never appear.
    ex.healthTracker.update (0, 300);
    ex.showSuggestions.store (false);
    GF_EXPECT (health::computeFooterView (proc).kind == health::FooterKind::LastGap);
}

GF_TEST (HealthGui, footer_component_buttons_follow_the_chip)
{
    GapFillAudioProcessor proc;
    proc.getVisualisationFifo().setEnabled (true);
    setParams (proc, { { "fill", 0.0f } });
    auto& ex = proc.getExplainState();

    health::FooterStatusComponent footer (proc);
    footer.setName ("footer");
    footer.setBounds (0, 0, 800, 22);

    FillEvent log[8];

    for (auto& l : log)
    {
        l.type = FillEventType::Rejected;
        l.reason = RejectReason::FillOff;
    }

    fixit::ApvtsParamAccess access (proc.getValueTreeState());
    const auto ev = fixit::buildEvidence (log, 8, 0);
    ex.fixitController.tick (0, ev, access, true, 0);
    ex.fixitController.tick (4500, ev, access, true, 0);
    footer.refreshForTests();

    GF_EXPECT (footer.getView().kind == health::FooterKind::Fixit);
    GF_EXPECT (hasVisiblePixels (render (footer, 800, 22)));

    // Apply through the real controller path, then the chip becomes "Applied ... Undo".
    GF_EXPECT (ex.applyFixit (false));
    footer.refreshForTests();
    GF_EXPECT (footer.getView().text.startsWith ("Applied: Fill 0 -> 50%"));
    GF_EXPECT (ex.undoFixit());
    footer.refreshForTests();
    GF_EXPECT (proc.getValueTreeState().getRawParameterValue ("fill")->load() == 0.0f);
    GF_EXPECT (footer.getView().kind != health::FooterKind::Fixit);
}

GF_TEST (HealthGui, details_panel_lists_everything_in_text)
{
    GapFillAudioProcessor proc;
    auto& ex = proc.getExplainState();
    ex.healthTracker.update (health::bitOf (health::HealthCode::NoSidechain) | health::bitOf (health::HealthCode::BypassActive), 1000);
    ex.muteHealthCode (health::HealthCode::BypassActive, true);
    proc.getMeterState().reasonCount[(int) RejectReason::TooShort].store (12);
    proc.getMeterState().reasonCount[(int) RejectReason::DensityRoll].store (3);

    health::DetailsPanel panel (proc);
    panel.setName ("details");
    const auto text = panel.getPlainText();

    GF_EXPECT (text.contains ("No sidechain - using input"));
    GF_EXPECT (text.contains ("Bypassed - dry signal only") && text.contains ("[x] Hide this message"));     // muted but listed
    GF_EXPECT (text.contains ("What to do:"));
    GF_EXPECT (text.contains ("Gap too short (12 gaps)"));
    GF_EXPECT (text.contains ("PDC "));
    GF_EXPECT (text.contains ("estimated") || text.contains ("not measured"));
    GF_EXPECT (text.contains ("never stores audio"));
    GF_EXPECT (text.contains ("RECORD"));
    GF_EXPECT (text.contains ("Show suggestions"));
    GF_EXPECT (hasVisiblePixels (render (panel, 480, panel.getHeight())));
}

GF_TEST (TimelineGui, a11y_list_matches_events)
{
    GapFillAudioProcessor proc;
    timeline::TimelineView view (proc);
    view.setBounds (0, 0, 900, 300);
    auto& model = proc.getExplainState().timelineModel;

    for (int i = 0; i < 25; ++i)
    {
        timeline::TimelineEvent e;
        e.type = i % 3 == 0 ? timeline::EventType::GapEval : timeline::EventType::Onset;
        e.reason = (std::uint8_t) (i % 3 == 0 ? RejectReason::TooShort : RejectReason::None);
        e.availMs = 30;
        e.needMs = 60;
        e.pos = i * 4800;
        model.push (e);
    }

    view.refreshForTests();
    const auto text = view.getListText();
    GF_EXPECT (view.getNumListRows() == 20 && text.size() == 20);

    timeline::TimelineEvent newest[20];
    GF_EXPECT (model.latest (newest, 20) == 20);

    for (int i = 0; i < 20; ++i)
        GF_EXPECT_MSG (text[i].endsWith (timeline::describe (newest[i])), text[i]);
}

GF_TEST (TimelineGui, screenshot_min_default_large)
{
    GapFillAudioProcessor proc;
    proc.getVisualisationFifo().setEnabled (true);
    setParams (proc, { { "fill", 100.0f }, { "density", 70.0f } });
    renderGoldenScenarioA (proc);
    proc.drainFillEvents();
    GF_EXPECT (proc.getExplainState().timelineModel.size() > 20);

    timeline::TimelineView view (proc);
    view.setName ("timeline");
    view.refreshForTests();

    for (const auto size : { juce::Point<int> (480, 160), juce::Point<int> (1000, 260), juce::Point<int> (2000, 600) })
    {
        const auto img = render (view, size.x, size.y);
        GF_EXPECT_MSG (hasVisiblePixels (img), juce::String (size.x) + "x" + juce::String (size.y));
    }

    // Every pixel inside the view: nothing is drawn outside its bounds by construction (Image is the bounds).
    view.setBounds (0, 0, 1000, 260);
    view.refreshForTests();
    GF_EXPECT (view.getNumListRows() > 0);
}

GF_TEST (TimelineGui, reduced_motion_static)
{
    GapFillAudioProcessor proc;

    {
        timeline::TimelineView normal (proc);
        normal.setVisible (true);
        normal.visibilityChanged();
        GF_EXPECT (normal.getRefreshIntervalMs() > 0 && normal.getRefreshIntervalMs() <= 100);
    }

    proc.setReducedMotionEnabled (true);
    timeline::TimelineView calm (proc);
    calm.setVisible (true);
    calm.visibilityChanged();
    GF_EXPECT (calm.getRefreshIntervalMs() >= 250);                 // redraw at 4 Hz, no scrolling animation

    calm.setVisible (false);
    calm.visibilityChanged();
    GF_EXPECT (calm.getRefreshIntervalMs() == 0);                   // not running while collapsed
}

GF_TEST (ExplainGui, editor_hosts_footer_and_overlay_and_keeps_min_size_layout)
{
    GapFillAudioProcessor proc;

    for (const auto size : { juce::Point<int> (1020, 620), juce::Point<int> (1100, 700), juce::Point<int> (1900, 1100) })
    {
        GapFillAudioProcessorEditor editor (proc);
        editor.setSize (size.x, size.y);

        health::FooterStatusComponent* footer = nullptr;
        explain::VisualiserOverlayComponent* overlay = nullptr;

        for (auto* c : editor.getChildren())
        {
            if (auto* f = dynamic_cast<health::FooterStatusComponent*> (c)) footer = f;
            if (auto* o = dynamic_cast<explain::VisualiserOverlayComponent*> (c)) overlay = o;
        }

        GF_EXPECT (footer != nullptr && overlay != nullptr);
        GF_EXPECT (footer->isVisible() && overlay->isVisible());
        GF_EXPECT (editor.getLocalBounds().contains (footer->getBounds()));
        GF_EXPECT (editor.getLocalBounds().contains (overlay->getBounds()));
        GF_EXPECT (footer->getHeight() >= 20);
        GF_EXPECT (overlay->getHeight() > 100 && overlay->getWidth() > 600);

        // The overlay never reaches into the footer.
        GF_EXPECT (! overlay->getBounds().intersects (footer->getBounds()));
    }
}
