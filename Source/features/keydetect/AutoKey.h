#pragma once

// F-X03 glue: the audio-thread key detector, its Hold / warm-start handling and
// the pure resolver that decides which key (manual, detected or held) the
// key-aware features use.

#include "ChromaTracker.h"
#include "KeyEstimator.h"
#include "KeySettings.h"

#include <atomic>

namespace gapfill::keydetect
{

enum class KeySource : std::uint8_t { None, Manual, Auto, Held };

struct ResolvedKey
{
    int rootPc = -1;                ///< -1 = no key
    int scaleType = 0;              ///< index of the `scaleType` choice (0 = Off)
    KeySource source = KeySource::None;
};

/** Manual wins when Auto is off. With Auto on: Held (when Hold and a valid held key),
    else the valid detected key, else the manual key, else none. Major -> Major,
    minor -> Natural Minor. Pure. */
ResolvedKey resolveKey (int manualRoot, int manualScaleType, bool autoOn, bool hold,
                        const KeyEstimate& held, const KeyEstimate& detected) noexcept;

class AutoKey
{
public:
    static constexpr int kEvaluateEveryFrames = 6;          ///< about one second

    void prepare (double sampleRate);
    void reset() noexcept;

    /** Audio thread. Does nothing (and costs nothing) while Auto key is off. */
    void process (const float* mono, int numSamples, const KeySettings&) noexcept;

    // ---- any thread ------------------------------------------------------------
    KeyEstimate estimate() const noexcept { return unpack (published.load (std::memory_order_relaxed)); }
    KeyEstimate heldKey() const noexcept  { return unpack (held.load (std::memory_order_relaxed)); }
    bool isRunning() const noexcept       { return running.load (std::memory_order_relaxed); }

    /** Message thread: warm start from a saved project (authoritative while Hold is on). */
    void requestWarmStart (const KeyEstimate&) noexcept;
    void requestReset() noexcept { resetRequested.store (true, std::memory_order_release); }

    const ChromaTracker& tracker() const noexcept { return chroma; }
    const KeyHysteresis& hysteresis() const noexcept { return hyst; }

private:
    void evaluateNow (const KeySettings&) noexcept;

    ChromaTracker chroma;
    KeyHysteresis hyst;
    int framesSinceEvaluation = 0;
    bool wasHolding = false;
    bool warmActive = false;
    double sr = 48000.0;

    std::atomic<std::uint32_t> published { 0xFFu };          // tonic -1 => none
    std::atomic<std::uint32_t> held { 0xFFu };
    std::atomic<bool> running { false };
    std::atomic<std::uint32_t> warmWord { 0xFFu };
    std::atomic<bool> warmPending { false }, resetRequested { false };
};

} // namespace gapfill::keydetect
