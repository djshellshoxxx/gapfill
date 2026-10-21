#include "GrooveTracker.h"

#include <algorithm>
#include <cmath>

namespace gapfill::groove
{

namespace
{
    constexpr float kAlpha = 0.25f;           // about the last four hits of a slot
    constexpr double kIdleBeats = 64.0;       // 16 bars of 4 beats
}

void GrooveTracker::reset() noexcept
{
    slots = {};
    settleArmed = false;
    settleUntil = -1.0e18;
    accepted = 0;
}

void GrooveTracker::setGrid (int gridSel) noexcept
{
    const int g = gridSel == 0 ? 0 : 1;

    if (g != gridSelection)
    {
        gridSelection = g;
        reset();
    }
}

void GrooveTracker::onTransportJump() noexcept
{
    reset();
    settleArmed = true;
}

void GrooveTracker::tick (double ppqNow, bool transportValid) noexcept
{
    if (! transportValid)
        return;

    if (settleArmed)
    {
        settleUntil = ppqNow + 1.0;
        settleArmed = false;
    }

    for (auto& s : slots)
        if (s.n > 0 && ppqNow - s.lastPpq > kIdleBeats)
        {
            s.n = (std::uint8_t) (s.n / 2);
            s.lastPpq = ppqNow;
        }
}

void GrooveTracker::onEvent (double eventPpq, float strength01) noexcept
{
    if (settleArmed || eventPpq < settleUntil || ! std::isfinite (eventPpq))
        return;

    const double u = eventPpq / stepBeats();
    const double k = std::round (u);
    const float x = (float) (u - k);

    if (std::abs (x) > kMaxOffsetSteps)
        return;                                  // off-grid ghost / triplet note: not groove

    const int slotIndex = (int) (((long long) k % kSlots + kSlots) % kSlots);
    auto& s = slots[(size_t) slotIndex];

    const float strength = std::clamp (strength01, 0.0f, 1.0f);
    const float w = kAlpha * (0.1f + 0.9f * strength);

    if (s.n == 0)
    {
        s.mean = x;
        s.var = 0.0f;
    }
    else
    {
        const float d = x - s.mean;
        s.mean += w * d;
        s.var += w * (d * d - s.var);
    }

    s.n = (std::uint8_t) std::min (255, s.n + 1);
    s.lastPpq = eventPpq;
    ++accepted;
}

GrooveSnapshot GrooveTracker::snapshot() const noexcept
{
    GrooveSnapshot out;
    float confSum = 0.0f;
    int active = 0;
    float oddSum = 0.0f;
    int oddCount = 0;

    for (int i = 0; i < kSlots; ++i)
    {
        const auto& s = slots[(size_t) i];
        out.offsetSteps[i] = s.n >= 3 ? s.mean : 0.0f;

        if (s.n < 3)
            continue;

        const float c = std::min (1.0f, (float) s.n / 4.0f)
                      * (1.0f - std::clamp (std::sqrt (std::max (0.0f, s.var)) / 0.20f, 0.0f, 1.0f));
        confSum += c;
        ++active;

        if ((i & 1) != 0)
        {
            oddSum += s.mean;
            ++oddCount;
        }
    }

    if (active > 0)
        out.confidence = (confSum / (float) active) * std::min(1.0f, (float) active / 3.0f);

    out.swingPercent = std::clamp (50.0f + 50.0f * (oddCount > 0 ? oddSum / (float) oddCount : 0.0f), 40.0f, 75.0f);
    out.valid = out.confidence >= kConfidenceGate;
    return out;
}

void GrooveTracker::load (const GrooveSnapshot& snap) noexcept
{
    reset();

    // A held groove is represented as slots that already have enough evidence.
    for (int i = 0; i < kSlots; ++i)
    {
        auto& s = slots[(size_t) i];
        s.mean = std::clamp (snap.offsetSteps[i], -kMaxOffsetSteps, kMaxOffsetSteps);
        s.var = 0.0f;
        s.n = 4;
        s.lastPpq = 1.0e18;                 // never idles out while held
    }
}

} // namespace gapfill::groove
