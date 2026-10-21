#pragma once

// U-7 / F-S09 first-run guide (docs/dev/features/F-S09-first-run-guide.md).
//
//  TourModel    pure logic: when to auto-show, step navigation, persistence via GlobalSettings
//  TourOverlay  full-editor overlay: scrim with cut-outs, a card with Back / Next / Skip /
//               Don't show again. Never modal, never blocks the message thread; mouse events
//               pass through only over the highlighted targets.
//
// Settings keys: `tourDoneVersion` (int, 0 = never finished), `tourSuppressForever` (bool).
// Nothing is stored in the plug-in state: a project must never carry the flag.

#include "../presetbrowser/GlobalSettings.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace gapfill::tour
{

inline constexpr int kTourVersion = 1;
inline constexpr const char* kKeyDoneVersion = "tourDoneVersion";
inline constexpr const char* kKeySuppress = "tourSuppressForever";

struct TourStep
{
    juce::String id, title, body;
    std::vector<juce::String> targetIds;      ///< see TourTargets; empty = centred card
    bool offersExample = false;               ///< shows "LOAD EXAMPLE" (applies a factory preset through selectPreset)
};

/** The three steps. Step 3 points at AUDITION when Fill Audition exists in this build,
    otherwise at UNDO and the preset arrows. */
std::vector<TourStep> defaultSteps (bool auditionAvailable);

/** Process-wide "a tour was already shown by some editor" flag (several instances in one project). */
bool& shownInProcess();

class TourModel
{
public:
    TourModel (settings::GlobalSettings&, int numSteps);

    /** Auto-show rules: settings readable AND writable (else the flag could not persist and the tour
        would loop), not suppressed, not finished at this version, not already shown in this process. */
    bool shouldAutoShow (bool alreadyShownInProcess) const;

    /** A later tour version after the user finished v1: a one-line hint instead of the overlay. */
    bool shouldShowWhatsNewHint() const;

    int numSteps() const noexcept   { return count; }
    int index() const noexcept      { return current; }
    bool isLast() const noexcept    { return current >= count - 1; }
    void start() noexcept           { current = 0; }
    void next() noexcept            { current = juce::jmin (count - 1, current + 1); }
    void back() noexcept            { current = juce::jmax (0, current - 1); }

    /** Skip / Esc: counts as finished for this version (does not suppress later versions). */
    void skip();
    /** Last step's Done. */
    void finish();
    void dontShowAgain();

private:
    settings::GlobalSettings& settingsFile;
    int count, current = 0;
};

//==============================================================================
class TourOverlay : public juce::Component
{
public:
    TourOverlay (std::vector<TourStep>, TourModel&);

    /** Finds the component for a target id (may return null: the card is then centred). */
    std::function<juce::Component* (const juce::String& id)> findTarget;
    std::function<void()> onLoadExample;
    std::function<void()> onClosed;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool hitTest (int x, int y) override;
    bool keyPressed (const juce::KeyPress&) override;
    void parentHierarchyChanged() override;

    int getStepIndex() const noexcept    { return model.index(); }
    const TourStep& getStep() const      { return steps[(size_t) model.index()]; }
    juce::Rectangle<int> getCardBounds() const noexcept    { return card.getBounds(); }
    std::vector<juce::Rectangle<int>> getHighlights() const;
    void setExampleStatus (const juce::String& text);

    // Test seams.
    void pressNext()       { nextButton.onClick(); }
    void pressSkip()       { skipButton.onClick(); }
    void pressDontShow()   { dontShowButton.onClick(); }
    juce::String getCardTitle() const    { return card.getTitle(); }

private:
    class Card : public juce::Component
    {
    public:
        void paint (juce::Graphics&) override;
    };

    void refresh();
    void close (int how);

    std::vector<TourStep> steps;
    TourModel& model;
    Card card;
    juce::Label title, body, status;
    juce::TextButton backButton { "BACK" }, nextButton { "NEXT" }, skipButton { "SKIP" },
                     dontShowButton { "DON'T SHOW AGAIN" }, exampleButton { "LOAD EXAMPLE" };
    bool closing = false;
};

} // namespace gapfill::tour
