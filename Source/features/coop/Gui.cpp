#include "Coop.h"
#include "../../PluginProcessor.h"
#include "../../gui/Knob.h"
#include "../../gui/SlotHost.h"
#include "../../gui/slots/MusicalStrip.h"

namespace gapfill::coop
{
namespace
{
    class Card : public juce::Component, private juce::Timer
    {
    public:
        explicit Card (GapFillAudioProcessor& p)
            : proc (p),
              enabled (p.getValueTreeState(), "coopDeterministic", "DETERMINISTIC CO-OP", "Use reproducible ownership; original co-op modes remain unchanged."),
              group (p.getValueTreeState(), "coopDetGroup", "GROUP", "Use the legacy group or choose A-H for deterministic cooperation."),
              grid (p.getValueTreeState(), "coopGrid", "GRID", "The lowest fixed slot supplies the shared musical grid."),
              slot (p.getValueTreeState(), "coopSlot", "FIXED SLOT", "A unique stable slot for this track.", colours::secondary),
              priority (p.getValueTreeState(), "coopPriority", "PRIORITY", "Relative share of owned musical slots.", colours::secondary),
              yield (p.getValueTreeState(), "coopYield", "YIELD", "Reduce natural fills when another track owns the slot.", colours::secondary)
        {
            setTitle ("Deterministic cooperation");
            fix.setButtonText ("FIX SLOT");
            recall.setButtonText ("GROUP RECALL");
            cancel.setButtonText ("CANCEL GROUP");
            configureButton (fix, "Repair duplicated co-op slot");
            configureButton (recall, "Recall this scene index in each cooperating track at one common boundary");
            configureButton (cancel, "Cancel pending group scene recalls; current sound is unchanged");
            fix.onClick = [this] { synchronizeSlot(); };
            recall.onClick = [this]
            {
                const int index = (int) scene.getValue() - 1;
                status.setText (proc.getCoop().requestGroupScene (index) ? "Group scene requested at a shared boundary"
                    : "Group recall unavailable: start transport, join a group and wait for membership", juce::dontSendNotification);
            };
            cancel.onClick = [this] { proc.getCoop().cancelGroupScenes(); };
            scene.setRange (1, 64, 1);
            scene.setValue (1);
            scene.setTitle ("Group scene number");
            scene.setTooltip ("Each track recalls this index from its own scene bank.");
            scene.setSliderStyle (juce::Slider::IncDecButtons);
            scene.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 48, 24);
            for (auto* c : std::initializer_list<juce::Component*> { &enabled, &group, &grid, &slot, &priority, &yield,
                                                                     &fix, &recall, &cancel, &scene, &status })
                addAndMakeVisible (c);
            status.setFont (fonts::label (10.0f));
            status.setColour (juce::Label::textColourId, colours::textMuted);
            status.setJustificationType (juce::Justification::topLeft);
            setSize (640, 192);
            refresh();
            startTimerHz (5);
        }

        ~Card() override { stopTimer(); }

        void resized() override
        {
            auto area = getLocalBounds();
            auto top = area.removeFromTop (32);
            enabled.setBounds (top.removeFromLeft (top.getWidth() / 2));
            top.removeFromLeft (8);
            group.setBounds (top);
            area.removeFromTop (8);
            auto values = area.removeFromTop (32);
            const int width = (values.getWidth() - 16) / 3;
            for (auto* c : { &slot, &priority, &yield })
            {
                c->setBounds (values.removeFromLeft (width));
                values.removeFromLeft (8);
            }
            area.removeFromTop (8);
            auto actions = area.removeFromTop (32);
            grid.setBounds (actions.removeFromLeft (actions.getWidth() / 2));
            actions.removeFromLeft (8);
            fix.setBounds (actions.removeFromLeft (72));
            actions.removeFromLeft (8);
            scene.setBounds (actions);
            area.removeFromTop (8);
            auto navigation = area.removeFromTop (32);
            recall.setBounds (navigation.removeFromLeft ((navigation.getWidth() - 8) / 2));
            navigation.removeFromLeft (8);
            cancel.setBounds (navigation);
            area.removeFromTop (8);
            status.setBounds (area);
        }

    private:
        void configureButton (juce::TextButton& button, const char* text)
        {
            button.setTitle (text);
            button.setTooltip (text);
        }
        void synchronizeSlot()
        {
            proc.getCoop().repairDuplicateSlot();
            auto* parameter = proc.getValueTreeState().getParameter ("coopSlot");
            const int pending = proc.getCoop().pendingSlotForHost ((int) parameter->convertFrom0to1 (parameter->getValue()));
            if (pending > 0)
            {
                parameter->beginChangeGesture();
                parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) pending));
                parameter->endChangeGesture();
            }
        }
        void refresh()
        {
            musical::fit (*this, musical::advancedView (proc), 192);
            const auto text = proc.getCoop().describe();
            if (status.getText() != text)
            {
                status.setText (text, juce::dontSendNotification);
                status.setTitle (text);
            }
        }
        void timerCallback() override { refresh(); }

        GapFillAudioProcessor& proc;
        GapToggle enabled;
        GapCombo group, grid;
        GapBar slot, priority, yield;
        juce::TextButton fix, recall, cancel;
        juce::Slider scene;
        juce::Label status;
    };

    const SlotRegistrar card ({ gapfill::Slot::PerformanceTab, 405,
        [] (GapFillAudioProcessor& proc) -> std::unique_ptr<juce::Component> { return std::make_unique<Card> (proc); } });
}
}
