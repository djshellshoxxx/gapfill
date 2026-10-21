#pragma once

// F-M14 Rhythmic bit/rate crush (docs/dev/12 section 7, VoiceFx).
//
// Distinct from Algo::Crush and the Crush modifier (both stay). Amount is the
// dry/wet blend of the voice (equal-gain linear). Quantiser
// q = 2^(bits-1), y = floor(x*q + 0.5)/q (platform independent). Sample-hold
// factor rcRate, or - with rcHold != Free - a hold length equal to the chosen
// division in samples (clamped 1..4096) following the fixed 8-step pattern of
// multipliers [1,1,2,1,4,1,2,1], advancing after every hold. Aliasing is the
// intended character. rcTone -100..0 is a one-pole low-pass (18 kHz -> 1.5 kHz),
// 0..+100 a one-pole high-pass (20 Hz -> 800 Hz). A 20 Hz DC blocker follows
// the quantiser; the wet path is hard-clamped at +-1.5.

#include "EffectCard.h"

namespace gapfill::fx
{

class RcrushCard final : public VoiceFx
{
public:
    static constexpr int kPatternLength = 8;
    static constexpr int kPattern[kPatternLength] { 1, 1, 2, 1, 4, 1, 2, 1 };
    static constexpr int kMaxHold = 4096;

    /** floor(x * 2^(bits-1) + 0.5) / 2^(bits-1) */
    static float quantise (float x, int bits) noexcept;

    /** Hold length in samples for pattern step `step` (0-based, wraps at 8). */
    static int holdLength (const RcrushSettings&, double samplesPerBeat, double sampleRate, int step) noexcept;

    CardId id() const noexcept override { return CardId::RCrush; }
    void prepare (double sampleRate, int maxBlock, int channels) override;
    void reset() override;
    bool enabled (const FxSettings& s) const noexcept override { return s.rcrush.amount01 > kOffThreshold; }

    bool startFill (const VoiceFxStart&) override;
    void processVoice (float* l, float* r, int n, const VoiceFxContext&) noexcept override;

    bool isFillActive() const noexcept { return active; }

private:
    struct Channel
    {
        float held = 0.0f;
        float lp = 0.0f;
        float dcX = 0.0f, dcY = 0.0f;
    };

    void clearState() noexcept;
    float processOne (Channel&, float in, bool refresh) noexcept;

    double sr = 48000.0;
    bool active = false;
    RcrushSettings s;
    double spb = 0.0;
    int step = 0;
    int counter = 0;
    float blend = 0.0f;
    float lpCoeff = 1.0f;       ///< one-pole coefficient of the tone filter
    int toneMode = 0;           ///< 0 off, 1 low-pass, 2 high-pass
    float dcR = 0.999f;
    Channel ch[2];
};

} // namespace gapfill::fx
