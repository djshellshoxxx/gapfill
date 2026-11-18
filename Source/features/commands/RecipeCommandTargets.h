#pragma once

// Immutable message -> audio history targets for MIDI recipe commands. The message thread publishes after history
// changes; MIDI captures that generation at receipt. A queued command whose target generation has changed is rejected
// rather than silently acting on a different recipe at the same history index. GUI commands already carry a recipe.

#include "../../dsp/EngineEvents.h"
#include "../common/TripleBuffer.h"

#include <array>
#include <cstdint>

namespace gapfill::commands
{

struct RecipeTargetSnapshot
{
    static constexpr int capacity = 8;
    std::uint32_t generation = 0;
    int count = 0;
    std::array<FillRecipe, capacity> recipes {};
    std::array<RecipeExtras, capacity> extras {};
};

constexpr bool needsRecipeTarget (CommandType type) noexcept
{
    return type == CommandType::LikeRecipe || type == CommandType::LockRecipe
        || type == CommandType::PreviewRecipe || type == CommandType::SkipRecipe;
}

class RecipeCommandTargets
{
public:
    /** Message thread only; payload is copied, never retained by reference. */
    void publish (const RecipeTargetSnapshot& value) noexcept { publisher.publish (value); }
    /** Audio thread, once before draining queued MIDI commands. */
    void adopt() noexcept { publisher.consume (active); }
    std::uint32_t generation() const noexcept { return active.generation; }

    bool resolve (CommandType type, int index, std::uint32_t generation, Command& out) const noexcept
    {
        if (! needsRecipeTarget (type) || generation != active.generation || active.count < 0
            || active.count > RecipeTargetSnapshot::capacity || index < 0 || index >= active.count
            || ! gapfill::isValid (active.recipes[(size_t) index])
            || ! gapfill::isValid (active.extras[(size_t) index]))
            return false;

        out.type = type;
        out.recipe = active.recipes[(size_t) index];
        out.extras = normalizeRecipeExtrasForAlgorithm (out.recipe.algo, active.extras[(size_t) index]);
        out.index = 0;                 // PreviewRecipe's index is gain trim, not the MIDI history-slot index.
        return true;
    }

private:
    TripleBuffer<RecipeTargetSnapshot> publisher;
    RecipeTargetSnapshot active;
};

} // namespace gapfill::commands
