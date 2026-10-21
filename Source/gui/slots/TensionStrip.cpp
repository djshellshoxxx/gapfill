// F-M21 Tension / release controls: TENSION switch, shape, length, end behaviour, start bar and
// the four lane depths, plus a text status ("Bar 5 of 8, rising" / "waiting for transport").
// EDIT CURVES... opens the curve editor popover (TensionEditor.cpp); the Custom shape reads the curves it
// publishes (saved in the <Tension> state). Registered in Slot::PerformanceTab (hidden until UI-0).

#include "../Knob.h"
#include "../HelpRegistry.h"
#include "../SlotHost.h"
#include "MusicalStrip.h"
#include "TensionEditor.h"
#include "../../PluginProcessor.h"

namespace gapfill
{

namespace
{
    class TensionStrip : public juce::Component,
                         private juce::Timer
    {
    public:
        explicit TensionStrip (GapFillAudioProcessor& p)
            : proc (p),
              on (p.getValueTreeState(), "fm21_on", "TENSION",
                  "Shape density, intensity, width and effect amount across a multi-bar build and release, following the host's bar position."),
              shape (p.getValueTreeState(), "fm21_shape", "Shape", "The curve: Build, Build+Drop, Breakdown, Swell or Custom."),
              bars (p.getValueTreeState(), "fm21_bars", "Bars", "Length of the curve in bars."),
              end (p.getValueTreeState(), "fm21_end", "End", "What happens after the last bar: repeat, keep the last value, or return to your knobs."),
              start (p.getValueTreeState(), "fm21_start", "Start bar", "The bar (counted from the start of the song) where the curve begins.", colours::secondary),
              dDensity (p.getValueTreeState(), "fm21_depthDensity", "Density", "How far density moves from your knob value. 0 = untouched.", colours::secondary),
              dIntensity (p.getValueTreeState(), "fm21_depthIntensity", "Intensity", "How far intensity moves from your knob value. 0 = untouched.", colours::secondary),
              dWidth (p.getValueTreeState(), "fm21_depthWidth", "Width", "How far stereo width moves from your knob value. 0 = untouched.", colours::secondary),
              dFx (p.getValueTreeState(), "fm21_depthFx", "FX send", "How much the effect amount is pulled down at the low end of the curve. 0 = untouched.", colours::secondary)
        {
            setTitle ("Tension and release");

            for (auto* c : std::initializer_list<juce::Component*> { &on, &shape, &bars, &end, &start, &dDensity, &dIntensity, &dWidth, &dFx, &status, &editCurves })
                addAndMakeVisible (c);

            editCurves.setTitle ("Edit tension curves");
            editCurves.setTooltip ("Draw the Custom curve for each lane: drag points, double-click to add, Delete to remove. "
                                   "Editing switches Shape to Custom.");
            editCurves.onClick = [this]
            {
                auto editor = std::make_unique<TensionCurveEditor> (proc);
                juce::CallOutBox::launchAsynchronously (std::move (editor), editCurves.getScreenBounds(), getTopLevelComponent());
            };

            status.setJustificationType (juce::Justification::centredLeft);
            status.setFont (fonts::label (11.0f));
            status.setColour (juce::Label::textColourId, colours::textMuted);
            setSize (musical::kStripWidth, 130);
            applyView();
            refresh();
            startTimerHz (4);
        }

        ~TensionStrip() override { stopTimer(); }

        void resized() override
        {
            auto r = getLocalBounds();
            auto top = r.removeFromTop (juce::jmin (62, r.getHeight()));
            const int cell = juce::jmax (52, top.getWidth() / 7);
            on.setBounds (top.removeFromLeft (cell + 24).withHeight (28));
            top.removeFromLeft (6);

            for (auto* c : std::initializer_list<juce::Component*> { &shape, &bars, &end, &start })
            {
                c->setBounds (top.removeFromLeft (cell));
                top.removeFromLeft (6);
            }

            editCurves.setBounds (top.removeFromLeft (juce::jmin (130, top.getWidth())).withHeight (28));

            r.removeFromTop (6);
            const int knob = juce::jmax (52, r.getWidth() / 7);

            for (auto* c : std::initializer_list<juce::Component*> { &dDensity, &dIntensity, &dWidth, &dFx })
            {
                c->setBounds (r.removeFromLeft (knob));
                r.removeFromLeft (6);
            }

            status.setBounds (r);
        }

    private:
        void applyView() { musical::fit (*this, musical::advancedView (proc), 130); }

        void timerCallback() override { applyView(); refresh(); }

        void refresh()
        {
            const auto& svc = proc.getContextState().tensionService();
            const bool enabled = proc.getValueTreeState().getRawParameterValue ("fm21_on")->load() > 0.5f;
            juce::String text;

            if (! enabled)
                text = "Tension: off";
            else if (! svc.lastActive())
                text = "Tension waiting for transport";
            else
            {
                const int totalBars = (int) std::lround (proc.getValueTreeState().getRawParameterValue ("fm21_bars")->load());
                static const int lengths[] = { 2, 4, 8, 16, 32 };
                const int n = lengths[juce::jlimit (0, 4, totalBars)];
                const int bar = 1 + (int) (svc.lastPhase() * (float) n);
                text = "Bar " + juce::String (juce::jlimit (1, n, bar)) + " of " + juce::String (n);
            }

            if (status.getText() != text)
            {
                status.setText (text, juce::dontSendNotification);
                status.setTitle (text);
            }
        }

        GapFillAudioProcessor& proc;
        GapToggle on;
        GapCombo shape, bars, end;
        GapKnob start, dDensity, dIntensity, dWidth, dFx;
        juce::Label status;
        juce::TextButton editCurves { "EDIT CURVES..." };
    };

    const SlotRegistrar tensionStrip ({ Slot::PerformanceTab, 470,
        [] (GapFillAudioProcessor& proc) -> std::unique_ptr<juce::Component>
        {
            return std::make_unique<TensionStrip> (proc);
        } });

    const HelpRegistrar helpTension (6070, "Tension and release", [] (HelpWriter& w)
    {
        w.body ("TENSION moves density, intensity, width and effect amount along a curve that lasts a number of bars, then repeats, holds or releases. "
                "Each lane has a depth; 0 leaves that lane alone. Shapes: Build rises to the end, Build+Drop rises and then snaps back to neutral for the "
                "last eighth, Breakdown falls and returns, Swell rises and falls, Custom uses the curves you draw.");
        w.body ("EDIT CURVES... opens the curve editor: pick a lane (Density, Intensity, Width, FX Send), drag a point to move it, double-click to add one "
                "(up to 16 per lane), right-click or Delete to remove it, and use BUILD, BUILD+DROP, BREAKDOWN or SWELL to start from a built-in shape. "
                "From the keyboard: Page Up and Page Down select a point, the arrow keys move it (Shift for ten times the step), Insert adds, Delete removes. "
                "The first edit switches Shape to Custom so you hear what you draw; the cyan line is the playhead.");
        w.body ("The position comes from the host's bar count, so loops, seeks and offline renders land on the same value. Start bar counts from the start "
                "of the song and assumes a constant time signature. Without host tempo and time signature it does nothing. Curves are saved in the project, "
                "not in .gapfill parameter files.");
    });
}

} // namespace gapfill
