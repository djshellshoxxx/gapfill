#pragma once

// F-L20 Formant shifting (docs/dev/12 section 7, BusFx, SP-0).
//
// Amount = wet blend: out = (1 - amount) x delayed dry + amount x processed. The
// STFT (N = 1024 up to 48 kHz, 2048 above, hop N/4) comes from SP-0. The spectral
// envelope is estimated by cepstral smoothing (lifter Q = round(sr/1000)); the
// formant shift warps the envelope on the frequency axis by 2^(fmtShift/12) and
// re-imposes env_warped/env (gain clamped +-24 dB) on the UNCHANGED fine structure
// and phases, so the fundamental pitch is preserved. fmtTilt adds 6 x tilt dB per
// octave about 1 kHz; fmtVowel imposes a fixed three-peak envelope (A 800/1200/2800,
// E 400/2000/2600, I 300/2200/3000, O 450/800/2800, U 325/700/2700 Hz, Q ~ 5). No
// speech recognition is implied. A transient guard (large spectral flux) bypasses the
// envelope warp for that frame. Latency: the card delays the whole path (wet and dry)
// by the SP-0 latency, declared via latencySamples(); the card applies the engine duck
// to its output so the next hit stays protected. With Scale Lock active (scaleMask != 0)
// the shift snaps to an in-scale number of semitones. Disabled in Lite.

#include "BusCardBase.h"
#include "../../../dsp/spectral/Stft.h"

namespace gapfill::fx::bus
{

class FormantCard final : public BusCardBase, private spectral::SpectralCallback
{
public:
    static constexpr float kMaxGainDb = 24.0f;
    static constexpr float kFluxBypass = 0.6f;

    /** Formant frequencies (Hz) of the vowels A E I O U. */
    static const float (&vowelFormants (int vowel) noexcept)[3];

    CardId id() const noexcept override { return CardId::Formant; }
    int latencySamples() const noexcept override { return stft.latencySamples(); }
    bool enabled (const FxSettings& s) const noexcept override { return ! s.lite && s.formant.amount01 > kOffThreshold; }

    /** Snaps a semitone shift to the nearest in-scale interval (identity when mask == 0). */
    static float snapShift (float semitones, std::uint16_t scaleMask) noexcept;

    std::int64_t framesBypassed() const noexcept { return bypassed; }
    std::size_t preallocatedBytes() const noexcept;

protected:
    float amountOf (const FxSettings& s) const noexcept override { return s.formant.amount01; }
    void prepareCard() override;
    void resetCard() noexcept override;
    void blockSettings (const FxSettings&, const BusFxContext&) noexcept override;
    void renderChunk (float*, float*, int, const float*, const float*, const BusFxContext&, const FxSettings&) noexcept override;

private:
    void processFrame (spectral::SpectralFrame&) noexcept override;

    spectral::Stft stft;
    std::unique_ptr<juce::dsp::FFT> cfft;
    std::vector<float> cepBuf, logMag, envLog, target, prevMag[2];
    DelayLine dry[2];
    int lifterQ = 48;

    float shiftSt = 0.0f, tiltDb = 0.0f;
    int vowel = 0;
    std::int64_t bypassed = 0;

    static constexpr int kMaxIn = BusCardBase::kChunk;
    float outL[kMaxIn] {}, outR[kMaxIn] {};
};

} // namespace gapfill::fx::bus
