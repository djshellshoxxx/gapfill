#pragma once

#include "../../gui/Knob.h"

namespace gapfill
{
class GapFillAudioProcessor;

namespace wetbus
{

/** Optional host routing controls. Bus enable/layout always belongs to the
    host; these parameters are project performance state, never preset state. */
class OutputsOptions : public juce::Component, private juce::Timer
{
public:
    explicit OutputsOptions (GapFillAudioProcessor&);
    ~OutputsOptions() override;
    void resized() override;

private:
    void timerCallback() override;
    void refresh();

    GapFillAudioProcessor& proc;
    juce::Label title, status, hint;
    GapCombo mainMode;
    GapBar level;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OutputsOptions)
};

} // namespace wetbus
} // namespace gapfill
