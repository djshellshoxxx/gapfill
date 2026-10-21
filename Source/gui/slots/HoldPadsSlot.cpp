// F-M10 / F-S07 / F-P02 / F-L25 performance pads (docs/dev/12 section 7, docs/dev/11): four hold pads
// (Stutter, Freeze, Reverse, Tape stop), Storm, Fill Now and Roulette, plus the hold options.
//
// Registered in Slot::PerformanceTab (Stack: this component keeps its own height). A pad press posts the
// same Command a MIDI note would, through GapFillAudioProcessor::postCommand: velocity 127 on mouse down,
// 0 on mouse up for hold pads (the HoldController turns that into momentary or latch behaviour).

#include "../Knob.h"
#include "../HelpRegistry.h"
#include "../SlotHost.h"
#include "../../PluginProcessor.h"

namespace gapfill
{

namespace
{
    class Pad : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        Pad (GapFillAudioProcessor& p, CommandType t, juce::String label, juce::String tip, bool hold, int effectBit)
            : proc (p), type (t), text (std::move (label)), isHold (hold), bit (effectBit)
        {
            setTitle (text);
            setDescription (tip);
            setTooltip (tip);
            setWantsKeyboardFocus (true);
        }

        void refresh()
        {
            const bool now = bit >= 0 && (proc.getLiveControl().getHoldEngagedMask() & (1u << bit)) != 0;
            if (now != lit)
            {
                lit = now;
                repaint();
            }
        }

        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat().reduced (1.0f);
            g.setColour (lit ? colours::primary : (down ? colours::edge : colours::panel));
            g.fillRoundedRectangle (r, metrics::controlRadius);
            g.setColour (hasKeyboardFocus (false) ? colours::text : colours::edge);
            g.drawRoundedRectangle (r, metrics::controlRadius, 1.0f);
            g.setColour (lit ? colours::background : colours::text);
            g.setFont (fonts::label (11.0f));
            g.drawText (text, getLocalBounds(), juce::Justification::centred, false);
        }

        void mouseDown (const juce::MouseEvent&) override  { down = true; send (isHold ? 127 : 1); repaint(); }
        void mouseUp (const juce::MouseEvent&) override    { down = false; if (isHold) send (0); repaint(); }

        bool keyPressed (const juce::KeyPress& k) override
        {
            if (k == juce::KeyPress::spaceKey || k == juce::KeyPress::returnKey)
            {
                send (isHold ? 127 : 1);
                if (isHold)
                    send (0);       // keyboard activation is a tap: latch mode toggles, momentary pulses
                return true;
            }
            return false;
        }

    private:
        void send (int velocity)
        {
            Command c;
            c.type = type;
            c.velocity = (std::uint8_t) velocity;
            c.issuedMs = juce::Time::getMillisecondCounter();
            proc.postCommand (c);
        }

        GapFillAudioProcessor& proc;
        CommandType type;
        juce::String text;
        bool isHold, lit = false, down = false;
        int bit;
    };

    class HoldPads : public juce::Component, private juce::Timer
    {
    public:
        explicit HoldPads (GapFillAudioProcessor& p)
            : proc (p),
              stutter (p, CommandType::MomentaryStutter, "STUTTER", "Hold a repeating stutter of the last moment of sound while pressed (Latch mode: toggles).", true, 0),
              freeze (p, CommandType::MomentaryFreeze, "FREEZE", "Hold a frozen wash of the last moment of sound.", true, 1),
              reverse (p, CommandType::MomentaryReverse, "REVERSE", "Play the last moment of sound backwards while pressed.", true, 2),
              tape (p, CommandType::MomentaryTape, "TAPE STOP", "Slow the last moment of sound to a stop.", true, 3),
              storm (p, CommandType::StutterStorm, "STORM", "Stutter Storm: a rapid accented stutter pattern on the 1/32 grid while pressed.", true, 4),
              fillNow (p, CommandType::FillNow, "FILL NOW", "Starts a fill at the next beat or bar even without a gap (Options set when).", false, -1),
              roulette (p, CommandType::Roulette, "ROULETTE", "Spins the Glitch Roulette: a random, safe change to the fill settings.", false, -1),
              mode (p.getValueTreeState(), "fm10_mode", "Mode", "Momentary holds only while pressed; Latch toggles on each press."),
              level (p.getValueTreeState(), "fm10_level", "Level", "Loudness of the hold sound.", colours::secondary),
              duck (p.getValueTreeState(), "fm10_dryDuck", "Dry duck", "Lowers the dry signal while a hold is active.", colours::secondary),
              protect (p.getValueTreeState(), "fm10_protect", "PROTECT", "Holds do not cover protected hits."),
              timeout (p.getValueTreeState(), "fm10_timeout", "Timeout", "A latched hold releases after this many seconds.", colours::secondary)
        {
            setTitle ("Performance pads");
            for (Pad* c : { &stutter, &freeze, &reverse, &tape, &storm, &fillNow, &roulette })
                addAndMakeVisible (*c);
            addAndMakeVisible (mode);
            addAndMakeVisible (level);
            addAndMakeVisible (duck);
            addAndMakeVisible (protect);
            addAndMakeVisible (timeout);
            setSize (400, 150);
            startTimerHz (10);
        }

        void resized() override
        {
            auto r = getLocalBounds();
            auto row = r.removeFromTop (40);
            Pad* pads[] = { &stutter, &freeze, &reverse, &tape, &storm, &fillNow, &roulette };
            const int w = row.getWidth() / 7;
            for (auto* pad : pads)
                pad->setBounds (row.removeFromLeft (w).reduced (2));

            r.removeFromTop (6);
            auto opts = r;
            const int kw = juce::jmax (48, opts.getWidth() / 5);
            mode.setBounds (opts.removeFromLeft (kw).withHeight (44));
            level.setBounds (opts.removeFromLeft (kw));
            duck.setBounds (opts.removeFromLeft (kw));
            timeout.setBounds (opts.removeFromLeft (kw));
            protect.setBounds (opts.withHeight (28));
        }

    private:
        void timerCallback() override
        {
            for (Pad* c : { &stutter, &freeze, &reverse, &tape, &storm })
                c->refresh();
        }

        GapFillAudioProcessor& proc;
        Pad stutter, freeze, reverse, tape, storm, fillNow, roulette;
        GapCombo mode;
        GapKnob level, duck;
        GapToggle protect;
        GapKnob timeout;
    };

    const SlotRegistrar holdPads ({ Slot::PerformanceTab, 100,
        [] (GapFillAudioProcessor& proc) -> std::unique_ptr<juce::Component>
        {
            return std::make_unique<HoldPads> (proc);
        } });

    const HelpRegistrar helpPads (6210, "Performance pads", [] (HelpWriter& w)
    {
        w.body ("The pads do what a MIDI note bound to the same command does. Stutter, Freeze, Reverse and Tape stop are hold effects: "
                "they play while the pad is held (Momentary) or toggle on each press (Latch). Storm is a short accented stutter burst. "
                "Fill Now starts a fill on the next beat or bar; Roulette makes a random, safe change to the settings.");
        w.body ("Level sets the hold loudness, Dry duck lowers the dry signal while a hold plays, PROTECT keeps holds off protected hits, "
                "and Timeout releases a latched hold by itself. Panic releases every hold.");
    });
}

} // namespace gapfill
