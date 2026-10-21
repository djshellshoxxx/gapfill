#pragma once

// F-L15 Dynamic dub feedback network (docs/dev/12 section 7, BusFx).
//
// Amount = wet blend (out = in + amount x echoes). Stereo ping-pong (dubPing) or
// parallel delay, dubDiv (kFallbackBpm without transport, max 2 s), the loop is a
// 12 dB/oct high-pass (dubLow) -> 12 dB/oct low-pass (dubHigh) -> saturator
// tanh (d x) / d with d = 1 + 3 dubSat (small-signal gain 1, output < 1/d x ...)
// -> feedback min (dubFeedback, 90 %). Bounded movement dubMove: the low-pass
// cutoff is modulated +-1.5 octave x move by a tempo-locked LFO (period 2 bars =
// 8 beats, 4 s without transport), updated every 16 samples and smoothed, plus
// repeat darkening (an extra in-loop one-pole at 0.9 x 3 x the low-pass cutoff).
// DC blocker, output soft clamp, per-card tail cap 8 s (setTailCapMs, 2-16 s)
// with a forced exponential decay once the input stops.

#include "BusCardBase.h"

namespace gapfill::fx::bus
{

class DubCard final : public BusCardBase
{
public:
    static constexpr float kMaxFeedback = 0.9f;

    CardId id() const noexcept override { return CardId::Dub; }
    int tailCapMs() const noexcept override { return tailMs.load (std::memory_order_relaxed); }

    /** Options: 2000..16000 ms (takes effect on the next prepare). */
    void setTailCapMs (int ms) noexcept { tailMs.store (std::clamp (ms, 2000, 16000), std::memory_order_relaxed); }

    /** The LFO value (-1..1) at a phase in 0..1 (exposed for tests). */
    static float lfoShape (float phase01) noexcept { return std::sin (kTwoPi * phase01); }

protected:
    float amountOf (const FxSettings& s) const noexcept override { return s.dub.amount01; }
    void prepareCard() override;
    void resetCard() noexcept override;
    void blockSettings (const FxSettings&, const BusFxContext&) noexcept override;
    void renderChunk (float*, float*, int, const float*, const float*, const BusFxContext&, const FxSettings&) noexcept override;

private:
    struct Chan
    {
        DelayLine line;
        Svf hp, lp;
        OnePole dark;
        DcBlock dc;
    };

    void updateFilters() noexcept;

    Chan ch[2];
    std::atomic<int> tailMs { 8000 };
    float delayCur[2] { 0, 0 }, delayTarget[2] { 0, 0 };
    bool primed = false;
    float slewK = 0.01f;

    float fb = 0.5f, drive = 1.0f;
    float lowHz = 120.0f, highHz = 4000.0f, move = 0.0f;
    bool ping = true;
    double lfoPeriod = 192000.0;
    double lfoPhase = 0.0;
    float cutoffSmooth = 4000.0f;
    int sinceUpdate = 0;
    float darkA = 1.0f;
};

} // namespace gapfill::fx::bus
