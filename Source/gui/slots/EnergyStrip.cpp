// F-L22 Musical energy tracking controls: Follow amount, what it steers, and a
// text badge "Energy: High" (text, not colour only; shown while Follow > 0).
// Registered in Slot::PerformanceStrip; the slot stays empty until UI-0 reserves it.

#include "../Knob.h"
#include "../HelpRegistry.h"
#include "../SlotHost.h"
#include "../../PluginProcessor.h"

namespace gapfill
{

namespace
{
    class EnergyStrip : public juce::Component,
                        private juce::Timer
    {
    public:
        explicit EnergyStrip (GapFillAudioProcessor& p)
            : proc (p),
              adapt (p.getValueTreeState(), "energyAdapt", "Energy follow",
                     "Let the density and intensity of fills follow how busy the music is. Sparse passages get more fills; busy passages fewer but bolder. 0 = off.",
                     colours::secondary),
              target (p.getValueTreeState(), "energyTarget", "Follows",
                      "Which setting the energy follow steers: Density, Intensity or Both.")
        {
            setTitle ("Energy follow");
            addAndMakeVisible (adapt);
            addAndMakeVisible (target);
            badge.setJustificationType (juce::Justification::centredLeft);
            badge.setFont (fonts::label (11.0f));
            badge.setColour (juce::Label::textColourId, colours::textMuted);
            addAndMakeVisible (badge);
            refresh();
            startTimerHz (4);
        }

        ~EnergyStrip() override { stopTimer(); }

        void resized() override
        {
            auto r = getLocalBounds();
            adapt.setBounds (r.removeFromLeft (juce::jmax (56, r.getWidth() / 4)));
            r.removeFromLeft (8);
            target.setBounds (r.removeFromLeft (juce::jmax (80, r.getWidth() / 3)));
            r.removeFromLeft (8);
            badge.setBounds (r);
        }

    private:
        void timerCallback() override { refresh(); }

        void refresh()
        {
            const auto* a = proc.getValueTreeState().getRawParameterValue ("energyAdapt");
            const bool on = a != nullptr && a->load() > 0.0f;
            badge.setVisible (on);

            if (! on)
                return;

            static const char* names[] = { "Low", "Mid", "High" };
            const auto lv = proc.getContextState().energyLevelForDisplay();
            const juce::String text = lv >= 0 && lv <= 2 ? juce::String ("Energy: ") + names[lv] : juce::String ("Energy: learning...");

            if (badge.getText() != text)
                badge.setText (text, juce::dontSendNotification);
        }

        GapFillAudioProcessor& proc;
        GapKnob adapt;
        GapCombo target;
        juce::Label badge;
    };

    const SlotRegistrar energyStrip ({ Slot::PerformanceStrip, 200,
        [] (GapFillAudioProcessor& proc) -> std::unique_ptr<juce::Component>
        {
            return std::make_unique<EnergyStrip> (proc);
        } });

    const HelpRegistrar helpEnergy (6020, "Energy follow", [] (HelpWriter& w)
    {
        w.body ("Energy follow listens to how busy the music is compared with the last minute (loudness and how many hits per second) and "
                "shows it as Low, Mid or High. With Follow above 0, sparse passages get a higher fill density and softer fills, busy passages a "
                "lower density and bolder fills. Follows chooses whether density, intensity or both are steered.");
        w.body ("It adapts to what has played: it needs about ten seconds to learn the range after you switch it on, changes class slowly on purpose, "
                "and relearns after Follow is set to 0. It is an estimate; a loud but sparse passage can read as busy.");
    });
}

} // namespace gapfill
