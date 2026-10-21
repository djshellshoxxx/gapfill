// E-1 Fill Audition, editor side (docs/dev/03 E-1, docs/dev/05 U-3, docs/dev/11).
//
// One slot component, no edits to the editor:
//   Slot::CreativeExtra  (order 110)  AUDITION button, next to CAPTURE. Right-click:
//                                     MIDI learn CC, trigger note / channel (U-3).
// The status line ("Preview: <Algorithm>", rejections, learn countdown) is part of the
// shared footer in features/recipes/RecipeFooter.cpp.
//
// The button never pushes a command unless the audio callback has been seen to
// run in the last 400 ms (heartbeat via MeterState::blockCounter), so a stopped
// host cannot hold a trigger.

#include "Audition.h"
#include "Heartbeat.h"
#include "PreviewChannel.h"
#include "../../gui/LookAndFeel.h"
#include "../../gui/SlotHost.h"
#include "../../PluginProcessor.h"

namespace gapfill
{

namespace
{
    constexpr const char* kTooltip =
        "Play one fill right now from the last few seconds of audio, using the current settings. "
        "Needs audio playing through the plugin. Right-click to map a MIDI CC or a trigger note.";

    constexpr int kMenuLearnCC = 1, kMenuClearCC = 2, kMenuLearnNote = 3, kMenuNoteOff = 4;
    constexpr int kMenuNoteBase = 1000, kMenuChannelBase = 2000;

    juce::String noteName (int n)
    {
        return juce::MidiMessage::getMidiNoteName (n, true, true, 3) + " (" + juce::String (n) + ")";
    }

    juce::String channelName (int c)   { return c == 0 ? juce::String ("Any") : juce::String (c); }
}

//==============================================================================
class AuditionButton : public juce::TextButton,
                       private juce::Timer
{
public:
    explicit AuditionButton (GapFillAudioProcessor& p)
        : proc (p)
    {
        setButtonText ("AUDITION");
        setName ("AUDITION");
        setTitle ("Audition");
        setDescription (kTooltip);
        setHelpText (kTooltip);
        setTooltip (kTooltip);
        setWantsKeyboardFocus (false);
        setColour (juce::TextButton::buttonOnColourId, colours::accentGreen);
        setColour (juce::TextButton::textColourOnId, colours::accentGreen);
        onClick = [this] { trigger(); };

        heartbeat.poll (proc.getMeterState());
        startTimerHz (15);
    }

    ~AuditionButton() override { stopTimer(); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            showMenu();
            return;
        }

        juce::TextButton::mouseDown (e);
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        getLookAndFeel().drawButtonBackground (g, *this,
                                               findColour (getToggleState() ? juce::TextButton::buttonOnColourId
                                                                            : juce::TextButton::buttonColourId),
                                               highlighted, down);

        g.setColour (getToggleState() ? colours::accentGreen : colours::textMuted);
        g.setFont (fonts::label (10.0f));
        g.drawFittedText (getButtonText(), getLocalBounds().reduced (2, 0), juce::Justification::centred, 1, 0.5f);
    }

    void trigger()
    {
        heartbeat.poll (proc.getMeterState());

        if (! heartbeat.isRunning())
        {
            proc.getRecipes().postMessage ("Start playback to audition");
            return;
        }

        Command c;
        c.type = CommandType::Preview;
        c.velocity = 100;
        c.issuedMs = juce::jmax<std::uint32_t> (1u, juce::Time::getMillisecondCounter());
        proc.postCommand (c);                 // a full queue simply drops the press
    }

private:
    void timerCallback() override
    {
        heartbeat.poll (proc.getMeterState());

        const bool playing = previewStatusOf ((std::uint32_t) proc.getMeterState().previewStatus.load (std::memory_order_relaxed))
                               == PreviewStatus::Playing;

        if (playing != getToggleState())
            setToggleState (playing, juce::dontSendNotification);
    }

    void showMenu()
    {
        auto& trig = proc.getRecipes().trigger;
        using Learn = audition::MidiTrigger::Learn;

        juce::PopupMenu menu;
        menu.addSectionHeader ("Fill Audition");
        menu.addItem (kMenuLearnCC, trig.getLearn() == Learn::CC ? "Cancel CC learn" : "MIDI Learn CC...");

        if (trig.getCC() >= 0)
            menu.addItem (kMenuClearCC, "Clear CC " + juce::String (trig.getCC()));

        menu.addSeparator();
        menu.addItem (kMenuLearnNote, trig.getLearn() == Learn::Note ? "Cancel note learn" : "Learn trigger note...");

        juce::PopupMenu notes;

        for (int octave = -2; octave <= 8; ++octave)
        {
            juce::PopupMenu oct;

            for (int n = (octave + 2) * 12; n < juce::jmin (128, (octave + 3) * 12); ++n)
                oct.addItem (kMenuNoteBase + n, noteName (n), true, n == trig.getNote());

            if (oct.getNumItems() > 0)
                notes.addSubMenu ("Octave " + juce::String (octave), oct);
        }

        menu.addSubMenu ("Trigger note: " + (trig.getNote() < 0 ? juce::String ("Off") : noteName (trig.getNote())), notes);

        if (trig.getNote() >= 0)
            menu.addItem (kMenuNoteOff, "Trigger note off");

        juce::PopupMenu channels;

        for (int c = 0; c <= 16; ++c)
            channels.addItem (kMenuChannelBase + c, channelName (c), true, c == trig.getChannel());

        menu.addSubMenu ("Channel: " + channelName (trig.getChannel()), channels);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                            [safe = juce::Component::SafePointer<AuditionButton> (this)] (int result)
        {
            if (safe == nullptr || result == 0)
                return;

            auto& t = safe->proc.getRecipes().trigger;

            if (result == kMenuLearnCC)
                t.getLearn() == Learn::CC ? t.stopLearn() : t.startLearn (Learn::CC);
            else if (result == kMenuClearCC)
                t.setCC (-1);
            else if (result == kMenuLearnNote)
                t.getLearn() == Learn::Note ? t.stopLearn() : t.startLearn (Learn::Note);
            else if (result == kMenuNoteOff)
                t.setNote (-1);
            else if (result >= kMenuChannelBase)
                t.setChannel (result - kMenuChannelBase);
            else if (result >= kMenuNoteBase)
                t.setNote (result - kMenuNoteBase);
        });
    }

    GapFillAudioProcessor& proc;
    audition::Heartbeat heartbeat;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AuditionButton)
};

//==============================================================================
namespace
{
    const SlotRegistrar auditionButton ({ Slot::CreativeExtra, 110,
        [] (GapFillAudioProcessor& proc) -> std::unique_ptr<juce::Component>
        {
            return std::make_unique<AuditionButton> (proc);
        } });
}

} // namespace gapfill
