#pragma once

// The Effects section (docs/dev/12 FX-0 GUI, docs/dev/11): foldable effect cards
// in permanent chain order. Registered in Slot::EffectsSection, so it shows up
// as soon as UI-0 (WP11) reserves that slot's rectangle in PluginEditor::resized.
//
//  - Each card has a header: enable LED (lit while Amount > 0), the card name and
//    a fold arrow. Clicking the LED switches the card off (remembering the
//    Amount) or on (the remembered Amount, 50 % the first time). Both go
//    through the parameter, so they are automatable and undoable like any edit.
//  - Controls of a card are hidden until it is enabled; the fold state persists
//    in the <Fx> state subtree (FxUiState).
//  - "Solo this effect" sets Fill Type to the card's carrier algorithm (Reverse
//    for Swell, Granular for Cloud).
//
// Message thread only; reads atomics (FxMeters, FxUiState), never audio memory.

#include "../Knob.h"
#include "../../PluginProcessor.h"
#include "../../features/fx/EffectCard.h"
#include "ScrubCard.h"

#include <memory>
#include <vector>

namespace gapfill::fxgui
{

struct ControlDef
{
    enum class Kind { Knob, Combo, Toggle, Button };   ///< Button: paramId names an action (see EffectCardView)

    Kind kind;
    const char* paramId;
    const char* label;
    const char* tooltip;
    bool advanced = false;      ///< shown under "More"
};

struct CardDef
{
    fx::CardId card;
    const char* title;
    const char* amountId;
    const char* blurb;
    int carrierAlgo;            ///< Algo index of the carrier algorithm, -1 = none
    std::vector<ControlDef> controls;
};

/** The static table the section is built from (also read by tests). */
const std::vector<CardDef>& cardDefinitions();

//==============================================================================
class EffectCardView : public juce::Component
{
public:
    EffectCardView (GapFillAudioProcessor&, const CardDef&);

    int getPreferredHeight (int width) const;
    void resized() override;
    void paint (juce::Graphics&) override;

    /** Re-reads Amount / fold state / meters (called from the section's timer). */
    void refresh();

    bool isEnabledCard() const;
    bool isFolded() const;
    void setFolded (bool shouldFold);
    void setEnabledCard (bool shouldEnable);
    const CardDef& getDefinition() const noexcept { return def; }
    juce::String getReadout() const { return readout.getText(); }

private:
    void toggleEnabled();
    void soloThisEffect();
    bool bodyVisible() const;
    void updateVisibility();

    GapFillAudioProcessor& proc;
    const CardDef& def;

    juce::TextButton led, fold, solo, more;
    std::unique_ptr<GapKnob> amountKnob;
    std::vector<std::unique_ptr<juce::Component>> controls;
    juce::Label readout;
    bool showMore = false;
    std::unique_ptr<juce::FileChooser> chooser;

    void runAction (const juce::String& action, juce::Button*);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectCardView)
};

//==============================================================================
class EffectsSection : public juce::Component,
                       private juce::Timer
{
public:
    explicit EffectsSection (GapFillAudioProcessor&);
    ~EffectsSection() override;

    void resized() override;
    void paint (juce::Graphics&) override;

    int getNumCards() const noexcept { return (int) cards.size(); }
    EffectCardView& getCard (int i) noexcept { return *cards[(size_t) i]; }
    ScrubCardView& getScrubCard() noexcept { return *scrub; }   ///< F-M19 Freeze & Scrub, the first card

private:
    void timerCallback() override;
    void layoutCards();

    GapFillAudioProcessor& proc;
    juce::Viewport viewport;
    juce::Component content;
    std::unique_ptr<ScrubCardView> scrub;
    std::vector<std::unique_ptr<EffectCardView>> cards;
    juce::Label routingLabel;
    std::vector<std::unique_ptr<GapToggle>> routing;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectsSection)
};

} // namespace gapfill::fxgui
