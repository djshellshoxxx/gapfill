#pragma once

// F-M19 Freeze & Scrub controls as a foldable card of the Effects section
// (docs/dev/11 progressive disclosure: controls of a feature show only while it is on).
//
//  - Header: the SCRUB switch (parameter "scrubOn", automatable and MIDI-learnable) and, while
//    it is on, a fold arrow.
//  - Body (only while SCRUB is on and the card is unfolded): POSITION and WIDTH as compact bars
//    ("scrubPos", "scrubWidth").
//
// Owned by EffectsSection; the fold state is a UI convenience and not saved. Message thread only.

#include "../Knob.h"
#include "../../PluginProcessor.h"

namespace gapfill::fxgui
{

class ScrubCardView : public juce::Component,
                      private juce::Timer
{
public:
    explicit ScrubCardView (GapFillAudioProcessor&);
    ~ScrubCardView() override;

    static constexpr int headerHeight = 28;

    /** Height this card needs: the header alone while SCRUB is off or folded. */
    int getPreferredHeight() const;

    bool isScrubOn() const;
    bool isFolded() const noexcept       { return folded; }
    void setFolded (bool shouldFold);
    bool isBodyVisible() const noexcept  { return position.isVisible() && width.isVisible(); }

    /** Re-reads the switch (also driven by the card's own timer). */
    void refresh();

    void resized() override;
    void paint (juce::Graphics&) override;

    /** Called when getPreferredHeight() changed (the section re-stacks its cards). */
    std::function<void()> onHeightChanged;

private:
    void timerCallback() override { refresh(); }
    void updateVisibility();
    bool bodyWanted() const { return isScrubOn() && ! folded; }

    GapFillAudioProcessor& proc;
    GapToggle toggle;
    juce::TextButton fold;
    GapBar position, width;
    bool folded = false;
    bool lastBody = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScrubCardView)
};

} // namespace gapfill::fxgui
