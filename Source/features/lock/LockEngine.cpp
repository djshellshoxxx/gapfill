// C-5 Fill Lock and C-11 More Like That, engine side: FillEngine members defined
// outside the hot FillEngine.cpp. Audio thread only; no allocation, no locks.

#include "../../dsp/FillEngine.h"
#include "LockCommands.h"

namespace gapfill
{

void FillEngine::lockLatest() noexcept                      { lockState.lockLatest(); }
void FillEngine::lockRecipe (const FillRecipe& r) noexcept  { lockState.setExplicit (r); }
void FillEngine::unlockRecipe() noexcept                    { lockState.clear(); }

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

    if (events == nullptr)
        return;                                 // keep it pending until a queue is attached

    FillEvent e;
    e.type = lockState.valid ? FillEventType::LockSet : FillEventType::LockCleared;
    e.recipe = lockState.valid ? lockState.recipe : FillRecipe {};
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
    if (t == CommandType::LockRecipe)       { engine.lockRecipe (command.recipe); return true; }
    if (t == CommandType::Unlock)           { engine.unlockRecipe(); return true; }
    if (t == CommandType::LikeLatest)       { engine.likeLatest(); return true; }
    if (t == CommandType::LikeRecipe)       { engine.likeRecipe (command.recipe); return true; }

    return false;
}

} // namespace gapfill::lock
