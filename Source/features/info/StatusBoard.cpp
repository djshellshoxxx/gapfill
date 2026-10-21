#include "StatusBoard.h"

#include "ParamInfo.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace gapfill::info
{

int priorityOf (StatusKind kind) noexcept
{
    // Hover outranks the last gap outcome (see the header); everything else is the spec order.
    switch (kind)
    {
        case StatusKind::Learn:   return 0;
        case StatusKind::Export:  return 1;
        case StatusKind::Preview: return 2;
        case StatusKind::Capture: return 3;
        case StatusKind::Health:  return 4;
        case StatusKind::Fixit:   return 5;
        case StatusKind::Hover:   return 6;
        case StatusKind::LastGap: return 7;
        case StatusKind::Count:
        default:                  return 99;
    }
}

void StatusBoard::set (StatusKind kind, const juce::String& text)
{
    if ((int) kind >= 0 && kind != StatusKind::Count)
        texts[(size_t) kind] = text;
}

juce::String StatusBoard::get (StatusKind kind) const
{
    return kind != StatusKind::Count ? texts[(size_t) kind] : juce::String();
}

std::optional<StatusBoard::Top> StatusBoard::top() const
{
    std::optional<Top> best;
    int bestPriority = 1000;

    for (int i = 0; i < (int) StatusKind::Count; ++i)
    {
        const auto kind = (StatusKind) i;

        if (texts[(size_t) i].isNotEmpty() && priorityOf (kind) < bestPriority)
        {
            bestPriority = priorityOf (kind);
            best = Top { kind, texts[(size_t) i] };
        }
    }

    return best;
}

std::optional<StatusBoard::Top> StatusBoard::topAbove (StatusKind limit) const
{
    std::optional<Top> best;
    int bestPriority = priorityOf (limit);

    for (int i = 0; i < (int) StatusKind::Count; ++i)
    {
        const auto kind = (StatusKind) i;

        if (texts[(size_t) i].isNotEmpty() && priorityOf (kind) < bestPriority)
        {
            bestPriority = priorityOf (kind);
            best = Top { kind, texts[(size_t) i] };
        }
    }

    return best;
}

juce::String describeParameter (juce::AudioProcessorValueTreeState& state, const juce::String& id)
{
    auto* parameter = state.getParameter (id);

    if (parameter == nullptr)
        return {};

    const auto* entry = ParamInfoRegistry::get().find (id);
    const juce::String name = entry != nullptr ? juce::String (entry->shortName) : parameter->getName (32);
    const auto base = parameter->getText (parameter->getValue(), 32);

    juce::String text = name + " " + base;

    if (const auto effective = ParamInfoRegistry::get().effectiveValue (id))
    {
        const auto effectiveText = parameter->getText (parameter->convertTo0to1 (*effective), 32);

        if (effectiveText != base)
            text << " (" << base << " -> " << effectiveText << ")";
    }

    return text;
}

} // namespace gapfill::info
