// C-2 / F-M23 macro panel (docs/dev/12, docs/dev/11): the four macro knobs, the Macros On switch and a
// compact mapping editor for the selected macro (target, min, max, curve, smoothing, remove).
// Registered in Slot::PerformanceTab (Stack; fixed height). Each mapping row has its own smoothing (F-M23, 0-500 ms);
// the row under the mappings edits the selected macro's LFO (sine / triangle / saw / square / sample-and-hold, free in Hz or
// locked to a tempo division, depth and phase).

#include "../Knob.h"
#include "../HelpRegistry.h"
#include "../SlotHost.h"
#include "../../PluginProcessor.h"

namespace gapfill
{

namespace
{
    using namespace macros;

    constexpr int kRowHeight = 26;
    constexpr int kLfoHeight = 30;

    class MappingRow : public juce::Component
    {
    public:
        std::function<void (const Mapping&)> onEdit;
        std::function<void()> onRemove;

        MappingRow()
        {
            smooth.setSliderStyle (juce::Slider::LinearBar);
            smooth.setRange (0.0, 500.0, 1.0);
            smooth.setTextValueSuffix (" ms");
            smooth.setTitle ("Smoothing");
            smooth.setTooltip ("Glide time of this mapping: how slowly the target follows the macro (0 = immediately).");
            smooth.onValueChange = [this] { if (! loading) emit(); };
            addAndMakeVisible (smooth);

            label.setFont (fonts::ui (11.0f));
            label.setMinimumHorizontalScale (0.7f);
            for (auto* s : { &lo, &hi })
            {
                s->setSliderStyle (juce::Slider::LinearBar);
                s->setRange (0.0, 100.0, 1.0);
                s->setTextValueSuffix (" %");
                s->onValueChange = [this] { if (! loading) emit(); };
            }
            lo.setTitle ("Minimum");
            hi.setTitle ("Maximum");
            curve.setTitle ("Curve");
            lo.setTooltip ("Value of the target when the macro is at 0.");
            hi.setTooltip ("Value of the target when the macro is at 100.");
            curve.addItemList ({ "Linear", "Ease in", "Ease out", "S-curve" }, 1);
            curve.setTooltip ("Shape of the response between the minimum and the maximum.");
            curve.onChange = [this] { if (! loading) emit(); };
            bipolar.setButtonText ("+/-");
            bipolar.setClickingTogglesState (true);
            bipolar.setTooltip ("Bipolar: the macro's centre is the target's midpoint, so the target moves both ways.");
            bipolar.onClick = [this] { if (! loading) emit(); };
            remove.setButtonText ("X");
            remove.setTooltip ("Remove this mapping.");
            remove.onClick = [this] { if (onRemove != nullptr) onRemove(); };
            for (auto* c : std::initializer_list<juce::Component*> { &label, &lo, &hi, &curve, &bipolar, &remove })
                addAndMakeVisible (*c);
        }

        void set (const Mapping& m)
        {
            const juce::ScopedValueSetter<bool> guard (loading, true);
            current = m;
            label.setText (targetInfo (m.target).label, juce::dontSendNotification);
            lo.setValue (m.min01 * 100.0, juce::dontSendNotification);
            hi.setValue (m.max01 * 100.0, juce::dontSendNotification);
            curve.setSelectedId (juce::jlimit (0, 3, (int) m.curve) + 1, juce::dontSendNotification);
            bipolar.setToggleState (m.bipolar != 0, juce::dontSendNotification);
            smooth.setValue (m.smoothMs, juce::dontSendNotification);
        }

        void resized() override
        {
            auto r = getLocalBounds();
            remove.setBounds (r.removeFromRight (24).reduced (0, 1));
            bipolar.setBounds (r.removeFromRight (34).reduced (0, 1));
            smooth.setBounds (r.removeFromRight (78).reduced (2, 1));
            curve.setBounds (r.removeFromRight (84).reduced (2, 1));
            const int w = juce::jmax (40, r.getWidth() / 3);
            label.setBounds (r.removeFromLeft (w));
            lo.setBounds (r.removeFromLeft (r.getWidth() / 2).reduced (2, 1));
            hi.setBounds (r.reduced (2, 1));
        }

