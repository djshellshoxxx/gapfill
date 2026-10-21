#include "Gui.h"

#include "../../PluginProcessor.h"
#include "../../gui/HelpRegistry.h"
#include "../../gui/LookAndFeel.h"
#include "../info/ParamInfo.h"

namespace gapfill::mutation
{

juce::String LabPanel::geneDisplayName (int gene)
{
    if (gene < 0 || gene >= kNumGenes)
        return {};

    if (const auto* info = info::ParamInfoRegistry::get().find (geneIds()[(size_t) gene]))
        return info->shortName;

    return geneIds()[(size_t) gene];
}

LabPanel::LabPanel (GapFillAudioProcessor& p) : proc (p), lab (p.getUiState().lab)
{
    setTitle ("Mutation Lab");
    setSize (preferredWidth, preferredHeight);
    lab.attach (proc.getValueTreeState(), [this] { proc.pushUndoSnapshot(); });

    mutateButton.setTooltip ("Make four variations of the current sound. Nothing changes until you press Keep.");
    revertButton.setTooltip ("Step back one generation and restore that parent (one Undo step).");
    reseedButton.setTooltip ("Draw a new random seed. The same seed and generation always give the same variations.");
    breedButton.setTooltip ("Cross the current sound with a saved favourite to make the next four variations.");

    mutateButton.onClick = [this] { lab.mutate(); refresh(); };
    revertButton.onClick = [this] { lab.revert(); refresh(); };
    reseedButton.onClick = [this] { lab.reseed(); refresh(); };
    breedButton.onClick = [this]
    {
        juce::PopupMenu menu;

        for (int i = 0; i < (int) lab.favourites.size(); ++i)
            menu.addItem (i + 1, lab.favourites[(size_t) i].name);

        if (lab.favourites.empty())
            menu.addItem (1000, "No favourites yet - press FAV on a variation", false);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&breedButton),
                            [safe = juce::Component::SafePointer<LabPanel> (this)] (int r)
                            {
                                if (safe != nullptr && r > 0 && r < 1000)
                                {
                                    safe->lab.breedWith (r - 1);
                                    safe->refresh();
                                }
                            });
    };

    for (auto* b : { &mutateButton, &revertButton, &reseedButton, &breedButton })
        addAndMakeVisible (*b);

    rateSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    rateSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 18);
    rateSlider.setRange (kMinRate * 100.0, kMaxRate * 100.0, 1.0);
    rateSlider.setTextValueSuffix (" %");
    rateSlider.setValue (lab.rate * 100.0, juce::dontSendNotification);
    rateSlider.setTitle ("Mutation rate");
    rateSlider.setTooltip ("How far each variation may move from the current sound: 5 to 50 percent of each control's range.");
    rateSlider.onValueChange = [this] { lab.setRate ((float) rateSlider.getValue() * 0.01f); };
    addAndMakeVisible (rateSlider);

    rateLabel.setText ("Rate", juce::dontSendNotification);
    rateLabel.setColour (juce::Label::textColourId, colours::textMuted);
    addAndMakeVisible (rateLabel);

    scopeBox.addItemList ({ "Character", "Character + Weights", "All" }, 1);
    scopeBox.setSelectedId (lab.scope + 1, juce::dontSendNotification);
    scopeBox.setTitle ("Scope");
    scopeBox.setTooltip ("Which controls may change: the five macros, plus the 15 algorithm weights, plus the spectral lanes.");
    scopeBox.onChange = [this] { lab.setScope (scopeBox.getSelectedId() - 1); };
    addAndMakeVisible (scopeBox);

    parentLabel.setColour (juce::Label::textColourId, colours::text);
    parentLabel.setFont (fonts::ui (12.0f));
    addAndMakeVisible (parentLabel);

    pinsLabel.setText ("Pinned controls never change:", juce::dontSendNotification);
    pinsLabel.setColour (juce::Label::textColourId, colours::textMuted);
    pinsLabel.setFont (fonts::ui (11.0f));
    addAndMakeVisible (pinsLabel);

    for (int i = 0; i < kNumGenes; ++i)
    {
        auto* t = pinToggles.add (new juce::ToggleButton (geneDisplayName (i)));
        t->setToggleState (lab.isPinned (i), juce::dontSendNotification);
        t->setTitle ("Pin " + geneDisplayName (i));
        t->setTooltip ("Pinned controls are copied unchanged into every variation.");
        t->onClick = [this, i, t] { lab.setPinned (i, t->getToggleState()); };
        pinContent.addAndMakeVisible (*t);
    }

    pinViewport.setViewedComponent (&pinContent, false);
    addAndMakeVisible (pinViewport);

    for (int c = 0; c < kNumChildren; ++c)
    {
        auto& card = cards[c];
        card.preview.setClickingTogglesState (true);
        card.preview.setTooltip ("Hear this variation now. Your knobs and automation are not touched.");
        card.keep.setTooltip ("Make this variation the new sound (one Undo step).");
        card.fav.setTooltip ("Save this variation as a favourite (up to 8).");

        card.preview.onClick = [this, c]
        {
            if (cards[c].preview.getToggleState())
                lab.preview (c);
            else
                lab.endPreview();

            refresh();
        };
        card.keep.onClick = [this, c]
        {
            lab.keep (c);
            refresh();

            if (auto* handler = getAccessibilityHandler())
                handler->postAnnouncement ("Kept variation " + juce::String::charToString ((juce::juce_wchar) ('A' + c)),
                                           juce::AccessibilityHandler::AnnouncementPriority::medium);
        };
        card.fav.onClick = [this, c] { lab.addFavourite (c); refresh(); };

        addAndMakeVisible (card.preview);
        addAndMakeVisible (card.keep);
        addAndMakeVisible (card.fav);
    }

    refresh();
    startTimerHz (15);
}

