// F-L09 Groove inheritance controls: amount, grid, hold, Learn / Reset and a text
// readout ("Groove: swing 62 %, confidence 74 %"). Registered in
// Slot::PerformanceStrip; the slot stays empty until UI-0 reserves its rectangle.

#include "../Knob.h"
#include "../HelpRegistry.h"
#include "../SlotHost.h"
#include "MusicalStrip.h"
#include "../../PluginProcessor.h"

namespace gapfill
{

namespace
{
    class GrooveStrip : public juce::Component,
                        private juce::Timer
    {
    public:
        explicit GrooveStrip (GapFillAudioProcessor& p)
            : proc (p),
              amount (p.getValueTreeState(), "fl09_amount", "Groove",
                      "Make the repeats of tempo-synced Stutter and Delay fills follow the swing of your music. Needs host tempo and Sync on. 0 = off.",
                      colours::secondary),
              grid (p.getValueTreeState(), "fl09_grid", "Grid", "The note grid the swing is measured against: 1/8 or 1/16."),
              hold (p.getValueTreeState(), "fl09_hold", "HOLD", "Freeze the learned groove so it no longer changes."),
              learn ("LEARN"),
              reset ("RESET")
        {
            setTitle ("Groove");
            learn.setTooltip ("Clear the groove and measure the next 8 bars. The groove is applied when the measurement ends, then held.");
            reset.setTooltip ("Forget the learned groove and start measuring again.");
            learn.onClick = [this] { Command c; c.type = CommandType::GrooveLearn; proc.postCommand (c); };
            reset.onClick = [this] { Command c; c.type = CommandType::GrooveReset; proc.postCommand (c); };

            for (auto* c : std::initializer_list<juce::Component*> { &amount, &grid, &hold, &learn, &reset, &readout })
                addAndMakeVisible (c);

            readout.setJustificationType (juce::Justification::centredLeft);
            readout.setFont (fonts::label (11.0f));
            readout.setColour (juce::Label::textColourId, colours::textMuted);
            setSize (musical::kStripWidth, 64);
            applyView();
            refresh();
            startTimerHz (4);
        }

        ~GrooveStrip() override { stopTimer(); }

        void resized() override
        {
            auto r = getLocalBounds();
            amount.setBounds (r.removeFromLeft (juce::jmax (56, r.getWidth() / 6)));
            r.removeFromLeft (8);
            grid.setBounds (r.removeFromLeft (juce::jmax (64, r.getWidth() / 7)));
            r.removeFromLeft (8);
            hold.setBounds (r.removeFromLeft (juce::jmax (64, r.getWidth() / 8)).withHeight (28));
            r.removeFromLeft (8);
            learn.setBounds (r.removeFromLeft (juce::jmax (56, r.getWidth() / 8)).withHeight (28));
            r.removeFromLeft (8);
            reset.setBounds (r.removeFromLeft (juce::jmax (56, r.getWidth() / 7)).withHeight (28));
            r.removeFromLeft (8);
            readout.setBounds (r);
        }

    private:
        void applyView() { musical::fit (*this, musical::advancedView (proc), 64); }

        void timerCallback() override { applyView(); refresh(); }

        void refresh()
        {
            auto& gs = proc.getContextState().grooveState();
            const auto* a = proc.getValueTreeState().getRawParameterValue ("fl09_amount");
            const bool on = a != nullptr && a->load() > 0.0f;

            juce::String text;
            groove::GrooveSnapshot snap;
            const bool haveSnap = gs.readSnapshot (snap);

            if (gs.learnBarsRemaining() > 0)
                text = "Groove: learning, " + juce::String (gs.learnBarsRemaining()) + " bars left";
            else if (! on && ! gs.isFrozenByLearn())
                text = "Groove: off";
            else if (gs.isWaitingForTempo())
                text = "Groove: waiting for host tempo";
            else if (haveSnap && snap.valid)
                text = "Groove: swing " + juce::String (juce::roundToInt (snap.swingPercent)) + " %, confidence "
                         + juce::String (juce::roundToInt (snap.confidence * 100.0f)) + " %";
            else
                text = "Groove: unclear (straight)";

            if (readout.getText() != text)
            {
                readout.setText (text, juce::dontSendNotification);
                readout.setTitle (text);
            }

            if (haveSnap)
            {
                juce::String tip = "Offsets in grid steps (+ = late): ";

                for (int i = 0; i < groove::kSlots; ++i)
                    tip << (i > 0 ? ", " : "") << juce::String (snap.offsetSteps[i], 2);

                readout.setTooltip (tip);
            }
        }

        GapFillAudioProcessor& proc;
        GapKnob amount;
        GapCombo grid;
        GapToggle hold;
        juce::TextButton learn, reset;
        juce::Label readout;
    };

    const SlotRegistrar grooveStrip ({ Slot::PerformanceTab, 440,
        [] (GapFillAudioProcessor& proc) -> std::unique_ptr<juce::Component>
        {
            return std::make_unique<GrooveStrip> (proc);
        } });

    const HelpRegistrar helpGroove (6030, "Groove", [] (HelpWriter& w)
    {
        w.body ("Groove listens to where your music's hits fall against the host's beat grid and makes the repeats of Stutter fills and the taps of "
                "Delay fills follow that feel, so a straight stutter over a swung hi-hat swings too. Only the wet repeats move; the start of a fill, "
                "the dry sound and the host timing never change.");
        w.body ("It needs rhythmic input, host tempo and the Sync switch on. Below 35 % confidence it applies nothing and says the groove is unclear. "
                "LEARN measures 8 bars and then holds the result; HOLD freezes whatever has been learned so far; RESET forgets it.");
    });
}

} // namespace gapfill
