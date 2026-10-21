#pragma once

// UI-0: containers for the Stack slots (docs/dev/12 UI-0).
//
//  StackPanel     a panel with a title and a viewport; the slot's components are
//                 stacked inside it and the scrolling is confined to the panel.
//  EffectsDrawer  the Effects section: a 22 px header with a fold toggle above
//                 a StackPanel, collapsed by default.
//
// They add no controls of their own. The editor creates them only when at least
// one feature registered a component for the slot, so an empty slot costs nothing
// and changes no pixel.

#include "LookAndFeel.h"
#include "SlotHost.h"

namespace gapfill
{

class StackPanel : public juce::Component
{
public:
    StackPanel (SlotHost&, Slot, juce::String title, juce::Colour tint);
    ~StackPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    juce::Viewport& getViewport() noexcept   { return viewport; }
    Slot getSlot() const noexcept            { return slot; }

private:
    SlotHost& host;
    Slot slot;
    juce::String title;
    juce::Colour tint;
    juce::Component content;
    juce::Viewport viewport;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StackPanel)
};

class EffectsDrawer : public juce::Component
{
public:
    EffectsDrawer (SlotHost&);

    void resized() override;
    void paint (juce::Graphics&) override;

    bool isExpanded() const noexcept        { return expanded; }
    void setExpanded (bool shouldExpand);

    /** Called after the user (or setExpanded) changed the state; the editor re-lays out. */
    std::function<void()> onExpandedChanged;

    static constexpr int headerHeight = 22;

private:
    bool expanded = false;
    juce::TextButton toggle { "EFFECTS" };
    StackPanel body;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectsDrawer)
};

/** Decides how the Performance section is reached (UI-0). A fourth tab only fits
    when the three existing tabs keep their widths; otherwise the section is a
    FILE-menu entry that swaps the middle panel. */
struct TabPlan
{
    bool performanceAsTab = false;
    int tabWidth = 130;                 ///< width of the Creative and Spectral tabs
    int weightsTabWidth = 150;
    int performanceTabWidth = 120;
};

TabPlan planTabs (int availableWidth, bool performancePopulated) noexcept;

} // namespace gapfill
