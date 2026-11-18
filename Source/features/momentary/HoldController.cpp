#include "HoldController.h"

#include "../../dsp/DspUtils.h"
#include "../Salts.h"

#include <cmath>

namespace gapfill::momentary
{

bool HoldController::levelOr (int e) const noexcept
{
    for (int s = 0; s < (int) Source::Count; ++s)
        if (level[e][s])
            return true;

    return false;
}

void HoldController::pushStack (int e) noexcept
{
    removeStack (e);
    legacyOrder[e] = ++order;

    if (stackSize < kNumEffects)
        stack[stackSize++] = (std::uint8_t) e;
}

void HoldController::removeStack (int e) noexcept
{
    int w = 0;

    for (int i = 0; i < stackSize; ++i)
        if (stack[i] != e)
            stack[w++] = stack[i];

    stackSize = w;
}

void HoldController::setLatch (bool latch) noexcept
{
    if (latch == latchMode)
        return;

    latchMode = latch;

    for (int e = 0; e < kNumEffects; ++e)
    {
        latched[e] = false;
        timedOut &= (std::uint8_t) ~(1u << e);
        heldSamples[e] = 0;
        engagePending[e] = false;
        removeStack (e);
        prevOr[e] = levelOr (e);

        if (! latchMode && prevOr[e])
            pushStack (e);
    }
}

void HoldController::setLevel (Effect effect, Source source, bool down) noexcept
{
    const int e = (int) effect;
    level[e][(int) source] = down;
    const bool now = levelOr (e);
    const bool rising = now && ! prevOr[e];
    const bool falling = ! now && prevOr[e];
    prevOr[e] = now;

    if (falling)
    {
        engagePending[e] = false;                    // a release before a delayed engage cancels it
        heldSamples[e] = 0;
        timedOut &= (std::uint8_t) ~(1u << e);       // releasing re-arms a timed-out hold
    }

    if (latchMode)
    {
        if (rising)
        {
            latched[e] = ! latched[e];

            if (latched[e])
                pushStack (e);
            else
                removeStack (e);
        }

        return;
    }

    if (rising && (timedOut & (1u << e)) == 0)
        pushStack (e);
    else if (falling)
        removeStack (e);
}

void HoldController::setControllerLevel (Effect effect, bool down, bool timeout, int slot) noexcept
{
    const int e = (int) effect;
    if (slot < 0) slot = e;
    if (e < 0 || e >= kNumEffects || slot < 0 || slot >= controllerSlots) return;
    auto& entry = controllerLevels[slot];
    if (! down && entry.effect != e) return;
    entry.effect = (std::uint8_t) e;
    if (down) entry.activationOrder = ++order;
    entry.active = down;
    entry.timeout = timeout;
    controllerTimedOut = 0;
    for (const auto& levelState : controllerLevels)
        if (levelState.timeout) controllerTimedOut |= (std::uint8_t) (1u << levelState.effect);
}
bool HoldController::controllerLevelActive (Effect effect, int slot) const noexcept
{
    if (slot < 0) slot = (int) effect;
    return slot >= 0 && slot < controllerSlots && controllerLevels[slot].active
        && controllerLevels[slot].effect == (int) effect;
}
void HoldController::clearControllerLevels() noexcept
{
    for (auto& entry : controllerLevels) entry = {};
    controllerTimedOut = 0;
}

void HoldController::toggle (Effect effect) noexcept
{
    if (! latchMode)
        return;

    const int e = (int) effect;
    latched[e] = ! latched[e];

    if (latched[e])
        pushStack (e);
    else
        removeStack (e);
}

void HoldController::tick (std::int64_t samples, std::int64_t timeoutSamples) noexcept
{
    if (latchMode || timeoutSamples <= 0)
        return;                                       // latch has no timeout: the user chose it

    for (int e = 0; e < kNumEffects; ++e)
    {
        if ((timedOut & (1u << e)) != 0 || ! levelOr (e))
            continue;

        heldSamples[e] += samples;

        if (heldSamples[e] >= timeoutSamples)
        {
            timedOut |= (std::uint8_t) (1u << e);     // "Stuck hold released": release and press again to re-engage
            removeStack (e);
        }
    }
}

void HoldController::panic() noexcept
{
    clearControllerLevels();
    order = 0;
    for (int e = 0; e < kNumEffects; ++e) legacyOrder[e] = 0;
    for (int e = 0; e < kNumEffects; ++e)
    {
        for (auto& l : level[e])
            l = false;

        prevOr[e] = false;
        latched[e] = false;
        heldSamples[e] = 0;
        engagePending[e] = false;
    }

    timedOut = 0;
    stackSize = 0;
}

std::uint8_t HoldController::engagedMask() const noexcept
{
    std::uint8_t mask = 0;

    for (int i = 0; i < stackSize; ++i)
    {
        const int e = stack[i];

        if (! engagePending[e])
            mask |= (std::uint8_t) (1u << e);
    }

    for (const auto& entry : controllerLevels)
        if (entry.active) mask |= (std::uint8_t) (1u << entry.effect);
    return mask;
}

int HoldController::topEffect() const noexcept
{
    bool haveController = false;
    for (const auto& entry : controllerLevels) haveController = haveController || entry.active;
    if (! haveController)
    {
        for (int i = stackSize - 1; i >= 0; --i)
            if (! engagePending[stack[i]]) return stack[i];
        return -1;
    }
    int newest = -1;
    std::uint64_t newestOrder = 0;
    for (int i = 0; i < stackSize; ++i)
    {
        const int effect = stack[i];
        if (! engagePending[effect] && legacyOrder[effect] >= newestOrder)
        { newest = effect; newestOrder = legacyOrder[effect]; }
    }
    for (const auto& entry : controllerLevels)
        if (entry.active && entry.activationOrder >= newestOrder)
        { newest = entry.effect; newestOrder = entry.activationOrder; }
    return newest;
}

void HoldController::engageAt (Effect effect, std::int64_t samplePos) noexcept
{
    const int e = (int) effect;
    engageAtPos[e] = samplePos;

    if (stackSize > 0)
        for (int i = 0; i < stackSize; ++i)
            if (stack[i] == e)
                engagePending[e] = true;
}

void HoldController::advanceTo (std::int64_t samplePos) noexcept
{
    for (int e = 0; e < kNumEffects; ++e)
        if (engagePending[e] && samplePos >= engageAtPos[e])
            engagePending[e] = false;
}

//==============================================================================
int stormStepUnits (int index) noexcept
{
    static constexpr int units[kStormSteps] = { 2, 2, 1, 1, 2, 1, 1, 1 };       // 1/16 1/16 1/32 1/32 1/16 1/32 1/32 1/32
    return units[((index % kStormSteps) + kStormSteps) % kStormSteps];
}

StormStep stormStepAt (double ppq) noexcept
{
    const double grid = std::floor (ppq / kStormGridPpq + 1.0e-9);
    const long long g = (long long) grid;
    const long long cycle = (g >= 0 ? g : g - (kStormGridUnits - 1)) / kStormGridUnits;
    const int inCycle = (int) (g - cycle * kStormGridUnits);

    int acc = 0;
    StormStep s;

    for (int i = 0; i < kStormSteps; ++i)
    {
        const int len = stormStepUnits (i);

        if (inCycle < acc + len)
        {
            s.index = i;
            s.stepLengthPpq = len * kStormGridPpq;
            s.stepStartPpq = (double) (cycle * kStormGridUnits + acc) * kStormGridPpq;
            break;
        }

        acc += len;
    }

    s.gateOpen = (ppq - s.stepStartPpq) < kStormGate * s.stepLengthPpq;
    s.accentDb = s.index == 0 ? 1.5f : 0.0f;
    return s;
}

std::uint64_t stormSeed (int userSeed, std::int64_t iteration) noexcept
{
    return hashCombine (hashCombine ((std::uint64_t) (std::uint32_t) userSeed, kSalt_Storm), (std::uint64_t) iteration);
}

} // namespace gapfill::momentary
