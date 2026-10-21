// N-MUS Scale Lock controls (docs/dev/12 section 5): root and scale choices, the
// snap amount, and a read-only "Key: D Natural Minor" chip whose tooltip lists the
// active pitch classes. Registered in Slot::PerformanceStrip; the slot stays empty
// until UI-0 (WP11) reserves its rectangle in PluginEditor::resized.

#include "../Knob.h"
#include "../HelpRegistry.h"
#include "../SlotHost.h"
#include "../../PluginProcessor.h"
#include "../../features/scale/ScaleLock.h"

namespace gapfill
{

namespace
{
    class ScaleStrip : public juce::Component,
                       private juce::Timer
    {
    public:
        explicit ScaleStrip (GapFillAudioProcessor& proc)
            : state (proc.getValueTreeState()),
              root (state, "scaleRoot", "Key",
                    "The song's tonic. Pitched fills are kept to intervals that belong to the chosen scale around this note."),
              type (state, "scaleType", "Scale",
                    "Scale Lock: keep pitched fills (Pitch Rise/Fall, Granular, Comb, Cloud) in key. Off leaves pitch untouched."),
              amount (state, "scaleAmount", "Snap",
                      "How often a pitch decision is snapped to the scale. 100% = always.", colours::secondary)
        {
            setTitle ("Scale Lock");
            addAndMakeVisible (root);
            addAndMakeVisible (type);
            addAndMakeVisible (amount);

            chip.setJustificationType (juce::Justification::centredLeft);
            chip.setColour (juce::Label::textColourId, colours::textMuted);
            chip.setFont (fonts::label (11.0f));
            addAndMakeVisible (chip);

            refreshChip();
            startTimerHz (4);
        }

        ~ScaleStrip() override { stopTimer(); }

        void resized() override
        {
            auto r = getLocalBounds();
            root.setBounds (r.removeFromLeft (juce::jmax (56, r.getWidth() / 6)));
            r.removeFromLeft (8);
            type.setBounds (r.removeFromLeft (juce::jmax (110, r.getWidth() / 3)));
            r.removeFromLeft (8);
            amount.setBounds (r.removeFromLeft (juce::jmax (56, r.getWidth() / 3)));
            r.removeFromLeft (8);
            chip.setBounds (r);
        }

    private:
        void timerCallback() override { refreshChip(); }

        void refreshChip()
        {
            const auto set = scale::makeScaleSet (rawChoice ("scaleRoot"), rawChoice ("scaleType"));
            char label[64] {}, classes[64] {};
            scale::describeShort (set, label, (int) sizeof (label));
            scale::describePitchClasses (set, classes, (int) sizeof (classes));

            const auto text = set.active() ? juce::String ("Key: ") + juce::String (label) : juce::String ("Key: not set");

            if (chip.getText() != text)
                chip.setText (text, juce::dontSendNotification);

            chip.setTooltip (set.active() ? juce::String ("Notes in key: ") + juce::String (classes) : juce::String ("Scale Lock is off."));
        }

        int rawChoice (const char* id) const
        {
            if (auto* p = state.getRawParameterValue (id))
                return (int) std::lround (p->load());

            return 0;
        }

        juce::AudioProcessorValueTreeState& state;
        GapCombo root, type;
        GapKnob amount;
        juce::Label chip;
    };

    const SlotRegistrar scaleStrip ({ Slot::PerformanceStrip, 100,
        [] (GapFillAudioProcessor& proc) -> std::unique_ptr<juce::Component>
        {
            return std::make_unique<ScaleStrip> (proc);
        } });

    const HelpRegistrar helpScale (6010, "Scale Lock", [] (HelpWriter& w)
    {
        w.body ("Scale Lock keeps pitched fills in key. Set Key to the song's tonic and Scale to its scale; Pitch Rise and Pitch Fall, "
                "Granular grains, Comb tuning and Granular Cloud pitch spread then land on notes that belong to it. Snap sets how often "
                "a pitch decision is snapped (100% = always). Off, or Snap 0%, leaves pitch exactly as before.");
        w.body ("Limit: GapFill does not know the pitch of each hit. Relative pitch shifts are kept to in-key intervals measured from the "
                "tonic you set, which is right when the hit is the tonic or a chord tone. Drums and other unpitched sounds are not retuned; "
                "they are only constrained to in-key intervals.");
    });
}

} // namespace gapfill
