#include "Calibrator.h"

#include <algorithm>
#include <cmath>

namespace gapfill::calibrate
{

namespace
{
    constexpr int kChunk = 1024;
    constexpr float kGapConf = 0.55f;                       // FillEngine's kEnterGapConf
    constexpr int kSens[] = { 30, 40, 50, 60, 70, 80 };
    constexpr int kThresh[] = { 40, 50, 60, 70 };
}

float signalFraction (const float* mono, int numSamples, double sampleRate)
{
    const int window = std::max (1, (int) (sampleRate * 0.1));
    int loud = 0, total = 0;

    for (int start = 0; start + window <= numSamples; start += window)
    {
        double sum = 0.0;

        for (int i = 0; i < window; ++i)
            sum += (double) mono[start + i] * mono[start + i];

        const auto rms = std::sqrt (sum / window);
        ++total;
        loud += rms > 0.001 ? 1 : 0;                        // -60 dBFS
    }

    return total > 0 ? (float) loud / (float) total : 0.0f;
}

CalibMetrics measure (const CalibInput& in, int sensitivity, int gapThresh, const std::atomic<bool>* cancel)
{
    AnalysisEngine engine;
    engine.prepare (in.sampleRate, kChunk);

    auto settings = in.base;
    settings.source = DetSource::Main;
    settings.sidechainActive = false;
    settings.sensitivity01 = (float) sensitivity * 0.01f;
    settings.gapThresh01 = (float) gapThresh * 0.01f;

    CalibMetrics m;
    std::int64_t events = 0, chatterEvents = 0, gapHops = 0, hops = 0;
    std::int64_t lastEvent = -1;
    const auto chatterSamples = (std::int64_t) (0.030 * in.sampleRate);

    std::vector<int> gapRuns;
    int currentRun = 0;
    std::vector<char> eventSeconds ((size_t) (in.numSamples / std::max (1.0, in.sampleRate)) + 1, 0);

    for (int pos = 0; pos < in.numSamples; pos += kChunk)
    {
        if (cancel != nullptr && cancel->load (std::memory_order_relaxed))
            return m;

        const auto n = std::min (kChunk, in.numSamples - pos);
        const auto start = engine.getAnalysisPosition();
        engine.processBlock (in.mono + pos, nullptr, n, settings);
        const auto end = engine.getAnalysisPosition();

        // Every event in [start, end): the engine keeps only 256 at a time.
        for (auto p = engine.nextDetectionEvent (start); p >= 0 && p < end; p = engine.nextDetectionEvent (p + 1))
        {
            ++events;
            eventSeconds[std::min (eventSeconds.size() - 1, (size_t) ((double) p / in.sampleRate))] = 1;

            if (lastEvent >= 0 && p - lastEvent < chatterSamples)
                ++chatterEvents;

            lastEvent = p;
        }

        for (auto p = start - start % AnalysisEngine::hopSize; p < end; p += AnalysisEngine::hopSize)
        {
            if (p < start)
                continue;

            ++hops;

            if (engine.frameAt (p).conf >= kGapConf)
            {
                ++gapHops;
                ++currentRun;
            }
            else if (currentRun > 0)
            {
                gapRuns.push_back (currentRun);
                currentRun = 0;
            }
        }

        engine.discardEventsBefore (end);
    }

    if (currentRun > 0)
        gapRuns.push_back (currentRun);

    const auto seconds = (double) in.numSamples / in.sampleRate;
    m.events = (int) events;
    m.eventsPerSec = seconds > 0.0 ? (float) ((double) events / seconds) : 0.0f;
    m.gapFraction = hops > 0 ? (float) ((double) gapHops / (double) hops) : 0.0f;
    {
        const auto buckets = std::max<size_t> (1, (size_t) (seconds));
        size_t filled = 0;

        for (size_t i = 0; i < std::min (buckets, eventSeconds.size()); ++i)
            filled += eventSeconds[i] != 0 ? 1 : 0;

        m.eventSpread = (float) filled / (float) buckets;
    }

    m.chatter = events > 0 ? (float) ((double) chatterEvents / (double) events) : 0.0f;

    if (! gapRuns.empty())
    {
        std::sort (gapRuns.begin(), gapRuns.end());
        const auto idx = (size_t) std::floor (0.2 * (double) (gapRuns.size() - 1));
        m.gapLenP20Ms = (float) (1000.0 * gapRuns[idx] * AnalysisEngine::hopSize / in.sampleRate);
    }

    return m;
}

float score (const CalibMetrics& m, int sens, int thresh, int curSens, int curThresh) noexcept
{
    const auto below = [] (float v, float low) { return v < low ? (low - v) / low : 0.0f; };
    const auto above = [] (float v, float high) { return v > high ? (v - high) / high : 0.0f; };

    const float d = below (m.eventsPerSec, kTargetEventsLow) + above (m.eventsPerSec, kTargetEventsHigh)
                  + below (m.gapFraction, kTargetGapLow) + above (m.gapFraction, kTargetGapHigh)
                  + above (m.chatter, kMaxChatter) * 2.0f;

    const float change = 0.5f * ((float) std::abs (sens - curSens) / 50.0f + (float) std::abs (thresh - curThresh) / 30.0f);
    return d + 0.05f * change;
}

CalibResult calibrate (const CalibInput& in, const std::atomic<bool>& cancel, const std::function<void (float)>& progress)
{
    CalibResult result;

    if (in.mono == nullptr || in.numSamples < (int) (in.sampleRate * 1.0))
    {
        result.reason = "The recording was too short.";
        return result;
    }

    if (signalFraction (in.mono, in.numSamples, in.sampleRate) < 0.25f)
    {
        result.reason = "Not enough signal - play your track through GapFill and try again.";
        return result;
    }

    float bestScore = 1e30f;
    CalibMetrics bestMetrics;
    int bestSens = in.currentSensitivity, bestThresh = in.currentGapThresh;
    bool anyEvents = false;
    int done = 0;
    const int total = (int) (std::size (kSens) * std::size (kThresh));

    for (const auto s : kSens)
        for (const auto t : kThresh)
        {
            if (cancel.load (std::memory_order_relaxed))
            {
                result.reason = "Cancelled";
                return result;
            }

            const auto m = measure (in, s, t, &cancel);

            if (cancel.load (std::memory_order_relaxed))
            {
                result.reason = "Cancelled";
                return result;
            }

            anyEvents = anyEvents || m.events > 0;
            const auto sc = score (m, s, t, in.currentSensitivity, in.currentGapThresh);

            // Tie -> lower sensitivity, then lower gap threshold (iteration order is ascending, so strict < keeps it).
            if (sc < bestScore - 1e-6f)
            {
                bestScore = sc;
                bestMetrics = m;
                bestSens = s;
                bestThresh = t;
            }

            if (progress != nullptr)
                progress ((float) ++done / (float) total);
        }

    // A steady pad: a few events clustered at the start, nothing afterwards. There is nothing to calibrate on.
    const bool noClearHits = bestMetrics.eventsPerSec < 0.2f || bestMetrics.eventSpread < 0.34f;

    if (! anyEvents || noClearHits)
    {
        result.reason = "No events found - this material has no clear hits to calibrate on.";
        return result;
    }

    result.ok = true;
    result.sensitivity = bestSens;
    result.gapThresh = bestThresh;
    result.chosen = bestMetrics;
    result.minGapMs = in.currentMinGapMs;

    if (bestMetrics.gapLenP20Ms > 0.0f)
    {
        result.minGapMs = std::clamp ((int) std::lround (0.8f * bestMetrics.gapLenP20Ms), 20, 200);
        result.minGapChanged = result.minGapMs != in.currentMinGapMs;
    }

    result.limited = bestMetrics.eventsPerSec < kTargetEventsLow || bestMetrics.eventsPerSec > kTargetEventsHigh
                  || bestMetrics.gapFraction < kTargetGapLow || bestMetrics.gapFraction > kTargetGapHigh
                  || bestMetrics.chatter >= kMaxChatter;
    return result;
}

juce::String describe (const CalibResult& r, const CalibInput& in)
{
    if (! r.ok)
        return r.reason;

    auto text = "Sensitivity " + juce::String (in.currentSensitivity) + " -> " + juce::String (r.sensitivity)
              + ", Gap Threshold " + juce::String (in.currentGapThresh) + " -> " + juce::String (r.gapThresh)
              + ", Min Gap " + juce::String (r.minGapMs) + " ms. Found " + juce::String (r.chosen.eventsPerSec, 1)
              + " hits/s, gaps " + juce::String ((int) std::lround (r.chosen.gapFraction * 100.0f)) + " % of the time.";

    if (r.limited)
        text << " (Best achievable for this material - limited.)";

    return text;
}

} // namespace gapfill::calibrate
