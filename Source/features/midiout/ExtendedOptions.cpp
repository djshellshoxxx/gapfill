#include "MidiOut.h"
#include "MidiScheduler.h"
#include "../../PluginProcessor.h"
#include "../../gui/Knob.h"
#include "../../gui/HelpRegistry.h"
#include "../../gui/SlotHost.h"
#include "../i18n/Strings.h"

namespace gapfill::midiout
{
namespace
{
class EventOptions final : public juce::Component, private juce::Timer
{
public:
    explicit EventOptions (GapFillAudioProcessor& processor)
        : proc (processor),
          master (proc.getValueTreeState(), "fl11_on", "MIDI OUT", "Enable detector event output in place of the legacy gap/fill mode."),
          hit (proc.getValueTreeState(), "fl11_hit_on", "HIT", "Send a short note for each detected onset."),
          gap (proc.getValueTreeState(), "fl11_gap_on", "GAP", "Hold a note during each eligible gap."),
          fill (proc.getValueTreeState(), "fl11_fill_on", "FILL", "Hold a note for each planned fill duration."),
          channel (proc.getValueTreeState(), "fl11_chan", "CHANNEL", "Channel for generated notes and confidence CC.", colours::primary),
          noteHit (proc.getValueTreeState(), "fl11_note_hit", "HIT NOTE", "Hit note number; C3 is MIDI note 60.", colours::primary),
          noteGap (proc.getValueTreeState(), "fl11_note_gap", "GAP NOTE", "Note held during gaps.", colours::primary),
          noteFill (proc.getValueTreeState(), "fl11_note_fill", "FILL NOTE", "Note held for planned fills.", colours::primary),
          length (proc.getValueTreeState(), "fl11_len_ms", "HIT LENGTH", "Hit note duration, from 10 to 500 ms.", colours::primary),
          cc (proc.getValueTreeState(), "fl11_cc", "CONFIDENCE CC", "0 is Off; controller numbers 120 and above are not allowed.", colours::primary),
          velocity (proc.getValueTreeState(), "fl11_vel", "VELOCITY", "Follow detector strength or use fixed velocity 100."),
          timing (proc.getValueTreeState(), "fl11_timing", "HIT TIMING", "Early affects Hit notes only; Gap and Fill stay output-aligned.")
    {
        setTitle ("Detector MIDI output");
        title.setText ("DETECTOR MIDI", juce::dontSendNotification);
        title.setFont (fonts::sectionHeader (11.0f)); title.setColour (juce::Label::textColourId, colours::text);
        activity.setFont (fonts::ui (10.0f)); activity.setColour (juce::Label::textColourId, colours::textMuted);
        activity.setTitle ("MIDI output activity");
        hint.setFont (fonts::ui (10.0f)); hint.setColour (juce::Label::textColourId, colours::textMuted);
        hint.setText (kDeclared ? "Early affects Hit notes only. Route MIDI output in your host; CC 0 is Off."
                               : "This build does not output MIDI. Event controls are unavailable.", juce::dontSendNotification);
        for (auto* label : { &title, &activity, &hint }) addAndMakeVisible (*label);
        for (auto* control : controls()) { addAndMakeVisible (*control); control->setEnabled (kDeclared); }
        refresh(); startTimerHz (4); setSize (400, 336);
    }

    ~EventOptions() override { stopTimer(); }

    void resized() override
    {
        auto area = getLocalBounds().reduced (0, 4);
        auto top = area.removeFromTop (16);
        title.setBounds (top.removeFromLeft (160)); activity.setBounds (top.withTrimmedRight (16));
        area.removeFromTop (8);
        auto toggles = area.removeFromTop (32);
        master.setBounds (toggles.removeFromLeft (112).reduced (0, 4));
        const int width = toggles.getWidth() / 3;
        hit.setBounds (toggles.removeFromLeft (width).reduced (4));
        gap.setBounds (toggles.removeFromLeft (width).reduced (4)); fill.setBounds (toggles.reduced (4));
        channel.setBounds (area.removeFromTop (32));
        noteHit.setBounds (area.removeFromTop (32)); noteGap.setBounds (area.removeFromTop (32));
        noteFill.setBounds (area.removeFromTop (32));
        auto choices = area.removeFromTop (48);
        velocity.setBounds (choices.removeFromLeft (choices.getWidth() / 2).withTrimmedRight (4));
        timing.setBounds (choices.withTrimmedLeft (4));
        length.setBounds (area.removeFromTop (32)); cc.setBounds (area.removeFromTop (32)); hint.setBounds (area);
    }

    void paint (juce::Graphics& graphics) override
    {
        graphics.setColour (active && ! proc.isReducedMotionEnabled() ? colours::primary : colours::textMuted);
        const juce::Rectangle<float> dot ((float) getWidth() - 10.0f, 8.0f, 6.0f, 6.0f);
        if (active && ! proc.isReducedMotionEnabled()) graphics.fillEllipse (dot);
        else graphics.drawEllipse (dot, 1.0f);
    }

private:
    std::array<juce::Component*, 12> controls() noexcept
    { return { &master, &hit, &gap, &fill, &channel, &noteHit, &noteGap, &noteFill, &length, &cc, &velocity, &timing }; }
    void refresh()
    {
        const auto count = proc.getExtendedMidiOut().getSentCount();
        const auto language = i18n::languageGeneration();
        const bool wasActive = active;
        active = count != lastSent;
        if (count != lastSent || language != lastLanguage)
            activity.setText (i18n::format ("MIDI events: {0}", { juce::String (count) }), juce::dontSendNotification);
        lastSent = count; lastLanguage = language;
        if (active || wasActive) repaint();
    }
    void timerCallback() override { refresh(); }

    GapFillAudioProcessor& proc;
    GapToggle master, hit, gap, fill;
    GapBar channel, noteHit, noteGap, noteFill, length, cc;
    GapCombo velocity, timing;
    juce::Label title, activity, hint;
    std::uint32_t lastSent = 0;
    std::uint64_t lastLanguage = std::numeric_limits<std::uint64_t>::max();
    bool active = false;
};

const SlotRegistrar eventOptions ({ Slot::OptionsExtra, 282,
    [] (GapFillAudioProcessor& proc) -> std::unique_ptr<juce::Component> { return std::make_unique<EventOptions> (proc); } });
const HelpRegistrar eventMidiHelp (1200, "Detector MIDI output", [] (HelpWriter& writer)
{
    writer.body ("Options > Detector MIDI enables Hit notes, held Gap and Fill notes, and a confidence CC. "
                 "MIDI OUT replaces the legacy gap/fill output while enabled. Output starts off. "
                 "Note names use C3 = 60. Detection-early moves Hit notes only; Gap and Fill stay aligned with output audio. "
                 "Incoming notes and learn CCs are never echoed while event output is active. "
                 "Route this effect's MIDI output to an instrument track in your host; support differs by host and format. "
                 "This build may have MIDI output disabled until real-host routing is validated. "
                 "On stop, panic, bypass or disable, sounding notes are closed; the activity counter also reports those note-offs.");
});
}
}
