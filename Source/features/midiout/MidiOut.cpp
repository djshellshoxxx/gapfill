#include "MidiOut.h"

#include <algorithm>
#include <cmath>

namespace gapfill::midiout
{

namespace
{
    inline int clampNote (int n) noexcept   { return std::clamp (n, 0, 127); }
    inline int clampChan (int c) noexcept   { return std::clamp (c, 1, 16); }
}

bool MidiOutEmitter::push (int offset, bool on, int note, int channel, int velocity, bool force) noexcept
{
    // NoteOns obey the per-block cap; a NoteOff of a sounding note is never dropped.
    if (! force && emittedThisBlock >= kMaxEventsPerBlock)
    {
        ++dropped;
        return false;
    }

    if (numPending >= kPending)
    {
        ++dropped;
        return false;
    }

    Msg m;
    m.offset = std::max (0, offset);
    m.status = (std::uint8_t) ((on ? 0x90 : 0x80) | (clampChan (channel) - 1));
    m.d1 = (std::uint8_t) clampNote (note);
    m.d2 = (std::uint8_t) (on ? std::clamp (velocity, 1, 127) : 0);
    m.order = order++;
    pending[(size_t) numPending++] = m;
    ++emittedThisBlock;
    return true;
}

void MidiOutEmitter::flushAll (int offset) noexcept
{
    if (gapOn)
    {
        push (offset, false, gapNote, gapChan, 0, true);
        gapOn = false;
    }

    for (; fillsOn > 0; --fillsOn)
        push (offset, false, fillNote, fillChan, 0, true);
}

void MidiOutEmitter::beginBlock (const Settings& wanted, int hostBlockSamples) noexcept
{
    numPending = 0;
    order = 0;
    emittedThisBlock = 0;
    hostSamples = std::max (1, hostBlockSamples);

    Settings s = wanted;
    s.note = clampNote (s.note);
    s.channel = clampChan (s.channel);

    // Anything that changes what a sounding note means ends it first (old note / channel).
    const bool gapsLost = gapOn && (! s.gaps() || s.note != gapNote || s.channel != gapChan);
    const bool fillsLost = fillsOn > 0 && (! s.fills() || clampNote (s.note + 1) != fillNote || s.channel != fillChan);

    if (flushRequested.exchange (false, std::memory_order_relaxed))
        flushAll (0);
    else
    {
        if (gapsLost && gapOn)
        {
            push (0, false, gapNote, gapChan, 0, true);
            gapOn = false;
        }

        if (fillsLost)
            for (; fillsOn > 0; --fillsOn)
                push (0, false, fillNote, fillChan, 0, true);
    }

    current = s;
}

int MidiOutEmitter::mapOffset (std::int64_t pos, std::int64_t chunkOutputStart, int chunkStartSample) const noexcept
{
    const auto raw = (std::int64_t) chunkStartSample + (pos - chunkOutputStart) + (std::int64_t) current.offsetSamples;
    return (int) std::clamp<std::int64_t> (raw, 0, hostSamples - 1);
}

void MidiOutEmitter::consume (const TransitionLog& log, std::int64_t chunkOutputStart, int chunkStartSample,
                              int chunkSamples) noexcept
{
    if (current.mode == ModeOff)
        return;

    const auto chunkEnd = chunkOutputStart + chunkSamples;

    for (int i = 0; i < log.count; ++i)
    {
        const auto& t = log.items[i];

        if (t.pos < chunkOutputStart || t.pos >= chunkEnd)
            continue;

        const auto offset = mapOffset (t.pos, chunkOutputStart, chunkStartSample);

        switch (t.type)
        {
            case Transition::GapOpen:
                if (current.gaps() && ! gapOn)
                {
                    const auto vel = std::max (1, (int) std::lround ((double) t.value * 127.0 / 255.0));

                    if (push (offset, true, current.note, current.channel, vel, false))
                    {
                        gapOn = true;
                        gapNote = current.note;
                        gapChan = current.channel;
                    }
                }
                break;

            case Transition::GapClose:
                if (gapOn)
                {
                    push (offset, false, gapNote, gapChan, 0, true);
                    gapOn = false;
                }
                break;

            case Transition::FillStart:
                if (current.fills() && fillsOn < 8)
                {
                    const auto note = clampNote (current.note + 1);

                    if (push (offset, true, note, current.channel, (int) t.value, false))
                    {
                        ++fillsOn;
                        fillNote = note;
                        fillChan = current.channel;
                    }
                }
                break;

            case Transition::FillEnd:
                if (fillsOn > 0)
                {
                    push (offset, false, fillNote, fillChan, 0, true);
                    --fillsOn;
                }
                break;
        }
    }
}

int MidiOutEmitter::finish (juce::MidiBuffer& midi) noexcept
{
    if (current.mode == ModeOff && numPending == 0)
        return 0;                                   // disabled and nothing to end: the buffer is left alone

    midi.clear();                                   // never echo the incoming MIDI

    // Insertion sort by (offset, arrival order): at most 48 entries, and unlike std::stable_sort it never allocates.
    for (int i = 1; i < numPending; ++i)
    {
        const auto m = pending[(size_t) i];
        int j = i - 1;

        while (j >= 0 && (pending[(size_t) j].offset > m.offset
                           || (pending[(size_t) j].offset == m.offset && pending[(size_t) j].order > m.order)))
        {
            pending[(size_t) j + 1] = pending[(size_t) j];
            --j;
        }

        pending[(size_t) j + 1] = m;
    }

    // A NoteOff that shares its sample with a NoteOn of the same note must come first (retrigger).
    for (int i = 0; i + 1 < numPending; ++i)
    {
        auto& a = pending[(size_t) i];
        auto& b = pending[(size_t) i + 1];

        if (a.offset == b.offset && (a.status & 0xF0) == 0x90 && (b.status & 0xF0) == 0x80
             && (a.status & 0x0F) == (b.status & 0x0F) && a.d1 == b.d1)
            std::swap (a, b);
    }

    for (int i = 0; i < numPending; ++i)
    {
        const auto& m = pending[(size_t) i];
        midi.addEvent (juce::MidiMessage (m.status, m.d1, m.d2), m.offset);
    }

    const auto written = numPending;
    numPending = 0;
    return written;
}

} // namespace gapfill::midiout
