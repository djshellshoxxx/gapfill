#pragma once

// F-L17 Mutation Lab drawer. Shown from FILE > Mutation Lab... inside a call-out; closing it ends any
// preview (the destructor calls MutationLab::endPreview). The panel owns no state: everything lives in
// the processor's ui::UiState, so favourites and settings survive the editor.

#include "MutationLab.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace gapfill
{
class GapFillAudioProcessor;
}

namespace gapfill::mutation
{

class LabPanel : public juce::Component,
                 private juce::Timer
{
public:
    explicit LabPanel (GapFillAudioProcessor&);
    ~LabPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int preferredWidth = 580;
    static constexpr int preferredHeight = 400;

    /** "Density +18, Chaos -9": the three biggest differences of child c, or "" without children. */
    juce::String describeChild (int child) const;
    static juce::String geneDisplayName (int gene);

    // Test seams.
    MutationLab& getLab() noexcept { return lab; }
    void pressMutate()          { mutateButton.onClick(); }
    void pressRevert()          { revertButton.onClick(); }
    void pressPreview (int c)   { cards[c].preview.setToggleState (! cards[c].preview.getToggleState(), juce::dontSendNotification); cards[c].preview.onClick(); }
    void pressKeep (int c)      { cards[c].keep.onClick(); }
    void pressFav (int c)       { cards[c].fav.onClick(); }
    void refreshForTests()      { refresh(); }
    juce::String getCardTitle (int c) const { return cards[c].title; }

private:
    struct Card
    {
        juce::TextButton preview { "PREVIEW" }, keep { "KEEP" }, fav { "FAV" };
        juce::String title, detail;
        juce::Rectangle<int> bounds;
    };

    void timerCallback() override;
    void refresh();

    GapFillAudioProcessor& proc;
    MutationLab& lab;
    Card cards[kNumChildren];
    juce::TextButton mutateButton { "MUTATE" }, revertButton { "REVERT" }, reseedButton { "RESEED" }, breedButton { "BREED WITH..." };
    juce::Slider rateSlider;
    juce::ComboBox scopeBox;
    juce::Label rateLabel, parentLabel, pinsLabel;
    juce::Viewport pinViewport;
    juce::Component pinContent;
    juce::OwnedArray<juce::ToggleButton> pinToggles;
    juce::String parentText;
    bool refreshing = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LabPanel)
};

} // namespace gapfill::mutation
