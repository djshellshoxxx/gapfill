#pragma once

// F-M22 session + view. SuggestSession is the message-thread state machine (consent, listen, rank);
// SuggestView is the strip shown inside the preset browser. Opt-in only: nothing is measured until the
// user presses Listen, nothing is stored, and loading a suggestion uses the validated preset path.

#include "../presetbrowser/GlobalSettings.h"
#include "Suggest.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace gapfill
{
class GapFillAudioProcessor;
}

namespace gapfill::suggest
{

inline constexpr const char* kConsentKey = "suggestConsent";

class SuggestSession
{
public:
    enum class Phase { Idle, Listening, Result, NoSignal };

    SuggestSession (GapFillAudioProcessor&, std::shared_ptr<settings::GlobalSettings>);
    ~SuggestSession();

    bool hasConsent() const;
    void setConsent (bool);

    /** False (and message set) without consent, when bypassed, or when not prepared for playback. */
    bool listen (float seconds = kDefaultSeconds);
    void cancel();
    bool tick();                                   ///< advances Listening -> Result / NoSignal; true when the phase changed

    Phase getPhase() const noexcept                { return phase; }
    float getProgress() const;
    float getSecondsListened() const;
    float getSecondsTotal() const noexcept         { return seconds; }
    const juce::String& getMessage() const noexcept { return message; }
    const InputProfile& getProfile() const noexcept { return profile; }
    int getNumSuggestions() const noexcept         { return count; }
    const Suggestion& getSuggestion (int i) const noexcept { return suggestions[i]; }

    /** Loads suggestion `i` through the validated preset path (selectPreset: one Undo step). */
    bool load (int i);

private:
    GapFillAudioProcessor& proc;
    std::shared_ptr<settings::GlobalSettings> settingsFile;
    Phase phase = Phase::Idle;
    float seconds = kDefaultSeconds;
    double rateAtStart = 0.0;
    juce::String message;
    InputProfile profile;
    Suggestion suggestions[3];
    int count = 0;
};

class SuggestView : public juce::Component,
                    private juce::Timer
{
public:
    SuggestView (GapFillAudioProcessor&, std::shared_ptr<settings::GlobalSettings>);
    ~SuggestView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    int getPreferredHeight() const;
    std::function<void()> onLayoutChanged;
    std::function<void (int presetIndex)> onLoaded;
    std::function<void()> onClose;

    SuggestSession& getSession() noexcept { return session; }

    // Test seams.
    void pressListen()      { listenButton.onClick(); }
    void pressCancel()      { cancelButton.onClick(); }
    void pressLoad (int i)  { loadButtons[i].onClick(); }
    void refreshForTests()  { timerCallback(); }
    juce::String getSummary() const { return summaryText; }
    juce::String getRowText (int i) const { return rowText[i]; }
    juce::TextButton& getListenButton() noexcept { return listenButton; }

private:
    void timerCallback() override;
    void refresh();

    GapFillAudioProcessor& proc;
    SuggestSession session;
    juce::TextButton listenButton { "LISTEN" }, cancelButton { "CANCEL" }, closeButton { "CLOSE" };
    juce::TextButton loadButtons[3] { juce::TextButton ("LOAD"), juce::TextButton ("LOAD"), juce::TextButton ("LOAD") };
    juce::ComboBox duration;
    juce::String summaryText, rowText[3];
    int lastHeight = 0;
    juce::Rectangle<int> summaryBounds, rowBounds[3];
};

} // namespace gapfill::suggest
