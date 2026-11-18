#include "Gui.h"
#include "../i18n/Strings.h"
#include "../info/ParamInfo.h"
#include "../i18n/ComponentTranslator.h"
#include <cmath>

namespace gapfill::modulation
{
namespace
{
    constexpr const char* sourceIds[] { "fm23_src1", "fm23_src2", "fm23_src3", "fm23_src4", "fm23_src5", "fm23_src6", "fm23_src7", "fm23_src8" };
    constexpr const char* depthIds[] { "fm23_depth1", "fm23_depth2", "fm23_depth3", "fm23_depth4", "fm23_depth5", "fm23_depth6", "fm23_depth7", "fm23_depth8" };
}

ExtendedControls::ExtendedControls (juce::AudioProcessorValueTreeState& state) : parameters (state)
{
    setTitle ("Extended macro sources");
    constexpr const char* knobIds[] { "fm23_macro5", "fm23_macro6", "fm23_macro7", "fm23_macro8" };
    constexpr const char* knobNames[] { "Macro 5", "Macro 6", "Macro 7", "Macro 8" };
    for (size_t index = 0; index < extraKnobs.size(); ++index)
    {
        extraKnobs[index] = std::make_unique<GapKnob> (state, knobIds[index], knobNames[index],
            "Extra macro control for the selected eight-macro setup.", colours::primary);
        addChildComponent (*extraKnobs[index]);
    }
    source.setWantsKeyboardFocus (true);
    i18n::markUserContent (source);
    i18n::markUserContent (sourceLabel);
    i18n::markUserContent (depthLabel);
    source.onChange = [this]
    {
        if (updating || sourceAttachment == nullptr) return;
        const auto choice = source.getSelectedId() - 1;
        if (choice < 0 || choice > 3) return;
        if (widthOwned && choice == (int) MacroSource::Energy)
        {
            source.setSelectedId (lastSource + 1, juce::dontSendNotification);
            if (onRejectedSource) onRejectedSource (i18n::text ("Energy cannot drive a width mapping. Choose a knob or LFO."));
            return;
        }
        sourceAttachment->setValueAsCompleteGesture ((float) choice);
    };
    source.onRightClick = [this] { if (selected >= 0) showParameterMenu (source, parameters, sourceIds[selected]); };
    depth.setSliderStyle (juce::Slider::LinearBar);
    depth.setTextValueSuffix (" %");
    depth.setRange (-100, 100, .01);
    depth.setDoubleClickReturnValue (true, 50);
    depth.onRightClick = [this] { if (selected >= 0) showParameterMenu (depth, parameters, depthIds[selected]); };
    for (auto* component : std::initializer_list<juce::Component*> { &source, &depth, &sourceLabel, &depthLabel, &lfoToggle }) addAndMakeVisible (*component);
    lfoToggle.setClickingTogglesState (true);
    lfoToggle.onClick = [this] { resized(); if (onHeightChanged) onHeightChanged(); };
    constexpr const char* shapeIds[] { "fm23_lfoAShape", "fm23_lfoBShape" };
    constexpr const char* divisionIds[] { "fm23_lfoADiv", "fm23_lfoBDiv" };
    constexpr const char* rateIds[] { "fm23_lfoAHz", "fm23_lfoBHz" };
    for (size_t index = 0; index < 2; ++index)
    {
        const auto letter = index == 0 ? "A" : "B";
        shapes[index] = std::make_unique<GapCombo> (state, shapeIds[index], "LFO " + juce::String (letter), "Shape of the shared LFO.");
        divisions[index] = std::make_unique<GapCombo> (state, divisionIds[index], "Division", "Musical cycle length while transport is available.");
        rates[index] = std::make_unique<GapKnob> (state, rateIds[index], "Free Hz", "Speed used without a usable host position or tempo.", colours::primary);
        addAndMakeVisible (*shapes[index]); addAndMakeVisible (*divisions[index]); addAndMakeVisible (*rates[index]);
    }
    refreshLanguage();
    selectMacro (0, false);
}

void ExtendedControls::refreshLanguage()
{
    const auto selectedId = source.getSelectedId();
    source.clear (juce::dontSendNotification);
    source.addItem (i18n::text ("Knob"), 1);
    source.addItem (i18n::text ("LFO A"), 2);
    source.addItem (i18n::text ("LFO B"), 3);
    source.addItem (i18n::text ("Energy"), 4);
    source.setSelectedId (selectedId, juce::dontSendNotification);
    sourceLabel.setText (i18n::format ("Macro {0} source", { juce::String (juce::jmax (0, selected) + 1) }), juce::dontSendNotification);
    depthLabel.setText (i18n::format ("Macro {0} depth", { juce::String (juce::jmax (0, selected) + 1) }), juce::dontSendNotification);
    lfoToggle.setButtonText (i18n::text ("LFOs"));
    lfoToggle.setTitle (i18n::text ("Show shared LFO controls"));
    if (selected >= 0)
    {
        source.setTitle (i18n::format ("Macro {0} source", { juce::String (selected + 1) }));
        depth.setTitle (i18n::format ("Macro {0} depth", { juce::String (selected + 1) }));
        source.setTooltip (info::tooltipFor (sourceIds[selected], {}));
        depth.setTooltip (info::tooltipFor (depthIds[selected], {}));
    }
}

void ExtendedControls::selectMacro (int index, bool ownsWidth)
{
    index = juce::jlimit (0, 7, index);
    widthOwned = ownsWidth;
    if (selected == index) return;
    sourceAttachment.reset(); depthAttachment.reset();
    selected = index;
    tagParameter (source, sourceIds[index]); tagParameter (sourceLabel, sourceIds[index]);
    tagParameter (depth, depthIds[index]); tagParameter (depthLabel, depthIds[index]);
    if (auto* parameter = parameters.getParameter (sourceIds[index]))
    {
        sourceAttachment = std::make_unique<juce::ParameterAttachment> (*parameter,
            [safe = juce::Component::SafePointer<ExtendedControls> (this)] (float value) { if (safe != nullptr) safe->updateSourceUi (value); }, nullptr);
        sourceAttachment->sendInitialUpdate();
    }
    depthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (parameters, depthIds[index], depth);
    refreshLanguage();
}

void ExtendedControls::updateSourceUi (float value)
{
    const juce::ScopedValueSetter<bool> guard (updating, true);
    lastSource = std::isfinite (value) ? juce::jlimit (0, 3, (int) std::lround (value)) : 0;
    source.setSelectedId (lastSource + 1, juce::dontSendNotification);
    depth.setEnabled (lastSource != (int) MacroSource::Knob);
}

void ExtendedControls::setExpanded (bool value) { expanded = value; resized(); }
void ExtendedControls::setTransportAvailable (bool value)
{
    if (transportAvailable == value) return;
    transportAvailable = value;
    resized();
}

int ExtendedControls::preferredHeight (int width) const noexcept
{
    return (expanded ? 72 : 0) + 48 + (width < 520 ? 28 : 0)
        + (width >= 520 || lfoToggle.getToggleState() ? 112 : 0);
}

void ExtendedControls::resized()
{
    auto area = getLocalBounds();
    auto extra = expanded ? area.removeFromTop (72) : juce::Rectangle<int>();
    for (auto& knob : extraKnobs)
    {
        knob->setVisible (expanded);
        knob->setBounds (extra.removeFromLeft (getWidth() / 4));
    }
    auto selectedRow = area.removeFromTop (48);
    const int half = selectedRow.getWidth() / 2;
    auto sourceArea = selectedRow.removeFromLeft (half).reduced (4, 0);
    sourceLabel.setBounds (sourceArea.removeFromTop (16)); source.setBounds (sourceArea.reduced (0, 2));
    auto depthArea = selectedRow.reduced (4, 0);
    depthLabel.setBounds (depthArea.removeFromTop (16)); depth.setBounds (depthArea.reduced (0, 2));
    const bool narrow = getWidth() < 520;
    lfoToggle.setVisible (narrow);
    if (narrow) lfoToggle.setBounds (area.removeFromTop (28).removeFromLeft (100).reduced (2));
    const bool show = ! narrow || lfoToggle.getToggleState();
    for (size_t index = 0; index < 2; ++index)
    {
        shapes[index]->setVisible (show); divisions[index]->setVisible (show); rates[index]->setVisible (show && ! transportAvailable);
        divisions[index]->getComboBox().setEnabled (transportAvailable);
        auto row = area.removeFromTop (56);
        const int part = row.getWidth() / (transportAvailable ? 2 : 3);
        shapes[index]->setBounds (row.removeFromLeft (part).reduced (4, 0));
        divisions[index]->setBounds (row.removeFromLeft (part).reduced (4, 0));
        rates[index]->setBounds (row.reduced (4, 0));
    }
}
} // namespace gapfill::modulation
