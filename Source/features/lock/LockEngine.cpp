// C-5 Fill Lock and C-11 More Like That, engine side: FillEngine members defined
// outside the hot FillEngine.cpp. Audio thread only; no allocation, no locks.

#include "../../dsp/FillEngine.h"
#include "LockCommands.h"
#include "LockPublication.h"

namespace gapfill
{

void FillEngine::lockLatest() noexcept { lockState.lockLatest(); flushLockEvents (0, nullptr); }
void FillEngine::lockRecipe (const FillRecipe& r, const RecipeExtras& extras) noexcept
{ lockState.setExplicit (r, extras); flushLockEvents (0, nullptr); }
void FillEngine::unlockRecipe() noexcept { lockState.clear(); flushLockEvents (0, nullptr); }

void FillEngine::setLockPublication (lock::LockPublication* publication) noexcept
{
    lockPublication = publication;
    lockRestoreEpoch = publication != nullptr ? 0 : 1;
}

bool FillEngine::adoptLockRestore (Command& accepted) noexcept
{
    if (lockPublication == nullptr)
        return false;
    lock::LockSnapshot state;
    if (lockRestoreEpoch == 0)
    {
        if (! lockPublication->tryLatest (state))
            return false;
        if (! state.valid && state.restoreEpoch == 1 && state.generation == 0)
        { lockRestoreEpoch = 1; return false; } // initial empty state makes no synthetic command
    }
    else if (! lockPublication->pendingRestore (lockRestoreEpoch, state))
        return false;
    if (state.valid)
        lockState.setExplicit (state.recipe, state.extras);
    else
        lockState.clear();
    lockRestoreEpoch = state.restoreEpoch;
    flushLockEvents (0, nullptr);
    accepted = {};
    accepted.type = state.valid ? CommandType::LockRecipe : CommandType::Unlock;
    accepted.recipe = state.recipe;
    accepted.extras = state.extras;
    accepted.lockEpoch = state.restoreEpoch;
    return true;
}

void FillEngine::likeLatest() noexcept
{
    if (lockState.haveLast)
        like.start (lockState.lastNatural);
}

void FillEngine::likeRecipe (const FillRecipe& r) noexcept  { like.start (r); }

void FillEngine::flushLockEvents (std::int64_t pos, FillEventQueue* events) noexcept
{
    if (! lockState.dirty)
        return;

    if (lockPublication != nullptr)
        lockPublication->publishAudio ({ lockState.valid, lockState.recipe, lockState.extras }, lockRestoreEpoch);

    if (events == nullptr)
        return;                                 // keep it pending until a queue is attached

    FillEvent e;
    e.type = lockState.valid ? FillEventType::LockSet : FillEventType::LockCleared;
    e.recipe = lockState.valid ? lockState.recipe : FillRecipe {};
    e.extras = lockState.valid ? lockState.extras : RecipeExtras {};
    e.pos = pos;
    events->push (e);
    lockState.dirty = false;
}

} // namespace gapfill

namespace gapfill::lock
{

bool handleCommand (FillEngine& engine, const Command& command) noexcept
{
    const auto t = command.type;

    if (t == CommandType::LockLatest)       { engine.lockLatest(); return true; }
    if (t == CommandType::LockRecipe)       { engine.lockRecipe (command.recipe, command.extras); return true; }
    if (t == CommandType::Unlock)           { engine.unlockRecipe(); return true; }
    if (t == CommandType::LikeLatest)       { engine.likeLatest(); return true; }
    if (t == CommandType::LikeRecipe)       { engine.likeRecipe (command.recipe); return true; }

    return false;
}

} // namespace gapfill::lock
