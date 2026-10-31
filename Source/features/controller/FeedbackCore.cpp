#include "FeedbackCore.h"
#include <algorithm>
#include <cmath>

namespace gapfill::controller
{
namespace
{
    bool sameAddress (OutEvent a, OutEvent b) noexcept
    { return a.status == b.status && a.data1 == b.data1; }
    bool valid (OutEvent event) noexcept
    {
        const auto kind = event.status & 0xf0;
        return (kind == 0x90 || kind == 0xb0) && event.data1 < 128 && event.data2 < 128;
    }
}
bool FeedbackCore::configure (std::span<const OutEvent> values) noexcept
{
    if (values.size() > entries.size()) return false;
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        if (! valid (values[i])) return false;
        for (std::size_t j = 0; j < i; ++j)
            if (sameAddress (values[i], values[j])) return false;
    }
    count = values.size(); cursor = sentCursor = 0;
    for (std::size_t i = 0; i < count; ++i)
    {
        entries[i] = {};
        entries[i].value = values[i];
    }
    sentTimes.fill (-1.0e9);
    tokens = 17.0; refillMs = -1.0;
    return true;
}
void FeedbackCore::setValue (std::size_t index, std::uint8_t value) noexcept
{
    if (index < count && value < 128) entries[index].value.data2 = value;
}
void FeedbackCore::noteInput (std::uint8_t channel, std::uint8_t cc, double now) noexcept
{
    if (channel < 1 || channel > 16 || cc > 127 || ! std::isfinite (now)) return;
    for (std::size_t i = 0; i < count; ++i)
        if (entries[i].value.status == (0xb0 | (channel - 1)) && entries[i].value.data1 == cc)
            entries[i].receivedMs = now;
}
void FeedbackCore::resync (double now) noexcept
{
    if (! std::isfinite (now)) return;
    for (std::size_t i = 0; i < count; ++i) entries[i].lastSent = -1;
    // Preserve rate/echo history across reconnect; otherwise repeated reconnects
    // could bypass either cap. Unsatisfied entries remain dirty until accepted.
}
int FeedbackCore::tick (double now, bool (*send) (void*, OutEvent) noexcept, void* context) noexcept
{
    if (count == 0 || send == nullptr || ! std::isfinite (now)) return 0;
    if (refillMs < 0.0) refillMs = now;
    if (now < refillMs) return 0; // A clock discontinuity never creates a burst.
    tokens = std::min (17.0, tokens + (now - refillMs) * 0.512);
    refillMs = now;
    int sent = 0;
    const auto start = cursor;
    for (std::size_t offset = 0; offset < count; ++offset)
    {
        const auto index = (start + offset) % count;
        auto& entry = entries[index];
        if (entry.lastSent == entry.value.data2 || now - entry.lastSentMs < 1000.0 / 30.0
            || now - entry.receivedMs < 250.0) continue;
        if (tokens < 1.0 || now - sentTimes[sentCursor] < 1000.0) break;
        if (! send (context, entry.value)) break;
        entry.lastSent = entry.value.data2;
        entry.lastSentMs = now;
        sentTimes[sentCursor] = now;
        sentCursor = (sentCursor + 1) % sentTimes.size();
        tokens -= 1.0;
        ++sent;
        cursor = (index + 1) % count;
    }
    return sent;
}
}
