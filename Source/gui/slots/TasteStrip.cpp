// F-X05 Local preference learning controls: LEARN MY TASTE, strength, Like / Skip the latest
// fill, Undo, Reset, and a text readout of what was learned ("Leaning toward: Stutter 5 of 6; ...").
// Registered in Slot::PerformanceTab; the slot stays empty until UI-0 reserves it.

#include "../Knob.h"
#include "../HelpRegistry.h"
#include "../SlotHost.h"
#include "MusicalStrip.h"
#include "../../PluginProcessor.h"

namespace gapfill
{

namespace
{
    juce::String describeItems (const std::vector<taste::ExplainItem>& items)
    {
        juce::StringArray parts;

        for (const auto& it : items)
            parts.add (juce::String (taste::featureName (it.feature)) + " " + juce::String (it.liked) + " of " + juce::String (it.total));

        return parts.joinIntoString ("; ");
    }

    class TasteStrip : public juce::Component,
                       private juce::Timer
    {
    public:
        explicit TasteStrip (GapFillAudioProcessor& p)
            : proc (p),
              learn (p.getValueTreeState(), "fx05_on", "LEARN MY TASTE",
                     "Remember which fills you like and skip, on this computer only, and gently tilt Auto fill choices toward them."),
              strength (p.getValueTreeState(), "fx05_strength", "Taste",
                        "How strongly your taste tilts the choice. It never forces a choice.", colours::secondary)
        {
            setTitle ("Taste");
            like.setButtonText ("LIKE");
            skip.setButtonText ("SKIP");
            undo.setButtonText ("UNDO");
            reset.setButtonText ("RESET");
            like.setTooltip ("Like the latest fill.");
            skip.setTooltip ("Skip this kind of fill: a dislike for the taste profile.");
            undo.setTooltip ("Undo the last rating.");
            reset.setTooltip ("Forget the taste profile of this project. Hold Alt to also clear your ratings library.");

            like.onClick = [this] { rateCommand (CommandType::LikeLatest); };
            skip.onClick = [this] { rateCommand (CommandType::SkipLatest); };
            undo.onClick = [this] { proc.getContextState().tasteService().undoLast(); refresh(); };
            reset.onClick = [this]
            {
                proc.getContextState().tasteService().resetProfile (juce::ModifierKeys::currentModifiers.isAltDown());
                refresh();
            };

            for (auto* c : std::initializer_list<juce::Component*> { &learn, &strength, &like, &skip, &undo, &reset, &readout })
                addAndMakeVisible (c);

            readout.setJustificationType (juce::Justification::topLeft);
            readout.setFont (fonts::label (11.0f));
            readout.setColour (juce::Label::textColourId, colours::textMuted);
            setSize (musical::kStripWidth, 100);
            applyView();
            refresh();
            startTimerHz (2);
        }

        ~TasteStrip() override { stopTimer(); }

        void resized() override
        {
            auto r = getLocalBounds();
            auto top = r.removeFromTop (juce::jmin (34, r.getHeight()));
            learn.setBounds (top.removeFromLeft (juce::jmax (110, top.getWidth() / 5)).withHeight (28));
            top.removeFromLeft (8);
            strength.setBounds (top.removeFromLeft (juce::jmax (56, top.getWidth() / 6)));
            top.removeFromLeft (8);

            for (auto* b : { &like, &skip, &undo, &reset })
            {
                b->setBounds (top.removeFromLeft (juce::jmax (48, top.getWidth() / 4)).withHeight (28));
                top.removeFromLeft (6);
            }

            readout.setBounds (r);
        }

    private:
        void applyView() { musical::fit (*this, musical::advancedView (proc), 100); }

        void timerCallback() override { applyView(); refresh(); }

        void rateCommand (CommandType type)
        {
            Command c;
            c.type = type;
            proc.postCommand (c);
        }

        void refresh()
        {
            auto& service = proc.getContextState().tasteService();
            const auto c = service.counts();
            const bool on = proc.getValueTreeState().getRawParameterValue ("fx05_on")->load() > 0.5f;

            juce::String text;

            if (! on)
                text = "Taste learning is off.";
            else if (! service.ready())
                text = "Taste: " + juce::String (c.likes) + " liked, " + juce::String (c.skips) + " skipped. Needs about 8 ratings, with at least 3 of each.";
            else
            {
                std::vector<taste::ExplainItem> toward, away;
                taste::explain (c, 3, toward, away);
                text = "Leaning toward: " + (toward.empty() ? juce::String ("nothing yet") : describeItems (toward))
                     + ". Leaning away from: " + (away.empty() ? juce::String ("nothing yet") : describeItems (away)) + ".";
            }

            if (on && service.firstUseNoticeNeeded())
            {
                text << " Your ratings are stored on this computer only (GapFill/ratings.xml); nothing leaves it.";

                if (++noticeTicks > 20)                      // shown for about ten seconds the first time
                    service.markNoticeShown();
            }

            if (readout.getText() != text)
            {
                readout.setText (text, juce::dontSendNotification);
                readout.setTitle (text);
            }
        }

        GapFillAudioProcessor& proc;
        GapToggle learn;
        GapKnob strength;
        juce::TextButton like, skip, undo, reset;
        juce::Label readout;
        int noticeTicks = 0;
    };

    const SlotRegistrar tasteStrip ({ Slot::PerformanceTab, 460,
        [] (GapFillAudioProcessor& proc) -> std::unique_ptr<juce::Component>
        {
            return std::make_unique<TasteStrip> (proc);
        } });

    const HelpRegistrar helpTaste (6060, "Taste", [] (HelpWriter& w)
    {
        w.body ("With LEARN MY TASTE on, press LIKE or SKIP on fills you hear (LIKE is also in the History drawer). GapFill keeps those ratings on this "
                "computer, in GapFill/ratings.xml, and nothing is sent anywhere. From about eight ratings (at least three of each) it gently tilts the odds "
                "of choosing each fill type and frequency band in Auto mode: never below half or above double, never switching on something you set to 0, "
                "never forcing a choice. Fill Lock and a forced Fill Type override it.");
        w.body ("The profile used by a project is saved in the project, so it renders the same on every machine. RESET forgets it (hold Alt to also clear "
                "the ratings library). It learns which fill types and bands you prefer, not timing.");
    });
}

} // namespace gapfill
