#include "OptionsView.h"

namespace gapfill
{

namespace
{
    constexpr int kRowHeight   = 28;
    constexpr int kTitleHeight = 34;
}

//==============================================================================
OptionsView::OptionsView()
{
    setOpaque (false);

    tooltipToggle.setColour (juce::ToggleButton::textColourId, colours::text);
    tooltipToggle.setColour (juce::ToggleButton::tickColourId, colours::accent);
    tooltipToggle.onClick = [this]
    {
        if (onTooltipsChanged != nullptr)
            onTooltipsChanged (tooltipToggle.getToggleState());
    };
    addAndMakeVisible (tooltipToggle);

    const auto hint = [] (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (fonts::ui (11.5f));
        l.setColour (juce::Label::textColourId, colours::textMuted);
        l.setJustificationType (juce::Justification::topLeft);
        l.setMinimumHorizontalScale (1.0f);
    };

    hint (tooltipHint, "Tooltips explain each control after a short hover. Turning them off does not "
                       "change how anything sounds.");
    addAndMakeVisible (tooltipHint);

    deviceButton.onClick = [this]
    {
        if (onShowDeviceSettings != nullptr)
            onShowDeviceSettings();
    };
    addAndMakeVisible (deviceButton);

    hint (deviceHint, {});
    addAndMakeVisible (deviceHint);

    closeButton.onClick = [this] { if (onClose != nullptr) onClose(); };
    addAndMakeVisible (closeButton);

    updateDeviceRow();
}

void OptionsView::refresh (bool tooltipsEnabled)
{
    tooltipToggle.setToggleState (tooltipsEnabled, juce::dontSendNotification);
    updateDeviceRow();
}

void OptionsView::updateDeviceRow()
{
    // In a plugin the host owns the audio and MIDI hardware, so the button is
    // disabled and the hint says why rather than leaving a dead control.
    const auto standalone = onShowDeviceSettings != nullptr;

    deviceButton.setEnabled (standalone);
    deviceHint.setText (standalone
                          ? "Choose the audio output device, sample rate, buffer size and which MIDI "
                            "inputs are enabled."
                          : "Your host owns the audio and MIDI devices, so this is only available in "
                            "the standalone version of GapFill.",
                        juce::dontSendNotification);
}

//==============================================================================
void OptionsView::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();

    GapFillLookAndFeel::drawShadow (g, b, metrics::panelRadius);

    g.setColour (colours::panel);
    g.fillRoundedRectangle (b, metrics::panelRadius);
    g.setColour (colours::edge);
    g.drawRoundedRectangle (b.reduced (0.5f), metrics::panelRadius, 1.0f);

    auto header = b.reduced ((float) metrics::edgePadding, 0.0f)
                   .withHeight ((float) kTitleHeight)
                   .translated (0.0f, (float) metrics::edgePadding);

    // Section header with the 2x12px accent bar from the identity spec.
    g.setColour (colours::accent);
    g.fillRect (header.getX(), header.getCentreY() - 6.0f, 2.0f, 12.0f);

    g.setColour (colours::text);
    g.setFont (fonts::sectionHeader (12.0f));
    g.drawText ("OPTIONS", header.withTrimmedLeft (10.0f), juce::Justification::centredLeft, false);

    GapFillLookAndFeel::drawSeparator (g, header.withY (header.getBottom()).withHeight (8.0f));
}

void OptionsView::resized()
{
    auto b = getLocalBounds().reduced (metrics::edgePadding);

    b.removeFromTop (kTitleHeight + metrics::grid);

    tooltipToggle.setBounds (b.removeFromTop (kRowHeight));
    tooltipHint.setBounds (b.removeFromTop (kRowHeight + metrics::grid).withTrimmedLeft (metrics::grid * 3));

    b.removeFromTop (metrics::grid * 2);

    deviceButton.setBounds (b.removeFromTop (metrics::buttonHeight).withWidth (210));
    b.removeFromTop (metrics::halfGrid);
    deviceHint.setBounds (b.removeFromTop (kRowHeight + metrics::grid));

    closeButton.setBounds (b.removeFromBottom (metrics::buttonHeight)
                            .removeFromRight (90));
}

} // namespace gapfill
