#pragma once

// U-1 Fill History: pure, localisation-ready text for a recipe chip
// (docs/dev/05-SPEC-USABILITY-VALUE.md U-1).
//
//   describeRecipe:  "Stutter 1/32 - High - 180 ms"
//
// All wording comes from one string table (`strings()`) so a later language pack
// (F-P04) replaces data, not code. No allocation-free promise: message thread only.

#include "../../dsp/FillRecipe.h"
#include "../../dsp/EngineSettings.h"

#include <juce_core/juce_core.h>

namespace gapfill::history
{

struct DescribeStrings
{
    const char* bands[3];       ///< Low, Mid, High
    const char* millis;         ///< unit after the length, e.g. "ms"
    const char* separator;      ///< between parts, e.g. " - "
    const char* locked;         ///< "Locked"
    const char* armed;          ///< "Lock armed"
};

const DescribeStrings& strings() noexcept;

/** "<Algorithm>[ <division>][ - <band>] - <length> ms". `bpm` > 0 is accepted for
    tempo-aware wording; the text does not change with it today. */
juce::String describeRecipe (const FillRecipe&, double bpm);

/** One letter per algorithm family (R rhythmic, T textural, P pitch/tape, D destructive):
    chips carry it so colour is never the only cue. */
char familyLetter (int algo) noexcept;

/** The Division index (0..11) whose length at the current tempo matches the Stutter /
    Delay time the fill uses, or FillRecipe::noDivision. */
std::uint8_t recipeDivision (int algo, const EngineSettings&, double sampleRate) noexcept;

} // namespace gapfill::history
