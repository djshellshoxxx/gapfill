#pragma once

// F-S06 recipe A/B (docs/dev/12-SPEC-BACKLOG-AND-NEW.md F-S06, 10 section 4.2 F-L23):
// two History recipes are set as slot A and slot B, and "A/B" alternates the audition
// between them. Each recipe's level is measured by the engine (RMS of the preview voice)
// and the same trim rule as the header A/B is applied to the PREVIEW gain, so the two
// sound equally loud even when one recipe is naturally quieter.
//
// Message thread only. The trim travels in Command::index (1/100 dB) of a PreviewRecipe
// command; the engine multiplies the preview amplitude by it.

#include "MatchLevels.h"
#include "../../dsp/EngineEvents.h"

namespace gapfill::abcompare
{

class RecipeAB
{
public:
    static constexpr int numSlots = 2;

    /** Sets slot 0 (A) or 1 (B). Invalid recipes are ignored. Resets that slot's measurement. */
    void setSlot (int slot, const FillRecipe& recipe) noexcept;
    void clear() noexcept;

    bool hasSlot (int slot) const noexcept             { return has[slot != 0]; }
    bool isReady() const noexcept                      { return has[0] && has[1]; }
    const FillRecipe& getSlot (int slot) const noexcept { return recipes[slot != 0]; }

    /** The slot the next press will play (alternates A, B, A, B ...). */
    int getNextSlot() const noexcept                   { return nextSlot; }

    /** Builds the PreviewRecipe command for the next press and advances the alternation.
        Returns false until both slots are set. */
    bool next (Command& out, std::uint32_t stampMs) noexcept;

    /** The preview started by next() finished playing; `rms` is the engine's measurement of
        it (with the applied trim in it). Records the recipe's raw level for later trims. */
    void onPreviewMeasured (float rms) noexcept;

    /** The trim (dB) a press of `slot` gets right now; 0 until both recipes have been heard. */
    float trimDbFor (int slot) const noexcept;

    /** The slot whose measurement is awaited (-1 when none). */
    int getAwaitingSlot() const noexcept               { return awaiting; }

    float getRawRms (int slot) const noexcept          { return rawRms[slot != 0]; }

private:
    FillRecipe recipes[numSlots];
    bool has[numSlots] { false, false };
    float rawRms[numSlots] { 0.0f, 0.0f };      ///< level with no trim applied; 0 = not heard yet
    int nextSlot = 0;
    int awaiting = -1;
    float awaitingTrimDb = 0.0f;
};

} // namespace gapfill::abcompare