    private:
        void emit()
        {
            Mapping m = current;
            m.min01 = (float) (lo.getValue() / 100.0);
            m.max01 = (float) (hi.getValue() / 100.0);
            m.curve = (std::uint8_t) juce::jmax (0, curve.getSelectedId() - 1);
            m.bipolar = bipolar.getToggleState() ? 1 : 0;
            m.smoothMs = (float) smooth.getValue();
            current = m;
            if (onEdit != nullptr)
                onEdit (m);
        }

        juce::Label label;
        juce::Slider lo, hi, smooth;
        juce::ComboBox curve;
        juce::TextButton bipolar, remove;
        Mapping current;
        bool loading = false;
    };

    /** The selected macro's LFO (F-M23): one per macro. */
    class LfoRow : public juce::Component
    {
    public:
        std::function<void (const LfoSettings&)> onEdit;

        LfoRow()
        {
            enabled.setButtonText ("LFO");
            enabled.setClickingTogglesState (true);
            enabled.setTooltip ("Moves this macro by itself with a low-frequency oscillator, on top of the knob.");
            enabled.onClick = [this] { if (! loading) emit(); };
            sync.setButtonText ("SYNC");
            sync.setClickingTogglesState (true);
            sync.setTooltip ("Lock the LFO to the host tempo (a tempo division). Without host transport it runs at the Rate instead.");
            sync.onClick = [this] { if (! loading) emit(); };
            shape.addItemList ({ "Sine", "Triangle", "Saw", "Square", "Sample & hold" }, 1);
            shape.setTooltip ("Shape of the LFO. Sample & hold picks a new seeded value on each cycle.");
            shape.setTitle ("LFO shape");
            shape.onChange = [this] { if (! loading) emit(); };
            const auto divs = divisionNames();
            for (int i = 0; i < divs.size(); ++i)
                division.addItem (divs[i], i + 1);
            division.setTooltip ("Cycle length as a tempo division (used while SYNC is on and the host has a transport).");
            division.setTitle ("LFO division");
            division.onChange = [this] { if (! loading) emit(); };

            rate.setSliderStyle (juce::Slider::LinearBar);
            rate.setRange (0.01, 20.0, 0.01);
            rate.setSkewFactorFromMidPoint (1.0);
            rate.setTextValueSuffix (" Hz");
            rate.setTitle ("LFO rate");
            rate.setTooltip ("Free-running speed in Hz (used when SYNC is off or the host has no transport).");
            depth.setSliderStyle (juce::Slider::LinearBar);
            depth.setRange (0.0, 100.0, 1.0);
            depth.setTextValueSuffix (" %");
            depth.setTitle ("LFO depth");
            depth.setTooltip ("How far the LFO swings the macro around its middle.");
            phase.setSliderStyle (juce::Slider::LinearBar);
            phase.setRange (0.0, 360.0, 1.0);
            phase.setTextValueSuffix (" deg");
            phase.setTitle ("LFO phase");
            phase.setTooltip ("Where in its cycle the LFO starts.");

            for (auto* s : { &rate, &depth, &phase })
                s->onValueChange = [this] { if (! loading) emit(); };

            for (auto* c : std::initializer_list<juce::Component*> { &enabled, &sync, &shape, &division, &rate, &depth, &phase })
                addAndMakeVisible (*c);
        }

        void set (const LfoSettings& l)
        {
            const juce::ScopedValueSetter<bool> guard (loading, true);
            current = l;
            enabled.setToggleState (l.enabled != 0, juce::dontSendNotification);
            sync.setToggleState (l.tempoSync != 0, juce::dontSendNotification);
            shape.setSelectedId (juce::jlimit (0, 4, (int) l.shape) + 1, juce::dontSendNotification);
            division.setSelectedId (juce::jlimit (0, (int) Division::NumDivisions - 1, (int) l.division) + 1, juce::dontSendNotification);
            rate.setValue (l.rateHz, juce::dontSendNotification);
            depth.setValue (l.depth01 * 100.0, juce::dontSendNotification);
            phase.setValue (l.phaseDeg, juce::dontSendNotification);
        }

