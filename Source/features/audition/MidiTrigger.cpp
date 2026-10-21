#include "MidiTrigger.h"

namespace gapfill::audition
{

bool MidiTrigger::scan (const juce::MidiBuffer& midi, std::uint8_t& velocity) noexcept
{
    if (midi.isEmpty())
        return false;

    const auto wantNote = note.load (std::memory_order_relaxed);
    const auto wantCC = cc.load (std::memory_order_relaxed);
    const auto wantChannel = channel.load (std::memory_order_relaxed);
    const auto learning = (Learn) learnKind.load (std::memory_order_relaxed);

    bool fired = false;

    for (const auto meta : midi)
    {
        if (meta.numBytes < 3)
            continue;

        const auto status = meta.data[0] & 0xF0;
        const auto ch = (meta.data[0] & 0x0F) + 1;
        const auto d1 = meta.data[1] & 0x7F;
        const auto d2 = meta.data[2] & 0x7F;

        const bool isNoteOn = status == 0x90 && d2 > 0;
        const bool isCC = status == 0xB0;

        if (! isNoteOn && ! isCC)
            continue;

        // A pending learn claims the first matching message on any channel.
        if (learning == Learn::Note && isNoteOn)
        {
            note.store (d1, std::memory_order_relaxed);
            learnKind.store ((int) Learn::None, std::memory_order_relaxed);
            learnLeft.store (0, std::memory_order_relaxed);
            continue;
        }

        if (learning == Learn::CC && isCC)
        {
            cc.store (d1, std::memory_order_relaxed);
            ccArmed.store (false, std::memory_order_relaxed);     // wait for it to fall before the first fire
            learnKind.store ((int) Learn::None, std::memory_order_relaxed);
            learnLeft.store (0, std::memory_order_relaxed);
            continue;
        }

        if (wantChannel != 0 && ch != wantChannel)
            continue;

        if (isNoteOn && wantNote >= 0 && d1 == wantNote)
        {
            velocity = (std::uint8_t) d2;
            fired = true;
        }
        else if (isCC && wantCC >= 0 && d1 == wantCC)
        {
            if (d2 >= ccFireAt && ccArmed.load (std::memory_order_relaxed))
            {
                ccArmed.store (false, std::memory_order_relaxed);
                velocity = 100;
                fired = true;
            }
            else if (d2 < ccRearmBelow)
            {
                ccArmed.store (true, std::memory_order_relaxed);
            }
        }
    }

    return fired;
}

void MidiTrigger::startLearn (Learn kind) noexcept
{
    learnLeft.store (kind == Learn::None ? 0 : learnTicks, std::memory_order_relaxed);
    learnKind.store ((int) kind, std::memory_order_relaxed);
}

void MidiTrigger::stopLearn() noexcept
{
    learnKind.store ((int) Learn::None, std::memory_order_relaxed);
    learnLeft.store (0, std::memory_order_relaxed);
}

bool MidiTrigger::advanceLearnTimeout() noexcept
{
    if (learnKind.load (std::memory_order_relaxed) == (int) Learn::None)
        return false;

    const auto left = learnLeft.load (std::memory_order_relaxed) - 1;

    if (left > 0)
    {
        learnLeft.store (left, std::memory_order_relaxed);
        return false;
    }

    stopLearn();
    return true;
}

void MidiTrigger::reset() noexcept
{
    note.store (-1, std::memory_order_relaxed);
    channel.store (0, std::memory_order_relaxed);
    cc.store (-1, std::memory_order_relaxed);
    ccArmed.store (true, std::memory_order_relaxed);
    stopLearn();
}

//==============================================================================
namespace
{
    bool intInRange (const juce::ValueTree& t, const char* name, int lo, int hi, bool& present)
    {
        present = t.hasProperty (name);

        if (! present)
            return true;

        const auto v = t.getProperty (name);

        if (! (v.isInt() || v.isInt64() || v.isString()))
            return false;

        const auto text = v.toString().trim();

        if (text.isEmpty() || ! text.containsOnly ("+-0123456789"))
            return false;

        const auto n = text.getLargeIntValue();
        return n >= lo && n <= hi;
    }
}

bool MidiTrigger::isValid (const juce::ValueTree& perf)
{
    bool present = false;
    return intInRange (perf, "trigNote", -1, 127, present)
        && intInRange (perf, "trigChan", 0, 16, present)
        && intInRange (perf, "trigCC", -1, 127, present);
}

void MidiTrigger::writeTo (juce::ValueTree& perf) const
{
    perf.setProperty ("trigNote", getNote(), nullptr);
    perf.setProperty ("trigChan", getChannel(), nullptr);
    perf.setProperty ("trigCC", getCC(), nullptr);
}

void MidiTrigger::readFrom (const juce::ValueTree& perf) noexcept
{
    setNote ((int) perf.getProperty ("trigNote", -1));
    setChannel ((int) perf.getProperty ("trigChan", 0));
    setCC ((int) perf.getProperty ("trigCC", -1));
}

} // namespace gapfill::audition
