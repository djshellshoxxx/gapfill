#pragma once

// F-G01 GLITCH tab: one scrolling panel (Slot::GlitchTab) with a section per
// module, built from a static table (also read by tests). Message thread only;
// the status line reads glitch::Meters atomics, never audio memory.

#include "../../gui/Knob.h"

#include <memory>
#include <vector>

namespace gapfill
{
class GapFillAudioProcessor;
}

namespace gapfill::glitch
{

struct ControlSpec
{
    enum class Kind { Knob, Combo, Toggle, Stab };
    Kind kind;
    const char* paramId;
    const char* label;
};

struct SectionSpec
{
    const char* title;
    std::vector<ControlSpec> controls;
};

/** The GLITCH tab layout: every fg01_* parameter appears exactly once. */
const std::vector<SectionSpec>& sectionSpecs();

/** STAB pad: press-and-hold drives fg01_stab with a host gesture (momentary). */
class StabPad : public juce::TextButton
{
public:
    explicit StabPad (juce::RangedAudioParameter*);
    ~StabPad() override;

private:
    void buttonStateChanged() override;
    juce::RangedAudioParameter* param = nullptr;
    bool down = false;
};

class GlitchPanel : public juce::Component,
                    private juce::Timer
{
public:
    explicit GlitchPanel (GapFillAudioProcessor&);
    ~GlitchPanel() override;

    void resized() override;
    void paint (juce::Graphics&) override;

    /** Height the panel needs at a given width (flow layout). */
    int heightForWidth (int width) const;

    juce::String getStatusText() const { return status.getText(); }
    int getNumControls() const noexcept { return (int) controls.size(); }

private:
    void timerCallback() override;
    int layout (int width, bool apply);

    struct Section
    {
        juce::Label title;
        std::vector<juce::Component*> members;
    };

    GapFillAudioProcessor& proc;
    std::vector<std::unique_ptr<juce::Component>> controls;
    std::vector<std::unique_ptr<Section>> sections;
    juce::Label status;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlitchPanel)
};

} // namespace gapfill::glitch
