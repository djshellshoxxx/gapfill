#pragma once

// U-9 status line model (docs/dev/05 U-9, docs/dev/12 N-USE-b "interaction with U-9").
//
// The footer shows ONE message. Features post messages by kind; the footer asks
// for the winner. Priority, highest first:
//
//   learn > export > preview > capture > health > fix-it > last gap outcome > hover
//
// Deviation (docs/progress/WP11.md): while a control is hovered or dragged the
// hover line outranks the *last gap outcome* only. With the literal order the hover
// line could never appear once the first gap had been evaluated, which would make
// the feature useless; every other kind still outranks it.
//
// Message thread only. No allocation-free guarantee is needed; nothing here is
// ever touched from the audio thread.

#include <juce_core/juce_core.h>

#include <array>
#include <optional>

namespace gapfill
{
class GapFillAudioProcessor;
}

namespace juce { class AudioProcessorValueTreeState; }

namespace gapfill::info
{

enum class StatusKind
{
    Learn = 0, Export, Preview, Capture, Health, Fixit, LastGap, Hover, Count
};

/** Orders kinds by the effective priority (smaller = shown first). */
int priorityOf (StatusKind) noexcept;

class StatusBoard
{
public:
    struct Top
    {
        StatusKind kind;
        juce::String text;
    };

    /** Empty text clears the kind. */
    void set (StatusKind, const juce::String& text);
    void clear (StatusKind kind)                      { set (kind, {}); }

    juce::String get (StatusKind) const;

    /** Highest-priority non-empty message, or nullopt. */
    std::optional<Top> top() const;

    /** Highest-priority non-empty message among the kinds that outrank `limit`
        (strictly above it); used by the footer, which owns health / fix-it / last gap. */
    std::optional<Top> topAbove (StatusKind limit) const;

private:
    std::array<juce::String, (size_t) StatusKind::Count> texts;
};

//==============================================================================
/** Limits how often the hover / drag line is rewritten (default 20 Hz). */
class UpdateLimiter
{
public:
    explicit UpdateLimiter (juce::uint32 minIntervalMs = 50) noexcept : interval (minIntervalMs) {}

    /** True when an update may be shown at `nowMs`; the first call always passes. */
    bool allow (juce::uint32 nowMs) noexcept
    {
        if (! started || nowMs - last >= interval)
        {
            started = true;
            last = nowMs;
            return true;
        }

        return false;
    }

    void reset() noexcept { started = false; }

private:
    juce::uint32 interval;
    juce::uint32 last = 0;
    bool started = false;
};

//==============================================================================
/** "Name value (base -> effective)" for a parameter; "Name value" when the
    effective value equals the base (or no provider is installed). */
juce::String describeParameter (juce::AudioProcessorValueTreeState&, const juce::String& id);

} // namespace gapfill::info
