#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "LookAndFeel.h"

namespace gapfill
{

//==============================================================================
/** The options page, reached from the FILE dropdown.

    It holds preferences rather than parameters: nothing here is automatable and
    nothing here belongs in a preset. The owner supplies the callbacks so this
    component never needs to know about the processor. */
class OptionsView : public juce::Component
{
public:
    OptionsView();

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Called with the new state whenever the tooltip switch is used. */
    std::function<void (bool)> onTooltipsChanged;

    /** Called when the user asks for the audio/MIDI device panel. Left unset by
        the plugin builds, where the host owns the devices - the button then
        explains that rather than disappearing. */
    std::function<void()> onShowDeviceSettings;

    std::function<void()> onClose;

    /** Pushes the current values in without firing the callbacks. */
    void refresh (bool tooltipsEnabled);

private:
    void updateDeviceRow();

    juce::ToggleButton tooltipToggle { "Show hover tooltips" };
    juce::Label        tooltipHint;

    juce::TextButton   deviceButton { "AUDIO / MIDI SETTINGS..." };
    juce::Label        deviceHint;

    juce::TextButton   closeButton { "CLOSE" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OptionsView)
};

} // namespace gapfill
