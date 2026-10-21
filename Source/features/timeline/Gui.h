#pragma once

// F-M03 Gap timeline view (docs/dev/12 F-M03, docs/dev/10 section 4.1).
//
// Draws the TimelineModel on a common time axis: events with their protected
// zones, gaps with the confidence line and the engine's threshold, fills, skipped
// gaps with the reason letter, preview / scene / panic markers. A keyboard
// reachable list repeats the last 20 events as text. Reasons come from the
// engine's records and are never recomputed here.

#include "../../gui/LookAndFeel.h"
#include "Model.h"

#include <functional>

namespace gapfill
{
class GapFillAudioProcessor;
}

namespace gapfill::timeline
{

class TimelineView : public juce::Component,
                     private juce::Timer,
                     private juce::ListBoxModel
{
public:
    explicit TimelineView (GapFillAudioProcessor&);
    ~TimelineView() override;

    /** The expanded view is only drawn (and the timer only runs) while visible. */
    void visibilityChanged() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onCloseRequested;

    /** The text the companion list shows, newest first (at most 20 rows). */
    juce::StringArray getListText() const;

    static constexpr int listRows = 20;

    // Test access
    int getNumListRows() const;
    void refreshForTests()    { refresh(); }
    int getRefreshIntervalMs() const noexcept { return isTimerRunning() ? getTimerInterval() : 0; }

private:
    void timerCallback() override;
    void refresh();
    std::int64_t liveEndPos() const;
    std::int64_t windowSamples() const;
    void paintLanes (juce::Graphics&, juce::Rectangle<float> plot);

    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;

    GapFillAudioProcessor& proc;
    juce::TextButton pauseButton { "PAUSE" }, zoom4 { "4 s" }, zoom8 { "8 s" }, zoom16 { "16 s" }, closeButton { "CLOSE" };
    juce::ListBox list { "Recent gap events", nullptr };
    std::vector<TimelineEvent> windowEvents;
    std::array<TimelineEvent, (size_t) listRows> listItems {};
    int numListItems = 0;
    std::int64_t shownEnd = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TimelineView)
};

} // namespace gapfill::timeline
