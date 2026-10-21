#pragma once

// WP11 integration point (the processor owns exactly ONE UiState, like ExplainState).
//
// Holds the message-thread / persisted state that belongs to the editor shell and
// must survive the editor being closed and reopened:
//   status    the U-9 status board (footer messages by kind)
//   viewMode  U-5 Simple / Advanced, persisted as the root property `viewMode`
//
// Later tickets add their members here (global settings handle, calibration
// recorder ...). The processor touches it through a handful of narrow calls:
//   getStateInformation / setStateInformation   writeState / readState
//   audio thread                                (U-6) calibration tap, one call per chunk
//
// Thread contract: everything is message-thread only except where a member says
// otherwise (viewMode is an atomic so a host thread can save state safely).

#include "../info/StatusBoard.h"

#include <juce_data_structures/juce_data_structures.h>

#include <atomic>

namespace gapfill::ui
{

enum class ViewMode { Simple = 0, Advanced = 1 };

class UiState
{
public:
    info::StatusBoard status;

    //--- U-5 ---------------------------------------------------------------------
    ViewMode getViewMode() const noexcept          { return (ViewMode) viewMode.load (std::memory_order_relaxed); }
    void setViewMode (ViewMode m) noexcept         { viewMode.store ((int) m, std::memory_order_relaxed); }

    //--- persistence -------------------------------------------------------------
    /** Root property written by getStateInformation. */
    static constexpr const char* viewModeProperty = "viewMode";

    void writeState (juce::ValueTree& root) const;

    /** Reads the root of a wrapped (GAPFILL_STATE) tree. A state saved before U-5
        has no `viewMode` property and opens in Advanced, so existing users see no
        change; a brand-new instance starts in Simple. Unknown values are ignored. */
    void readState (const juce::ValueTree& root);

private:
    std::atomic<int> viewMode { (int) ViewMode::Simple };
};

} // namespace gapfill::ui
