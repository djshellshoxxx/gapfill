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

#include "../calibrate/RecordTap.h"
#include "../info/StatusBoard.h"
#include "../mutation/MutationLab.h"
#include "../suggest/Profile.h"

#include <juce_data_structures/juce_data_structures.h>

#include <atomic>
#include <functional>

namespace gapfill::ui
{

enum class ViewMode { Simple = 0, Advanced = 1 };

class UiState
{
public:
    info::StatusBoard status;

    /** U-6: audio-thread input recorder for Auto-Calibrate (idle = one atomic load per chunk). */
    calibrate::RecordTap recordTap;

    /** F-M22: audio-thread input profile for Smart preset suggestions (idle = one atomic load per chunk). */
    suggest::ProfileAccumulator profile;

    /** F-L17: audio-side preview overlay (applied first in the effective-settings pipeline) and the lab controller. */
    mutation::OverlayState mutationOverlay;
    mutation::MutationLab lab { mutationOverlay };

    /** U-8: "Reduced to 125 % to fit this screen" (empty = none). Written by the editor, shown in Options. */
    juce::String scaleNote;

    /** Entry points of editor components that the FILE menu also offers (completeness audit 2026-10-21).
        Message thread only. A component registers its action when it is created and clears it when it is
        destroyed, so an empty function means "no editor / no such component right now" (the menu greys the
        item out). */
    struct EditorActions
    {
        std::function<void()> openHistory;       ///< recipes drawer: open it (F-L02)
        std::function<void()> exportLatestFill;  ///< wet-only WAV of the selected / latest fill (F-L04)
        std::function<void()> auditionNow;       ///< same as the AUDITION button (E-1)
        std::function<bool()> battleFromHeaderAb; ///< F-M18: styles A / B := the header A/B snapshots (false = could not)
    };

    EditorActions editorActions;

    //--- U-5 ---------------------------------------------------------------------
    ViewMode getViewMode() const noexcept          { return (ViewMode) viewMode.load (std::memory_order_relaxed); }
    void setViewMode (ViewMode m) noexcept         { viewMode.store ((int) m, std::memory_order_relaxed); }

    //--- U-4: preset stepping requested from outside the editor ---------------------
    /** Any thread (a MIDI command handler, say): ask the open editor to step to the
        previous (-1) / next (+1) preset. Requests are summed and clamped to +-64; the
        editor's timer applies them on the message thread. Without an open editor they
        wait for the next one. */
    void requestPresetStep (int delta) noexcept
    {
        int current = presetStep.load (std::memory_order_relaxed);
        int next;
        do { next = juce::jlimit (-64, 64, current + delta); }
        while (! presetStep.compare_exchange_weak (current, next, std::memory_order_relaxed));
    }

    /** Message thread: returns the accumulated request and resets it. */
    int takePresetStep() noexcept                  { return presetStep.exchange (0, std::memory_order_relaxed); }

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
    std::atomic<int> presetStep { 0 };
};

} // namespace gapfill::ui
