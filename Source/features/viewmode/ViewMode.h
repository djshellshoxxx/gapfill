#pragma once

// U-5 / F-M04 Simple / Advanced view (docs/dev/features/F-M04-simple-advanced-view.md).
//
// The view mode itself lives in ui::UiState (processor-owned, persisted as the root
// property `viewMode`). This module holds the declarative visibility table (so tests can
// check it without a GUI) and the toggle component (registered in Slot::TopBarRight, which
// the editor reserves at the right end of the tab strip row - decision D1).
//
// Hiding a control never resets, disables, unmaps or un-automates it.

#include "../ui/UiState.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace gapfill::viewmode
{

struct ViewRule
{
    const char* id;
    bool simple;
    bool advanced;
};

/** One row per hideable group; ids not in the table are always visible. */
const std::vector<ViewRule>& rules();

bool isVisible (const juce::String& id, ui::ViewMode) noexcept;

/** Global-settings key: the "More controls: Advanced" hint was dismissed. */
inline constexpr const char* kHintDismissedKey = "viewHintDismissed";

} // namespace gapfill::viewmode