        void resized() override
        {
            auto r = getLocalBounds();
            enabled.setBounds (r.removeFromLeft (44).reduced (0, 1));
            r.removeFromLeft (4);
            sync.setBounds (r.removeFromLeft (46).reduced (0, 1));
            r.removeFromLeft (4);
            shape.setBounds (r.removeFromLeft (juce::jmax (80, r.getWidth() / 5)).reduced (0, 1));
            r.removeFromLeft (4);
            division.setBounds (r.removeFromLeft (juce::jmax (52, r.getWidth() / 7)).reduced (0, 1));
            r.removeFromLeft (4);
            const int w = juce::jmax (50, r.getWidth() / 3);
            rate.setBounds (r.removeFromLeft (w).reduced (2, 1));
            depth.setBounds (r.removeFromLeft (w).reduced (2, 1));
            phase.setBounds (r.reduced (2, 1));
        }

    private:
        void emit()
        {
            LfoSettings l = current;
            l.enabled = enabled.getToggleState() ? 1 : 0;
            l.tempoSync = sync.getToggleState() ? 1 : 0;
            l.shape = (std::uint8_t) juce::jmax (0, shape.getSelectedId() - 1);
            l.division = (std::uint8_t) juce::jmax (0, division.getSelectedId() - 1);
            l.rateHz = (float) rate.getValue();
            l.depth01 = (float) (depth.getValue() / 100.0);
            l.phaseDeg = (float) phase.getValue();
            current = l;

            if (onEdit != nullptr)
                onEdit (l);
        }

        juce::TextButton enabled, sync;
        juce::ComboBox shape, division;
        juce::Slider rate, depth, phase;
        LfoSettings current;
        bool loading = false;
    };

    class MacroEditor : public juce::Component, private juce::Timer
    {
    public:
        explicit MacroEditor (GapFillAudioProcessor& p)
            : proc (p),
              macrosOn (p.getValueTreeState(), "macrosOn", "MACROS",
                        "Lets the four macros drive their targets. Off leaves every target under your control.", colours::accent),
              knobs { GapKnob (p.getValueTreeState(), "macroActivity",    "Activity",    "One knob for how busy the fills are.", colours::primary),
                      GapKnob (p.getValueTreeState(), "macroChaos",       "Chaos",       "One knob for how unpredictable the fills are.", colours::primary),
                      GapKnob (p.getValueTreeState(), "macroSpace",       "Space",       "One knob for how roomy the fills sound.", colours::primary),
                      GapKnob (p.getValueTreeState(), "macroDestruction", "Destruction", "One knob for how damaged the fills sound.", colours::primary) }
        {
            setTitle ("Macros");
            addAndMakeVisible (macrosOn);
            for (auto& k : knobs)
                addAndMakeVisible (k);

            for (int i = 0; i < kNumMacros; ++i)
            {
                select[i].setButtonText ("M" + juce::String (i + 1));
                select[i].setClickingTogglesState (false);
                select[i].setTooltip ("Edit the mappings of macro " + juce::String (i + 1));
                select[i].onClick = [this, i] { selected = i; rebuild(); };
                addAndMakeVisible (select[i]);
            }

            addTarget.setTitle ("Add target");
            addTarget.setTextWhenNothingSelected ("+ Add target...");
            addTarget.setTooltip ("Map this macro to another setting. A setting belongs to one macro at a time.");
            addTarget.onChange = [this]
            {
                const int id = addTarget.getSelectedId();
                if (id <= 0)
                    return;
                Mapping m;
                m.target = (std::uint8_t) (id - 1);
                proc.getLiveControl().macroMappings().addMapping (selected, m);
                addTarget.setSelectedId (0, juce::dontSendNotification);
                rebuild();
            };
            addAndMakeVisible (addTarget);

            reset.setButtonText ("RESET");
            reset.setTooltip ("Restore the factory mappings of this macro.");
            reset.onClick = [this] { proc.getLiveControl().macroMappings().resetMacro (selected); rebuild(); };
            addAndMakeVisible (reset);

            info.setFont (fonts::ui (11.0f));
            info.setColour (juce::Label::textColourId, colours::textMuted);
            addAndMakeVisible (info);

            for (auto& r : rows)
                addChildComponent (r);

            lfoRow.onEdit = [this] (const LfoSettings& l) { proc.getLiveControl().macroMappings().setLfo (selected, l); };
            addAndMakeVisible (lfoRow);

            setSize (400, 4 + 72 + 4 + 28 + kMaxMappings * kRowHeight + 22 + kLfoHeight);
            rebuild();
            startTimerHz (4);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (0, 2);
            auto top = r.removeFromTop (72);
            macrosOn.setBounds (top.removeFromLeft (80).withHeight (28));
            const int w = juce::jmax (48, top.getWidth() / 4);
            for (auto& k : knobs)
                k.setBounds (top.removeFromLeft (w));

            r.removeFromTop (4);
            auto sel = r.removeFromTop (28);
            for (auto& b : select)
                b.setBounds (sel.removeFromLeft (38).reduced (1));
            sel.removeFromLeft (4);
            reset.setBounds (sel.removeFromRight (60).reduced (1));
            addTarget.setBounds (sel.reduced (2, 1));

            for (auto& row : rows)
                row.setBounds (r.removeFromTop (kRowHeight));
            lfoRow.setBounds (r.removeFromTop (kLfoHeight).reduced (0, 2));
            info.setBounds (r.removeFromTop (20));
        }

