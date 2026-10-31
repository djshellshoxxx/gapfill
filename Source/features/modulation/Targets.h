#pragma once

#include "ExtendedConfig.h"

namespace gapfill::modulation
{
struct TargetInfo
{
    const char* id = nullptr;
    const char* label = nullptr;
    float low = 0, high = 1;
    float skew = 1;
    bool integer = false;
};

struct TargetBase
{
    float tensionDepth[tension::kLanes] {};
};

struct TargetOutputs
{
    float fxSend01 = 1; ///< Virtual target: compose with Tension once after macro evaluation.
};

/** Message-thread/startup only. Resolves all physical fields once. */
void initTargets();
TargetInfo targetInfo (int index) noexcept;
const char* targetParameterId (int index, bool detailedTapeActive = false) noexcept;
float targetRealValue (int index, float normalised) noexcept;
TargetBase captureTargetBase (const EngineSettings&) noexcept;
void applyTarget (EngineSettings&, int index, float normalised, const TargetBase&, TargetOutputs&) noexcept;
std::uint32_t coreOwnership (int index) noexcept;
std::uint32_t advancedOwnership (int index) noexcept;
bool isContinuousTarget (int index) noexcept;
} // namespace gapfill::modulation
