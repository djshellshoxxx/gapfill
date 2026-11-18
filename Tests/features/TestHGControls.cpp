#include "../support/TestRig.h"
#include "features/harmonic/PitchSyncGui.h"
#include "features/i18n/Strings.h"
#include "features/info/ParamInfo.h"
#include "gui/Knob.h"
#include <array>
using namespace gapfill;
namespace
{
    void walkControls (juce::Component& root, const std::function<void(juce::Component&)>& visit)
    {
        visit (root);
        for (auto* child : root.getChildren()) walkControls (*child, visit);
    }
}
GF_TEST (HGControls, all_seven_controls_are_visible_focusable_and_have_bounded_info)
{
    const i18n::ScopedLanguage english ("en");
    GapFillAudioProcessor processor;
    gftest::setParams (processor, { { "fx01_mode", 1 } });
    auto panel = harmonic::makePitchSyncPanel (processor);
    constexpr std::array<const char*, 7> ids { "fx01_mode", "fx01_intervals", "fx01_voices", "fx01_motion", "fx01_scatter", "fx01_tone", "fx01_snap" };
    std::array<int, 7> interactive {};
    walkControls (*panel, [&] (juce::Component& control)
    {
        const auto id = control.getProperties()[kParamIdProperty].toString();
        if (dynamic_cast<juce::Slider*> (&control) == nullptr && dynamic_cast<juce::ComboBox*> (&control) == nullptr) return;
        for (std::size_t i = 0; i < ids.size(); ++i)
            if (id == ids[i])
            {
                ++interactive[i];
                GF_EXPECT (control.isVisible() && control.getWantsKeyboardFocus());
                GF_EXPECT (control.getExplicitFocusOrder() == (int) i + 1);
                GF_EXPECT (control.getTitle().isNotEmpty() && control.getDescription().isNotEmpty());
                GF_EXPECT (control.getWidth() > 0 && control.getHeight() > 0);
                GF_EXPECT (panel->getLocalBounds().contains (panel->getLocalArea (&control, control.getLocalBounds())));
            }
    });
    for (std::size_t i = 0; i < ids.size(); ++i)
    {
        GF_EXPECT (interactive[i] == 1);
        const auto* entry = info::ParamInfoRegistry::get().find (ids[i]);
        GF_EXPECT (entry != nullptr && juce::String (entry->oneLine).length() <= info::kMaxOneLineLength);
    }
}
GF_TEST (HGControls, opting_in_changes_only_the_new_parameter_and_panel_can_reopen)
{
    GapFillAudioProcessor processor;
    const auto originalAmount = processor.getValueTreeState().getRawParameterValue ("hgAmount")->load();
    const auto originalVoices = processor.getValueTreeState().getRawParameterValue ("hgVoices")->load();
    {
        auto panel = harmonic::makePitchSyncPanel (processor);
        juce::ComboBox* mode = nullptr;
        walkControls (*panel, [&] (juce::Component& control)
        {
            if (control.getProperties()[kParamIdProperty].toString() == "fx01_mode")
                if (auto* combo = dynamic_cast<juce::ComboBox*> (&control)) mode = combo;
        });
        GF_EXPECT (mode != nullptr && mode->getNumItems() == 2);
        mode->setSelectedId (2, juce::sendNotificationSync);
        GF_EXPECT (processor.getValueTreeState().getRawParameterValue ("fx01_mode")->load() == 1);
    }
    auto reopened = harmonic::makePitchSyncPanel (processor);
    GF_EXPECT (reopened != nullptr);
    GF_EXPECT (processor.getValueTreeState().getRawParameterValue ("hgAmount")->load() == originalAmount);
    GF_EXPECT (processor.getValueTreeState().getRawParameterValue ("hgVoices")->load() == originalVoices);
}
GF_TEST (HGControls, off_hides_the_body_and_unknown_key_disables_snap_without_resetting_it)
{
    GapFillAudioProcessor processor;
    gftest::setParams (processor, { { "fx01_mode", 0 }, { "fx01_snap", 70 }, { "scaleType", 0 }, { "scaleFollow", 0 } });
    {
        auto panel = harmonic::makePitchSyncPanel (processor);
        bool found = false;
        walkControls (*panel, [&] (juce::Component& control)
        {
            if (control.getProperties()[kParamIdProperty].toString() == "fx01_snap")
                if (auto* slider = dynamic_cast<juce::Slider*> (&control))
                {
                    found = true;
                    GF_EXPECT (! slider->isEnabled() && ! slider->isShowing());
                }
        });
        GF_EXPECT (found);
    }
    gftest::setParams (processor, { { "fx01_mode", 1 }, { "scaleType", 1 } });
    auto panel = harmonic::makePitchSyncPanel (processor);
    bool enabled = false;
    walkControls (*panel, [&] (juce::Component& control)
    {
        if (control.getProperties()[kParamIdProperty].toString() == "fx01_snap")
            if (auto* slider = dynamic_cast<juce::Slider*> (&control)) enabled = slider->isEnabled();
    });
    GF_EXPECT (enabled && processor.getValueTreeState().getRawParameterValue ("fx01_snap")->load() == 70);
}
