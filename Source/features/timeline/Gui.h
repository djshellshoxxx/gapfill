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
#include <limits>

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
    bool keyPressed (const juce::KeyPress&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    std::function<void()> onCloseRequested;

    /** The text the companion list shows, newest first (at most 20 rows). */
    juce::StringArray getListText() const;

    static constexpr int listRows = 20;

    // Test access
    int getNumListRows() const;
    void refreshForTests()    { refresh(); }
    int getRefreshIntervalMs() const noexcept { return isTimerRunning() ? getTimerInterval() : 0; }
    juce::String getInspectorText() const { return inspectorText; }
    bool isEventListShown() const noexcept { return listOnly; }
    struct RetainedMemory
    {
        std::size_t model = 0, frameSnapshot = 0, eventSnapshot = 0, view = 0, textUpperBound = 0;
        std::size_t total() const noexcept { return model + frameSnapshot + eventSnapshot + view + textUpperBound; }
    };
    /** Message-thread test accounting of all retained model/view data, including
        frozen snapshots and conservative text storage; excludes raster images. */
    RetainedMemory retainedMemoryForTests() const;

private:
    static constexpr int controlsHeight = 28;
    void timerCallback() override;
    void refresh();
    std::int64_t liveEndPos() const;
    std::int64_t windowSamples() const;
    void paintLanes (juce::Graphics&, juce::Rectangle<float> plot);
    void collectFrames();
    void paintFrameLanes (juce::Graphics&, juce::Rectangle<float> audioLane,
                         juce::Rectangle<float> gapLane, juce::Rectangle<float> protectionLane,
                         juce::Rectangle<float> bandLanes);
    juce::Rectangle<int> plotBounds() const;
    void updateInspector (float x);
    void setPaused (bool);
    void setListOnly (bool);

    int getNumRows() override;
    juce::String getNameForRow (int row) override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    juce::String listRowText (int row) const;

    GapFillAudioProcessor& proc;
    juce::TextButton pauseButton { "FREEZE" }, zoom4 { "4 s" }, zoom8 { "8 s" }, zoom16 { "16 s" },
                     listButton { "EVENT LIST" }, closeButton { "CLOSE" };
    juce::ListBox list { "Recent gap events", nullptr };
    std::vector<TimelineEvent> windowEvents;
    std::array<TimelineEvent, (size_t) listRows> listItems {};
    int numListItems = 0;
    std::int64_t shownEnd = 0;
    static constexpr int maxDisplayColumns = 2048;
    std::vector<FrameHistory::Column> displayFrames;
    bool listOnly = false, inspectorShown = false, snapshotReady = false;
    juce::String inspectorText;
    float inspectorX = 0.0f;
    std::int64_t dragStartEnd = 0;
    std::uint64_t displayedLanguageGeneration = std::numeric_limits<std::uint64_t>::max();
    std::uint32_t displayedEpoch = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TimelineView)
};

} // namespace gapfill::timeline
