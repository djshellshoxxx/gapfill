#include "../../PluginProcessor.h"
#include "LockCommands.h"

namespace gapfill
{
bool GapFillAudioProcessor::postCommand (const Command& command) noexcept
{
    auto stamped = command;
    if (lock::isStateCommand (stamped.type) && stamped.lockEpoch == 0)
        stamped.lockEpoch = recipeServices.lockPublication.currentEpoch();
    return commands.push (stamped);
}

void GapFillAudioProcessor::adoptPendingLockRestore() noexcept
{
    Command accepted;
    if (engine.adoptLockRestore (accepted))
        explainState.onCommand (accepted); // already applied; record exactly once
}
}
