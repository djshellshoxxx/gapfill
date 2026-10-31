#pragma once

#include "../../gui/Knob.h"
#include "Sources.h"
#include <array>

namespace gapfill::modulation
{
/** Message-thread controls for the explicit eight-macro mode. Owns APVTS
    attachments; selecting a macro rebinds its source/depth pair safely. */
class ExtendedControls final : public juce::Component
{
public:
    explicit ExtendedControls (juce::AudioProcessorValueTreeState&);
    void selectMacro (int index, bool ownsWidth);
    void setExpanded (bool);
    void setTransportAvailable (bool);
    void refreshLanguage();
    MacroSource selectedSource() const noexcept { return (MacroSource) lastSource; }
    int preferredHeight (int width) const noexcept;
    void resized() override;
    std::function<void()> onHeightChanged;
    std::function<void (const juce::String&)> onRejectedSource;
    juce::Slider& extraSlider (int index) noexcept { return extraKnobs[(size_t) index]->getSlider(); }

private:
    void updateSourceUi (float);
    juce::AudioProcessorValueTreeState& parameters;
    std::array<std::unique_ptr<GapKnob>, 4> extraKnobs;
    ParamComboBox source;
    FineSlider depth;
    juce::Label sourceLabel, depthLabel;
    juce::TextButton lfoToggle;
    std::array<std::unique_ptr<GapCombo>, 2> shapes, divisions;
    std::array<std::unique_ptr<GapKnob>, 2> rates;
    std::unique_ptr<juce::ParameterAttachment> sourceAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> depthAttachment;
    int selected = -1, lastSource = 0;
    bool widthOwned = false, expanded = false, transportAvailable = false, updating = false;
};
} // namespace gapfill::modulation
