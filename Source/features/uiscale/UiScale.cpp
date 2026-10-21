#include "UiScale.h"

#include "../ui/UiState.h"

#include "../../PluginProcessor.h"
#include "../../gui/Knob.h"
#include "../../gui/SlotHost.h"
#include "../../gui/HelpRegistry.h"
#include "../presetbrowser/GlobalSettings.h"

namespace gapfill::uiscale
{

int snapScalePercent (int requested) noexcept
{
    if (requested < 100 || requested > 400)
        return 100;

    int best = 100;
    int bestDistance = 1 << 30;

    for (const auto step : kSteps)
    {
        const auto d = std::abs (step - requested);

        if (d < bestDistance)
        {
            best = step;
            bestDistance = d;
        }
    }

    return best;
}

int clampScalePercent (int requested, juce::Rectangle<int> workArea, juce::Point<int> logicalSize, float hostScale) noexcept
{
    const auto wanted = snapScalePercent (requested);

    if (workArea.isEmpty() || logicalSize.x <= 0 || logicalSize.y <= 0)
        return wanted;

    const auto host = hostScale > 0.0f ? hostScale : 1.0f;

    for (int i = (int) kSteps.size() - 1; i >= 0; --i)
    {
        const auto step = kSteps[(size_t) i];

        if (step > wanted)
            continue;

        const auto w = (double) logicalSize.x * host * step / 100.0;
        const auto h = (double) logicalSize.y * host * step / 100.0;

        if (w <= workArea.getWidth() * 0.95 && h <= workArea.getHeight() * 0.95)
            return step;
    }

    return 100;
}

float effectiveScale (float hostScale, int percent) noexcept
{
    return (hostScale > 0.0f ? hostScale : 1.0f) * (float) snapScalePercent (percent) / 100.0f;
}

//==============================================================================
namespace
{
    class ScaleOptions : public juce::Component,
                         private juce::Timer
    {
    public:
        explicit ScaleOptions (GapFillAudioProcessor& p) : proc (p), settings (settings::GlobalSettings::shared())
        {
            setTitle ("Interface size");

            label.setText ("Interface size", juce::dontSendNotification);
            label.setColour (juce::Label::textColourId, colours::text);
            label.setFont (fonts::ui (12.5f));
            addAndMakeVisible (label);

            for (size_t i = 0; i < kSteps.size(); ++i)
                combo.addItem (juce::String (kSteps[i]) + " %", (int) i + 1);

            combo.setTitle ("Interface size");
            combo.setDescription ("Scales the whole plug-in window. Your saved layout does not change.");
            combo.setTooltip ("Scale the whole interface. Your DAW's own scaling is applied on top.");
            combo.setSelectedId (indexOf (snapScalePercent (settings->getInt (settings::GlobalSettings::keyUiScale, 100))) + 1,
                                 juce::dontSendNotification);
            combo.onChange = [this]
            {
                const auto index = combo.getSelectedId() - 1;

                if (juce::isPositiveAndBelow (index, (int) kSteps.size()))
                    settings->setInt (settings::GlobalSettings::keyUiScale, kSteps[(size_t) index]);
            };
            addAndMakeVisible (combo);

            hint.setText ("Makes the whole plug-in larger. Does not change your saved layout.", juce::dontSendNotification);
            hint.setFont (fonts::ui (11.0f));
            hint.setColour (juce::Label::textColourId, colours::textMuted);
            hint.setMinimumHorizontalScale (1.0f);
            addAndMakeVisible (hint);

            startTimerHz (4);
        }

        ~ScaleOptions() override { stopTimer(); }

        void resized() override
        {
            auto b = getLocalBounds();
            auto row = b.removeFromTop (26);
            label.setBounds (row.removeFromLeft (150));
            combo.setBounds (row.removeFromLeft (110));
            hint.setBounds (b.removeFromTop (34));
        }

    private:
        static int indexOf (int percent)
        {
            for (size_t i = 0; i < kSteps.size(); ++i)
                if (kSteps[i] == percent)
                    return (int) i;

            return 0;
        }

        void timerCallback() override
        {
            const auto& note = proc.getUiState().scaleNote;
            hint.setText (note.isNotEmpty() ? note : juce::String ("Makes the whole plug-in larger. Does not change your saved layout."),
                          juce::dontSendNotification);
        }

        GapFillAudioProcessor& proc;
        std::shared_ptr<settings::GlobalSettings> settings;
        juce::Label label, hint;
        juce::ComboBox combo;
    };

    const SlotRegistrar scaleOptions ({ Slot::OptionsExtra, 100,
        [] (GapFillAudioProcessor& proc) -> std::unique_ptr<juce::Component>
        {
            auto c = std::make_unique<ScaleOptions> (proc);
            c->setSize (400, 62);
            return c;
        } });

    const HelpRegistrar helpScale (1030, "Interface size", [] (HelpWriter& w)
    {
        w.body ("Options > Interface size makes the whole plug-in 100, 125, 150, 175 or 200 percent of its normal size. "
                "It is a per-user setting that applies to every GapFill window, and it never changes the size saved in your project. "
                "If the chosen size would not fit your screen, GapFill uses the largest size that does and says so there. "
                "Your DAW's own display scaling is applied on top.");
    });
}

} // namespace gapfill::uiscale
