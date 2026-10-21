#pragma once

// U-6 / F-L05 GUI: the progress / result card. The LEARN button lives in the Detection panel header;
// this card appears over the visualiser while a calibration is running or has something to say.
// Message thread only; it polls the session from its own timer.

#include "Session.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace gapfill::calibrate
{

class CalibrateCard : public juce::Component,
                      private juce::Timer
{
public:
    explicit CalibrateCard (CalibrationSession&);
    ~CalibrateCard() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    /** LEARN pressed. */
    void begin();

    static constexpr int preferredWidth = 520;
    static constexpr int preferredHeight = 96;

    // Test seams.
    void pressApply()    { applyButton.onClick(); }
    void pressDiscard()  { discardButton.onClick(); }
    void pressRevert()   { revertButton.onClick(); }
    void pressCancel()   { cancelButton.onClick(); }
    void refreshForTests() { timerCallback(); }
    juce::String getShownText() const { return text; }

private:
    void timerCallback() override;
    void refresh();

    CalibrationSession& session;
    juce::TextButton cancelButton { "CANCEL" }, applyButton { "APPLY" }, discardButton { "DISCARD" },
                     revertButton { "REVERT" }, closeButton { "CLOSE" };
    juce::String text, lastAnnounced;
    Phase shownPhase = Phase::Idle;
    int idleMessageTicks = 0;
};

} // namespace gapfill::calibrate
