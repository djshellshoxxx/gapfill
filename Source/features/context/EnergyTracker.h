#pragma once

// F-L22 Musical energy tracking (docs/dev/12 F-L22, docs/dev/features/F-L22).
//
// Pure, JUCE-free, allocation-free. Frames are fixed at round(0.5 s * sr) INPUT
// samples, so every result is independent of how the host cuts blocks and of the
// transport. Per frame it extracts FrameFeatures (shared with F-X04), maps them
// through a slowly adapting range (5th / 95th percentile of the last 60 s, with a
// minimum span so noise cannot stretch the scale) to an energy index
//     e = 0.6 * norm(onsets per second, mean of 4 frames) + 0.4 * norm(loudness dB)
// smooths it (attack 1 s, release 3 s) and quantises it to Low / Mid / High with
// +-0.05 hysteresis (two consecutive frames) and a 4 s minimum dwell. The first
// 10 s after start/reset are neutral (Mid).

#include <array>
#include <cstdint>

namespace gapfill::context
{

enum class EnergyLevel : std::uint8_t { Low = 0, Mid = 1, High = 2 };

struct FrameFeatures
{
    float rmsDb = -60.0f;          ///< frame loudness, floor -60 dBFS
    float onsetsPerSec = 0.0f;     ///< band-energy rises > 3 dB per second, capped
    float bandShare[3] { 0.0f, 0.0f, 0.0f };   ///< low / mid / high energy share
    float brightness = 0.0f;       ///< high share + 0.5 mid share (centroid proxy)
};

class EnergyTracker
{
public:
    static constexpr int kHistoryFrames = 120;      ///< 60 s of 0.5 s frames
    static constexpr int kWarmupFrames = 20;        ///< 10 s neutral
    static constexpr float kMinSpanDb = 6.0f;
    static constexpr float kMinSpanOnsets = 2.0f;
    static constexpr float kMaxOnsetsPerSec = 30.0f;

    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    /** Feeds mono input. Cheap and allocation-free; completes a frame every 0.5 s. */
    void process (const float* mono, int numSamples) noexcept;

    EnergyLevel level() const noexcept          { return currentLevel; }
    float energy() const noexcept               { return smoothed; }
    bool warmedUp() const noexcept              { return framesSeen >= kWarmupFrames; }
    int frameLength() const noexcept            { return frameLen; }
    int levelChanges() const noexcept           { return changes; }
    const FrameFeatures& lastFrame() const noexcept { return last; }

    /** Normalises through the adaptive range (public for tests). */
    static float normalise (float x, float lo, float hi, float minSpan) noexcept;

private:
    void endHop() noexcept;
    void endFrame() noexcept;
    void percentiles (const float* ring, float& lo, float& hi) const noexcept;

    double sr = 48000.0;
    int frameLen = 24000;
    int hopLen = 480;

    // per-sample state
    float lpLow = 0.0f, lpHigh = 0.0f;
    float aLow = 0.0f, aHigh = 0.0f;
    int posInFrame = 0, posInHop = 0;
    double frameSq = 0.0;
    double hopSq[3] { 0, 0, 0 }, frameBand[3] { 0, 0, 0 };
    double prevHop[3] { 0, 0, 0 };
    int onsets = 0;

    // per-frame state
    FrameFeatures last;
    std::array<float, kHistoryFrames> dbRing {}, onsetRing {};
    std::array<float, 4> onsetRecent {};
    int ringCount = 0, ringWrite = 0, recentWrite = 0;
    int framesSeen = 0;
    float smoothed = 0.5f;
    EnergyLevel currentLevel = EnergyLevel::Mid;
    int pendingDir = -1, pendingCount = 0;
    int dwellFrames = 0;
    int changes = 0;
};

} // namespace gapfill::context
