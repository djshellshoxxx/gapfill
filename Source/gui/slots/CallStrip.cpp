// F-L10 Call and response controls: mode, strength, call and response lengths and a
// text status chip ("CALL 2/3", "RESPONSE 1/2", "No response: motif unclear (48 %)").
// Registered in Slot::PerformanceStrip; the slot stays empty until UI-0 reserves it.

#include "../Knob.h"
#include "../HelpRegistry.h"
#include "../SlotHost.h"
#include "../../PluginProcessor.h"
#include "../../features/callresponse/Responder.h"

namespace gapfill
{

namespace
{
    juce::String statusText (const callresponse::ResponderStatus& s)
    {
        using callresponse::ResponderState;

        if (s.off)
            return "Call and response: off";

        switch (s.state)
        {
            case ResponderState::Collecting:  return "CALL " + juce::String (s.k) + "/" + juce::String (s.n);
            case ResponderState::Responding:  return "RESPONSE " + juce::String (s.k) + "/" + juce::String (s.n);
            case ResponderState::Unclear:     return "No response: motif unclear (" + juce::String (s.confidencePercent) + " %)";
        }

        return {};
    }

    class CallStrip : public juce::Component,
                      private juce::Timer
    {
    public:
        explicit CallStrip (GapFillAudioProcessor& p)
            : proc (p),
              mode (p.getValueTreeState(), "fl10_mode", "Call mode",
                    "GapFill listens to its last few fills and answers with a complementary (or similar) character. It only nudges choices."),
              strength (p.getValueTreeState(), "fl10_strength", "Strength",
                        "How strongly the response is steered.", colours::secondary),
              calls (p.getValueTreeState(), "fl10_calls", "Calls", "How many fills to listen to before answering.", colours::secondary),
              replies (p.getValueTreeState(), "fl10_replies", "Replies", "How many fills answer.", colours::secondary)
        {
            setTitle ("Call and response");

            for (auto* c : std::initializer_list<juce::Component*> { &mode, &strength, &calls, &replies, &chip })
                addAndMakeVisible (c);

            chip.setJustificationType (juce::Justification::centredLeft);
            chip.setFont (fonts::label (11.0f));
            chip.setColour (juce::Label::textColourId, colours::textMuted);
            refresh();
            startTimerHz (4);
        }

        ~CallStrip() override { stopTimer(); }

        void resized() override
        {
            auto r = getLocalBounds();
            mode.setBounds (r.removeFromLeft (juce::jmax (90, r.getWidth() / 5)));
            r.removeFromLeft (8);
            strength.setBounds (r.removeFromLeft (juce::jmax (56, r.getWidth() / 7)));
            r.removeFromLeft (8);
            calls.setBounds (r.removeFromLeft (juce::jmax (56, r.getWidth() / 7)));
            r.removeFromLeft (8);
            replies.setBounds (r.removeFromLeft (juce::jmax (56, r.getWidth() / 7)));
            r.removeFromLeft (8);
            chip.setBounds (r);
        }

    private:
        void timerCallback() override { refresh(); }

        void refresh()
        {
            const auto s = callresponse::unpackStatus (proc.getFillEngine().getResponder().statusWord());
            const auto text = statusText (s);

            if (chip.getText() != text)
            {
                chip.setText (text, juce::dontSendNotification);
                chip.setTitle ("Call and response status: " + text);
            }
        }

        GapFillAudioProcessor& proc;
        GapCombo mode;
        GapKnob strength, calls, replies;
        juce::Label chip;
    };

    const SlotRegistrar callStrip ({ Slot::PerformanceStrip, 250,
        [] (GapFillAudioProcessor& proc) -> std::unique_ptr<juce::Component>
        {
            return std::make_unique<CallStrip> (proc);
        } });

    const HelpRegistrar helpCall (6050, "Call and response", [] (HelpWriter& w)
    {
        w.body ("Call and response watches GapFill's own last few fills (the call), summarises them as a motif, and shapes the next few fills "
                "(the response). Complement answers with a different kind of fill, Echo with a similar one. It only nudges the odds and lengths: "
                "it never creates or removes fills, and it never changes what counts as a gap.");
        w.body ("A forced Fill Type, Fill Lock and More Like That take precedence and pause it. Response fills are marked \"Response\" in History, "
                "so you can lock one. When the last fills do not form a clear motif, the chip says \"No response\" and fills stay ordinary.");
    });
}

} // namespace gapfill
