#include "Gui.h"

#include "../../PluginProcessor.h"
#include "../../gui/HelpRegistry.h"
#include "../../gui/LookAndFeel.h"
#include "../presetbrowser/PresetCatalog.h"

namespace gapfill::suggest
{

SuggestSession::SuggestSession (GapFillAudioProcessor& p, std::shared_ptr<settings::GlobalSettings> s)
    : proc (p), settingsFile (std::move (s)) {}

SuggestSession::~SuggestSession()
{
    cancel();
}

bool SuggestSession::hasConsent() const
{
    return settingsFile->getBool (kConsentKey, false);
}

void SuggestSession::setConsent (bool on)
{
    settingsFile->setBool (kConsentKey, on);
}

bool SuggestSession::listen (float s)
{
    if (! hasConsent())
    {
        message = "Please allow GapFill to listen first.";
        return false;
    }

    if (proc.getSampleRate() <= 0.0)
    {
        message = "Suggesting presets needs playback - start your host's transport and try again.";
        return false;
    }

    if (proc.getValueTreeState().getRawParameterValue ("bypass")->load() > 0.5f)
    {
        message = "GapFill is bypassed - switch bypass off to listen.";
        return false;
    }

    seconds = juce::jlimit (kMinSeconds, kMaxSeconds, s);
    rateAtStart = proc.getSampleRate();
    auto& acc = proc.getUiState().profile;
    acc.prepare (rateAtStart);
    acc.startListening (seconds);
    phase = Phase::Listening;
    count = 0;
    message = "Listening...";
    return true;
}

void SuggestSession::cancel()
{
    if (phase == Phase::Listening)
    {
        proc.getUiState().profile.cancel();
        phase = Phase::Idle;
        message = "Cancelled";
    }
}

float SuggestSession::getProgress() const
{
    return phase == Phase::Listening ? proc.getUiState().profile.getProgress() : (phase == Phase::Idle ? 0.0f : 1.0f);
}

float SuggestSession::getSecondsListened() const
{
    return getProgress() * seconds;
}

bool SuggestSession::tick()
{
    if (phase != Phase::Listening)
        return false;

    auto& acc = proc.getUiState().profile;

    // Bypass, a sample-rate change or a stopped host abandon the run.
    if (proc.getSampleRate() != rateAtStart || proc.getValueTreeState().getRawParameterValue ("bypass")->load() > 0.5f)
    {
        cancel();
        message = "The run was abandoned (bypass or a sample-rate change).";
        return true;
    }

    const auto state = acc.getState();

    if (state == ProfileAccumulator::State::Done)
    {
        acc.takeProfile (profile);
        count = rankPresets (profile, suggestions);
        phase = Phase::Result;
        message = measuredSummary (profile);
        return true;
    }

    if (state == ProfileAccumulator::State::NoSignal)
    {
        acc.takeProfile (profile);
        phase = Phase::NoSignal;
        message = "Not enough signal - play something and try again.";
        return true;
    }

    if (state == ProfileAccumulator::State::Idle)         // someone cancelled underneath us
    {
        phase = Phase::Idle;
        return true;
    }

    return false;
}

bool SuggestSession::load (int i)
{
    if (i < 0 || i >= count)
        return false;

    juce::String error;
    presets::PresetEntry entry;
    entry.origin = presets::PresetEntry::Origin::Factory;
    entry.factoryIndex = suggestions[i].presetIndex;

    if (entry.factoryIndex < 0 || entry.factoryIndex >= proc.getNumPrograms())
        return false;

    proc.selectPreset (entry.factoryIndex);              // one Undo step, host program change
    return true;
}

//==============================================================================
SuggestView::SuggestView (GapFillAudioProcessor& p, std::shared_ptr<settings::GlobalSettings> s)
    : proc (p), session (p, std::move (s))
{
    setTitle ("Suggest presets");

    listenButton.setTooltip ("Listen to your input for a few seconds and recommend presets that suit it. Nothing is recorded or sent.");
    listenButton.setTitle ("Listen");
    listenButton.onClick = [this]
    {
        session.setConsent (true);
        session.listen ((float) duration.getText().getIntValue());
        refresh();
    };

    cancelButton.setTitle ("Cancel listening");
    cancelButton.onClick = [this] { session.cancel(); refresh(); };
    closeButton.setTitle ("Close suggestions");
    closeButton.onClick = [this] { session.cancel(); if (onClose != nullptr) onClose(); };

    for (int i = 0; i < 3; ++i)
    {
        loadButtons[i].setTooltip ("Load this preset (you can undo).");
        loadButtons[i].onClick = [this, i]
        {
            if (session.load (i) && onLoaded != nullptr)
                onLoaded (session.getSuggestion (i).presetIndex);
        };
        addChildComponent (loadButtons[i]);
    }

    duration.addItemList ({ "4", "10", "20", "30" }, 1);
    duration.setSelectedId (2, juce::dontSendNotification);
    duration.setTitle ("Listening time in seconds");
    addChildComponent (duration);

    for (auto* b : { &listenButton, &cancelButton, &closeButton })
        addChildComponent (*b);

    refresh();
    startTimerHz (10);
}

SuggestView::~SuggestView()
{
    stopTimer();
}

void SuggestView::timerCallback()
{
    session.tick();
    refresh();
}

int SuggestView::getPreferredHeight() const
{
    const auto phase = session.getPhase();

    if (phase == SuggestSession::Phase::Result)
        return 30 + juce::jmax (1, session.getNumSuggestions()) * 24 + 18;

    return session.hasConsent() ? 32 : 46;
}

void SuggestView::refresh()
{
    using P = SuggestSession::Phase;
    const auto phase = session.getPhase();

    if (phase == P::Listening)
        summaryText = "Listening " + juce::String (juce::roundToInt (session.getSecondsListened())) + " of "
                    + juce::String (juce::roundToInt (session.getSecondsTotal())) + " s";
    else if (phase == P::Result)
        summaryText = "Measured: " + session.getMessage() + " (with your current detector settings)";
    else if (phase == P::NoSignal)
        summaryText = session.getMessage();
    else if (! session.hasConsent())
        summaryText = "GapFill will listen to your input for about 10 seconds on this computer. Nothing is recorded or sent anywhere.";
    else
        summaryText = session.getMessage().isNotEmpty() ? session.getMessage() : juce::String ("Press LISTEN while your track plays.");

    for (int i = 0; i < 3; ++i)
    {
        const bool show = phase == P::Result && i < session.getNumSuggestions();
        loadButtons[i].setVisible (show);

        if (show)
        {
            const auto& s = session.getSuggestion (i);
            rowText[i] = juce::String (proc.getProgramName (s.presetIndex)) + "  -  " + confidenceWord (s.confidence)
                       + "  -  " + describeReason (session.getProfile(), s.reason);
            loadButtons[i].setTitle ("Load " + juce::String (proc.getProgramName (s.presetIndex)) + ", "
                                     + confidenceWord (s.confidence) + ", " + describeReason (session.getProfile(), s.reason));
        }
        else
        {
            rowText[i] = {};
        }
    }

    listenButton.setVisible (phase != P::Listening);
    duration.setVisible (phase != P::Listening && session.hasConsent());
    cancelButton.setVisible (phase == P::Listening);
    closeButton.setVisible (true);

    const auto h = getPreferredHeight();

    if (h != lastHeight)
    {
        lastHeight = h;

        if (onLayoutChanged != nullptr)
            onLayoutChanged();
    }

    resized();
    repaint();
}

void SuggestView::resized()
{
    auto b = getLocalBounds().reduced (6, 3);
    auto top = b.removeFromTop (session.hasConsent() ? 24 : 38);
    closeButton.setBounds (top.removeFromRight (56).withHeight (22));
    top.removeFromRight (4);

    if (cancelButton.isVisible())
        cancelButton.setBounds (top.removeFromRight (64).withHeight (22));

    if (listenButton.isVisible())
    {
        listenButton.setBounds (top.removeFromRight (64).withHeight (22));
        top.removeFromRight (4);

        if (duration.isVisible())
            duration.setBounds (top.removeFromRight (52).withHeight (22));
    }

    summaryBounds = top;

    int y = b.getY() + 4;

    for (int i = 0; i < 3; ++i)
    {
        rowBounds[i] = juce::Rectangle<int> (b.getX(), y, b.getWidth(), 22);

        if (loadButtons[i].isVisible())
            loadButtons[i].setBounds (rowBounds[i].removeFromRight (56));

        y += 24;
    }
}

void SuggestView::paint (juce::Graphics& g)
{
    g.setColour (colours::accent.withAlpha (0.12f));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 3.0f);

