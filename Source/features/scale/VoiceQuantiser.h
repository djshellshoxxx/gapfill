#pragma once

// N-MUS Scale Lock: the per-voice pitch snapper (docs/dev/12 section 5).
//
// A FillVoice owns one. begin() latches the settings and seeds a SALTED
// sub-generator, so the voice's own legacy random draws are never perturbed;
// the only draw is the `scaleAmount` coin, and only when 0 < amount < 1.
// All methods are allocation-free and the identity when the lock is off.

#include "ScaleLock.h"
#include "../Salts.h"
#include "../../dsp/DspUtils.h"

namespace gapfill::scale
{

class VoiceQuantiser
{
public:
    void begin (const ScaleSettings& s, std::uint64_t voiceSeed) noexcept
    {
        settings = s;
        rng.seed (hashCombine (voiceSeed, kSalt_Scale));
    }

    void reset() noexcept { settings = {}; }
    bool active() const noexcept { return settings.active(); }

    /** True when this pitch decision is snapped (draws only when 0 < amount < 1). */
    bool coin() noexcept
    {
        if (settings.amount01 >= 0.9999f)
            return true;

        return settings.amount01 > 0.0f && rng.chance (settings.amount01);
    }

    float snapRatio (float ratio) noexcept
    {
        return (active() && coin()) ? scale::snapRatio (ratio, settings.set) : ratio;
    }

    float snapFrequency (float hz) noexcept
    {
        return (active() && coin()) ? scale::snapFrequency (hz, settings.set) : hz;
    }

    float snapCents (float cents) noexcept
    {
        return (active() && coin()) ? scale::snapCents (cents, settings.set) : cents;
    }

    /** CloudPitchQuantiser adapter (features/cloud/CloudMode.h). */
    static float cloudQuantise (float cents, void* user) noexcept
    {
        return static_cast<VoiceQuantiser*> (user)->snapCents (cents);
    }

private:
    ScaleSettings settings;
    Rng rng;
};

} // namespace gapfill::scale
