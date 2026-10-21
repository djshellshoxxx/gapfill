#pragma once

// E-1 Fill Audition: command handling on the audio thread.
//
// Contract
//  * Preview / PreviewRecipe commands arrive through the processor's `commands`
//    queue and are drained once per block. Several presses in one block collapse
//    to the last (the engine mailbox holds one request).
//  * A Preview older than kPreviewStaleMs (judged by the stamp the editor put on
//    it) is discarded so a stalled host can never fire it later. Unstamped
//    commands (tests, MIDI) are never stale.
//  * Nothing here allocates, locks or logs.

#include "../../dsp/EngineEvents.h"

namespace gapfill
{

class FillEngine;

namespace audition
{

/** Handles Preview / PreviewRecipe. Returns true when `command` was one of them. */
bool handleCommand (FillEngine&, const Command&) noexcept;

/** Same, with an explicit clock (unit tests). */
bool handleCommand (FillEngine&, const Command&, std::uint32_t nowMs) noexcept;

} // namespace audition
} // namespace gapfill
