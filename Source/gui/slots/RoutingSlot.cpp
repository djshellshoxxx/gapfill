// WP10 routing and system settings (docs/dev/11 placement: Performance tab, folded in one card):
// F-M17 wet-only output, F-L12 Wet bus status, F-L13 sidechain gate / snap, F-L11 MIDI output,
// F-X06 co-op group, F-D03 quality mode. Registered in Slot::PerformanceTab (Stack; fixed height).
//
// Every control is a plain parameter control (GapToggle / GapCombo / GapBar), so automation, MIDI learn,
// tooltips from the info table and accessibility names come for free. The status labels are read-only
// text (never colour alone) refreshed at 4 Hz from audio-thread atomics.

#include "../Knob.h"
#include "../HelpRegistry.h"
#include "../LookAndFeel.h"
#include "../SlotHost.h"
#include "../../PluginProcessor.h"
#include "../../features/wetbus/WetBus.h"

namespace gapfill
{

namespace
{
    class RoutingCard : public juce::Component, private juce::Timer
    {
    public:
        explicit RoutingCard (GapFillAudioProcessor& p)
            : proc (p),
              wetOnly   (p.getValueTreeState(), "wetSolo",     "WET ONLY", "Main output carries only what GapFill generates; Dry and Wet are ignored."),
              scGate    (p.getValueTreeState(), "scGate",      "SIDECHAIN GATE", "Fill only while the sidechain is quiet or only while it is active."),
              scThresh  (p.getValueTreeState(), "scThresh",    "SC THRESHOLD", "Level that separates a quiet sidechain from an active one.", colours::primary),
              scSnap    (p.getValueTreeState(), "scSnap",      "SC SNAP", "Delay a fill start to a nearby sidechain hit.", colours::primary),
              midiMode  (p.getValueTreeState(), "midiOutMode", "MIDI OUT", "Send MIDI notes for gaps, fills or both."),
              midiNote  (p.getValueTreeState(), "midiOutNote", "NOTE", "Note for gaps; fills use the next note up.", colours::primary),
              midiChan  (p.getValueTreeState(), "midiOutChan", "CHANNEL", "MIDI channel of the notes.", colours::primary),
              coopGroup (p.getValueTreeState(), "coopGroup",   "CO-OP GROUP", "GapFill instances in the same group take turns."),
              coopMode  (p.getValueTreeState(), "coopMode",    "CO-OP MODE", "Avoid overlap, alternate bars, or follow other fills."),
              quality   (p.getValueTreeState(), "qualityMode", "QUALITY", "Full is the reference sound; Balanced and Lite use less CPU; Auto steps down under load.")
        {
            setTitle ("Routing and system");

            title.setText ("ROUTING AND SYSTEM", juce::dontSendNotification);
            title.setFont (fonts::sectionHeader (11.0f));
            title.setColour (juce::Label::textColourId, colours::text);
            addAndMakeVisible (title);

            for (auto* l : { &wetStatus, &scStatus, &midiStatus, &coopStatus, &qualityStatus })
            {
                l->setFont (fonts::ui (10.0f));
                l->setColour (juce::Label::textColourId, colours::textMuted);
                l->setMinimumHorizontalScale (0.8f);
                addAndMakeVisible (*l);
            }

            for (auto* c : { (juce::Component*) &wetOnly, (juce::Component*) &scGate, (juce::Component*) &scThresh,
                             (juce::Component*) &scSnap, (juce::Component*) &midiMode, (juce::Component*) &midiNote,
                             (juce::Component*) &midiChan, (juce::Component*) &coopGroup, (juce::Component*) &coopMode,
                             (juce::Component*) &quality })
                addAndMakeVisible (*c);

            // MIDI output is a build option: without it the controls explain themselves instead of doing nothing.
            const bool midiAvailable = midiout::kDeclared;
            midiMode.setEnabled (midiAvailable);
            midiNote.setEnabled (midiAvailable);
            midiChan.setEnabled (midiAvailable);

            refresh();
            startTimerHz (4);
            setSize (400, kHeight);
        }

        ~RoutingCard() override { stopTimer(); }

        void resized() override
        {
            auto r = getLocalBounds().reduced (4, 2);
            title.setBounds (r.removeFromTop (18));

            auto row = [&] (std::initializer_list<std::pair<juce::Component*, int>> items)
            {
                auto line = r.removeFromTop (kRow);
                int totalWeight = 0;

                for (auto& it : items)
                    totalWeight += it.second;

                const auto width = line.getWidth();

                for (auto& it : items)
                    it.first->setBounds (line.removeFromLeft (width * it.second / juce::jmax (1, totalWeight)).reduced (2, 0));
            };

            row ({ { &wetOnly, 1 }, { &wetStatus, 2 } });
            row ({ { &scGate, 2 }, { &scThresh, 2 }, { &scSnap, 2 } });
            row ({ { &scStatus, 1 } });
            row ({ { &midiMode, 2 }, { &midiNote, 2 }, { &midiChan, 2 } });
            row ({ { &midiStatus, 1 } });
            row ({ { &coopGroup, 2 }, { &coopMode, 2 }, { &coopStatus, 3 } });
            row ({ { &quality, 2 }, { &qualityStatus, 4 } });
        }

