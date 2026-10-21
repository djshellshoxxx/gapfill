#pragma once

// Maps a parameter id to the EngineSettings field it drives, and applies a REAL
// (host-unit) value to that field. Shared by the scene overlay (F-L07), the
// macro stage (C-2) and Beat Battles (F-M18) so all three agree on units.
//
// Pure data and arithmetic: no JUCE, no allocation. The id -> Ref lookup is
// meant for start-up (message thread); apply() is audio-thread safe.

#include "../../dsp/EngineSettings.h"

#include <cstdint>

namespace gapfill::targets
{

enum class Kind : std::uint8_t
{
    None = 0, Fill, Intensity, Complexity, Chaos, Density, FillType, Weight,
    ProtTrans, ProtNext, ProtSC, MinGap, MaxFill, ProtectHold,
    BandOn, BandAmt, BandInt, BandProb, XoverLM, XoverMH
};

struct Ref
{
    Kind kind = Kind::None;
    std::uint8_t sub = 0;        ///< algorithm (Weight) or band (Band*)

    explicit operator bool() const noexcept { return kind != Kind::None; }
};

/** Start-up lookup by permanent parameter id; Kind::None when the parameter
    has no EngineSettings field (it is then only committed, never overlaid). */
Ref refForId (const char* id) noexcept;

/** Bit of the macro target table (C-2 macroTargets(): fill 0, intensity 1,
    complexity 2, chaos 3, density 4, width 5, weights 6..20) this reference
    corresponds to, or 0 when macros cannot own it. */
std::uint32_t macroBit (Ref ref) noexcept;

/** Writes `real` (the parameter's own unit: percent, ms, Hz, choice index) into
    the settings field. Audio-thread safe. */
void apply (EngineSettings& s, Ref ref, float real) noexcept;

} // namespace gapfill::targets
