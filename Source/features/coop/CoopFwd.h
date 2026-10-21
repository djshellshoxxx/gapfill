#pragma once

// F-X06: the only part of the co-op module that EngineSettings needs.

namespace gapfill::coop
{

struct CoopView;

struct CoopSettings
{
    const CoopView* view = nullptr;        ///< null = Off, alone, or paused: the engine skips the check
};

} // namespace gapfill::coop