        void paint (juce::Graphics& g) override
        {
            g.setColour (colours::edge);
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 4.0f, 1.0f);
        }

    private:
        static constexpr int kRow = 34;
        static constexpr int kHeight = 18 + 7 * kRow + 12;

        void timerCallback() override   { refresh(); }

        void refresh()
        {
            auto& v = proc.getValueTreeState();

            // Wet bus: declared or not, enabled by the host or not.
            juce::String wet;
            if (! wetbus::isDeclared (juce::PluginHostType::getPluginLoadedAs()))
                wet = "Wet bus: not available in this plug-in format";
            else if (proc.getBusCount (false) > wetbus::kWetBusIndex && proc.getBus (false, wetbus::kWetBusIndex)->isEnabled())
                wet = "Wet bus: enabled by the host (stereo)";
            else
                wet = "Wet bus: off - enable it in your DAW's plug-in output routing";

            if (v.getRawParameterValue ("wetSolo")->load() > 0.5f)
                wet += "  |  WET ONLY: Dry and Wet are bypassed";

            setText (wetStatus, wet);

            const bool scSet = v.getRawParameterValue ("scGate")->load() > 0.5f || v.getRawParameterValue ("scSnap")->load() > 0.0f;
            setText (scStatus, scSet ? (proc.getMeterState().sidechainConnected.load()
                                            ? "Sidechain gate and snap active"
                                            : "Sidechain gate / snap are idle: connect a sidechain input in your DAW")
                                     : juce::String());

            setText (midiStatus, midiout::kDeclared ? juce::String ("Output-aligned notes; some hosts ignore MIDI from effect plug-ins")
                                                    : juce::String ("MIDI output is not available in this build"));

            setText (coopStatus, proc.getCoop().describe());

            char buf[64];
            const int mode = (int) std::lround (v.getRawParameterValue ("qualityMode")->load());
            juce::String q = quality::footerText (mode, proc.getAppliedQualityLevel(), buf, 64);

            if (proc.getQualityController().requestedLevel() != proc.getAppliedQualityLevel())
                q += (q.isEmpty() ? "" : "  |  ") + juce::String ("change waits for a quiet moment");

            setText (qualityStatus, q);
        }

        static void setText (juce::Label& l, const juce::String& t)
        {
            if (l.getText() != t)
                l.setText (t, juce::dontSendNotification);
        }

        GapFillAudioProcessor& proc;
        juce::Label title, wetStatus, scStatus, midiStatus, coopStatus, qualityStatus;
        GapToggle wetOnly;
        GapCombo scGate;
        GapBar scThresh, scSnap;
        GapCombo midiMode;
        GapBar midiNote, midiChan;
        GapCombo coopGroup, coopMode, quality;
    };

    const SlotRegistrar routingCard ({ Slot::PerformanceTab, 400,
        [] (GapFillAudioProcessor& proc) -> std::unique_ptr<juce::Component>
        {
            return std::make_unique<RoutingCard> (proc);
        } });

    const HelpRegistrar helpRouting (6400, "Routing and system", [] (HelpWriter& w)
    {
        w.body ("WET ONLY makes the main output carry only what GapFill generates, at unity: Dry and Wet are ignored, Output still applies, "
                "bypass wins and Panic still silences it. It is a routing switch, so presets, Random and scenes never change it. "
                "The optional Wet output bus (VST3 and CLAP, off until your DAW enables it) carries the same signal on its own output, "
                "aligned with the main output; Dry, Wet and Output do not touch it and bypass silences it.");
        w.body ("SIDECHAIN GATE fills only while the sidechain is quiet (ducking) or only while it is active, using the threshold. "
                "SC SNAP moves a fill start to the nearest sidechain hit within 30 ms at 100 percent, never earlier than the gap start. "
                "Both do nothing until a sidechain input is connected.");
        w.body ("MIDI OUT sends a note for every detected gap and a note one higher for every fill. It needs a build with MIDI output "
                "enabled, and some hosts ignore MIDI from effect plug-ins. Time-stamps are aligned with the delayed audio.");
        w.body ("CO-OP lets GapFill instances in the same host process share a group (A to D). Avoid overlap and Follow react to what "
                "the other instances are doing right now, so their results can differ from run to run; Alternate gives each member every "
                "n-th bar and is repeatable. Hosts that run every plug-in in its own process cannot see each other: the status then reads "
                "no other GapFill found and nothing changes. Off shares nothing.");
        w.body ("QUALITY trades richness for CPU. Balanced and Lite use fewer voices, grains and a shorter bus-effect tail; Lite also turns "
                "the formant, spectral freeze and convolution effects off. Latency never changes. Auto steps down when GapFill's own "
                "estimated load stays high and back up after a long calm period; it behaves as Full when the host renders offline. "
                "Switches wait for a quiet moment.");
    });
}

} // namespace gapfill
