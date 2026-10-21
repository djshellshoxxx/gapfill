// F-M18 Beat Battles card (docs/dev/12 F-M18): the `battleEvery` switch, STORE A / STORE B (the current sound
// becomes style A or B) and a status line ("Battle: A", "Battle: needs transport and time signature" ...).
// Registered in Slot::PerformanceTab. The styles are session state (not saved with the project), exactly like
// the header A/B they stand in for. FROM A/B copies the header A and B snapshots into the two styles.

#include "../Knob.h"
#include "../HelpRegistry.h"
#include "../LookAndFeel.h"
#include "../SlotHost.h"
#include "../../PluginProcessor.h"

namespace gapfill
{

namespace
{
    class BattleCard : public juce::Component, private juce::Timer
    {
    public:
        explicit BattleCard (GapFillAudioProcessor& p)
            : proc (p),
              every (p.getValueTreeState(), "battleEvery", "BEAT BATTLE",
                     "Alternates two stored fill styles, A and B, every 1, 2, 4 or 8 bars. Needs host transport and a time signature. "
                     "Host automation and your own settings are never changed: the styles are applied on top.")
        {
            setTitle ("Beat Battle");

            store[0].setButtonText ("STORE A");
            store[1].setButtonText ("STORE B");
            store[0].setTooltip ("Store the current sound (creative, weights, effects and musical settings) as style A.");
            store[1].setTooltip ("Store the current sound (creative, weights, effects and musical settings) as style B.");
            store[0].setTitle ("Store style A");
            store[1].setTitle ("Store style B");
            fromAb.setButtonText ("FROM A/B");
            fromAb.setTitle ("Take styles from the header A and B");
            fromAb.setTooltip ("Use the header A snapshot as style A and the header B snapshot as style B. "
                               "Copy a sound into B first (COPY), change it, then press this.");
            fromAb.onClick = [this]
            {
                const auto& f = proc.getUiState().editorActions.battleFromHeaderAb;
                notice = (f != nullptr && f()) ? juce::String() : juce::String ("  (header A/B is not available)");
                refresh();
            };
            clear.setButtonText ("CLEAR");
            clear.setTitle ("Clear both battle styles");
            clear.setTooltip ("Forget both stored styles. The Beat Battle switch stays where it is but does nothing until two styles are stored.");

            for (int i = 0; i < 2; ++i)
                store[i].onClick = [this, i]
                {
                    proc.getLiveControl().battles().setStyleFromCurrent (i, proc.getValueTreeState());
                    refresh();
                };

            clear.onClick = [this] { proc.getLiveControl().battles().clear(); refresh(); };

            addAndMakeVisible (every);

            for (auto* b : { &store[0], &store[1], &fromAb, &clear })
                addAndMakeVisible (*b);

            status.setFont (fonts::ui (10.5f));
            status.setColour (juce::Label::textColourId, colours::textMuted);
            status.setMinimumHorizontalScale (0.8f);
            addAndMakeVisible (status);

            setSize (400, 52);
            refresh();
            startTimerHz (4);
        }

        ~BattleCard() override { stopTimer(); }

        void resized() override
        {
            auto r = getLocalBounds();
            auto top = r.removeFromTop (30);
            every.setBounds (top.removeFromLeft (juce::jmax (120, top.getWidth() / 3)));
            top.removeFromLeft (8);
            store[0].setBounds (top.removeFromLeft (juce::jmax (64, top.getWidth() / 3)).reduced (0, 2));
            top.removeFromLeft (4);
            store[1].setBounds (top.removeFromLeft (juce::jmax (64, top.getWidth() / 2)).reduced (0, 2));
            top.removeFromLeft (4);
            fromAb.setBounds (top.removeFromLeft (juce::jmax (72, top.getWidth() / 2)).reduced (0, 2));
            top.removeFromLeft (4);
            clear.setBounds (top.reduced (0, 2));
            status.setBounds (r.removeFromTop (18));
        }

    private:
        void timerCallback() override { refresh(); }

        void refresh()
        {
            auto& live = proc.getLiveControl();
            const auto base = juce::String (battle::statusText (live.getBattleStatus()));
            const auto text = base + "   [A " + (live.battles().hasStyle (0) ? "stored" : "empty")
                                + ", B " + (live.battles().hasStyle (1) ? "stored" : "empty") + "]" + notice;

            if (status.getText() != text)
            {
                status.setText (text, juce::dontSendNotification);
                status.setTitle (text);
            }
        }

        GapFillAudioProcessor& proc;
        GapCombo every;
        juce::TextButton store[2], fromAb, clear;
        juce::Label status;
        juce::String notice;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BattleCard)
    };

    const SlotRegistrar battleCard ({ Slot::PerformanceTab, 250,
        [] (GapFillAudioProcessor& proc) -> std::unique_ptr<juce::Component>
        {
            return std::make_unique<BattleCard> (proc);
        } });

    const HelpRegistrar helpBattle (6225, "Beat Battles", [] (HelpWriter& w)
    {
        w.body ("Beat Battle alternates two fill styles, A and B, in time with the music. Set up a sound you like and press STORE A, change it, "
                "press STORE B (or press FROM A/B to take the header A and B snapshots), then choose how often to switch: every 1, 2, 4 or 8 bars. Even windows play A, odd windows play B, counted from the start "
                "of the song, so looping and seeking give the same pattern.");
        w.body ("It needs host transport and a time signature, and two different styles; the line under the controls says what is missing. "
                "A style holds the creative, weight, effect and musical settings. Your own settings and host automation are never changed: "
                "the styles are applied on top while a battle runs. The styles are not saved with the project.");
    });
}

} // namespace gapfill
