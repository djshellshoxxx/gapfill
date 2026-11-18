#include "Takeover.h"
#include <cmath>
namespace gapfill::controller
{
void Takeover::reset() noexcept
{
    waiting = true;
    havePrevious = haveApplied = false;
}
bool Takeover::accepts (TakeoverMode mode, float input, float current) noexcept
{
    if (! std::isfinite (input) || ! std::isfinite (current)) return false;
    if (mode == TakeoverMode::Jump) return true;
    if (haveApplied && std::abs (current - lastApplied) > 1.0e-6f)
    {
        waiting = true;
        havePrevious = false;
        haveApplied = false;
    }
    const bool crosses = havePrevious && ((previousInput <= current && input >= current)
                                       || (previousInput >= current && input <= current));
    if (waiting && (std::abs (input - current) <= 2.0f / 127.0f || crosses)) waiting = false;
    previousInput = input;
    havePrevious = true;
    return ! waiting;
}
void Takeover::applied (float actual) noexcept
{
    lastApplied = actual;
    haveApplied = true;
}
}
