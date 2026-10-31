#include "HelpView.h"
#include "../features/i18n/Strings.h"
#include "../features/i18n/ComponentTranslator.h"

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
    i18n::languageChanges().addChangeListener (this);
    rebuildContent();
}

HelpContent::~HelpContent() { i18n::languageChanges().removeChangeListener (this); }

void HelpContent::rebuildContent()
{
    text.clear();
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
    lastWidth = -1.0f;
    rebuildLayout ((float) getWidth() - 32.0f);
    if (onContentChanged != nullptr)
        onContentChanged();
    repaint();
}

void HelpContent::changeListenerCallback (juce::ChangeBroadcaster*) { rebuildContent(); }

void HelpContent::heading (const juce::String& s)
{
    const auto localized = i18n::text (s);
    text.append ("\n" + localized + "\n", i18n::fontForText (headingFont(), localized), colours::accent);
}

void HelpContent::title (const juce::String& s)
{
    const auto localized = i18n::text (s);
    text.append ("\n" + localized + "\n", i18n::fontForText (titleFont(), localized), colours::text);
}

void HelpContent::body (const juce::String& s)
{
    const auto localized = i18n::text (s);
    text.append (localized + "\n", i18n::fontForText (bodyFont(), localized), colours::text.withAlpha (0.86f));
}

void HelpContent::entry (const juce::String& term, const juce::String& description)
{
    const auto localizedTerm = i18n::text (term);
    const auto localizedDescription = i18n::text (description);
    text.append ("  " + localizedTerm + "  ", i18n::fontForText (termFont(), localizedTerm), colours::accentWarm);
    text.append (localizedDescription + "\n", i18n::fontForText (bodyFont(), localizedDescription), colours::text.withAlpha (0.86f));
}

void HelpContent::versionLine (const juce::String& s)
{
    text.append (s + "\n", fonts::mono (10.0f), colours::textMuted);
}

void HelpContent::rebuildLayout (float width)
{
    // Construction has no text geometry yet. Let resized() supply the real
    // width instead of shaping all Help at a throwaway 120-pixel fallback.
    if (width <= 0.0f)
        return;

    if (std::abs (width - lastWidth) < 1.0f)
        return;

    lastWidth = width;
    layout.createLayout (text, juce::jmax (120.0f, width));
}

void HelpContent::resized()
{
    rebuildLayout ((float) getWidth() - 32.0f);
}

bool HelpContent::hasHorizontalOverflow() const
{
    const auto available = juce::jmax (120.0f, (float) getWidth() - 32.0f);
    for (int index = 0; index < layout.getNumLines(); ++index)
    {
        const auto bounds = layout.getLine (index).getLineBoundsX();
        if (bounds.getStart() < -1.0f || bounds.getEnd() > available + 1.0f)
            return true;
    }
    return false;
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
    content.onContentChanged = [this] { resized(); repaint(); };
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
    g.drawText (i18n::text ("HELP"), b.removeFromTop (34.0f).withTrimmedLeft (18.0f).toNearestInt(),
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