LabPanel::~LabPanel()
{
    stopTimer();
    lab.endPreview();                      // closing the drawer always ends the preview
}

juce::String LabPanel::describeChild (int child) const
{
    const auto* gen = lab.currentGeneration();

    if (gen == nullptr || child < 0 || child >= kNumChildren)
        return {};

    GeneDiff diffs[3];
    const auto n = biggestDifferences (gen->parent, gen->children.child[(size_t) child], diffs, 3);
    juce::StringArray parts;

    for (int i = 0; i < n; ++i)
    {
        const auto pct = juce::roundToInt (diffs[i].delta * 100.0f);
        parts.add (geneDisplayName (diffs[i].gene) + (pct >= 0 ? " +" : " ") + juce::String (pct));
    }

    return parts.joinIntoString (", ");
}

void LabPanel::timerCallback()
{
    lab.tick();

    // tick() may have ended the preview (something else changed the sound).
    if (lab.previewing() < 0)
        for (auto& card : cards)
            card.preview.setToggleState (false, juce::dontSendNotification);

    repaint();
}

void LabPanel::refresh()
{
    if (refreshing)
        return;

    const juce::ScopedValueSetter<bool> guard (refreshing, true);
    const auto* gen = lab.currentGeneration();

    parentText = gen != nullptr ? "Parent: your current sound  |  generation " + juce::String (gen->index)
                                        + "  |  " + juce::String (lab.numGenerations()) + " kept in history"
                                : juce::String ("Parent: your current sound. Press MUTATE to make four variations.");
    parentLabel.setText (parentText, juce::dontSendNotification);

    for (int c = 0; c < kNumChildren; ++c)
    {
        auto& card = cards[c];
        const bool have = gen != nullptr;
        const auto letter = juce::String::charToString ((juce::juce_wchar) ('A' + c));
        card.detail = have ? describeChild (c) : juce::String();
        card.title = "Variation " + letter + (have ? ", differs in " + card.detail : juce::String());
        card.preview.setEnabled (have);
        card.keep.setEnabled (have);
        card.fav.setEnabled (have && (int) lab.favourites.size() < kMaxFavourites);
        card.preview.setToggleState (lab.previewing() == c, juce::dontSendNotification);
        card.preview.setTitle ("Preview variation " + letter);
        card.keep.setTitle ("Keep variation " + letter);
        card.fav.setTitle ("Favourite variation " + letter);
    }

    revertButton.setEnabled (lab.numGenerations() > 0);
    breedButton.setEnabled (! lab.favourites.empty() && lab.isAttached());
    repaint();
}

