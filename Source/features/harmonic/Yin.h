#pragma once

// F-X01 Harmonic granular resynthesis: the pitch step (docs/dev/12 F-X01).
//
// YIN (de Cheveigne and Kawahara, 2002) on the last 1024 samples of the ~12 kHz decimated mono
// source, tau <= 400, threshold 0.15. Pure, allocation-free (fixed stack arrays), bounded work
// (about 250 k multiply-adds), deterministic in its input.

#include <cstdint>

namespace gapfill::harmonic
{

inline constexpr int kYinWindow = 1024;              ///< decimated samples analysed
inline constexpr int kYinMaxTau = 400;
inline constexpr float kYinThreshold = 0.15f;
inline constexpr float kVoicedConfidence = 0.7f;     ///< doc 12: the fill goes harmonic at >= 0.7

struct YinResult
{
    float f0Hz = 0.0f;
    float confidence = 0.0f;                         ///< 1 - d'(tau*)
    bool  voiced = false;                            ///< found a period and confidence >= kVoicedConfidence
};

/** Decimation factor used for a sample rate: round (sr / 12000), at least 1. */
int yinDecimation (double sampleRate) noexcept;

/** Number of source samples a call needs: kYinWindow * yinDecimation (sr). */
int yinSourceSamples (double sampleRate) noexcept;

/** The YIN core on `kYinWindow` already decimated samples (newest last) at rate `decimatedRate`. */
YinResult yinDetectDecimated (const float* x, double decimatedRate) noexcept;

/** `mono` holds at least `yinSourceSamples (sr)` samples at `sampleRate`, newest last. */
YinResult yinDetect (const float* mono, int numSamples, double sampleRate) noexcept;

} // namespace gapfill::harmonic
