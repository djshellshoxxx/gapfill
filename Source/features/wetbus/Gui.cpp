#include "Gui.h"

#include "WetBus.h"
#include "../../PluginProcessor.h"
#include "../../gui/LookAndFeel.h"
#include "../../gui/SlotHost.h"
#include "../i18n/Strings.h"

namespace gapfill::wetbus
{

OutputsOptions::OutputsOptions (GapFillAudioProcessor& p)
    : proc (p),
      mainMode (p.getValueTreeState(), "wetBusMainMode", "MAIN OUTPUT",
                "Dry only removes fills from the main output while the Wet bus is enabled in your DAW. Wet Only takes precedence."),
      level (p.getValueTreeState(), "wetBusLevel", "WET BUS LEVEL",
             "Trim only the separate Wet output. Dry, Wet and Output do not affect this bus; bypass and Panic silence it.", colours::primary)
{
    title.setFont (fonts::sectionHeader (11.0f));
    title.setColour (juce::Label::textColourId, colours::text);
    for (auto* label : { &status, &hint })
    {
        label->setFont (fonts::ui (10.0f));
        label->setColour (juce::Label::textColourId, colours::textMuted);
        label->setMinimumHorizontalScale (0.8f);
    }
    mainMode.setTitle ("Main output mode");
    level.setTitle ("Wet bus level");
    for (auto* component : std::initializer_list<juce::Component*> { &title, &mainMode, &level, &status, &hint })
        addAndMakeVisible (component);
    setSize (400, 96);
    refresh();
    startTimerHz (4);
}

OutputsOptions::~OutputsOptions() { stopTimer(); }

void OutputsOptions::resized()
{
    auto r = getLocalBounds();
    title.setBounds (r.removeFromTop (18));
    auto controls = r.removeFromTop (38);
    const auto half = controls.getWidth() / 2;
    mainMode.setBounds (controls.removeFromLeft (half).withTrimmedRight (8));
    level.setBounds (controls);
    status.setBounds (r.removeFromTop (20));
    hint.setBounds (r.removeFromTop (20));
}

void OutputsOptions::timerCallback() { refresh(); }

void OutputsOptions::refresh()
{
    setTitle (i18n::text ("Outputs"));
    title.setText (i18n::text ("OUTPUTS"), juce::dontSendNotification);
    const auto* bus = proc.getBusCount (false) > kWetBusIndex ? proc.getBus (false, kWetBusIndex) : nullptr;
    const bool declared = bus != nullptr;
    const bool enabled = declared && bus->isEnabled();
    mainMode.setEnabled (enabled);
    level.setEnabled (enabled);

    juce::String stateText;
    if (! declared)
        stateText = i18n::text ("Separate Wet output is unavailable in this plug-in format.");
    else if (! enabled)
        stateText = i18n::text ("Wet bus is off. Enable it in your DAW's output routing.");
    else if (bus->getNumberOfChannels() == 1)
        stateText = i18n::text ("Wet bus: enabled by host (mono).");
    else
        stateText = i18n::text ("Wet bus: enabled by host (stereo).");
    const auto hintText = enabled ? i18n::text ("Dry, Wet and Output do not change its level.")
                                  : i18n::text ("Fills stay in the main output while the Wet bus is off.");
    status.setText (stateText, juce::dontSendNotification);
    status.setTitle (stateText);
    hint.setText (hintText, juce::dontSendNotification);
    hint.setTitle (hintText);
    setDescription (stateText + "\n" + hintText);
}

namespace
{
    const SlotRegistrar outputsOptions ({ Slot::OptionsExtra, 310,
        [] (GapFillAudioProcessor& proc) -> std::unique_ptr<juce::Component>
        {
            return std::make_unique<OutputsOptions> (proc);
        } });
}

} // namespace gapfill::wetbus