void LabPanel::resized()
{
    auto b = getLocalBounds().reduced (12);
    b.removeFromTop (20);                                   // title painted
    parentLabel.setBounds (b.removeFromTop (22));
    b.removeFromTop (4);

    auto controls = b.removeFromTop (26);
    mutateButton.setBounds (controls.removeFromLeft (84));
    controls.removeFromLeft (4);
    revertButton.setBounds (controls.removeFromLeft (70));
    controls.removeFromLeft (4);
    breedButton.setBounds (controls.removeFromLeft (110));
    controls.removeFromLeft (4);
    reseedButton.setBounds (controls.removeFromLeft (70));

    b.removeFromTop (6);
    auto settingsRow = b.removeFromTop (26);
    rateLabel.setBounds (settingsRow.removeFromLeft (40));
    rateSlider.setBounds (settingsRow.removeFromLeft (220));
    settingsRow.removeFromLeft (10);
    scopeBox.setBounds (settingsRow.removeFromLeft (170));

    b.removeFromTop (8);
    auto cardRow = b.removeFromTop (130);
    const auto cardW = (cardRow.getWidth() - 3 * 8) / 4;

    for (int c = 0; c < kNumChildren; ++c)
    {
        auto cell = cardRow.removeFromLeft (cardW);
        cardRow.removeFromLeft (8);
        cards[c].bounds = cell;
        auto buttons = cell.removeFromBottom (24).reduced (4, 0);
        const auto w = buttons.getWidth() / 3;
        cards[c].preview.setBounds (buttons.removeFromLeft (w + 12).reduced (1));
        cards[c].keep.setBounds (buttons.removeFromLeft (w - 2).reduced (1));
        cards[c].fav.setBounds (buttons.reduced (1));
    }

    b.removeFromTop (8);
    pinsLabel.setBounds (b.removeFromTop (16));
    pinViewport.setBounds (b);

    if (pinToggles.size() != kNumGenes)
        return;                                            // resized() runs from setSize() before the toggles exist

    constexpr int cols = 4;
    const auto colW = (pinViewport.getMaximumVisibleWidth() > 0 ? pinViewport.getMaximumVisibleWidth() : b.getWidth() - 10) / cols;

    for (int i = 0; i < kNumGenes; ++i)
        pinToggles[i]->setBounds ((i % cols) * colW, (i / cols) * 22, colW, 22);

    pinContent.setSize (colW * cols, ((kNumGenes + cols - 1) / cols) * 22);
}

void LabPanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
    g.setColour (colours::edge);
    g.drawRect (getLocalBounds(), 1);

    g.setColour (colours::text);
    g.setFont (fonts::sectionHeader (12.0f));
    g.drawText ("MUTATION LAB", 12, 10, 200, 16, juce::Justification::centredLeft);

    for (int c = 0; c < kNumChildren; ++c)
    {
        const auto& card = cards[c];
        auto r = card.bounds.toFloat();
        const bool previewing = lab.previewing() == c;

        g.setColour (previewing ? colours::accent.withAlpha (0.18f) : colours::background);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (previewing ? colours::accent : colours::edge);
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, previewing ? 2.0f : 1.0f);

        g.setColour (colours::text);
        g.setFont (fonts::ui (16.0f, true));
        g.drawText (juce::String::charToString ((juce::juce_wchar) ('A' + c)) + (previewing ? "  (hearing)" : ""),
                    card.bounds.reduced (8, 4).removeFromTop (22), juce::Justification::centredLeft);

        g.setColour (colours::textMuted);
        g.setFont (fonts::ui (11.0f));
        g.drawFittedText (card.detail.isEmpty() ? juce::String ("-") : card.detail, card.bounds.reduced (8, 4).withTrimmedTop (26).withTrimmedBottom (28),
                          juce::Justification::topLeft, 4, 1.0f);
    }
}

namespace
{
    const HelpRegistrar helpLab (1170, "Mutation Lab", [] (HelpWriter& w)
    {
        w.body ("FILE > Mutation Lab... opens a drawer for exploring variations of your current sound. MUTATE makes four children (A-D) by "
                "nudging the creative controls with seeded random noise: the same seed and generation always give the same children. "
                "PREVIEW plays a child through the engine without moving a single knob or touching host automation. KEEP writes it to the "
                "controls as one Undo step. REVERT steps back one generation. FAV saves a child (up to 8); BREED WITH... crosses the current "
                "sound with a favourite. Pinned controls are never changed. Closing the drawer ends a preview.");
    });
}

} // namespace gapfill::mutation
