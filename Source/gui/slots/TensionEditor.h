#pragma once

// F-M21 curve editor popover content (docs/dev/features/F-M21 s6). One lane at a time (Density / Intensity /
// Width / FX Send), a 480 x 160 canvas with a 1/16 grid, draggable points, double-click to add, Delete to
// remove, shape buttons that copy a built-in shape into the lane, RESET LANE, and a playhead.
//
// The edit rules live in features/tension/CurveEdit.h (pure, tested); this class only maps mouse / keyboard to
// them and publishes the result through TensionService::publishCurves. The first edit switches Shape to Custom,
// otherwise the drawn curve would not be heard.

#include <juce_gui_basics/juce_gui_basics.h>

#include "../../features/tension/CurveEdit.h"
#include "../../features/tension/Tension.h"

namespace gapfill
{

class GapFillAudioProcessor;

class TensionCurveEditor : public juce::Component,
                           private juce::Timer
{
public:
    explicit TensionCurveEditor (GapFillAudioProcessor&);
    ~TensionCurveEditor() override;

    static constexpr int kWidth = 492, kHeight = 252, kCanvasHeight = 160;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    // Operations shared by mouse, keyboard and tests. All of them publish the curves.
    void setLane (int lane);
    int getLane() const noexcept            { return lane; }
    int getSelectedPoint() const noexcept   { return selected; }
    void selectPoint (int index);
    void addPointAt (int xPerMille, int yPerMille);
    bool removeSelected();
    void nudgeSelected (int dxPerMille, int dyPerMille);
    void applyShape (int shape);
    void resetCurrentLane();
    tension::TensionCurves getCurves() const noexcept { return curves; }

    juce::Rectangle<int> getCanvasBounds() const noexcept { return canvas; }
    juce::String describeSelection() const;

private:
    void timerCallback() override;
    void commit();
    void refreshAnnouncement();
    int xToPerMille (int px) const noexcept;
    int yToPerMille (int py) const noexcept;
    juce::Point<float> toPixels (const tension::TensionPoint&) const noexcept;

    GapFillAudioProcessor& proc;
    tension::TensionCurves curves;
    int lane = 0, selected = 0;
    bool dragging = false;
    float lastPlayhead = -1.0f;
    juce::Rectangle<int> canvas;
    juce::OwnedArray<juce::TextButton> laneButtons, shapeButtons;
    juce::TextButton resetButton { "RESET LANE" };
    juce::Label status;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TensionCurveEditor)
};

} // namespace gapfill
