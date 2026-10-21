// GapFill
// Copyright © 2026 Sheldon Davidson.
// Licensed under AGPL-3.0-or-later. See LICENSE.
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../LookAndFeel.h"
#include <functional>

namespace gapfill
{

/** Compact disclosure tile that borrows an existing feature panel.

    The tile owns presentation only. The editor owns the content component,
    and collapsing never changes processor parameters or preset state.
*/
class DisclosureTile final : public juce::Component
{
public:
    DisclosureTile (juce::String title, juce::Component& panel, int preferredContentHeight);

    void resized() override;
    void paint (juce::Graphics&) override;

    void setExpanded (bool shouldExpand);
    bool isExpanded() const noexcept { return expanded; }
    int getPreferredHeight() const noexcept;
    std::function<void()> onHeightChanged;

private:
    juce::Component& content;
    juce::TextButton disclosureButton;
    juce::String sectionTitle;
    int preferredHeight;
    bool expanded = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DisclosureTile)
};

} // namespace gapfill
