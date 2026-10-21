// F-X01 Harmonic granular controls: Amount, Voices, Motion, Tone and a pitch readout.
// Registered in Slot::PerformanceTab; Advanced view only (view rule "performance.musical").

#include "../Knob.h"
#include "../HelpRegistry.h"
#include "../SlotHost.h"
#include "MusicalStrip.h"
#include "../../PluginProcessor.h"

namespace gapfill
{

namespace
{
    class HarmonicStrip : public juce::Component,
                          private juce::Timer
    {
    public:
        explicit HarmonicStrip (GapFillAudioProcessor& p)
            : proc (p),
              amount (p.getValueTreeState(), "hgAmount", "Harmonic", "How often a Granular fill rebuilds itself as stacked harmonies of the pitch heard in the source. 0 = off.", colours::secondary),
              voices (p.getValueTreeState(), "hgVoices", "Voices", "How many harmony layers play: unison, octave, fifth, third, octave down, two octaves up.", colours::secondary),
              motion (p.getValueTreeState(), "hgMotion", "Motion", "How much the grains drift through the source and detune.", colours::secondary),
              tone (p.getValueTreeState(), "hgTone", "Tone", "Darker to brighter harmonic layers.", colours::secondary)
        {
            setTitle ("Harmonic granular");

            for (auto* c : std::initializer_list<juce::Component*> { &amount, &voices, &motion, &tone, &status })
                addAndMakeVisible (c);

            status.setFont (fonts::label (11.0f));
            status.setColour (juce::Label::textColourId, colours::textMuted);
            setSize (musical::kStripWidth, 90);
            applyView();
            refresh();
            startTimerHz (4);
        }

        ~HarmonicStrip() override { stopTimer(); }

        void resized() override
        {
            auto r = getLocalBounds();
            const int knob = juce::jmax (52, r.getWidth() / 7);

            for (auto* c : std::initializer_list<juce::Component*> { &amount, &voices, &motion, &tone })
            {
                c->setBounds (r.removeFromLeft (knob));
                r.removeFromLeft (6);
            }

            status.setBounds (r);
        }

    private:
        void applyView() { musical::fit (*this, musical::advancedView (proc), 90); }
        void timerCallback() override { applyView(); refresh(); }

        void refresh()
        {
            const auto& m = proc.getFxMeters();
            const int state = m.hgState.load();
            juce::String text = "Harmonic: idle";

            if (state == 1)
                text = "Pitch " + juce::String (m.hgF0.load(), 1) + " Hz";
            else if (state == 2)
                text = "Source not pitched: plain Granular";

            if (status.getText() != text)
            {
                status.setText (text, juce::dontSendNotification);
                status.setTitle (text);
            }
        }

        GapFillAudioProcessor& proc;
        GapKnob amount, voices, motion, tone;
        juce::Label status;
    };

    const SlotRegistrar harmonicStrip ({ Slot::PerformanceTab, 490,
        [] (GapFillAudioProcessor& proc) -> std::unique_ptr<juce::Component>
        {
            return std::make_unique<HarmonicStrip> (proc);
        } });

    const HelpRegistrar helpHarmonic (6090, "Harmonic granular", [] (HelpWriter& w)
    {
        w.body ("Harmonic turns Granular fills into stacked harmonies of the note heard in the source. GapFill finds the pitch of the last moments before the "
                "fill; when the source is clearly pitched the grains are cut a whole number of periods long and replayed at musical intervals. "
                "With drums or noise it falls back to ordinary Granular and the strip says so.");
        w.body ("Voices adds layers in this order: unison, octave, fifth, third, octave down, two octaves up. With Scale Lock on, the third is minor when the scale has one.");
    });
}

} // namespace gapfill
