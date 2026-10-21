// U-3 Options rows: "Audition trigger" note and channel, with LEARN NOTE / LEARN CC (docs/dev/05 U-3). The
// same settings are in the AUDITION button's right-click menu; both edit audition::MidiTrigger, which stores
// them in <Perf>. Registered in Slot::OptionsExtra (a Stack slot inside the scrolling Options area).

#include "MidiTrigger.h"
#include "../../PluginProcessor.h"
#include "../../gui/LookAndFeel.h"
#include "../../gui/SlotHost.h"

namespace gapfill::audition
{

namespace
{
    class TriggerOptions : public juce::Component, private juce::Timer
    {
    public:
        explicit TriggerOptions (GapFillAudioProcessor& p) : proc (p)
        {
            setTitle ("Audition trigger");

            label.setText ("Audition trigger (MIDI)", juce::dontSendNotification);
            label.setFont (fonts::label (10.0f));
            label.setColour (juce::Label::textColourId, colours::textMuted);
            addAndMakeVisible (label);

            note.addItem ("Note: off", 1);
            for (int n = 0; n < 128; ++n)
                note.addItem (juce::MidiMessage::getMidiNoteName (n, true, true, 3) + " (" + juce::String (n) + ")", n + 2);
            note.setTitle ("Audition trigger note");
            note.setTooltip ("A MIDI note that plays a Fill Audition, so a pad or key can preview a fill. Off by default.");
            note.onChange = [this] { if (! loading) trig().setNote (note.getSelectedId() - 2); };
            addAndMakeVisible (note);

            channel.addItem ("Any channel", 1);
            for (int c = 1; c <= 16; ++c)
                channel.addItem ("Channel " + juce::String (c), c + 1);
            channel.setTitle ("Audition trigger channel");
            channel.setTooltip ("The MIDI channel the trigger note listens on.");
            channel.onChange = [this] { if (! loading) trig().setChannel (channel.getSelectedId() - 1); };
            addAndMakeVisible (channel);

            learnNote.setButtonText ("LEARN NOTE");
            learnNote.setTooltip ("Press, then play the note you want to use as the audition trigger (10 seconds).");
            learnNote.onClick = [this]
            {
                trig().getLearn() == MidiTrigger::Learn::Note ? trig().stopLearn() : trig().startLearn (MidiTrigger::Learn::Note);
                refresh();
            };
            learnCC.setButtonText ("LEARN CC");
            learnCC.setTooltip ("Press, then move the knob or press the button you want to use as the audition trigger (10 seconds).");
            learnCC.onClick = [this]
            {
                trig().getLearn() == MidiTrigger::Learn::CC ? trig().stopLearn() : trig().startLearn (MidiTrigger::Learn::CC);
                refresh();
            };
            clearCC.setButtonText ("CLEAR CC");
            clearCC.setTooltip ("Forget the learned audition CC.");
            clearCC.onClick = [this] { trig().setCC (-1); refresh(); };

            for (auto* b : { &learnNote, &learnCC, &clearCC })
                addAndMakeVisible (*b);

            status.setFont (fonts::ui (10.0f));
            status.setColour (juce::Label::textColourId, colours::textMuted);
            addAndMakeVisible (status);

            setSize (400, 82);
            refresh();
            startTimerHz (4);
        }

        ~TriggerOptions() override { stopTimer(); }

        void resized() override
        {
            auto r = getLocalBounds();
            label.setBounds (r.removeFromTop (16));
            auto a = r.removeFromTop (24);
            note.setBounds (a.removeFromLeft (a.getWidth() / 2).reduced (0, 1));
            a.removeFromLeft (6);
            channel.setBounds (a.reduced (0, 1));
            r.removeFromTop (2);
            auto b = r.removeFromTop (24);
            learnNote.setBounds (b.removeFromLeft (92).reduced (0, 1));
            b.removeFromLeft (4);
            learnCC.setBounds (b.removeFromLeft (80).reduced (0, 1));
            b.removeFromLeft (4);
            clearCC.setBounds (b.removeFromLeft (80).reduced (0, 1));
            b.removeFromLeft (6);
            status.setBounds (b);
        }

    private:
        MidiTrigger& trig() { return proc.getRecipes().trigger; }

        void timerCallback() override { refresh(); }

        void refresh()
        {
            const juce::ScopedValueSetter<bool> guard (loading, true);
            auto& t = trig();

            if (note.getSelectedId() != t.getNote() + 2)
                note.setSelectedId (t.getNote() + 2, juce::dontSendNotification);

            if (channel.getSelectedId() != t.getChannel() + 1)
                channel.setSelectedId (t.getChannel() + 1, juce::dontSendNotification);

            clearCC.setEnabled (t.getCC() >= 0);
            const auto learn = t.getLearn();
            learnNote.setToggleState (learn == MidiTrigger::Learn::Note, juce::dontSendNotification);
            learnCC.setToggleState (learn == MidiTrigger::Learn::CC, juce::dontSendNotification);

            const auto text = learn != MidiTrigger::Learn::None ? juce::String ("Waiting for MIDI...")
                                                                 : (t.getCC() >= 0 ? "CC " + juce::String (t.getCC()) : juce::String ("No CC"));
            if (status.getText() != text)
                status.setText (text, juce::dontSendNotification);
        }

        GapFillAudioProcessor& proc;
        juce::Label label, status;
        juce::ComboBox note, channel;
        juce::TextButton learnNote, learnCC, clearCC;
        bool loading = false;
    };

    const SlotRegistrar triggerOptions ({ Slot::OptionsExtra, 290,
        [] (GapFillAudioProcessor& proc) -> std::unique_ptr<juce::Component>
        {
            return std::make_unique<TriggerOptions> (proc);
        } });
}

} // namespace gapfill::audition
