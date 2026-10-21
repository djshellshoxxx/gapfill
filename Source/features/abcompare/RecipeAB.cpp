#include "RecipeAB.h"

namespace gapfill::abcompare
{

void RecipeAB::setSlot (int slot, const FillRecipe& recipe) noexcept
{
    if (! isValid (recipe))
        return;

    const auto i = slot != 0 ? 1 : 0;
    recipes[i] = recipe;
    recipes[i].flags = 0;
    has[i] = true;
    rawRms[i] = 0.0f;                       // a new recipe has not been heard yet

    if (awaiting == i)
        awaiting = -1;
}

void RecipeAB::clear() noexcept
{
    has[0] = has[1] = false;
    rawRms[0] = rawRms[1] = 0.0f;
    nextSlot = 0;
    awaiting = -1;
}

float RecipeAB::trimDbFor (int slot) const noexcept
{
    const auto i = slot != 0 ? 1 : 0;
    const auto other = 1 - i;

    if (! (rawRms[i] > 0.0f) || ! (rawRms[other] > 0.0f))
        return 0.0f;

    // The same rule as the header A/B, in amplitude terms: bring this recipe to the other's level.
    const auto db = 20.0f * std::log10 (rawRms[other] / rawRms[i]);
    return std::isfinite (db) ? juce::jlimit (-kMaxTrimDb, kMaxTrimDb, db) : 0.0f;
}

bool RecipeAB::next (Command& out, std::uint32_t stampMs) noexcept
{
    if (! isReady())
        return false;

    const auto slot = nextSlot;
    const auto trim = trimDbFor (slot);

    out = {};
    out.type = CommandType::PreviewRecipe;
    out.velocity = 100;
    out.recipe = recipes[slot];
    out.index = (std::int16_t) std::lround (trim * 100.0f);
    out.issuedMs = stampMs;

    awaiting = slot;
    awaitingTrimDb = trim;
    nextSlot = 1 - slot;
    return true;
}

void RecipeAB::onPreviewMeasured (float rms) noexcept
{
    if (awaiting < 0 || ! (rms > 0.0f))
        return;

    // Undo the trim that was applied so the stored level is the recipe's own.
    rawRms[awaiting] = rms / std::pow (10.0f, awaitingTrimDb / 20.0f);
    awaiting = -1;
}

} // namespace gapfill::abcompare
