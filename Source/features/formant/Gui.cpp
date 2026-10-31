#include "../../PluginProcessor.h"
#include "../../gui/Knob.h"
#include "../../gui/SlotHost.h"

namespace gapfill::formant
{
namespace
{
    class FormantPanel final : public juce::Component
    {
    public:
        explicit FormantPanel (juce::AudioProcessorValueTreeState& state)
            : amount (state, "fl20_amount", "Amount", "Per-fill formant processing; 0 is off.", colours::secondary),
              shift (state, "fl20_shift", "Shift", "Moves resonant character without changing pitch.", colours::secondary),
              tilt (state, "fl20_tilt", "Tilt", "Brightness slope in dB per octave.", colours::secondary),
              preserve (state, "fl20_preserve", "Preserve", "Original versus vowel envelope, in percent.", colours::secondary),
              vowel (state, "fl20_vowel", "Vowel", "Vowel-like resonances colour the sound; they do not make words."),
              detail (state, "fl20_detail", "Detail", "Resolution of the spectral-envelope estimate.")
        {
            setTitle ("Voice formant controls");
            setDescription ("Values are sampled at the next fill start. Active processing delays the fill onset by about eleven milliseconds.");
            for (auto* control : std::initializer_list<juce::Component*> { &amount, &shift, &tilt, &vowel, &preserve, &detail })
                addAndMakeVisible (*control);
            setSize (360, 184);
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (8);
            auto top = area.removeFromTop (80);
            const int width = (top.getWidth() / 3 / 8) * 8;
            amount.setBounds (top.removeFromLeft (width));
            shift.setBounds (top.removeFromLeft (width));
            tilt.setBounds (top);
            area.removeFromTop (8);
            vowel.setBounds (area.removeFromLeft (width).withHeight (48));
            preserve.setBounds (area.removeFromLeft (width));
            detail.setBounds (area.withHeight (48));
        }

    private:
        GapKnob amount, shift, tilt, preserve;
        GapCombo vowel, detail;
    };

    class FormantButton final : public juce::TextButton
    {
    public:
        explicit FormantButton (GapFillAudioProcessor& proc) : juce::TextButton ("FORMANT")
        {
            setTitle ("Voice formant");
            setDescription ("Open per-fill formant Amount, Shift, Tilt, Vowel, Preserve and Detail controls.");
            setTooltip ("Per-fill formant controls. The existing Formant bus effect is separate.");
            onClick = [this, owner = &proc]
            {
                if (auto* parent = getTopLevelComponent())
                    juce::CallOutBox::launchAsynchronously (std::make_unique<FormantPanel> (owner->getValueTreeState()),
                                                          parent->getLocalArea (this, getLocalBounds()), parent);
            };
        }
    };

    const SlotRegistrar formantButton ({ Slot::CreativeExtra, 130,
        [] (GapFillAudioProcessor& proc) { return std::make_unique<FormantButton> (proc); } });
}
} // namespace gapfill::formant
