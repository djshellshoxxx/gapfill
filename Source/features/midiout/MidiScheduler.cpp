#include "MidiScheduler.h"

#include <algorithm>
#include <cmath>

namespace gapfill::midiout
{

std::int64_t MidiScheduler::add (std::int64_t a, std::int64_t b) noexcept
{
    if (b > 0 && a > std::numeric_limits<std::int64_t>::max() - b) return std::numeric_limits<std::int64_t>::max();
    if (b < 0 && a < std::numeric_limits<std::int64_t>::min() - b) return std::numeric_limits<std::int64_t>::min();
    return a + b;
}

void MidiScheduler::prepare (double sampleRate) noexcept
{
    rate = std::isfinite (sampleRate) ? std::clamp (sampleRate, 8000.0, 192000.0) : 48000.0;
    flushMask = 7;
    previousValid = ccValid = false;
}

bool MidiScheduler::hasSoundingNotes() const noexcept
{
    for (const auto& note : active) if (note.on) return true;
    return false;
}

void MidiScheduler::beginBlock (const MidiOutSettings& wanted, std::int64_t out, int n, std::int64_t in,
                                int eventBudget) noexcept
{
    outputStart = out; inputStart = in; samples = std::max (1, n);
    budget = std::clamp (eventBudget, 0, kSchedulerMaxEvents);
    queuedCount = pendingCount = nextOrder = 0;
    suppressed = flushRequested.exchange (false, std::memory_order_acq_rel);
    if (suppressed || (previousValid && outputStart != previousEnd))
    {
        flushMask = 7;
        ccValid = false;
    }
    previousEnd = add (outputStart, samples); previousValid = true;
    MidiOutSettings next = wanted;
    next.channel = std::clamp (next.channel, 1, 16);
    next.noteHit = std::clamp (next.noteHit, 0, 127);
    next.noteGap = std::clamp (next.noteGap, 0, 127);
    next.noteFill = std::clamp (next.noteFill, 0, 127);
    next.hitLenMs = std::isfinite (next.hitLenMs) ? std::clamp (next.hitLenMs, 10.0f, 500.0f) : 60.0f;
    if (next.cc < 1 || next.cc > 119) next.cc = 0;
    const int maxShift = (int) std::lround (rate * 0.1);
    next.offsetSamples = std::clamp (next.offsetSamples, -maxShift, maxShift);
    const int notes[] { next.noteHit, next.noteGap, next.noteFill };
    const bool enabled[] { next.hitOn, next.gapOn, next.fillOn };
    for (int i = 0; i < NumSources; ++i)
        if (active[(size_t) i].on && (! next.on || ! enabled[i] || active[(size_t) i].note != notes[i]
                                      || active[(size_t) i].channel != next.channel))
            flushMask |= (std::uint8_t) (1 << i);
    if (next.cc != current.cc || next.channel != current.channel || ! next.on) ccValid = false;
    current = next;
}

void MidiScheduler::queue (const OutEvent& event, bool early) noexcept
{
    if (! current.on || suppressed || (int) event.type > (int) OutEvent::Confidence) return;
    const auto origin = early ? inputStart : outputStart;
    if (event.pos < origin || event.pos >= add (origin, samples)) return;
    const auto offset = std::clamp<std::int64_t> (event.pos - origin + current.offsetSamples, 0, samples - 1);
    Queued value { event, add (outputStart, offset), nextOrder++ };
    if (queuedCount < EngineEventBuffer::capacity) queued[(size_t) queuedCount++] = value;
    else
    {
        ++dropped;
        int victim = -1, lowest = EngineEventBuffer::priority (event.type);
        for (int i = queuedCount - 1; i >= 0; --i)
            if (EngineEventBuffer::priority (queued[(size_t) i].event.type) < lowest)
            { victim = i; lowest = EngineEventBuffer::priority (queued[(size_t) i].event.type); }
        if (victim >= 0)
        {
            for (int i = victim; i + 1 < queuedCount; ++i) queued[(size_t) i] = queued[(size_t) i + 1];
            queued[(size_t) queuedCount - 1] = value;
        }
    }
}

void MidiScheduler::consume (const EngineEventBuffer& events) noexcept
{
    dropped += (std::uint64_t) std::max (0, events.dropped);
    for (int i = 0; i < std::clamp (events.count, 0, EngineEventBuffer::capacity); ++i)
        if (events.ev[i].type != OutEvent::Hit || ! current.early) queue (events.ev[i], false);
}

void MidiScheduler::consumeEarlyHits (const EngineEventBuffer& events) noexcept
{
    if (! current.early) return;
    dropped += (std::uint64_t) std::max (0, events.dropped);
    for (int i = 0; i < std::clamp (events.count, 0, EngineEventBuffer::capacity); ++i)
        if (events.ev[i].type == OutEvent::Hit) queue (events.ev[i], true);
}

bool MidiScheduler::packet (std::int64_t time, int status, int data1, int data2) noexcept
{
    if (pendingCount >= budget) return false;
    auto& out = pending[(size_t) pendingCount++];
    out.offset = (int) std::clamp<std::int64_t> (time - outputStart, 0, samples - 1);
    out.status = (std::uint8_t) status; out.data1 = (std::uint8_t) data1; out.data2 = (std::uint8_t) data2;
    return true;
}

bool MidiScheduler::noteOff (int source, std::int64_t time) noexcept
{
    auto& note = active[(size_t) source];
    if (! note.on) return true;
    // Retriggers at the very same sample coalesce an On that has not yet been rendered. Otherwise sorting Off
    // before both Ons would leave two instrument voices with only one future Off.
    if (note.start == time)
        for (int i = pendingCount - 1; i >= 0; --i)
        {
            const auto& p = pending[(size_t) i];
            if ((p.status & 0xf0) == 0x90 && (p.status & 15) == note.channel - 1 && p.data1 == note.note
                && p.offset == time - outputStart)
            {
                for (int j = i; j + 1 < pendingCount; ++j) pending[(size_t) j] = pending[(size_t) j + 1];
                --pendingCount; note.on = false; return true;
            }
        }
    if (! packet (time, 0x80 | (note.channel - 1), note.note, 0)) return false;
    note.on = false;
    return true;
}

int MidiScheduler::velocity (float strength) const noexcept
{
    return current.velFollow ? std::clamp ((int) std::lround (std::clamp (strength, 0.0f, 1.0f) * 127.0f), 1, 127) : 100;
}

void MidiScheduler::noteOn (Source source, int note, float strength, std::int64_t time, std::int64_t duration) noexcept
{
    if (! std::isfinite (strength)) return;
    int conflicts = 0;
    for (int i = 0; i < NumSources; ++i)
        if (active[(size_t) i].on && (i == source || (active[(size_t) i].note == note && active[(size_t) i].channel == current.channel)))
            ++conflicts;
    // Three permanently reserved slots cover every remaining sounding-note Off, including disable/panic.
    if (pendingCount + conflicts + 1 > budget - NumSources) { ++dropped; return; }
    for (int i = 0; i < NumSources; ++i)
        if (active[(size_t) i].on && (i == source || (active[(size_t) i].note == note && active[(size_t) i].channel == current.channel)))
            if (! noteOff (i, time)) return;
    if (! packet (time, 0x90 | (current.channel - 1), note, velocity (strength))) { ++dropped; return; }
    active[(size_t) source] = { true, current.channel, note, time,
        duration == std::numeric_limits<std::int64_t>::max() ? duration : add (time, duration) };
}

void MidiScheduler::fireDue (std::int64_t time) noexcept
{
    for (int i = 0; i < NumSources; ++i)
        if (active[(size_t) i].on && active[(size_t) i].deadline <= time)
            if (! noteOff (i, active[(size_t) i].deadline)) flushMask |= (std::uint8_t) (1 << i);
}

void MidiScheduler::process (const Queued& item) noexcept
{
    const auto& event = item.event;
    const auto time = item.wireTime;
    const auto minimum = std::max<std::int64_t> (1, (std::int64_t) std::ceil (rate * 0.001));
    switch (event.type)
    {
        case OutEvent::Hit:
            if (current.hitOn) noteOn (HitSource, current.noteHit, event.value, time, (std::int64_t) std::lround (rate * current.hitLenMs * 0.001));
            break;
        case OutEvent::GapOpen:
            if (current.gapOn && ! active[GapSource].on)
                noteOn (GapSource, current.noteGap, event.value, time, std::numeric_limits<std::int64_t>::max());
            break;
        case OutEvent::GapClose:
            if (active[GapSource].on) active[GapSource].deadline = std::max (time, add (active[GapSource].start, minimum));
            break;
        case OutEvent::FillStart:
            if (current.fillOn && event.lengthSamples > 0)
                noteOn (FillSource, current.noteFill, event.value, time, std::max<std::int64_t> (minimum, event.lengthSamples));
            break;
        case OutEvent::FillEnd:
            break; // planned end belongs to the start that scheduled it; an older overlapping voice must not end its replacement.
        case OutEvent::Confidence:
            if (current.cc != 0 && std::isfinite (event.value))
            {
                const int value = (int) std::lround (std::clamp (event.value, 0.0f, 1.0f) * 127.0f);
                const auto interval = (std::int64_t) std::ceil (rate / 66.0);
                if ((! ccValid || (time - lastCcTime >= interval && std::abs (value - lastCc) >= 2))
                    && pendingCount < budget - NumSources && packet (time, 0xb0 | (current.channel - 1), current.cc, value))
                { lastCc = value; lastCcTime = time; ccValid = true; }
            }
            break;
    }
}

int MidiScheduler::render (std::array<MidiPacket, kSchedulerMaxEvents>& result) noexcept
{
    if (samples <= 0) return 0;
    if (flushRequested.exchange (false, std::memory_order_acq_rel)) { flushMask = 7; suppressed = true; }
    for (int i = 0; i < NumSources; ++i)
        if ((flushMask & (1 << i)) != 0 && noteOff (i, outputStart)) flushMask &= (std::uint8_t) ~(1 << i);
    for (int i = 1; i < queuedCount; ++i)
    {
        const auto item = queued[(size_t) i];
        int j = i;
        while (j > 0 && (queued[(size_t) j - 1].wireTime > item.wireTime
            || (queued[(size_t) j - 1].wireTime == item.wireTime && queued[(size_t) j - 1].order > item.order)))
        { queued[(size_t) j] = queued[(size_t) j - 1]; --j; }
        queued[(size_t) j] = item;
    }
    fireDue (outputStart);
    if (current.on && ! suppressed)
        for (int i = 0; i < queuedCount; ++i) { fireDue (queued[(size_t) i].wireTime); process (queued[(size_t) i]); }
    fireDue (add (outputStart, samples - 1));
    for (int i = 1; i < pendingCount; ++i)
    {
        const auto item = pending[(size_t) i];
        int j = i;
        while (j > 0 && (pending[(size_t) j - 1].offset > item.offset
            || (pending[(size_t) j - 1].offset == item.offset && (item.status & 0xf0) == 0x80
                && (pending[(size_t) j - 1].status & 0xf0) != 0x80)))
        { pending[(size_t) j] = pending[(size_t) j - 1]; --j; }
        pending[(size_t) j] = item;
    }
    const int count = pendingCount;
    for (int i = 0; i < count; ++i) result[(size_t) i] = pending[(size_t) i];
    if (count > 0) sentCount.fetch_add ((std::uint32_t) count, std::memory_order_relaxed);
    pendingCount = queuedCount = 0;
    return count;
}

}