    private:
        void rebuild()
        {
            auto& model = proc.getLiveControl().macroMappings();
            const auto& cfg = model.get();
            shownCount = cfg.count[selected];
            lfoRow.set (cfg.lfo[selected]);

            for (int i = 0; i < kNumMacros; ++i)
                select[i].setColour (juce::TextButton::buttonColourId, i == selected ? colours::primary : colours::panel);

            for (int i = 0; i < kMaxMappings; ++i)
            {
                auto& row = rows[i];
                const bool used = i < cfg.count[selected];
                row.setVisible (used);
                if (! used)
                    continue;
                row.set (cfg.map[selected][i]);
                row.onEdit = [this, i] (const Mapping& m) { proc.getLiveControl().macroMappings().editMapping (selected, i, m); };
                row.onRemove = [this, i]
                {
                    proc.getLiveControl().macroMappings().removeMapping (selected, i);
                    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MacroEditor> (this)] { if (safe != nullptr) safe->rebuild(); });
                };
            }

            addTarget.clear (juce::dontSendNotification);
            for (int t = 0; t < kNumTargets; ++t)
                if (model.isTargetAvailable (t) && model.ownerOf (t) < 0)
                    addTarget.addItem (targetInfo (t).label, t + 1);
            addTarget.setEnabled (cfg.count[selected] < kMaxMappings);

            info.setText (juce::String (cfg.names[selected]) + ": " + juce::String ((int) cfg.count[selected]) + " of "
                              + juce::String (kMaxMappings) + " mappings" + (cfg.lfo[selected].enabled != 0 ? ", LFO running" : ""),
                          juce::dontSendNotification);
        }

        void timerCallback() override
        {
            // Another path (state restore, bank import, controller profile) may have changed the table.
            if (proc.getLiveControl().macroMappings().get().count[selected] != shownCount)
                rebuild();
        }

        GapFillAudioProcessor& proc;
        GapToggle macrosOn;
        GapKnob knobs[kNumMacros];
        juce::TextButton select[kNumMacros], reset;
        juce::ComboBox addTarget;
        MappingRow rows[kMaxMappings];
        LfoRow lfoRow;
        juce::Label info;
        int selected = 0;
        int shownCount = -1;
    };

    const SlotRegistrar macroEditor ({ Slot::PerformanceTab, 300,
        [] (GapFillAudioProcessor& proc) -> std::unique_ptr<juce::Component>
        {
            return std::make_unique<MacroEditor> (proc);
        } });

    const HelpRegistrar helpMacroEditor (6230, "Macros", [] (HelpWriter& w)
    {
        w.body ("The four macro knobs each move several settings at once. Turn MACROS on to let them drive their targets; "
                "a target that a macro owns follows the macro, and the knob shows where it really is. "
                "Pick M1 to M4 to see what a macro moves. Min and Max set the range, the curve shapes it, the last slider smooths how fast the "
                "target follows (0 to 500 ms), +/- makes it move both ways around the middle, X removes a mapping, and + Add target maps another "
                "setting. A setting belongs to one macro at a time. RESET restores the factory mappings of the selected macro.");
        w.body ("Each macro can also have an LFO: switch LFO on, pick a shape (Sine, Triangle, Saw, Square or Sample & hold), and either "
                "lock it to the tempo with SYNC and a division, or let it run free at a rate in Hz. Depth is how far it swings the macro, "
                "Phase is where in its cycle it starts.");
    });
}

} // namespace gapfill
