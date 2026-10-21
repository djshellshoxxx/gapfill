#pragma once

// Shared helpers of the WP7 musical-context strips (Scale Lock, key, energy, groove, call and
// response, taste, tension, arrangement). They live in Slot::PerformanceTab, a Stack slot in which
// every component keeps its own height. Simple view hides them (view rule "performance.musical");
// hiding collapses the strip to height 0 and never resets, unmaps or un-automates a parameter.

#include "../../PluginProcessor.h"
#include "../../features/viewmode/ViewMode.h"

namespace gapfill::musical
{

inline constexpr int kStripWidth = 640;       // initial width; the stack gives the real one

inline bool advancedView (GapFillAudioProcessor& p)
{
    return viewmode::isVisible ("performance.musical", p.getUiState().getViewMode());
}

/** Gives the strip `fullHeight` when shown, 0 (collapsed) when not. */
inline void fit (juce::Component& c, bool show, int fullHeight)
{
    const int h = show ? fullHeight : 0;

    if (c.getHeight() != h)
        c.setSize (c.getWidth() > 0 ? c.getWidth() : kStripWidth, h);
}

} // namespace gapfill::musical
