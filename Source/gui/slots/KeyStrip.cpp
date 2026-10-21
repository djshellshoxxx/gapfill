// F-X03 Automatic key detection controls: AUTO KEY and LOCK KEY switches and a text
// readout ("Auto: A minor, 87 %" / "Auto: not detected - using manual C major").
// Registered in Slot::PerformanceTab; the slot stays empty until UI-0 reserves it.

#include "../Knob.h"
#include "../HelpRegistry.h"
#include "../SlotHost.h"
#include "MusicalStrip.h"
#include "../../PluginProcessor.h"
#include "../../features/keydetect/AutoKey.h"
#include "../../features/scale/ScaleLock.h"

namespace gapfill
{

namespace
{
    class KeyStrip : public juce::Component,
                     private juce::Timer
    {
    public:
        explicit KeyStrip (GapFillAudioProcessor& p)
            : proc (p),
              autoKey (p.getValueTreeState(), "scaleFollow", "AUTO KEY",
                       "Listen to the music and use its key for Scale Lock instead of the Key and Scale you set by hand. "
                       "Drums and noise give no key; then your manual setting is used."),
              lockKey (p.getValueTreeState(), "fx03_hold", "LOCK KEY",
                       "Freeze the detected key and save it with the project, so renders are reproducible."),
              window (p.getValueTreeState(), "fx03_window", "Window",
                      "How many seconds of music the key is estimated from. Longer is steadier, shorter follows key changes sooner.",
                      colours::secondary),
              minConf (p.getValueTreeState(), "fx03_minConf", "Min conf",
                       "How sure the detector must be before Scale Lock follows the detected key. Below this your manual key is used.",
                       colours::secondary)
        {
            setTitle ("Automatic key");
            addAndMakeVisible (autoKey);
            addAndMakeVisible (lockKey);
            addAndMakeVisible (window);
            addAndMakeVisible (minConf);
            readout.setJustificationType (juce::Justification::centredLeft);
            readout.setFont (fonts::label (11.0f));
            readout.setColour (juce::Label::textColourId, colours::textMuted);
            addAndMakeVisible (readout);
            setSize (musical::kStripWidth, 64);
            applyView();
            refresh();
            startTimerHz (4);
        }

        ~KeyStrip() override { stopTimer(); }

        void resized() override
        {
            auto r = getLocalBounds();
            autoKey.setBounds (r.removeFromLeft (juce::jmax (84, r.getWidth() / 5)).withHeight (28));
            r.removeFromLeft (8);
            lockKey.setBounds (r.removeFromLeft (juce::jmax (84, r.getWidth() / 5)).withHeight (28));
            r.removeFromLeft (8);
            window.setBounds (r.removeFromLeft (juce::jmax (56, r.getWidth() / 6)));
            r.removeFromLeft (8);
            minConf.setBounds (r.removeFromLeft (juce::jmax (56, r.getWidth() / 5)));
            r.removeFromLeft (8);
            readout.setBounds (r);
        }

    private:
        void applyView() { musical::fit (*this, musical::advancedView (proc), 64); }

        void timerCallback() override { applyView(); refresh(); }

        int raw (const char* id) const
        {
            if (auto* v = proc.getValueTreeState().getRawParameterValue (id))
                return (int) std::lround (v->load());

            return 0;
        }

        void refresh()
        {
            juce::String text;
            const auto manual = scale::makeScaleSet (raw ("scaleRoot"), raw ("scaleType"));
            char manualLabel[48] {};
            scale::describeShort (manual, manualLabel, (int) sizeof (manualLabel));

            if (raw ("scaleFollow") == 0)
            {
                text = manual.active() ? "Key: " + juce::String (manualLabel) + " (manual)" : "Key: not set";
            }
            else
            {
                const auto& ak = proc.getContextState().autoKey();
                const auto e = raw ("fx03_hold") != 0 && ak.heldKey().valid ? ak.heldKey() : ak.estimate();

                if (e.valid && e.tonic >= 0)
                    text = juce::String (raw ("fx03_hold") != 0 ? "Locked: " : "Auto: ") + scale::rootName (e.tonic) + (e.mode == 0 ? " major, " : " minor, ")
                             + juce::String ((int) e.confidence) + " %";
                else
                    text = "Auto: not detected" + (manual.active() ? " - using manual " + juce::String (manualLabel) : juce::String());
            }

            if (readout.getText() != text)
            {
                readout.setText (text, juce::dontSendNotification);
                readout.setTitle (text);
            }
        }

        GapFillAudioProcessor& proc;
        GapToggle autoKey, lockKey;
        GapKnob window, minConf;
        juce::Label readout;
    };

    const SlotRegistrar keyStrip ({ Slot::PerformanceTab, 420,
        [] (GapFillAudioProcessor& proc) -> std::unique_ptr<juce::Component>
        {
            return std::make_unique<KeyStrip> (proc);
        } });

    const HelpRegistrar helpKey (6040, "Key detection", [] (HelpWriter& w)
    {
        w.body ("AUTO KEY listens to the incoming music and estimates its key, for example A minor, with a confidence. While it is confident, "
                "Scale Lock uses that key instead of the Key and Scale you chose by hand. Drums, noise and silence give no key: the readout says "
                "\"not detected\" and your manual setting is used. It never guesses.");
        w.body ("Auto key adapts to what has played, so a render can differ if you start from the middle of a song. Switch LOCK KEY on to freeze the "
                "detected key and save it with the project when you need identical renders.");
    });
}

} // namespace gapfill
