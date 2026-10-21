#pragma once

// The <Fx> state subtree (docs/dev/12 FX-0): which effect cards are folded in the
// Effects section and the amount each card had when it was last switched off, so
// the enable LED can restore it (50 % the first time). <= 1 KiB, no audio.
//
// Atomic members: written by the editor / state loader, read by the host's state
// save, possibly on different threads. Never read by the audio thread.

#include "EffectCard.h"

#include <juce_data_structures/juce_data_structures.h>

#include <atomic>

namespace gapfill
{
class GapFillAudioProcessor;
}

namespace gapfill::fx
{

/** Stable string id of a card, used in the subtree and for the help text. */
const char* cardKey (CardId id) noexcept;

struct FxUiState
{
    static constexpr float kDefaultEnableAmount = 50.0f;

    std::atomic<bool>  folded[kNumCards];             ///< true = card body hidden (default: folded)
    std::atomic<float> rememberedAmount[kNumCards];   ///< 0 = never set

    FxUiState() { reset(); }

    void reset() noexcept;
    bool isDefault() const noexcept;
};

/** <Fx> row of the reserved subtree table (StateSubtrees.cpp). */
bool validateFxTree (const juce::ValueTree&);
void applyFxTree (GapFillAudioProcessor&, const juce::ValueTree&);
juce::ValueTree serialiseFxTree (const GapFillAudioProcessor&);
void resetFxTree (GapFillAudioProcessor&);

} // namespace gapfill::fx
