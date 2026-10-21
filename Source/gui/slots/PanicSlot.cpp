// N-USE-a: compact Panic / Recover chip pinned in the footer of every tab
// (docs/dev/12 N-USE-a, docs/dev/11 "reachable without a popup").
//
// Registered in Slot::FooterStatus. The footer strip is shared with the version
// marker, so the chip only claims (and only hit-tests) its own left-hand
// rectangle. Ctrl/Cmd + '.' triggers the same action while the editor has focus.

#include "../LookAndFeel.h"
#include "../SlotHost.h"
#include "../HelpRegistry.h"
#include "../../PluginProcessor.h"

namespace gapfill
{

namespace
{
    constexpr int kChipWidth = 150;

    class PanicChip : public juce::Component,
                      public juce::SettableTooltipClient,
                      private juce::Timer,
                      private juce::KeyListener
    {
    public:
        explicit PanicChip (GapFillAudioProcessor& p) : proc (p)
        {
            setTitle ("Panic: silence GapFill effects now");
            setDescription ("Silences the wet signal within 5 milliseconds. The dry signal is not affected. "
                            "Press again, or use Recover, to bring the effects back.");
            setTooltip ("Panic: silence the wet signal now (Ctrl/Cmd + .). Dry audio is untouched.");
            setWantsKeyboardFocus (false);
            startTimerHz (10);
        }

        ~PanicChip() override
        {
            if (auto* top = getTopLevelComponent(); top != nullptr)
                top->removeKeyListener (this);
        }

        bool hitTest (int x, int y) override
        {
            return x >= 0 && x < kChipWidth && y >= 0 && y < getHeight();
        }

        void paint (juce::Graphics& g) override
        {
            const auto area = getLocalBounds().removeFromLeft (kChipWidth).toFloat().reduced (0.5f);
            const bool panicked = proc.getLiveControl().panic().isPanicked();

            g.setColour (panicked ? colours::clip : colours::panel);
            g.fillRoundedRectangle (area, metrics::controlRadius);
            g.setColour (panicked ? colours::text : colours::clip);
            g.drawRoundedRectangle (area, metrics::controlRadius, 1.0f);
            g.setFont (fonts::label (10.0f));
            g.drawText (panicked ? "RECOVER (wet silenced)" : "PANIC  Ctrl+.", area.toNearestInt(),
                        juce::Justification::centred, false);
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (e.mouseWasClicked() && hitTest (e.x, e.y))
                trigger();
        }

        void parentHierarchyChanged() override
        {
            if (auto* top = getTopLevelComponent(); top != nullptr && top != this)
            {
                top->removeKeyListener (this);
                top->addKeyListener (this);
            }
        }

    private:
        void trigger()
        {
            auto& panic = proc.getLiveControl().panic();

            if (panic.isPanicked())
                panic.requestRecover();
            else
                panic.requestPanic();

            repaint();
        }

        bool keyPressed (const juce::KeyPress& key, juce::Component*) override
        {
            if (key == juce::KeyPress ('.', juce::ModifierKeys::commandModifier, 0))
            {
                trigger();
                return true;
            }

            return false;
        }

        void timerCallback() override
        {
            const bool panicked = proc.getLiveControl().panic().isPanicked();

            if (panicked != lastShown)
            {
                lastShown = panicked;
                setTitle (panicked ? "Recover: bring GapFill effects back" : "Panic: silence GapFill effects now");
                repaint();
            }
        }

        GapFillAudioProcessor& proc;
        bool lastShown = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PanicChip)
    };

    const SlotRegistrar panicChip ({ Slot::FooterStatus, 90,
        [] (GapFillAudioProcessor& proc) -> std::unique_ptr<juce::Component>
        {
            return std::make_unique<PanicChip> (proc);
        } });

    const HelpRegistrar panicHelp (6200, "Panic and Recover",
        "Panic silences only GapFill's wet (generated) signal within 5 milliseconds and stops every running fill. "
        "Your dry signal, the bypass state, the latency and every parameter value stay exactly as they were.\n"
        "Press the PANIC chip in the footer, Ctrl/Cmd + . with the editor focused, or a MIDI note/CC bound to Panic. "
        "Panic is never delayed by quantisation, scene changes, smoothing or automation.\n"
        "Recover brings the wet signal back over 20 milliseconds; fills resume at the next natural gap. "
        "By default pressing Panic again also recovers. Options can switch that off or recover automatically after 1, 2 or 4 seconds.\n"
        "Panic is never saved in a project: a project always opens un-panicked.");
}

} // namespace gapfill
