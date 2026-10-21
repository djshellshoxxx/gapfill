#include "Gui.h"

#include "../../PluginProcessor.h"
#include "../../gui/HelpRegistry.h"
#include "../../gui/LookAndFeel.h"
#include "../presetbrowser/GlobalSettings.h"

namespace gapfill::calibrate
{

CalibrateCard::CalibrateCard (CalibrationSession& s) : session (s)
{
    setTitle ("Auto-Calibrate");
    setWantsKeyboardFocus (true);
    setVisible (false);

    for (auto* b : { &cancelButton, &applyButton, &discardButton, &revertButton, &closeButton })
    {
        b->setWantsKeyboardFocus (true);
        addChildComponent (*b);
    }

    cancelButton.setTitle ("Cancel calibration");
    applyButton.setTitle ("Apply the suggested settings");
    discardButton.setTitle ("Discard the suggestion");
    revertButton.setTitle ("Revert to the previous settings");
    closeButton.setTitle ("Close");

    cancelButton.onClick = [this] { session.cancel(); refresh(); };
    applyButton.onClick = [this] { session.apply(); refresh(); };
    discardButton.onClick = [this] { session.discard(); refresh(); };
    revertButton.onClick = [this] { session.revert(); refresh(); };
    closeButton.onClick = [this]
    {
        if (session.getPhase() == Phase::Applied)
            session.discard();

        idleMessageTicks = 0;
        setVisible (false);
    };

    startTimerHz (15);
}

CalibrateCard::~CalibrateCard()
{
    stopTimer();
}

void CalibrateCard::begin()
{
    if (! session.start (settings::GlobalSettings::shared()->getInt ("calibDurationSec", kDefaultSeconds)))
        idleMessageTicks = 90;             // show the refusal for about six seconds

    refresh();
    grabKeyboardFocus();
}

void CalibrateCard::timerCallback()
{
    session.tick();

    if (session.getPhase() == Phase::Result && settings::GlobalSettings::shared()->getBool ("calibAutoApply", false))
        session.apply();

    if (idleMessageTicks > 0 && session.getPhase() == Phase::Idle)
        --idleMessageTicks;

    refresh();
}

void CalibrateCard::refresh()
{
    const auto phase = session.getPhase();
    const bool showIdleMessage = phase == Phase::Idle && idleMessageTicks > 0 && session.getMessage().isNotEmpty();
    const bool show = phase != Phase::Idle || showIdleMessage;

    juce::String t = session.getMessage();

    if (phase == Phase::Recording)
        t = "Listening... " + juce::String (juce::roundToInt (session.getSecondsLeft())) + " s left. Play representative audio through GapFill.";
    else if (phase == Phase::Analysing)
        t = "Analysing... " + juce::String (juce::roundToInt (session.getProgress() * 100.0f)) + " %";

    if (phase == Phase::Result || phase == Phase::Applied)
        if (const auto warning = session.getSourceWarning(); warning.isNotEmpty())
            t << "  " << warning << ".";

    text = t;

    cancelButton.setVisible (phase == Phase::Recording || phase == Phase::Analysing);
    applyButton.setVisible (phase == Phase::Result);
    discardButton.setVisible (phase == Phase::Result);
    revertButton.setVisible (phase == Phase::Applied);
    revertButton.setEnabled (session.canRevert());
    revertButton.setTooltip (session.canRevert() ? "Restore the settings from before Auto-Calibrate"
                                                  : "Undo history moved on");
    closeButton.setVisible (phase == Phase::Applied || showIdleMessage);

    if (show != isVisible() || phase != shownPhase)
    {
        setVisible (show);
        shownPhase = phase;
        resized();

        if (phase == Phase::Result)
            applyButton.grabKeyboardFocus();                      // Apply is the default focus
    }

    if (show && text != lastAnnounced)
    {
        lastAnnounced = text;
        setDescription (text);

        // Announce state changes, not every countdown tick.
        if (phase != Phase::Recording && phase != Phase::Analysing)
            if (auto* handler = getAccessibilityHandler())
                handler->postAnnouncement (text, juce::AccessibilityHandler::AnnouncementPriority::medium);
    }

    repaint();
}

void CalibrateCard::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    GapFillLookAndFeel::drawShadow (g, b, metrics::panelRadius);
    g.setColour (colours::panel);
    g.fillRoundedRectangle (b, metrics::panelRadius);
    g.setColour (colours::accent);
    g.drawRoundedRectangle (b.reduced (0.5f), metrics::panelRadius, 1.5f);

    auto area = getLocalBounds().reduced (12, 8);
    auto bar = area.removeFromBottom (6);
    area.removeFromBottom (24);

    g.setColour (colours::text);
    g.setFont (fonts::ui (12.5f));
    g.drawFittedText (text, area, juce::Justification::topLeft, 3, 1.0f);

    const auto phase = session.getPhase();

    if (phase == Phase::Recording || phase == Phase::Analysing)
    {
        // A static, stepped bar (no pulsing): safe with Reduced Motion.
        g.setColour (colours::edge);
        g.fillRect (bar);
        g.setColour (colours::accent);
        g.fillRect (bar.withWidth (juce::roundToInt ((float) bar.getWidth() * session.getProgress())));
    }
}

void CalibrateCard::resized()
{
    auto b = getLocalBounds().reduced (12, 8);
    b.removeFromBottom (6);
    juce::ignoreUnused (b);

    // Apply is the rightmost; the rest stack to its left.
    int x = getWidth() - 12;

    for (auto* btn : { &applyButton, &revertButton, &discardButton, &closeButton, &cancelButton })
        if (btn->isVisible())
        {
            x -= 84;
            btn->setBounds (x, getHeight() - 8 - 6 - 24, 80, 24);
            x -= 4;
        }
}

bool CalibrateCard::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        if (cancelButton.isVisible())      session.cancel();
        else if (discardButton.isVisible()) session.discard();
        else                               closeButton.onClick();

        refresh();
        return true;
    }

    if (key == juce::KeyPress::returnKey && applyButton.isVisible())
    {
        applyButton.onClick();
        return true;
    }

    return false;
}

namespace
{
    const HelpRegistrar helpCalibrate (1140, "Auto-Calibrate", [] (HelpWriter& w)
    {
        w.body ("LEARN (in the Detection panel header) listens to your track for about 8 seconds and suggests Sensitivity, Gap Threshold and "
                "Min Gap that suit it. Play representative, rhythmic material through GapFill while it listens. You always see the suggestion first: "
                "Apply sets the three values (one Undo step), Discard changes nothing, and Revert restores the old values.");
        w.body ("It needs real playback (not an offline render) and a signal above -60 dBFS for a quarter of the time. Pads and other material "
                "without clear hits are reported as such and left alone. With a Sidechain or Hybrid detector source it calibrates from the main input only. "
                "Only the three detector values are touched: protection, fill, creative and mix settings stay exactly as they are.");
    });
}

} // namespace gapfill::calibrate
