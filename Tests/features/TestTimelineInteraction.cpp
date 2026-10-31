#include "../support/TestRig.h"

#include "features/explain/OverlaySlot.h"
#include "features/timeline/Gui.h"

using namespace gapfill;
using namespace gapfill::timeline;

GF_TEST (Timeline, paused_window_does_not_mark_future_losses_as_visible)
{
    TimelineModel model;
    model.noteDropped (2000);
    GF_EXPECT (! model.isIncomplete (1000, 500));
    GF_EXPECT (model.isIncomplete (2200, 500));
}

GF_TEST (Timeline, scrub_is_clamped_to_retained_history_and_live_end)
{
    TimelineModel model;
    TimelineEvent event;
    event.pos = 200;
    model.push (event);
    model.setPaused (true, 1000);
    model.scrubTo (-100000, 1000);
    GF_EXPECT (model.viewEnd (2000) == 200);
    model.scrubTo (100000, 1000);
    GF_EXPECT (model.viewEnd (2000) == 1000);
    model.setPaused (false, 0);
    model.scrubTo (-100000, 1000);
    GF_EXPECT (model.viewEnd (2000) == 2000);
}

GF_TEST (Timeline, initial_frame_epoch_uses_the_full_position_anchor)
{
    FrameHistory history;
    history.configure (192000.0, 8.0);
    history.setPositionAnchor (0x200000040ll);
    VizFrame frame;
    frame.posLo = 32;
    history.push (frame);
    GF_EXPECT (history.at (0).position == 0x200000020ll);
}

GF_TEST (TimelineGui, host_shortcuts_are_ignored_until_keyboard_navigation_is_enabled)
{
    GapFillAudioProcessor proc;
    proc.setKeyboardNavigationEnabled (false);
    TimelineView view (proc);
    view.setSize (1000, 300);
    GF_EXPECT (! view.keyPressed (juce::KeyPress ('f')));
    GF_EXPECT (! proc.getExplainState().timelineModel.isPaused());

    proc.setKeyboardNavigationEnabled (true);
    GF_EXPECT (view.keyPressed (juce::KeyPress ('f')));
    GF_EXPECT (proc.getExplainState().timelineModel.isPaused());
    GF_EXPECT (view.keyPressed (juce::KeyPress (juce::KeyPress::homeKey)));
    GF_EXPECT (! proc.getExplainState().timelineModel.isPaused());
}

GF_TEST (TimelineGui, list_and_expansion_preferences_survive_state_roundtrip)
{
    GapFillAudioProcessor proc;
    proc.setKeyboardNavigationEnabled (true);
    explain::VisualiserOverlayComponent overlay (proc);
    overlay.setBounds (0, 0, 1000, 200);
    overlay.setTimelineExpanded (true);
    auto& view = overlay.getTimeline();
    GF_EXPECT (view.keyPressed (juce::KeyPress ('l')));
    GF_EXPECT (view.isEventListShown());

    juce::MemoryBlock state;
    proc.getStateInformation (state);
    GapFillAudioProcessor restored;
    restored.setStateInformation (state.getData(), (int) state.getSize());
    explain::VisualiserOverlayComponent other (restored);
    GF_EXPECT (other.isTimelineExpanded());
    GF_EXPECT (other.getTimeline().isEventListShown());
}

GF_TEST (TimelineGui, neutral_preferences_preserve_the_legacy_health_shape)
{
    GapFillAudioProcessor proc;
    proc.getExplainState().legendCollapsed.store (true);
    const auto health = explain::serialiseSubtree (proc);
    GF_EXPECT (health.isValid());
    GF_EXPECT (! health.hasProperty ("timelineExpanded"));
    GF_EXPECT (! health.hasProperty ("timelineList"));
    GF_EXPECT (health.getNumProperties() == 5);
}

GF_TEST (TimelineGui, expansion_stays_within_the_editor_available_area)
{
    GapFillAudioProcessor proc;
    explain::VisualiserOverlayComponent overlay (proc);
    overlay.setBounds (10, 90, 1000, 150);
    overlay.setTimelineExpanded (true);
    overlay.setExpansionBounds ({ 10, 90, 1000, 400 });
    GF_EXPECT (overlay.getBounds() == juce::Rectangle<int> (10, 90, 1000, 400));
    GF_EXPECT (overlay.getBottom() == 490); // the editor's supplied safety boundary
}

GF_TEST (TimelineGui, inspector_uses_the_engine_reason_record)
{
    GapFillAudioProcessor proc;
    proc.setKeyboardNavigationEnabled (true);
    TimelineEvent event;
    event.type = EventType::GapEval;
    event.reason = (std::uint8_t) RejectReason::TooShort;
    event.availMs = 38;
    event.needMs = 60;
    event.pos = -4800;
    proc.getExplainState().timelineModel.push (event);

    TimelineView view (proc);
    view.setSize (1000, 300);
    view.refreshForTests();
    GF_EXPECT (view.keyPressed (juce::KeyPress ('i')));
    GF_EXPECT (view.getInspectorText().contains ("38"));
    GF_EXPECT (view.getInspectorText().contains ("60"));
    GF_EXPECT (view.getInspectorText().contains ("Gap"));
    GF_EXPECT (view.getAccessibilityHandler() != nullptr);
}

GF_TEST (TimelineGui, freeze_retains_the_visible_list_while_engine_records_continue)
{
    GapFillAudioProcessor proc;
    proc.setKeyboardNavigationEnabled (true);
    TimelineEvent event;
    event.type = EventType::GapEval;
    event.reason = (std::uint8_t) RejectReason::TooShort;
    event.pos = -9600;
    auto& model = proc.getExplainState().timelineModel;
    model.push (event);
    TimelineView view (proc);
    view.setSize (1000, 300);
    view.refreshForTests();
    GF_EXPECT (view.keyPressed (juce::KeyPress ('f')));
    const auto frozen = view.getListText();
    event.pos = -4800;
    event.reason = (std::uint8_t) RejectReason::DensityRoll;
    model.push (event);
    view.refreshForTests();
    GF_EXPECT (model.size() == 2);
    GF_EXPECT (view.getListText() == frozen);
    GF_EXPECT (view.keyPressed (juce::KeyPress (juce::KeyPress::homeKey)));
    GF_EXPECT (view.getListText().size() == 2);
}
