#pragma once

// N-FX2 Granular Cloud (docs/dev/12 section 4): a render mode of Algo::Granular
// voices (recipe flag bit4) with directly controllable grains.
//
//  - Source: the voice's source anchor (the latched Capture material while
//    Capture holds the history, else the recent window [anchor - span, anchor]);
//    never beyond the anchor, never before the start of valid history.
//  - Legacy model 1: inter-onset 1/density +-50 % uniform; grain length
//    `size` +-20 %, Hann window (Tukey below 20 ms).
//  - Model 2: stratified +-5 % onsets, Tukey windows and bounded window-energy
//    normalisation reduce amplitude holes at the SAME Density/Size values.
//    Model2 also matches bounded windowed source energy per grain; silent
//    estimates fall back to unity source gain. Both use equal-power pan.
//  - Position jitter: back = span * (0.5 + jitter * (u - 0.5)); 0 = every grain
//    from the middle of the span, 100 = uniform over the span.
//  - Pitch spread: triangular within +-cents; N-MUS Scale Lock quantises it via
//    the optional hook (identity until Scale Lock is wired).
//  - Bounds: 48 grains per voice (preallocated); estimated overlap
//    density * size is limited to 40 by lowering the effective density; a grain
//    that cannot be allocated is dropped and counted. Output is scaled by
//    1/sqrt(max(1, overlap)) in legacy model 1. Model 2 window gain is bounded\n//    at 2 and source gain at 4 (combined per-grain multiplier at most 8).

#include "../../dsp/DspUtils.h"
#include "../fx/FxSettings.h"

#include <cstdint>

namespace gapfill::fx
{

inline constexpr int kCloudMaxGrains = 48;
inline constexpr float kCloudMaxOverlap = 40.0f;

struct CloudDensity
{
    float effective = 0.0f;       ///< grains per second actually used
    float overlap = 0.0f;         ///< estimated simultaneous grains at that density
    bool  limited = false;
};

/** Applies the concurrency limit: density * size / 1000 <= kCloudMaxOverlap. */
CloudDensity limitCloudDensity (float densityHz, float sizeMs) noexcept;

/** Optional hook (N-MUS Scale Lock): maps a pitch shift in cents to the shift
    of the nearest in-scale interval. Null = identity. */
using CloudPitchQuantiser = float (*) (float cents, void* user);

struct CloudGrain
{
    bool   active = false;
    double pos = 0.0;
    double rate = 1.0;
    int    age = 0;
    int    dur = 1;
    float  panL = 1.0f, panR = 1.0f;
    float  amp = 1.0f;
    float  sourceGain = 1.0f;       ///< Model2: bounded source-energy levelling, separate from window gain.
    bool   tukey = false;
};

class CloudMode
{
public:
    void reset() noexcept;
    bool isActive() const noexcept { return active; }

    /** Engages the cloud when requested. `anchor` is the absolute history
        position the fill is cut from. Returns whether the cloud is active. */
    bool configure (bool requested, const CloudSettings& s, float chaos01, std::int64_t anchor,
                    std::uint64_t voiceSeed, double sampleRate) noexcept;

    /** One output sample. False when inactive (caller renders plain Granular). */
    bool generate (float& l, float& r, const SourceHistory& history, int channels) noexcept;

    /** Draws one grain's parameters (public for tests). `oldest` is the first
        valid absolute history position. */
    CloudGrain drawGrain (std::int64_t oldest) noexcept;

    void setPitchQuantiser (CloudPitchQuantiser q, void* user) noexcept { quantiser = q; quantiserUser = user; }

    int  activeGrains() const noexcept;
    int  droppedGrains() const noexcept { return dropped; }
    const CloudDensity& density() const noexcept { return dens; }
    /** Legacy nominal gain; model2 uses time-varying bounded overlap gain. */
    float outputNorm() const noexcept { return norm; }

private:
    bool active = false;
    CloudSettings s;
    float chaos = 0.0f;
    std::int64_t anchor = 0;
    double sr = 48000.0;
    Rng rng;
    CloudDensity dens;
    float norm = 1.0f;
    float sourceRms = 0.0f;
    bool sourceRmsReady = false;
    int countdown = 0;
    int dropped = 0;
    CloudPitchQuantiser quantiser = nullptr;
    void* quantiserUser = nullptr;
    int grainCap = kCloudMaxGrains;     ///< F-D03 quality cap (<= kCloudMaxGrains)
    CloudGrain grains[kCloudMaxGrains];
};

} // namespace gapfill::fx
