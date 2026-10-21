#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "LookAndFeel.h"
#include "HelpRegistry.h"

namespace gapfill
{

//==============================================================================
/** The scrollable body of the help section. */
class HelpContent : public juce::Component,
                    private HelpWriter
{
public:
    HelpContent();

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Height needed for the current width. */
    int idealHeight() const { return (int) std::ceil (layout.getHeight()) + 40; }

private:
    // HelpWriter: what registered sections write into.
    void title (const juce::String&) override;
    void versionLine (const juce::String&) override;
    void body (const juce::String&) override;
    void entry (const juce::String&, const juce::String&) override;
    void heading (const juce::String&);

    void rebuildLayout (float width);

    juce::AttributedString text;
    juce::TextLayout layout;
    float lastWidth = 0.0f;
};

//==============================================================================
/** Full-panel help overlay with its own scroll bar and close button. */
class HelpView : public juce::Component
{
public:
    HelpView();

    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onClose;

private:
    juce::Viewport viewport;
    HelpContent content;
    juce::TextButton closeButton { "CLOSE" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HelpView)
};

} // namespace gapfill
