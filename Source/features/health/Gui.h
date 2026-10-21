#pragma once

// N-USE-b footer + detail view (docs/dev/12 N-USE-b), with the N-EASY fix-it chip,
// the E-3 last-gap line, the F-S01 latency / CPU readout and the F-D02
// diagnostics drawer. One footer component shows ONLY the highest-priority
// message: health > fix-it > last gap outcome. Nothing is shown when healthy and
// no gap has been evaluated (no "all OK" ticks).

#include "../../gui/LookAndFeel.h"
#include "../reasons/Reasons.h"
#include "Health.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace gapfill
{
class GapFillAudioProcessor;
}

namespace gapfill::health
{

/** The detail view: all active codes (muted ones flagged), the suggestion with
    Apply / Don't suggest, the "Why so few fills?" list, latency + estimated CPU
    and the diagnostics drawer. Opened from the footer in a call-out. */
class DetailsPanel : public juce::Component,
                     private juce::Timer
{
public:
    explicit DetailsPanel (GapFillAudioProcessor&);
    ~DetailsPanel() override;

    void resized() override;
    void paint (juce::Graphics&) override;

    /** Plain-text version of everything shown, for tests and screen readers. */
    juce::String getPlainText() const;

private:
    class Rows;
    void timerCallback() override;
    void rebuild();
    juce::String contentSignature() const;

    GapFillAudioProcessor& proc;
    juce::Viewport viewport;
    std::unique_ptr<Rows> rows;
    juce::String signature;
    std::shared_ptr<juce::FileChooser> chooser;

    friend class Rows;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DetailsPanel)
};

/** What the footer decided to show (exposed so tests need no pixels). */
struct FooterView
{
    FooterKind kind = FooterKind::Nothing;
    juce::String text;
    bool healthIsCritical = false;
};

FooterView computeFooterView (GapFillAudioProcessor&);

class FooterStatusComponent : public juce::Component,
                              private juce::Timer
{
public:
    explicit FooterStatusComponent (GapFillAudioProcessor&);
    ~FooterStatusComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool hitTest (int x, int y) override;
    void mouseUp (const juce::MouseEvent&) override;

    const FooterView& getView() const noexcept   { return view; }
    void refreshForTests()                       { update (true); }

    /** Width on the right that stays free for the editor's version marker. */
    static constexpr int reservedRight = 96;

private:
    void timerCallback() override;
    void update (bool force);
    void showDetails();

    GapFillAudioProcessor& proc;
    juce::TextButton statusButton { "STATUS" }, applyButton { "APPLY" }, dismissButton { "X" }, undoButton { "UNDO" };
    FooterView view;
    reasons::StatusRateLimiter limiter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FooterStatusComponent)
};

} // namespace gapfill::health
