#pragma once

// F-M22 ranking: pure and deterministic. The measured InputProfile is compared with a fixed table
// of "typical input" rows, one per factory preset, by weighted distance. No machine learning.

#include "Profile.h"

#include <juce_core/juce_core.h>

#include <vector>

namespace gapfill::suggest
{

struct FamilyRow
{
    const char* preset;          ///< factory preset name (resolved against getFactoryPresets() at run time)
    float eventsPerSec, gapFraction, levelDb, lowShare, highShare;
    const char* family;          ///< informational: matches the browser categories
};

/** The frozen table (docs/dev/features/F-M22 section 3, extended with one row per later factory preset). */
const std::vector<FamilyRow>& familyTable();

enum class Confidence { Good, Fair, Weak };
enum class Reason { DenseHits, SparseHits, ShortGaps, LongGaps, Loud, Quiet, BassHeavy, TrebleHeavy };

struct Suggestion
{
    int presetIndex = -1;        ///< index into getFactoryPresets()
    float distance = 0.0f;
    Confidence confidence = Confidence::Weak;
    Reason reason = Reason::DenseHits;
};

/** Up to 3 best presets (never "Init"). Ties broken by preset index. Returns the count. */
int rankPresets (const InputProfile&, Suggestion out[3]);

juce::String confidenceWord (Confidence);
juce::String describeReason (const InputProfile&, Reason);
juce::String measuredSummary (const InputProfile&);        ///< "6.2 hits/s, gaps 28 %, -14 dB"

/** Indices of factory presets that have no row (the coverage test requires this to be empty except Init). */
std::vector<int> presetsWithoutRow();

} // namespace gapfill::suggest
