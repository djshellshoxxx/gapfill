#include "../../PluginProcessor.h"
#include "../../gui/Knob.h"
#include "../../gui/SlotHost.h"
#include "../../gui/HelpRegistry.h"
#include "../i18n/Strings.h"

namespace gapfill::models
{
namespace
{
    class ModelOption final : public juce::Component, private juce::Timer
    {
    public:
        explicit ModelOption (GapFillAudioProcessor& owner) : proc (owner)
        {
            setTitle ("Sound models");
            label.setText ("Sound models", juce::dontSendNotification);
            label.setFont (fonts::label (11.0f));
            hint.setFont (fonts::label (10.0f));
            hint.setColour (juce::Label::textColourId, colours::textMuted);
            upgrade.setButtonText ("UPGRADE MODELS");
            upgrade.setTitle ("Upgrade sound models");
            upgrade.setTooltip ("Use the improved Cloud, Taste, Arrangement and Groove models. Changes future fills and learning; Undo restores the previous versions.");
            upgrade.onClick = [this] { proc.upgradeModels(); refresh(); };
            for (auto* child : std::initializer_list<juce::Component*> { &label, &hint, &upgrade })
                addAndMakeVisible (*child);
            setSize (400, 64);
            startTimerHz (2);
            refresh();
        }
        ~ModelOption() override { stopTimer(); }
        void resized() override
        {
            auto area = getLocalBounds();
            auto top = area.removeFromTop (28);
            upgrade.setBounds (top.removeFromRight (144));
            label.setBounds (top);
            hint.setBounds (area);
        }
    private:
        void timerCallback() override { refresh(); }
        void refresh()
        {
            const auto v = proc.getModelVersions();
            const auto text = i18n::format (v == ModelVersions {}
                ? "Cloud {0}, Taste {1}, Arrangement {2}, Groove {3}. Latest models."
                : "Cloud {0}, Taste {1}, Arrangement {2}, Groove {3}. Saved legacy behaviour; upgrade is optional and undoable.",
                { juce::String ((int) v.cloud), juce::String ((int) v.taste),
                  juce::String ((int) v.arrangement), juce::String ((int) v.groove) });
            hint.setText (text, juce::dontSendNotification);
            hint.setTitle (text);
            upgrade.setEnabled (v != ModelVersions {});
        }
        GapFillAudioProcessor& proc;
        juce::Label label, hint;
        juce::TextButton upgrade;
    };
    const SlotRegistrar modelsOption ({ Slot::OptionsExtra, 320,
        [] (GapFillAudioProcessor& proc) { return std::make_unique<ModelOption> (proc); } });

    const HelpRegistrar modelsHelp (6065, "Sound models", [] (HelpWriter& help)
    {
        help.body ("Options shows the Cloud, Taste, Arrangement and Groove model versions saved in this project. Older states and presets without this metadata "
                   "keep version 1. New instances, factory presets and Reset use version 2. UPGRADE MODELS explicitly selects the improved versions; "
                   "Undo restores your previous selection. Saving a project or user preset preserves the selection.");
        help.body ("Groove 2 makes synced Scramble slices follow the learned timing too. Older saved states keep straight Scramble slices. "
                   "Cloud 2 matches grain levels and reduces window holes without changing Density or Size. Taste 2 learns preferences faster from explicit Like/Skip "
                   "ratings; an unrated fill remains neutral. An existing saved taste profile stays intact until a later rating or Rebuild. "
                   "Upgrades affect future processing and do not rewrite your recorded audio or ratings library.");
    });
}
} // namespace gapfill::models
