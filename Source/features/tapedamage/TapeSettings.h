#pragma once

namespace gapfill::fx
{
/** Opt-in detailed F-M20 path. Independent from the released td* card. */
struct TapeExtraSettings
{
    float amount01 = 0.0f;          ///< probability per fill, not master depth
    float wow01 = 0.30f;
    float flutter01 = 0.25f;
    float sat01 = 0.30f;
    float dropDensity01 = 0.20f;
    float dropDepth01 = 0.60f;
    float splice01 = 0.10f;
    float instab01 = 0.20f;
    float mix01 = 1.0f;
};
} // namespace gapfill::fx
