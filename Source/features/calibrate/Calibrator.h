#pragma once

// U-6 / F-L05 analysis (docs/dev/05 U-6, docs/dev/features/F-L05-auto-calibrate.md): pure, deterministic,
// no JUCE threads. Runs the REAL detector (a private AnalysisEngine) over a recording for the 24 candidate
// settings (sensitivity {30,40,50,60,70,80} x gap threshold {40,50,60,70}) and picks the best one.
// It never touches the processor's own AnalysisEngine.

#include "../../dsp/AnalysisEngine.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>
#include <vector>

namespace gapfill::calibrate
{

struct CalibMetrics
{
    float eventsPerSec = 0.0f;
    float gapFraction = 0.0f;       ///< time with confidence >= 0.55
    float chatter = 0.0f;           ///< fraction of events closer than 30 ms to the previous one
    float gapLenP20Ms = 0.0f;       ///< 20th percentile of the measured gap lengths (0 = none)
    float eventSpread = 0.0f;       ///< fraction of 1-second buckets that contain at least one event (a pad's few events cluster)
    int events = 0;
};

struct CalibResult
{
    bool ok = false;
    juce::String reason;            ///< why not ok (shown to the user)
    int sensitivity = 50, gapThresh = 50, minGapMs = 60;
    bool minGapChanged = false;
    CalibMetrics chosen;
    bool limited = false;           ///< best achievable, but outside the target bands
};

struct CalibInput
{
    const float* mono = nullptr;
    int numSamples = 0;
    double sampleRate = 48000.0;
    AnalysisSettings base;          ///< the current detector settings (mode, hold, release, crossovers ...)
    int currentSensitivity = 50, currentGapThresh = 50, currentMinGapMs = 60;
};

/** Fraction of 100 ms windows whose RMS is above -60 dBFS. */
float signalFraction (const float* mono, int numSamples, double sampleRate);

/** One candidate run. Counts EVERY event (the engine keeps only 256 at a time, so the queue is drained per chunk). */
CalibMetrics measure (const CalibInput&, int sensitivity, int gapThresh, const std::atomic<bool>* cancel = nullptr);

/** Distance of the metrics from the target bands, plus the small prefer-minimal-change penalty. */
float score (const CalibMetrics&, int sensitivity, int gapThresh, int currentSensitivity, int currentGapThresh) noexcept;

/** The full pipeline. `progress` gets 0..1 after each candidate (may be null). Cancel returns ok=false, reason "Cancelled". */
CalibResult calibrate (const CalibInput&, const std::atomic<bool>& cancel, const std::function<void (float)>& progress = {});

/** "Sensitivity 50 -> 62, Gap Threshold 60 -> 55, Min Gap 55 ms. Found 3.2 hits/s, gaps 28 % of the time." */
juce::String describe (const CalibResult&, const CalibInput&);

inline constexpr float kTargetEventsLow = 0.8f, kTargetEventsHigh = 10.0f;
inline constexpr float kTargetGapLow = 0.15f, kTargetGapHigh = 0.45f, kMaxChatter = 0.05f;

} // namespace gapfill::calibrate
