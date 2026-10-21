// F-X04 Arrangement awareness controls: ARRANGEMENT switch, Calm / Lift / Settle / Sensitivity
// knobs and a text readout ("Learning" / "Section 3 - high energy - next change in ~4 bars").
// Registered in Slot::PerformanceStrip (hidden until UI-0 reserves its rectangle).

#include "../Knob.h"
#include "../HelpRegistry.h"
#include "../SlotHost.h"
#include "../../PluginProcessor.h"

namespace gapfill
{

namespace
{
    class ArrangementStrip : public juce::Component,
                             private juce::Timer
    {
    public:
        explicit ArrangementStrip (GapFillAudioProcessor& p)
            : proc (p),
              on (p.getValueTreeState(), "fx04_on", "ARRANGEMENT",
                  "Learn the sections of the song (verse, chorus, breakdown) from the audio and let the fills follow them."),
              calm (p.getValueTreeState(), "fx04_calm", "Calm", "Thin the fills out in quiet sections.", colours::secondary),
              lift (p.getValueTreeState(), "fx04_lift", "Lift", "More and longer fills just before an expected section change and during a build.", colours::secondary),
              settle (p.getValueTreeState(), "fx04_settle", "Settle", "Hold fills back right after a new section starts.", colours::secondary),
              sensitivity (p.getValueTreeState(), "fx04_sensitivity", "Sensitivity", "How readily a change in the music counts as a new section.", colours::secondary)
        {
            setTitle ("Arrangement");

            for (auto* c : std::initializer_list<juce::Component*> { &on, &calm, &lift, &settle, &sensitivity, &readout })
                addAndMakeVisible (c);

            readout.setJustificationType (juce::Justification::centredLeft);
            readout.setFont (fonts::label (11.0f));
            readout.setColour (juce::Label::textColourId, colours::textMuted);
            refresh();
            startTimerHz (2);
        }

        ~ArrangementStrip() override { stopTimer(); }

        void resized() override
        {
            auto r = getLocalBounds();
            on.setBounds (r.removeFromLeft (juce::jmax (110, r.getWidth() / 6)).withHeight (28));
            r.removeFromLeft (8);

            for (auto* c : std::initializer_list<juce::Component*> { &calm, &lift, &settle, &sensitivity })
            {
                c->setBounds (r.removeFromLeft (juce::jmax (52, r.getWidth() / 6)));
                r.removeFromLeft (6);
            }

            readout.setBounds (r);
        }

    private:
        void timerCallback() override { refresh(); }

        void refresh()
        {
            const bool enabled = proc.getValueTreeState().getRawParameterValue ("fx04_on")->load() > 0.5f;
            const auto st = proc.getContextState().arrangementState();
            juce::String text;

            if (! enabled)
                text = "Arrangement: off";
            else if (st.learning)
                text = "Learning (" + juce::String (juce::roundToInt ((float) proc.getContextState().arrangementFrames() * 0.5f)) + " s of 8 needed)";
            else
            {
                static const char* names[] = { "low", "mid", "high" };
                const int cls = st.energy01 < 0.33f ? 0 : (st.energy01 < 0.66f ? 1 : 2);
                text = "Section " + juce::String ((int) st.sectionIndex + 1) + " - " + names[cls] + " energy";

                if (st.expectedIn >= 0)
                    text << " - next change in about " << juce::String (juce::roundToInt ((float) st.expectedIn * 0.5f)) << " s";
            }

            if (readout.getText() != text)
            {
                readout.setText (text, juce::dontSendNotification);
                readout.setTitle (text);
            }
        }

        GapFillAudioProcessor& proc;
        GapToggle on;
        GapKnob calm, lift, settle, sensitivity;
        juce::Label readout;
    };

    const SlotRegistrar arrangementStrip ({ Slot::PerformanceStrip, 450,
        [] (GapFillAudioProcessor& proc) -> std::unique_ptr<juce::Component>
        {
            return std::make_unique<ArrangementStrip> (proc);
        } });

    const HelpRegistrar helpArrangement (6080, "Arrangement", [] (HelpWriter& w)
    {
        w.body ("ARRANGEMENT listens to how the music changes (loudness, how busy it is, which frequencies dominate) and finds the sections of the song. "
                "It starts in a Learning state where it does nothing. Calm thins out fills in quiet sections such as breakdowns, Lift makes fills more likely "
                "and longer just before an expected change and during a build, Settle holds fills back right after a section starts so the downbeat lands cleanly.");
        w.body ("It never forces a fill into a protected or ineligible gap and never touches the dry sound. The result depends on what has played, so it relearns after "
                "a seek or loop restart. A single drum bus shows little structure; then it stays neutral.");
    });
}

} // namespace gapfill
