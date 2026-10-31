#pragma once

// Audio-thread handler for the C-5 / C-11 commands: LockLatest, LockRecipe, Unlock,
// LikeLatest, LikeRecipe (docs/dev/12 section 2.3 ids 2-6, permanent).

#include "../../dsp/EngineEvents.h"

namespace gapfill
{

class FillEngine;

namespace lock
{

constexpr bool isStateCommand (CommandType type) noexcept
{
    return type == CommandType::LockLatest || type == CommandType::LockRecipe || type == CommandType::Unlock;
}

constexpr bool acceptsCommandEpoch (const Command& command, std::uint64_t currentEpoch) noexcept
{
    return ! isStateCommand (command.type) || command.lockEpoch == 0 || command.lockEpoch == currentEpoch;
}

/** Returns true when `command` was one of the five. */
bool handleCommand (FillEngine&, const Command&) noexcept;

} // namespace lock
} // namespace gapfill
