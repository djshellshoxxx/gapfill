#include "ViewMode.h"

#include "../../PluginProcessor.h"
#include "../../gui/HelpRegistry.h"
#include "../../gui/Knob.h"
#include "../../gui/SlotHost.h"
#include "../presetbrowser/GlobalSettings.h"

namespace gapfill::viewmode
{

const std::vector<ViewRule>& rules()
{
    // Decision D2 (recommended override of U-5): Sensitivity and Transient Protect stay in Simple.
    static const std::vector<ViewRule> table =
    {
        { "tab.weights",            false, true },
        { "tab.spectral",           false, true },
        { "creative.advancedRows",  false, true },    // seed, wet width, tempo sync + divisions, ping pong, mono wet
        { "detection.rest",         false, true },    // everything in the Detection panel except Sensitivity
        { "protection.rest",        false, true },    // everything in the Protection panel except Transient Protect
        { "performance.musical",    false, true },    // WP7 musical-context strips in the Performance tab (Scale Lock keeps a read-only key chip)
        { "creative.macros",        true,  true },
        { "creative.fillType",      true,  true },
        { "creative.extra",         true,  true },
        { "mix",                    true,  true },
        { "visualiser",             true,  true },
        { "header",                 true,  true },
    };

    return table;
}

bool isVisible (const juce::String& id, ui::ViewMode mode) noexcept
{
    for (const auto& r : rules())
        if (id == r.id)
            return mode == ui::ViewMode::Simple ? r.simple : r.advanced;

    return true;
}

//==============================================================================
namespace
{
    class ViewToggle : public juce::Component,
                       private juce::Timer
    {
    public:
        explicit ViewToggle (GapFillAudioProcessor& p) : proc (p)
        {
            setTitle ("View mode");
            setDescription ("Simple shows the main sound controls. Advanced shows everything.");
            setFocusContainerType (juce::Component::FocusContainerType::focusContainer);

            for (auto* b : { &simpleButton, &advancedButton })
            {
                b->setClickingTogglesState (true);
                b->setRadioGroupId (7701);
                b->setWantsKeyboardFocus (false);
                b->setTooltip ("Simple shows the main sound controls. Advanced shows detector, protection, weights and spectral "
                               "lanes. Nothing is reset when you switch.");
                addAndMakeVisible (b);
            }

            simpleButton.setTitle ("Simple view");
            advancedButton.setTitle ("Advanced view");
            simpleButton.onClick = [this] { choose (ui::ViewMode::Simple); };
            advancedButton.onClick = [this] { choose (ui::ViewMode::Advanced); };

            hint.setText ("More controls: Advanced", juce::dontSendNotification);
            hint.setFont (fonts::ui (11.0f));
            hint.setColour (juce::Label::textColourId, colours::textMuted);
            hint.setJustificationType (juce::Justification::centredRight);
            hint.setInterceptsMouseClicks (false, false);
            addChildComponent (hint);

            settings = settings::GlobalSettings::shared();
            sync();
            startTimerHz (10);
        }

        ~ViewToggle() override { stopTimer(); }

        void resized() override
        {
            auto b = getLocalBounds();
            advancedButton.setBounds (b.removeFromRight (juce::jmin (80, b.getWidth() / 2)));
            simpleButton.setBounds (b.removeFromRight (juce::jmin (70, b.getWidth())));
            hint.setBounds (b.removeFromRight (juce::jmax (0, b.getWidth() - 8)));
        }

    private:
        void choose (ui::ViewMode m)
        {
            proc.getUiState().setViewMode (m);
            settings->setBool (kHintDismissedKey, true);       // any use of the toggle dismisses the hint
            sync();
        }

        void sync()
        {
            const bool simple = proc.getUiState().getViewMode() == ui::ViewMode::Simple;
            simpleButton.setToggleState (simple, juce::dontSendNotification);
            advancedButton.setToggleState (! simple, juce::dontSendNotification);
            hint.setVisible (simple && ! settings->getBool (kHintDismissedKey, false) && getWidth() > 220);
        }

        void timerCallback() override { sync(); }

        GapFillAudioProcessor& proc;
        std::shared_ptr<settings::GlobalSettings> settings;
        juce::TextButton simpleButton { "SIMPLE" }, advancedButton { "ADVANCED" };
        juce::Label hint;
    };

    const SlotRegistrar viewToggle ({ Slot::TopBarRight, 100,
        [] (GapFillAudioProcessor& proc) -> std::unique_ptr<juce::Component>
        {
            return std::make_unique<ViewToggle> (proc);
        } });

    const HelpRegistrar helpView (1300, "Simple and Advanced view", [] (HelpWriter& w)
    {
        w.body ("The SIMPLE | ADVANCED switch at the right end of the tab row chooses how much of GapFill you see. "
                "Simple shows the visualiser, the five macros (Fill, Intensity, Complexity, Chaos, Density), Fill Type, "
                "Sensitivity, Transient Protect, the mix section and the header buttons. Advanced shows everything: the full "
                "detector and protection panels, the Auto Weights and Spectral tabs, and the extra Creative rows.");
        w.body ("Switching never resets, disables or unmaps anything: hidden controls keep their values, host automation and MIDI "
                "mappings keep working. New instances open in Simple; projects saved before this switch existed open in Advanced.");
    });
}

} // namespace gapfill::viewmode
