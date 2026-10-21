// N-USE-a / F-L25 live safety card (docs/dev/12 N-USE-a "fixed high-visibility PANIC button at the left of the
// Performance tab", F-L25 "last good scene" and "wet safety limiter").
//
// Registered first in Slot::PerformanceTab. The footer chip (PanicSlot.cpp) stays the always-visible shortcut; this
// card gives the large (>= 32 px) hit target the spec asks for, a LAST GOOD SCENE recall and the WET SAFETY switch.
// PANIC / RECOVER does exactly what the chip and the MIDI command do (LiveControl::panic()).

#include "../Knob.h"
#include "../HelpRegistry.h"
#include "../LookAndFeel.h"
#include "../SlotHost.h"
#include "../../PluginProcessor.h"

namespace gapfill
{

namespace
{
    constexpr int kButtonHeight = 36;

    class LiveSafetyCard : public juce::Component, private juce::Timer
    {
    public:
        explicit LiveSafetyCard (GapFillAudioProcessor& p)
            : proc (p),
              wetSafety (p.getValueTreeState(), "wetSafety", "WET SAFETY",
                         "A soft limiter on the generated signal only, so a stacked effect can never jump out of the mix. "
                         "Below about -8 dBFS it changes the level by less than 0.1 dB. The dry signal is not touched.",
                         colours::accentGreen)
        {
            setTitle ("Live safety");

            panicButton.setTitle ("Panic: silence GapFill effects now");
            panicButton.setTooltip ("Panic: silence the wet signal within 5 ms and stop every fill (Ctrl/Cmd + .). "
                                    "The dry signal is not touched. Press again to recover.");
            panicButton.setColour (juce::TextButton::buttonColourId, colours::panel);
            panicButton.setColour (juce::TextButton::textColourOffId, colours::clip);
            panicButton.onClick = [this]
            {
                auto& panic = proc.getLiveControl().panic();

                if (panic.isPanicked())
                    panic.requestRecover();
                else
                    panic.requestPanic();

                refresh();
            };
            addAndMakeVisible (panicButton);

            lastGood.setButtonText ("LAST GOOD SCENE");
            lastGood.setTitle ("Recall the last good scene");
            lastGood.setTooltip ("Goes back to the last scene that was recalled without a problem. Never implied by Recover.");
            lastGood.onClick = [this] { proc.getLiveControl().scenes().gotoLastGood(); };
            addAndMakeVisible (lastGood);

            addAndMakeVisible (wetSafety);

            status.setFont (fonts::ui (10.0f));
            status.setColour (juce::Label::textColourId, colours::textMuted);
            addAndMakeVisible (status);

            setSize (400, kButtonHeight + 4 + 16);
            refresh();
            startTimerHz (10);
        }

        ~LiveSafetyCard() override { stopTimer(); }

        void resized() override
        {
            auto r = getLocalBounds();
            status.setBounds (r.removeFromBottom (16));
            r.removeFromBottom (4);
            panicButton.setBounds (r.removeFromLeft (juce::jmax (150, r.getWidth() / 3)));
            r.removeFromLeft (8);
            lastGood.setBounds (r.removeFromLeft (juce::jmax (130, r.getWidth() / 2)));
            r.removeFromLeft (8);
            wetSafety.setBounds (r.removeFromLeft (juce::jmax (96, r.getWidth())).withHeight (kButtonHeight));
        }

        juce::TextButton& getPanicButton() noexcept { return panicButton; }

    private:
        void timerCallback() override { refresh(); }

        void refresh()
        {
            const bool panicked = proc.getLiveControl().panic().isPanicked();

            if (panicked != shownPanicked || panicButton.getButtonText().isEmpty())
            {
                shownPanicked = panicked;
                panicButton.setButtonText (panicked ? "RECOVER (wet silenced)" : "PANIC  Ctrl+.");
                panicButton.setColour (juce::TextButton::buttonColourId, panicked ? colours::clip : colours::panel);
                panicButton.setColour (juce::TextButton::textColourOffId, panicked ? colours::text : colours::clip);
                status.setText (panicked ? "Panic is active: only the dry signal is playing. Press Recover to bring the effects back."
                                         : "Panic silences only the generated (wet) signal. Dry audio is never touched.",
                                juce::dontSendNotification);
            }
        }

        GapFillAudioProcessor& proc;
        juce::TextButton panicButton, lastGood;
        GapToggle wetSafety;
        juce::Label status;
        bool shownPanicked = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LiveSafetyCard)
    };

    const SlotRegistrar liveSafety ({ Slot::PerformanceTab, 50,
        [] (GapFillAudioProcessor& proc) -> std::unique_ptr<juce::Component>
        {
            return std::make_unique<LiveSafetyCard> (proc);
        } });

    const HelpRegistrar helpSafety (6205, "Live safety", [] (HelpWriter& w)
    {
        w.body ("The large PANIC button (also in the footer, and on Ctrl/Cmd + .) silences only GapFill's generated signal within 5 milliseconds. "
                "It turns into RECOVER while Panic is active.");
        w.body ("LAST GOOD SCENE returns to the last scene that was recalled without a problem; Recover never recalls a scene by itself. "
                "WET SAFETY puts a soft limiter on the generated signal only, so stacked effects cannot jump out of the mix; "
                "below about -8 dBFS it changes the level by less than 0.1 dB. It is performance state: Random, Surprise and Undo never touch it.");
    });
}

} // namespace gapfill
