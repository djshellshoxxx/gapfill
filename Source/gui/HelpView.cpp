#include "HelpView.h"

namespace gapfill
{

namespace
{
    juce::Font titleFont()   { return juce::Font (juce::FontOptions (20.0f)).boldened(); }
    juce::Font headingFont() { return juce::Font (juce::FontOptions (14.5f)).boldened(); }
    juce::Font bodyFont()    { return juce::Font (juce::FontOptions (13.0f)); }
    juce::Font termFont()    { return juce::Font (juce::FontOptions (13.0f)).boldened(); }
}

//==============================================================================
HelpContent::HelpContent()
{
    text.setLineSpacing (3.0f);

    // Content comes from the registry (core text: HelpSections.cpp; features
    // register their own sections), already sorted by order.
    for (const auto& section : HelpRegistry::getSections())
    {
        if (section.title.isNotEmpty())
            heading (section.title);

        if (section.write != nullptr)
            section.write (*this);
    }

    text.append ("\n", bodyFont(), colours::text);
}

void HelpContent::heading (const juce::String& s)
{
    text.append ("\n" + s + "\n", headingFont(), colours::accent);
}

void HelpContent::title (const juce::String& s)
{
    text.append ("\n" + s + "\n", titleFont(), colours::text);
}

void HelpContent::body (const juce::String& s)
{
    text.append (s + "\n", bodyFont(), colours::text.withAlpha (0.86f));
}

void HelpContent::entry (const juce::String& term, const juce::String& description)
{
    text.append ("  " + term + "  ", termFont(), colours::accentWarm);
    text.append (description + "\n", bodyFont(), colours::text.withAlpha (0.86f));
}

void HelpContent::versionLine (const juce::String& s)
{
    text.append (s + "\n", fonts::mono (10.0f), colours::textMuted);
}

void HelpContent::rebuildLayout (float width)
{
    if (std::abs (width - lastWidth) < 1.0f)
        return;

    lastWidth = width;
    layout.createLayout (text, juce::jmax (120.0f, width));
}

void HelpContent::resized()
{
    rebuildLayout ((float) getWidth() - 32.0f);
}

void HelpContent::paint (juce::Graphics& g)
{
    layout.draw (g, juce::Rectangle<float> (16.0f, 10.0f,
                                            (float) getWidth() - 32.0f,
                                            (float) getHeight() - 20.0f));
}

//==============================================================================
HelpView::HelpView()
{
    setOpaque (false);

    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    closeButton.setWantsKeyboardFocus (false);
    closeButton.onClick = [this] { if (onClose != nullptr) onClose(); };
    addAndMakeVisible (closeButton);
}

void HelpView::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();

    // Matches the options overlay: the shared panel radius and the house shadow,
    // rather than a radius of its own.
    GapFillLookAndFeel::drawShadow (g, b, metrics::panelRadius);

    g.setColour (juce::Colour (0xf20b0d11));
    g.fillRoundedRectangle (b, metrics::panelRadius);
    g.setColour (colours::outline);
    g.drawRoundedRectangle (b.reduced (0.5f), metrics::panelRadius, 1.0f);

    g.setColour (colours::accent);
    g.setFont (juce::Font (juce::FontOptions (13.0f)).withExtraKerningFactor (0.18f));
    g.drawText ("HELP", b.removeFromTop (34.0f).withTrimmedLeft (18.0f).toNearestInt(),
                juce::Justification::centredLeft);
}

void HelpView::resized()
{
    auto b = getLocalBounds();
    auto header = b.removeFromTop (34);
    closeButton.setBounds (header.removeFromRight (90).reduced (8, 5));

    viewport.setBounds (b.reduced (8, 4));

    const auto contentWidth = juce::jmax (200, viewport.getMaximumVisibleWidth());
    content.setSize (contentWidth, 100);
    content.setSize (contentWidth, content.idealHeight());
}

} // namespace gapfill
