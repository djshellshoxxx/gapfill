#pragma once

// WP5 shaping controls (docs/dev/04 C-1/C-3/C-4/C-6/C-7/C-8/C-10, docs/dev/12 F-D01, docs/dev/11).
//
// One "SHAPE" button lives in Slot::CreativeExtra next to CAPTURE and opens a call-out with the
// grouped controls. Simple view shows only the Character group (Personality); Advanced shows every
// group. Hiding a control never changes it. All controls are parameter-backed (automatable,
// MIDI-learnable, tooltips from the ParamInfo table); the two capture buttons post commands.
//
// The Options row for the fallback tempo (E-2) is a separate component in Slot::OptionsExtra.
//
// Message thread only; reads atomics (telemetry, capture status), never audio memory.

#include "../../PluginProcessor.h"
#include "../../gui/Knob.h"

#include <memory>
#include <vector>

namespace gapfill::shapegui
{

struct Cell
{
    enum class Kind { Knob, Combo, Toggle };

    Kind kind;
    const char* paramId;
    const char* label;
    const char* tooltip;
};

struct Group
{
    const char* title;
    bool advancedOnly;
    std::vector<Cell> cells;
};

/** The static table the panel is built from (tests read it). */
const std::vector<Group>& groups();

//==============================================================================
/** Draws the selected Fill Shape curve (C-4 preview); repaints when the parameters change. */
class ShapePreview : public juce::Component,
                     private juce::Timer
{
public:
    explicit ShapePreview (juce::AudioProcessorValueTreeState&);
    ~ShapePreview() override;
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    juce::AudioProcessorValueTreeState& state;
    int lastShape = -1;
    float lastAmount = -1.0f;
};

//==============================================================================
class ShapePanel : public juce::Component,
                   private juce::Timer
{
public:
    static constexpr int kContentWidth = 600;

    explicit ShapePanel (GapFillAudioProcessor&);
    ~ShapePanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Height the panel needs at kContentWidth for the current view mode. */
    int getPreferredHeight() const;

    /** Shows/hides the advanced groups for the processor's current view mode. */
    void applyViewMode();

    juce::String getCaptureStatusText() const { return captureStatus.getText(); }
    juce::String getEvolveText() const        { return evolveLabel.getText(); }
    int getNumVisibleControls() const;

private:
    struct Placed
    {
        const Group* group;
        std::vector<std::unique_ptr<juce::Component>> controls;
    };

    void timerCallback() override;
    int layoutGroups (bool apply);

    GapFillAudioProcessor& proc;
    std::vector<Placed> placed;
    ShapePreview preview;
    juce::Label evolveLabel, captureStatus;
    juce::TextButton recapture { "RECAPTURE" }, release { "RELEASE" };
    bool advanced = true;
    std::vector<int> titleY;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShapePanel)
};

} // namespace gapfill::shapegui
