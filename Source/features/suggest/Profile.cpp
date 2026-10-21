#include "Profile.h"

#include <algorithm>
#include <cmath>

namespace gapfill::suggest
{

namespace
{
    constexpr float kGapConf = 0.55f;                   // FillEngine's kEnterGapConf
    constexpr double kSilence = 0.001;                  // -60 dBFS
}

void ProfileAccumulator::prepare (double sr) noexcept
{
    if (sr != sampleRate.load())
        cancel();

    sampleRate.store (sr);
}

void ProfileAccumulator::startListening (float seconds) noexcept
{
    seconds = std::clamp (seconds, kMinSeconds, kMaxSeconds);
    int expected = (int) State::Idle;

    // From Done / NoSignal / Idle only; a running request is not restarted.
    for (auto from : { State::Done, State::NoSignal, State::Idle })
    {
        expected = (int) from;

        if (state.compare_exchange_strong (expected, (int) State::Requested, std::memory_order_acq_rel))
        {
            requestedHopsMillis.store ((std::int64_t) std::llround (seconds * 1000.0f));
            doneHops.store (0);
            targetHops.store (std::max<std::int64_t> (1, (std::int64_t) std::llround (seconds * sampleRate.load() / AnalysisEngine::hopSize)));
            fresh.store (false);
            return;
        }
    }
}

void ProfileAccumulator::cancel() noexcept
{
    const auto s = state.load();

    if (s == (int) State::Requested || s == (int) State::Listening)
    {
        int expected = s;
        state.compare_exchange_strong (expected, (int) State::Idle, std::memory_order_acq_rel);
    }

    doneHops.store (0);
}

float ProfileAccumulator::getProgress() const noexcept
{
    const auto target = targetHops.load();
    return target > 0 ? std::clamp ((float) ((double) doneHops.load() / (double) target), 0.0f, 1.0f) : 0.0f;
}

void ProfileAccumulator::run (const AnalysisEngine& analysis, std::int64_t fromPos, std::int64_t toPos) noexcept
{
    constexpr int hop = AnalysisEngine::hopSize;

    if (state.load (std::memory_order_acquire) == (int) State::Requested)
    {
        nextHop = fromPos / hop;
        eventCursor = nextHop * hop;
        hops = gapHops = signalHops = events = 0;
        sumRef = sumLow = sumMid = sumHigh = 0.0;

        int expected = (int) State::Requested;

        if (! state.compare_exchange_strong (expected, (int) State::Listening, std::memory_order_acq_rel))
            return;                                          // cancelled in between
    }

    const auto endHop = toPos / hop;                         // hops strictly below this are completely analysed
    const auto limit = endHop * hop;
    const auto target = targetHops.load (std::memory_order_relaxed);

    for (auto h = nextHop; h < endHop && hops < target; ++h)
    {
        const auto& f = analysis.frameAt (h * hop);
        ++hops;
        gapHops += f.conf >= kGapConf ? 1 : 0;
        sumRef += f.reference;
        sumLow += f.bandEnergy[0];
        sumMid += f.bandEnergy[1];
        sumHigh += f.bandEnergy[2];
        signalHops += (double) f.reference > kSilence ? 1 : 0;
        nextHop = h + 1;
    }

    // Events up to the last complete hop (before the engine discards them).
    const auto eventLimit = std::min<std::int64_t> (limit, nextHop * hop);

    for (auto p = analysis.nextDetectionEvent (eventCursor); p >= 0 && p < eventLimit; p = analysis.nextDetectionEvent (eventCursor))
    {
        ++events;
        eventCursor = p + 1;
    }

    eventCursor = std::max (eventCursor, eventLimit);
    doneHops.store (hops, std::memory_order_relaxed);

    if (hops >= target)
        finish();
}

void ProfileAccumulator::finish() noexcept
{
    const auto sr = sampleRate.load();
    InputProfile p;
    p.seconds = (float) ((double) hops * AnalysisEngine::hopSize / sr);
    p.eventsPerSec = p.seconds > 0.0f ? (float) ((double) events / (double) p.seconds) : 0.0f;
    p.gapFraction = hops > 0 ? (float) ((double) gapHops / (double) hops) : 0.0f;
    p.levelDb = (float) (20.0 * std::log10 (std::max (1.0e-6, hops > 0 ? sumRef / (double) hops : 0.0)));

    const auto total = sumLow + sumMid + sumHigh;
    p.lowShare = total > 0.0 ? (float) (sumLow / total) : 0.0f;
    p.highShare = total > 0.0 ? (float) (sumHigh / total) : 0.0f;
    p.valid = hops > 0 && (double) signalHops / (double) hops >= 0.25;

    seq.fetch_add (1, std::memory_order_acq_rel);            // odd: writing
    published = p;
    seq.fetch_add (1, std::memory_order_release);            // even: stable
    fresh.store (true, std::memory_order_release);

    int expected = (int) State::Listening;
    state.compare_exchange_strong (expected, p.valid ? (int) State::Done : (int) State::NoSignal, std::memory_order_acq_rel);
}

bool ProfileAccumulator::takeProfile (InputProfile& out) noexcept
{
    const auto s = getState();

    if ((s != State::Done && s != State::NoSignal) || ! fresh.exchange (false, std::memory_order_acq_rel))
        return false;

    for (int attempt = 0; attempt < 8; ++attempt)
    {
        const auto before = seq.load (std::memory_order_acquire);

        if (before & 1u)
            continue;

        out = published;

        if (seq.load (std::memory_order_acquire) == before)
            return out.valid;
    }

    return false;
}

} // namespace gapfill::suggest
