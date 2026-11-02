#include "../support/LiveRig.h"
#include "UiTestUtil.h"
#include "features/modulation/Gui.h"
#include "gui/SlotHost.h"

using namespace gapfill;
using namespace gftest;

namespace
{
    template <typename Type>
    Type* parameterControl (juce::Component& root, const juce::String& id)
    {
        std::vector<juce::Component*> children;
        collectAll (root, children);
        for (auto* child : children)
            if (child->getProperties()[kParamIdProperty].toString() == id)
                if (auto* control = dynamic_cast<Type*> (child)) return control;
        return nullptr;
    }

    juce::TextButton* button (juce::Component& root, const juce::String& title)
    {
        std::vector<juce::Component*> children;
        collectAll (root, children);
        for (auto* child : children)
            if (auto* control = dynamic_cast<juce::TextButton*> (child))
                if (control->getTitle() == title || control->getButtonText() == title) return control;
        return nullptr;
    }
}

GF_TEST (MacroExtGui, selecting_each_macro_binds_its_actual_source_and_depth_without_changing_other_macros)
{
    GapFillAudioProcessor processor;
    auto& state = processor.getValueTreeState();
    modulation::ExtendedControls controls (state);
    controls.setBounds (0, 0, 640, controls.preferredHeight (640));
    for (int index = 0; index < 8; ++index)
    {
        controls.selectMacro (index, false);
        const auto sourceId = "fm23_src" + juce::String (index + 1);
        const auto depthId = "fm23_depth" + juce::String (index + 1);
        auto* source = parameterControl<juce::ComboBox> (controls, sourceId);
        auto* depth = parameterControl<juce::Slider> (controls, depthId);
        GF_EXPECT (source != nullptr && depth != nullptr);
        source->setSelectedId (2, juce::sendNotificationSync);
        depth->setValue (-25.0 - index, juce::sendNotificationSync);
        GF_EXPECT_NEAR (state.getRawParameterValue (sourceId)->load(), 1, 1e-6f);
        GF_EXPECT_NEAR (state.getRawParameterValue (depthId)->load(), -25.0 - index, 1e-4);
    }
    for (int index = 0; index < 8; ++index)
        GF_EXPECT_NEAR (state.getRawParameterValue ("fm23_depth" + juce::String (index + 1))->load(), -25.0 - index, 1e-4);
}

GF_TEST (MacroExtGui, energy_width_is_rejected_before_parameter_change_and_lfos_follow_transport_presentation)
{
    GapFillAudioProcessor processor;
    auto& state = processor.getValueTreeState();
    modulation::ExtendedControls controls (state);
    controls.setBounds (0, 0, 640, controls.preferredHeight (640));
    controls.selectMacro (2, true);
    auto* source = parameterControl<juce::ComboBox> (controls, "fm23_src3");
    bool rejected = false;
    controls.onRejectedSource = [&] (const juce::String&) { rejected = true; };
    source->setSelectedId (4, juce::sendNotificationSync);
    GF_EXPECT (rejected && source->getSelectedId() == 1);
    GF_EXPECT_NEAR (state.getRawParameterValue ("fm23_src3")->load(), 0, 1e-6f);
    auto* rate = parameterControl<juce::Slider> (controls, "fm23_lfoAHz");
    auto* division = parameterControl<juce::ComboBox> (controls, "fm23_lfoADiv");
    GF_EXPECT (rate != nullptr && division != nullptr);
    GF_EXPECT (visibleInTree (*rate, controls) && ! division->isEnabled());
    controls.setTransportAvailable (true);
    GF_EXPECT (! visibleInTree (*rate, controls) && division->isEnabled());
    controls.setTransportAvailable (false);
    GF_EXPECT (visibleInTree (*rate, controls) && ! division->isEnabled());
}

GF_TEST (MacroExtGui, explicit_setup_switch_extra_controls_and_undo_preserve_the_eighth_macro)
{
    GapFillAudioProcessor processor;
    std::unique_ptr<juce::Component> panel;
    for (const auto& factory : getSlotFactories (Slot::PerformanceTab))
        if (factory.order == 300) panel = factory.make (processor);
    GF_EXPECT (panel != nullptr);
    auto* mode = button (*panel, "Choose macro setup");
    GF_EXPECT (mode != nullptr);
    mode->onClick();
    auto& model = processor.getLiveControl().macroMappings();
    GF_EXPECT (model.isExtendedEnabled());
    auto* expand = button (*panel, "Show macros 5 to 8");
    expand->setToggleState (true, juce::dontSendNotification); expand->onClick();
    auto* eighth = button (*panel, "Edit macro 8");
    GF_EXPECT (eighth != nullptr && visibleInTree (*eighth, *panel));
    eighth->onClick();
    auto* slider = parameterControl<juce::Slider> (*panel, "fm23_macro8");
    GF_EXPECT (slider != nullptr && visibleInTree (*slider, *panel));
    slider->setValue (67, juce::sendNotificationSync);
    GF_EXPECT_NEAR (processor.getValueTreeState().getRawParameterValue ("fm23_macro8")->load(), 67, .01);
    model.rename (7, "Random");
    mode->onClick();
    GF_EXPECT (! model.isExtendedEnabled());
    auto* undo = button (*panel, "UNDO SETUP");
    GF_EXPECT (undo != nullptr && undo->isVisible());
    undo->onClick();
    GF_EXPECT (model.isExtendedEnabled());
    GF_EXPECT (juce::String::fromUTF8 (model.getExtended().names[7]) == "Random");
}
