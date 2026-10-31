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

    void notifyTasteChanged (GapFillAudioProcessor& proc)
    {
        proc.updateHostDisplay (juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged (true));
    }

    void confirmReset (GapFillAudioProcessor& proc, juce::Component& source, bool clearLibrary)
    {
        // A modal confirmation may dismiss the profile callout. Keep the
        // editor as the lifetime anchor so an intentional reset still runs.
        juce::Component* anchor = proc.getActiveEditor();
        if (anchor == nullptr) anchor = source.findParentComponentOfClass<juce::AudioProcessorEditor>();
        if (anchor == nullptr) anchor = &source;
        const juce::Component::SafePointer<juce::Component> safe (anchor);
        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
            .withIconType (juce::MessageBoxIconType::QuestionIcon)
            .withTitle (clearLibrary ? "Clear taste and ratings library?" : "Reset project taste?")
            .withMessage (clearLibrary ? "This clears this project's taste profile and your local ratings library."
                                       : "This resets the project's profile. Your ratings library remains available for Rebuild.")
            .withButton (clearLibrary ? "Clear both" : "Reset profile")
            .withButton ("Cancel").withAssociatedComponent (anchor),
            [safe, owner = &proc, clearLibrary] (int result)
            {
                if (safe != nullptr && result == 1)
                {
                    owner->getContextState().tasteService().resetProfile (clearLibrary);
                    notifyTasteChanged (*owner);
                }
            });
    }

    class TasteBar final : public juce::Component
    {
    public:
        TasteBar()
        {
            text.setFont (fonts::label (11.0f));
            addAndMakeVisible (text);
        }
        void set (int feature, float value)
        {
            multiplier = value;
            const auto label = juce::String (taste::featureName (feature)) + " x" + juce::String (value, 2);
            text.setText (label, juce::dontSendNotification);
            text.setTitle (label);
            repaint(); // direct update; no animation, including Reduced Motion
        }
        void resized() override { text.setBounds (getLocalBounds().removeFromTop (24)); }
        void paint (juce::Graphics& graphics) override
        {
            const auto bar = getLocalBounds().removeFromBottom (8).toFloat();
            graphics.setColour (colours::textMuted.withAlpha (0.2f));
            graphics.fillRect (bar);
            graphics.setColour (colours::secondary);
            graphics.fillRect (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, multiplier / 2.0f)));
        }
    private:
        juce::Label text;
        float multiplier = 1.0f;
    };

    class TasteProfile final : public juce::Component, private juce::Timer
    {
    public:
        explicit TasteProfile (GapFillAudioProcessor& owner) : proc (owner)
        {
            setTitle ("Taste profile");
            setDescription ("All fifteen algorithm multipliers, Like and Skip counts, explanations, rebuild and reset actions.");
            counts.setFont (fonts::label (12.0f));
            explanation.setFont (fonts::label (11.0f));
            explanation.setJustificationType (juce::Justification::topLeft);
            addAndMakeVisible (counts);
            addAndMakeVisible (explanation);
            for (auto& bar : bars) addAndMakeVisible (bar);
            rebuild.setButtonText ("REBUILD FROM HISTORY");
            rebuild.setTitle ("Rebuild taste from local ratings history");
            rebuild.setTooltip ("Explicitly replace this project's profile using the ratings library on this computer and the selected Taste model.");
            rebuild.onClick = [this]
            {
                proc.getContextState().tasteService().rebuildFromLibrary();
                notifyTasteChanged (proc);
                refresh();
            };
            reset.setButtonText ("RESET PROFILE");
            reset.setTitle ("Reset project taste profile");
            reset.onClick = [this] { confirmReset (proc, *this, false); };
            clear.setButtonText ("CLEAR LIBRARY");
            clear.setTitle ("Clear project taste and local ratings library");
            clear.onClick = [this] { confirmReset (proc, *this, true); };
            for (auto* button : { &rebuild, &reset, &clear }) addAndMakeVisible (button);
            setSize (480, 352);
            refresh();
            startTimerHz (2);
        }
        ~TasteProfile() override { stopTimer(); }
        void resized() override
        {
            auto area = getLocalBounds().reduced (8);
            counts.setBounds (area.removeFromTop (24));
            explanation.setBounds (area.removeFromTop (72));
            auto actions = area.removeFromBottom (32);
            rebuild.setBounds (actions.removeFromLeft (184).reduced (2));
            reset.setBounds (actions.removeFromLeft (136).reduced (2));
            clear.setBounds (actions.reduced (2));
            for (int row = 0; row < 5; ++row)
            {
                auto line = area.removeFromTop (40);
                const auto columnWidth = line.getWidth() / 3;
                for (int column = 0; column < 3; ++column)
                    bars[(std::size_t) (row * 3 + column)].setBounds (line.removeFromLeft (columnWidth).reduced (4));
            }
        }
    private:
        void timerCallback() override { refresh(); }
        void refresh()
        {
            const auto& service = proc.getContextState().tasteService();
            const auto data = service.counts();
            const auto bias = service.currentBias();
            const auto header = juce::String (data.likes) + " Likes, " + juce::String (data.skips) + " Skips; Taste model "
                              + juce::String ((int) service.getModelVersion()) + (bias.ready ? ". Ready." : ". Needs at least 3 of each and 8 total.");
            counts.setText (header, juce::dontSendNotification);
            counts.setTitle (header);
            std::vector<taste::ExplainItem> toward, away;
            taste::explain (data, 3, toward, away, service.getModelVersion());
            const auto text = "Toward: " + (toward.empty() ? juce::String ("no counted features yet") : describeItems (toward))
                            + ". Away: " + (away.empty() ? juce::String ("none yet") : describeItems (away))
                            + ". Saved project multipliers stay portable; Rebuild uses this computer's library.";
            explanation.setText (text, juce::dontSendNotification);
            explanation.setTitle (text);
            for (int feature = 0; feature < taste::kAlgoFeatures; ++feature)
                bars[(std::size_t) feature].set (feature, bias.ready ? bias.algo[feature] : 1.0f);
        }
        GapFillAudioProcessor& proc;
        std::array<TasteBar, taste::kAlgoFeatures> bars;
        juce::Label counts, explanation;
        juce::TextButton rebuild, reset, clear;
    };

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
            profile.setButtonText ("PROFILE");
            profile.setTitle ("Open detailed taste profile");
            profile.setTooltip ("See all fifteen algorithm multipliers, explanations and counts, or rebuild/reset your local profile.");
            like.setTitle ("Like the latest fill");
            skip.setTitle ("Dislike the latest fill");
            undo.setTitle ("Undo the last taste rating");
            reset.setTitle ("Reset taste with confirmation");
            like.setTooltip ("Like the latest fill.");
            skip.setTooltip ("Skip this kind of fill: a dislike for the taste profile.");
            undo.setTooltip ("Undo the last rating.");
            reset.setTooltip ("Forget the taste profile of this project. Hold Alt to also clear your ratings library.");

            like.onClick = [this] { rateCommand (CommandType::LikeLatest); };
            skip.onClick = [this] { rateCommand (CommandType::SkipLatest); };
            undo.onClick = [this] { if (proc.getContextState().tasteService().undoLast()) notifyTasteChanged (proc); refresh(); };
            reset.onClick = [this] { confirmReset (proc, *this, juce::ModifierKeys::currentModifiers.isAltDown()); };
            profile.onClick = [this]
            {
                if (auto* parent = getTopLevelComponent())
                    juce::CallOutBox::launchAsynchronously (std::make_unique<TasteProfile> (proc),
                                                          parent->getLocalArea (&profile, profile.getLocalBounds()), parent);
            };

            for (auto* c : std::initializer_list<juce::Component*> { &learn, &strength, &like, &skip, &undo, &reset, &profile, &readout })
                addAndMakeVisible (c);

            readout.setJustificationType (juce::Justification::topLeft);
            readout.setFont (fonts::label (11.0f));
            readout.setColour (juce::Label::textColourId, colours::textMuted);
            setSize (musical::kStripWidth, 132);
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

            profile.setBounds (r.removeFromTop (28).removeFromRight (88));
            readout.setBounds (r);
        }

    private:
        void applyView() { musical::fit (*this, musical::advancedView (proc), 132); }

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
                taste::explain (c, 3, toward, away, service.getModelVersion());
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
        juce::TextButton like, skip, undo, reset, profile;
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
                "the ratings library). It learns which fill types and bands you prefer, not timing. PROFILE shows all fifteen multipliers with names and numbers, "
                "counts, explanations and Rebuild from history. Rebuild explicitly replaces the project profile from this computer's ratings library. "
                "Reset and Clear library ask before removing data. An explicit Skip is dislike; leaving a fill unrated supplies no signal. "
                "Options shows saved sound-model versions and offers an undoable upgrade for older projects.");
    });
}

} // namespace gapfill
