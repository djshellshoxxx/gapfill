// U-6 / F-L05 Options rows: "Calibration length" (4-15 s) and "Apply the result automatically" (default off).
// Both are per-user preferences in the global settings file (keys calibDurationSec / calibAutoApply, read by
// CalibrateCard); they are never saved in a project and are not parameters.
// Registered in Slot::OptionsExtra (a Stack slot inside the scrolling Options area).

#include "RecordTap.h"
#include "../presetbrowser/GlobalSettings.h"
#include "../../PluginProcessor.h"
#include "../../gui/HelpRegistry.h"
#include "../../gui/LookAndFeel.h"
#include "../../gui/SlotHost.h"

namespace gapfill::calibrate
{

namespace
{
    constexpr int kMinSeconds = 4;
    constexpr int kMaxSeconds = 15;

    class CalibrateOptions : public juce::Component
    {
    public:
        explicit CalibrateOptions (GapFillAudioProcessor&)
            : settings (settings::GlobalSettings::shared())
        {
            setTitle ("Auto-Calibrate options");

            label.setText ("Auto-Calibrate listening time", juce::dontSendNotification);
            label.setFont (fonts::label (10.0f));
            label.setColour (juce::Label::textColourId, colours::textMuted);
            addAndMakeVisible (label);

            seconds.setSliderStyle (juce::Slider::LinearHorizontal);
            seconds.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
            seconds.setRange (kMinSeconds, kMaxSeconds, 1.0);
            seconds.setTextValueSuffix (" s");
            seconds.setTitle ("Auto-Calibrate listening time");
            seconds.setTooltip ("How long LEARN listens to the input before it suggests Sensitivity, Gap Threshold and Min Gap (4 to 15 seconds).");
            seconds.setValue (juce::jlimit (kMinSeconds, kMaxSeconds, settings->getInt ("calibDurationSec", kDefaultSeconds)),
                              juce::dontSendNotification);
            seconds.onValueChange = [this] { settings->setInt ("calibDurationSec", (int) seconds.getValue()); };
            addAndMakeVisible (seconds);

            autoApply.setButtonText ("Apply the result automatically");
            autoApply.setTitle ("Apply the Auto-Calibrate result automatically");
            autoApply.setTooltip ("Off by default: the result is shown first so you can see what will change. "
                                  "On: the suggestion is applied as soon as it is ready (you can still revert it).");
            autoApply.setColour (juce::ToggleButton::textColourId, colours::text);
            autoApply.setColour (juce::ToggleButton::tickColourId, colours::accent);
            autoApply.setToggleState (settings->getBool ("calibAutoApply", false), juce::dontSendNotification);
            autoApply.onClick = [this] { settings->setBool ("calibAutoApply", autoApply.getToggleState()); };
            addAndMakeVisible (autoApply);

            setSize (400, 58);
        }

        void resized() override
        {
            auto r = getLocalBounds();
            label.setBounds (r.removeFromTop (16));
            seconds.setBounds (r.removeFromTop (22));
            autoApply.setBounds (r.removeFromTop (20));
        }

        juce::Slider& getSlider() noexcept        { return seconds; }
        juce::ToggleButton& getToggle() noexcept  { return autoApply; }

    private:
        std::shared_ptr<settings::GlobalSettings> settings;
        juce::Label label;
        juce::Slider seconds;
        juce::ToggleButton autoApply;
    };

    const SlotRegistrar calibrateOptions ({ Slot::OptionsExtra, 270,
        [] (GapFillAudioProcessor& proc) -> std::unique_ptr<juce::Component>
        {
            return std::make_unique<CalibrateOptions> (proc);
        } });
}

} // namespace gapfill::calibrate
