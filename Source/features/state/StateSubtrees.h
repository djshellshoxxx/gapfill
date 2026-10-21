#pragma once

// State versioning and optional subtrees (docs/dev/02-FOUNDATIONS.md F0-6).
//
// The plugin state is the APVTS tree plus root properties. Features that need
// non-parameter state (macro mappings, the lock recipe, history, scenes ...)
// own ONE named subtree each, registered in the fixed table in
// StateSubtrees.cpp. Loading is atomic for parameters (a bad APVTS value
// rejects everything, as before) but FAILS SOFT for subtrees: a corrupt or
// oversized optional subtree is dropped and reset to its default while the
// rest of the state is still applied. A corrupt recipe must never block a
// valid preset.
//
// Never serialised: audio samples, the capture latch contents, momentary or
// command state, preview state.

#include <juce_data_structures/juce_data_structures.h>

#include <cstddef>
#include <utility>
#include <vector>

namespace gapfill
{

class GapFillAudioProcessor;

namespace state
{

/** Written to the root property `stateVersion`. Absent means 1 (everything
    shipped before versioning). Phase 0 writes 2; 3 is written once the doc-12
    subtrees have owners (v2 -> v3 sets nothing). */
inline constexpr int kStateVersion = 2;

/** Sum of all subtree sizes (serialised XML text) accepted from one state. */
inline constexpr std::size_t kTotalSubtreeBudget = 64 * 1024;

/** One feature-owned subtree. All members are plain function pointers so the
    table is constant data; null members mean "nothing to do". */
struct SubtreeDesc
{
    const char* name;           ///< ValueTree type, e.g. "Lock"
    std::size_t maxBytes;       ///< cap on the serialised size of this subtree
    bool (*validate) (const juce::ValueTree&);                       ///< structure + ranges; null = accept
    void (*apply) (GapFillAudioProcessor&, const juce::ValueTree&);  ///< message thread
    juce::ValueTree (*serialise) (const GapFillAudioProcessor&);     ///< invalid tree = write nothing
    void (*reset) (GapFillAudioProcessor&);                          ///< back to the default
};

/** The reserved table (docs/dev/12 section 2.3 caps): Macros Lock History Perf
    Scenes Cmd Tension Groove Fx Health Learn. */
const std::vector<SubtreeDesc>& reservedSubtrees();

/** Version stored in `root` (1 when absent or not a positive integer). */
int readStateVersion (const juce::ValueTree& root);

/** Upgrades `root` in place from `fromVersion` to kStateVersion. v1 -> v2 and
    v2 -> v3 set nothing (new parameters default to neutral); the function
    exists so the first real migration has a place to go. */
void migrate (juce::ValueTree& root, int fromVersion);

/** What to do with the subtrees of a loaded state. */
struct SubtreePlan
{
    std::vector<std::pair<const SubtreeDesc*, juce::ValueTree>> toApply;
    std::vector<const SubtreeDesc*> toReset;     ///< absent, invalid, oversized or over budget
    std::vector<const SubtreeDesc*> dropped;     ///< present but rejected (subset of toReset)
};

/** Step 3 of the load order: validate EVERY subtree before anything is applied. */
SubtreePlan planSubtrees (const juce::ValueTree& root, const std::vector<SubtreeDesc>& table);

/** Step 4: applies the plan (after the parameters). */
void applyPlan (GapFillAudioProcessor&, const SubtreePlan&);

/** Appends each non-empty, in-cap serialised subtree to `root`. */
void writeSubtrees (const GapFillAudioProcessor&, juce::ValueTree& root, const std::vector<SubtreeDesc>& table);

/** Serialised size used for the caps. */
std::size_t serialisedSize (const juce::ValueTree&);

} // namespace state
} // namespace gapfill
