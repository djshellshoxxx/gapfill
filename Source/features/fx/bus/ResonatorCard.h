#pragma once

// F-M15 Comb resonator (docs/dev/12 section 7, BusFx).
//
// Amount = wet blend: out = in + amount x resonated. A feedback comb per
// channel tuned to f0 = 440 x 2^((note-69)/12) with a Lagrange-3 fractional
// delay, decay RT60 0.05-2 s (g = 10^(-3 D / (sr RT60)), capped at 0.97),
// in-loop one-pole damping and DC blocker, a 40 Hz input high-pass, gain
// compensation sqrt(1-g), and a stereo spread of +-15 cents x resSpread (L down,
// R up). The loop's own phase delay (damping, DC blocker) is subtracted from the
// delay so the pitch is right. Scale degrees mode (needs Scale Lock, else it
// behaves as Manual) picks an in-scale note within +-12 semitones of resNote at
// every fill start with the sub-RNG kSalt_Resonator; retuning crossfades 10 ms
// between two combs. Tail cap 2 s; ducked by the engine duck.

#include "BusCardBase.h"

namespace gapfill::fx::bus
{

inline constexpr float kDcR = 0.999f;

class ResonatorCard final : public BusCardBase
{
public:
    static constexpr float kMaxFeedback = 0.97f;

    CardId id() const noexcept override { return CardId::Resonator; }
    int tailCapMs() const noexcept override { return 2000; }

    /** Frequency of a MIDI note. */
    static float noteToHz (float note) noexcept { return 440.0f * std::exp2 ((note - 69.0f) / 12.0f); }

    /** The note picked for a fill: an in-scale pitch within +-12 semitones of `centre`
        (24..96), or `centre` when `scaleMask` is 0. Deterministic in `seed`. */
    static int pickScaleNote (int centre, std::uint16_t scaleMask, int scaleRoot, std::uint32_t seed) noexcept;

    float currentNote() const noexcept { return note; }

protected:
    float amountOf (const FxSettings& s) const noexcept override { return s.resonator.amount01; }
    void prepareCard() override;
    void resetCard() noexcept override;
    void blockSettings (const FxSettings&, const BusFxContext&) noexcept override;
    void renderChunk (float*, float*, int, const float*, const float*, const BusFxContext&, const FxSettings&) noexcept override;

private:
    struct Comb
    {
        DelayLine line;
        OnePole damp;
        DcBlock dc;
        float delay = 100.0f;
        float g = 0.5f;
        float dampA = 1.0f;

        float process (float x) noexcept
        {
            auto fb = line.read (delay);
            fb = damp.lp (fb, dampA);
            fb = dc.process (fb, kDcR);
            const auto y = x + g * fb;
            line.push (flush (y));
            return y;
        }
    };

    void retune (float noteNow, const ResonatorSettings&) noexcept;
    void tuneComb (Comb&, float hz, const ResonatorSettings&) const noexcept;

    Comb comb[2][2];                 ///< [channel][A/B]
    OnePole inHp[2];
    int active[2] { 0, 0 };
    float xf = 1.0f;                 ///< crossfade position 0..1 towards the other comb
    bool crossfading = false;
    int xfLen = 480;

    float note = 60.0f;
    float lastRequested = -1.0f;
    float lastSpread = -1.0f, lastDecay = -1.0f, lastDamp = -1.0f;
    float comp = 0.5f;
};

} // namespace gapfill::fx::bus
