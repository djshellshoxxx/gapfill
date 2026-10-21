// F-M19 Freeze & Scrub controls (docs/dev/12 section 7): a SCRUB switch, the
// window position and its width, registered in Slot::PerformanceStrip. The slot
// is empty until UI-0 (WP11) reserves its rectangle in PluginEditor::resized.

#include "../Knob.h"
#include "../HelpRegistry.h"
#include "../SlotHost.h"
#include "../../PluginProcessor.h"

namespace gapfill
{

namespace
{
    class ScrubStrip : public juce::Component
    {
    public:
        explicit ScrubStrip (GapFillAudioProcessor& proc)
            : toggle (proc.getValueTreeState(), "scrubOn", "SCRUB",
                      "Freeze the last second of sound and loop a short window of it. Move Position to scan through it. "
                      "Switching it off fades out; the sound is a snapshot taken when you switch it on."),
              position (proc.getValueTreeState(), "scrubPos", "Position",
                        "Where in the frozen second the looping window sits. Automatable and MIDI-learnable.",
                        colours::secondary),
              width (proc.getValueTreeState(), "scrubWidth", "Width",
                     "Length of the looping window.", colours::secondary)
        {
            setTitle ("Freeze and scrub");
            addAndMakeVisible (toggle);
            addAndMakeVisible (position);
            addAndMakeVisible (width);
        }

        void resized() override
        {
            auto r = getLocalBounds();
            toggle.setBounds (r.removeFromLeft (juce::jmin (96, r.getWidth() / 3)).withHeight (28));
            r.removeFromLeft (8);
            position.setBounds (r.removeFromLeft (juce::jmax (60, r.getWidth() / 2 - 4)));
            r.removeFromLeft (8);
            width.setBounds (r);
        }

    private:
        GapToggle toggle;
        GapKnob position, width;
    };

    const SlotRegistrar scrubStrip ({ Slot::PerformanceStrip, 400,
        [] (GapFillAudioProcessor& proc) -> std::unique_ptr<juce::Component>
        {
            return std::make_unique<ScrubStrip> (proc);
        } });

    const HelpRegistrar helpScrub (4070, "Freeze and Scrub", [] (HelpWriter& w)
    {
        w.body ("SCRUB freezes a snapshot of the last second of sound (while Capture is on, the captured sound) and loops a short window of it "
                "with smooth loop points. Position moves the window through the snapshot: it glides rather than jumping, and its speed is limited "
                "so scanning stays clean. Position can be automated or driven from a controller.");
        w.body ("Switching SCRUB off fades out over about 8 ms. Panic (and a transport stop) also fades it out; switch SCRUB off and on again to restart it. "
                "The scrub sound follows the same protection of hits as fills.");
    });
}

} // namespace gapfill