    g.setColour (colours::text);
    g.setFont (fonts::ui (11.5f));
    g.drawFittedText (summaryText, summaryBounds, juce::Justification::centredLeft, 2, 1.0f);

    if (session.getPhase() == SuggestSession::Phase::Listening)
    {
        // Whole-step bar, no easing (Reduced Motion safe).
        auto bar = getLocalBounds().reduced (6, 0).removeFromBottom (5).toFloat();
        g.setColour (colours::edge);
        g.fillRect (bar);
        g.setColour (colours::accent);
        g.fillRect (bar.withWidth (bar.getWidth() * session.getProgress()));
    }

    g.setFont (fonts::ui (11.5f));

    for (int i = 0; i < 3; ++i)
        if (rowText[i].isNotEmpty())
        {
            g.setColour (colours::text);
            g.drawText (rowText[i], rowBounds[i].withTrimmedRight (60), juce::Justification::centredLeft, true);
        }
}

namespace
{
    const HelpRegistrar helpSuggest (1220, "Smart preset suggestions", [] (HelpWriter& w)
    {
        w.body ("In the preset browser, SUGGEST... listens to your input for 4 to 30 seconds and recommends up to three factory presets that "
                "suit it. It measures only a few simple things with GapFill's own detector: hits per second, how much of the time is gap, the level, "
                "and the bass and treble share. It never changes your settings, never loads anything by itself, and does not record, store or send any audio: "
                "everything stays on this computer and is discarded when you close the browser.");
        w.body ("Suggestions use your current detector settings; they are more meaningful after Auto-Calibrate. A Weak match means the material "
                "is unlike any preset's typical input.");
    });
}

} // namespace gapfill::suggest
