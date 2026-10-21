// F-L11 Options row: "MIDI output offset" (-100..+100 ms). Shifts the emitted gap / fill notes for hosts that
// do not delay MIDI by the plug-in's reported latency. Not a parameter: stored in <Perf> (midiOutOffsetMs).
// Registered in Slot::OptionsExtra (a Stack slot inside the scrolling Options area). Builds without
// GAPFILL_MIDI_OUT do not declare MIDI output, so the row is shown disabled with a reason.

#include "MidiOut.h"
#include "../../PluginProcessor.h"
#include "../../gui/LookAndFeel.h"
#include "../../gui/SlotHost.h"

namespace gapfill::midiout
{

namespace
{
    class MidiOutOptions : public juce::Component
    {
    public:
        explicit MidiOutOptions (GapFillAudioProcessor& p) : proc (p)
        {
            setTitle ("MIDI output offset");

            label.setText ("MIDI output offset", juce::dontSendNotification);
            label.setFont (fonts::label (10.0f));
            label.setColour (juce::Label::textColourId, colours::textMuted);
            addAndMakeVisible (label);

            slider.setSliderStyle (juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
            slider.setRange (-kMaxOffsetMs, kMaxOffsetMs, 1.0);
            slider.setTextValueSuffix (" ms");
            slider.setTitle ("MIDI output offset");
            slider.setTooltip ("Moves the gap and fill notes earlier (negative) or later (positive) to line up with the audio in hosts that "
                               "do not delay MIDI by the plug-in's latency. Saved with the project.");
            slider.setValue (proc.getMidiOutOffsetMs(), juce::dontSendNotification);
            slider.onValueChange = [this] { proc.setMidiOutOffsetMs (slider.getValue()); };
            addAndMakeVisible (slider);

            hint.setFont (fonts::ui (10.0f));
            hint.setColour (juce::Label::textColourId, colours::textMuted);
            hint.setText (kDeclared ? juce::String ("Only used while MIDI output (Performance > Routing) is not Off.")
                                                : juce::String ("This build does not declare MIDI output, so no notes are sent."),
                          juce::dontSendNotification);
            addAndMakeVisible (hint);

            slider.setEnabled (kDeclared);
            setSize (400, 52);
        }

        void resized() override
        {
            auto r = getLocalBounds();
            label.setBounds (r.removeFromTop (16));
            hint.setBounds (r.removeFromBottom (14));
            slider.setBounds (r);
        }

        juce::Slider& getSlider() noexcept { return slider; }

    private:
        GapFillAudioProcessor& proc;
        juce::Label label, hint;
        juce::Slider slider;
    };

    const SlotRegistrar midiOutOptions ({ Slot::OptionsExtra, 280,
        [] (GapFillAudioProcessor& proc) -> std::unique_ptr<juce::Component>
        {
            return std::make_unique<MidiOutOptions> (proc);
        } });
}

} // namespace gapfill::midiout
