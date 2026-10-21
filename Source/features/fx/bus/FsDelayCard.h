#pragma once

// F-L19 Frequency-shifted delay (docs/dev/12 section 7, BusFx).
//
// Amount = wet blend (out = in + amount x echoes). A stereo feedback delay (<= 2 s
// per channel, preallocated) with a single-sideband frequency shifter inside the
// loop, so every repeat shifts by fsdShift Hz (sign = direction): a 63-tap Kaiser
// FIR Hilbert pair plus a quadrature phasor renormalised every 256 samples (no
// drift). The Hilbert delay (31 samples) is subtracted from the loop delay (minimum
// loop delay 64 samples). Time = fsdTime division (kFallbackBpm without transport)
// slewed over ~50 ms; feedback 0-85 %; damping = 4-pole low-pass in the loop
// (cutoff lerp(16 kHz, 2.5 kHz, damp), never above 0.45 sr); R delay x(1 + 0.25 x
// stereo) and the shift sign is inverted at stereo 100 %. DC blocker, hard loop gain
// cap 0.85, tail cap 4 s. fsdShift = 0 bypasses the Hilbert (a plain damped delay).

#include "BusCardBase.h"

namespace gapfill::fx::bus
{

class FsDelayCard final : public BusCardBase
{
public:
    static constexpr int kHilbertTaps = 63;
    static constexpr int kHilbertDelay = 31;
    static constexpr int kMinLoopDelay = 64;
    static constexpr float kMaxFeedback = 0.85f;

    CardId id() const noexcept override { return CardId::FsDelay; }
    int tailCapMs() const noexcept override { return 4000; }

    /** Largest delay line allocated at this sample rate (samples). */
    int maxDelaySamples() const noexcept { return (int) (2.0 * sr); }

protected:
    float amountOf (const FxSettings& s) const noexcept override { return s.fsdelay.amount01; }
    void prepareCard() override;
    void resetCard() noexcept override;
    void blockSettings (const FxSettings&, const BusFxContext&) noexcept override;
    void renderChunk (float*, float*, int, const float*, const float*, const BusFxContext&, const FxSettings&) noexcept override;

private:
    struct Chan
    {
        DelayLine line;
        OnePole lp[4];
        DcBlock dc;
        float hist[64] {};
        int hw = 0;
        float cs = 1.0f, sn = 0.0f;          ///< quadrature phasor
        float cd = 1.0f, sd = 0.0f;          ///< per-sample rotation
        float delayCur = 0.0f, delayTarget = 0.0f;
        int sinceNorm = 0;
        bool primed = false;
    };

    float hilbertTap[kHilbertTaps] {};
    Chan ch[2];
    float fb = 0.4f, dampA = 1.0f;
    float shiftHz[2] { 0.0f, 0.0f };
    bool shiftOn[2] { false, false };
    float slewK = 0.01f;
};

} // namespace gapfill::fx::bus
