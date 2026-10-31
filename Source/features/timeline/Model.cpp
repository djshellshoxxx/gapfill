#include "Model.h"

#include "../../Parameters.h"
#include "../../dsp/VisualisationState.h"
#include "../reasons/Reasons.h"
#include "../i18n/Strings.h"

#include <cmath>

namespace gapfill::timeline
{

namespace
{
    std::uint16_t sat16 (float v) noexcept
    {
        return (std::uint16_t) juce::jlimit (0, 65535, (int) std::lround (juce::jmax (0.0f, std::isfinite (v) ? v : 0.0f)));
    }
}

const char* typeName (EventType t) noexcept
{
    switch (t)
    {
        case EventType::Onset:        return "Event";
        case EventType::GapOpen:      return "Gap opened";
        case EventType::GapEval:      return "Gap evaluated";
        case EventType::FillStart:    return "Fill started";
        case EventType::FillEnd:      return "Fill ended";
        case EventType::Preview:      return "Preview";
        case EventType::SceneApplied: return "Scene applied";
        case EventType::Panic:        return "Panic";
        case EventType::Recover:      return "Recover";
        case EventType::None:         break;
    }

    return "Unknown";
}

int fromEngineEvent (const FillEvent& e, TimelineEvent out[2]) noexcept
{
    TimelineEvent base;
    base.pos = e.pos;
    base.confidence8 = e.conf8;
    base.generation = e.generation;

    switch (e.type)
    {
        case FillEventType::Started:
        {
            auto eval = base;
            eval.type = EventType::GapEval;
            eval.reason = (std::uint8_t) RejectReason::None;
            eval.availMs = sat16 (e.availMs);
            out[0] = eval;

            auto start = base;
            start.type = EventType::FillStart;
            start.algo = e.recipe.algo;
            start.lenMs = e.recipe.lengthMs;
            out[1] = start;
            return 2;
        }

        case FillEventType::Rejected:
            base.type = EventType::GapEval;
            base.reason = (std::uint8_t) e.reason;
            base.availMs = sat16 (e.availMs);
            base.needMs = sat16 (e.needMs);
            out[0] = base;
            return 1;

        case FillEventType::Onset:
            base.type = EventType::Onset;
            base.protHoldMs = e.protHoldMs;
            base.lookaheadMs = e.lookaheadMs;
            out[0] = base;
            return 1;

        case FillEventType::GapOpen:       base.type = EventType::GapOpen;      break;
        case FillEventType::FillEnd:       base.type = EventType::FillEnd;      break;
        case FillEventType::Preview:       base.type = EventType::Preview;      break;
        case FillEventType::SceneApplied:  base.type = EventType::SceneApplied; break;
        case FillEventType::Panic:         base.type = EventType::Panic;        break;
        case FillEventType::Recover:       base.type = EventType::Recover;      break;
        case FillEventType::None:          return 0;
    }

    out[0] = base;
    return 1;
}

juce::String describe (const TimelineEvent& e)
{
    switch (e.type)
    {
        case EventType::GapEval:
            if (e.reason == (std::uint8_t) RejectReason::None)
                return i18n::text ("Gap evaluated: filled");

            return i18n::format ("Gap skipped: {0}. {1}",
                { reasons::labelWithNumbers ((RejectReason) e.reason, (float) e.availMs, (float) e.needMs),
                  i18n::text (reasons::advice ((RejectReason) e.reason)) });

        case EventType::FillStart:
        {
            if (e.algo < numAlgos)
                return i18n::format ("Fill started: {0}, {1} ms",
                                      { i18n::text (algoName ((Algo) e.algo)), juce::String (e.lenMs) });
            return i18n::text ("Fill started");
        }

        default:
            return i18n::text (typeName (e.type));
    }
}

//==============================================================================
void TimelineModel::push (const TimelineEvent& e) noexcept
{
    if (count < capacity)
    {
        ring[(size_t) ((head + count) % capacity)] = e;
        ++count;
    }
    else
    {
        ring[(size_t) head] = e;                  // overwrite the oldest
        head = (head + 1) % capacity;
    }
}

void TimelineModel::pushEngineEvent (const FillEvent& e) noexcept
{
    if (! adoptEpoch (e.timelineEpoch, e.sampleRateHz))
        return;

    TimelineEvent out[2];
    const auto n = fromEngineEvent (e, out);

    for (int i = 0; i < n; ++i)
        push (out[i]);
}

bool TimelineModel::adoptEpoch (std::uint32_t incoming, std::uint32_t sampleRateHz) noexcept
{
    if (sampleRateHz != 0 && sampleRateHz < 8000)
        return false;
    if (incoming == 0)
    {
        if (currentEpoch != 0)
            return false;
        if (sampleRateHz != 0)
            currentSampleRate = (double) sampleRateHz;
        return true;
    }
    if (incoming == currentEpoch)
        return sampleRateHz == 0 || (double) sampleRateHz == currentSampleRate;
    if (currentEpoch != 0 && (std::int32_t) (incoming - currentEpoch) <= 0)
        return false;

    clear();
    currentEpoch = incoming;
    if (sampleRateHz != 0)
        currentSampleRate = (double) sampleRateHz;
    return true;
}

bool TimelineModel::pushFrame (const VizFrame& frame, std::int64_t positionAnchor)
{
    if (! adoptEpoch (frame.timelineEpoch, frame.sampleRateHz))
        return false;
    if (frames.capacity() > 0 && frames.sampleRate() != currentSampleRate)
        frames.configure (currentSampleRate, 16.0);
    frames.setPositionAnchor (positionAnchor);
    frames.push (frame);
    return true;
}

const TimelineEvent& TimelineModel::at (int i) const noexcept
{
    return ring[(size_t) ((head + juce::jlimit (0, juce::jmax (0, count - 1), i)) % capacity)];
}

void TimelineModel::window (std::int64_t endPos, std::int64_t windowSamples, std::vector<TimelineEvent>& out) const
{
    out.clear();

    for (int i = 0; i < count; ++i)
    {
        const auto& e = at (i);

        if (e.pos <= endPos && e.pos > endPos - windowSamples)
            out.push_back (e);
    }
}

int TimelineModel::latest (TimelineEvent* out, int maxItems) const noexcept
{
    const auto n = juce::jmin (maxItems, count);

    for (int i = 0; i < n; ++i)
        out[i] = at (count - 1 - i);

    return n;
}

void TimelineModel::clear() noexcept
{
    head = 0;
    count = 0;
    haveLoss = false;
    lossUpToPos = 0;
    paused = false;
    frozenPos = 0;
    frames.clear();
}

void TimelineModel::scrubTo (std::int64_t position, std::int64_t livePos) noexcept
{
    if (! paused)
        return;

    auto first = livePos;
    if (count > 0)
        first = juce::jmin (first, at (0).pos);
    if (frames.size() > 0)
        first = juce::jmin (first, frames.at (0).position);

    frozenPos = juce::jlimit (first, juce::jmax (first, livePos), position);
}

} // namespace gapfill::timeline
