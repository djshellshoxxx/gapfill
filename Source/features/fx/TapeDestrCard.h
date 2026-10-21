#pragma once

// F-M20 Tape destruction (docs/dev/12 section 7, VoiceFx).
//
// Amount is the master depth that scales every sub-control and is also the
// wet blend. Signal path per channel:
//   variable delay line (wow 0.4-2 Hz sine + slow seeded noise, up to +-6 ms;
//   flutter 6-14 Hz, up to +-0.4 ms; splice jumps of the read pointer of
//   5-60 ms with a 2 ms crossfade)  ->  tanh saturation, level matched  ->
//   dropouts (up to 8/s, depth to -40 dB, 5-80 ms, raised-cosine edges 2-8 ms)
//   ->  10 Hz DC blocker.
// Every random event comes from the voice seed (sub-RNG kSalt_TapeDestr), so a
// fill repeats exactly per seed and independently of host block size. The read
// index is clamped inside the line at all times. Independent of Algo::TapeStop.
//
// Deviation from the spec text: the line holds 128 ms (not 25 ms) because the
// splice jumps reach back up to 60 ms; wow + flutter alone stay within 25 ms.

#include "EffectCard.h"

#include "../../dsp/DspUtils.h"

#include <vector>

namespace gapfill::fx
{

class TapeDestrCard final : public VoiceFx
{
public:
    static constexpr float kLineMs = 128.0f;
    static constexpr float kBaseDelayMs = 7.0f;
    static constexpr float kWowDepthMs = 6.0f;
    static constexpr float kFlutterDepthMs = 0.4f;
    static constexpr float kMaxDropoutsPerSecond = 8.0f;
    static constexpr float kMaxSplicesPerSecond = 4.0f;

    CardId id() const noexcept override { return CardId::TapeDestr; }
    void prepare (double sampleRate, int maxBlock, int channels) override;
    void reset() override;
    bool enabled (const FxSettings& s) const noexcept override { return s.tape.amount01 > kOffThreshold; }

    bool startFill (const VoiceFxStart&) override;
    void processVoice (float* l, float* r, int n, const VoiceFxContext&) noexcept override;

    bool isFillActive() const noexcept { return active; }

    // --- test observability ---------------------------------------------------
    float minDelaySeen() const noexcept { return minDelay; }
    float maxDelaySeen() const noexcept { return maxDelay; }
    int   dropoutsStarted() const noexcept { return dropoutCount; }
    int   splicesStarted() const noexcept { return spliceCount; }

private:
    float readLine (const std::vector<float>& line, double delaySamples) const noexcept;
    double nextInterval (float ratePerSecond) noexcept;
    void startDropout() noexcept;
    void startSplice() noexcept;

    double sr = 48000.0;
    int lineSize = 0;
    std::vector<float> lineL, lineR;
    int writeIndex = 0;

    bool active = false;
    TapeSettings s;
    float depth = 0.0f;            // master amount 0..1
    Rng rng;

    // modulation
    double wowPhase = 0.0, wowInc = 0.0, flutPhase = 0.0, flutInc = 0.0;
    float noiseTarget = 0.0f, noiseValue = 0.0f;
    int noiseCountdown = 0;
    float wowDepthSamples = 0.0f, flutDepthSamples = 0.0f;

    // saturation
    float drive = 1.0f;
    float satNorm = 1.0f;
    bool saturate = false;

    // dropouts
    double nextDropoutIn = 1.0e18;
    bool dropActive = false;
    int dropPos = 0, dropLen = 0, dropEdge = 1;
    float dropMinGain = 1.0f;
    float dropRate = 0.0f;

    // splices
    double nextSpliceIn = 1.0e18;
    float spliceRate = 0.0f;
    double offsetFrom = 0.0, offsetTo = 0.0;
    int spliceFade = 0, spliceFadeLen = 1;
    bool spliceSide = false;

    // dc blocker
    float dcR = 0.999f;
    float dcXl = 0, dcYl = 0, dcXr = 0, dcYr = 0;

    // observability
    float minDelay = 1.0e9f, maxDelay = 0.0f;
    int dropoutCount = 0, spliceCount = 0;
};

} // namespace gapfill::fx
