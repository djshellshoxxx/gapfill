// GapFill
// Copyright © 2026 Sheldon Davidson.
// Licensed under AGPL-3.0-or-later. See LICENSE.
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "DisclosureTile.h"

namespace gapfill
{

namespace
{
    constexpr int headerHeight = 30;
}

DisclosureTile::DisclosureTile (juce::String title, juce::Component& panel, int contentHeight)
    : content (panel), disclosureButton ("-  " + title),
      sectionTitle (std::move (title)), preferredHeight (contentHeight)
{
    disclosureButton.setWantsKeyboardFocus (false);
    disclosureButton.setTitle (sectionTitle);
    disclosureButton.setDescription ("Expand or collapse this feature panel");
    disclosureButton.onClick = [this] { setExpanded (! expanded); };

    addAndMakeVisible (disclosureButton);
    addAndMakeVisible (content);
}

void DisclosureTile::setExpanded (bool shouldExpand)
{
    if (expanded == shouldExpand)
        return;

    if (! shouldExpand && content.hasKeyboardFocus (true))
        disclosureButton.grabKeyboardFocus();

    expanded = shouldExpand;
    content.setVisible (expanded);
    disclosureButton.setButtonText ((expanded ? "-  " : "+  ") + sectionTitle);
    disclosureButton.setToggleState (expanded, juce::dontSendNotification);
    resized();
    repaint();

    if (onHeightChanged != nullptr)
        onHeightChanged();
}

int DisclosureTile::getPreferredHeight() const noexcept
{
    return headerHeight + (expanded ? preferredHeight : 0);
}

void DisclosureTile::paint (juce::Graphics& g)
{
    g.setColour (colours::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 5.0f);
    g.setColour (colours::outline);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 5.0f, 1.0f);
}

void DisclosureTile::resized()
{
    auto bounds = getLocalBounds();
    disclosureButton.setBounds (bounds.removeFromTop (headerHeight).reduced (2, 1));
    content.setBounds (expanded ? bounds.reduced (0, 2) : juce::Rectangle<int>());
}

} // namespace gapfill
