#pragma once

// Small helpers for the WP11 GUI tests. A test editor is never put on the desktop, so Component::isShowing()
// is always false; "visible" here means: every component from `c` up to `root` has its visible flag set.

#include <juce_gui_basics/juce_gui_basics.h>

namespace gftest
{

inline bool visibleInTree (const juce::Component& c, const juce::Component& root)
{
    for (const juce::Component* p = &c; p != nullptr; p = p->getParentComponent())
    {
        if (p == &root)
            return true;

        if (! p->isVisible())
            return false;
    }

    return false;
}

inline void collectAll (juce::Component& root, std::vector<juce::Component*>& out)
{
    for (auto* c : root.getChildren())
    {
        out.push_back (c);
        collectAll (*c, out);
    }
}

} // namespace gftest
