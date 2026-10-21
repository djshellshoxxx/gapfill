#pragma once

// C-5 Fill Lock: the audio-thread state machine (docs/dev/04-SPEC-CREATIVE.md C-5).
//
// Contract
//  * Owned and mutated by the audio thread only (FillEngine member). The message
//    thread learns about changes through FillEvent::LockSet and drives the engine
//    through the LockRecipe / LockLatest / Unlock commands.
//  * "Engaged" follows the `fl01_lock` parameter. Engaging stores the latest
//    completed NATURAL recipe (previews are never natural). With no such recipe
//    the lock is ARMED and the next natural fill becomes the lock. Disengaging
//    keeps the recipe; re-engaging uses the latest fill again, unless the recipe
//    was set explicitly (LockRecipe: History "Lock this", state restore).
//  * Substitution happens only while engaged, valid and not armed.
//  * No allocation, no locks. Everything is plain data.

#include "../../dsp/FillRecipe.h"

namespace gapfill
{

struct LockState
{
    FillRecipe recipe;                 ///< the locked recipe
    bool valid = false;
    bool explicitSet = false;          ///< recipe came from LockRecipe, not "latest"
    bool armed = false;                ///< engaged with nothing to lock yet
    bool engaged = false;              ///< last seen value of fl01_lock
    bool dirty = false;                ///< `recipe`/`valid` changed: publish LockSet

    FillRecipe lastNatural;            ///< latest started natural fill (previews excluded)
    bool haveLast = false;

    /** True while fills must be replaced by `recipe`. */
    bool substituting() const noexcept     { return engaged && valid && ! armed; }

    /** Once per block with the current value of the `fl01_lock` parameter. */
    void update (bool lockOn) noexcept
    {
        if (lockOn && ! engaged)
        {
            engaged = true;

            if (! explicitSet && haveLast)
            {
                recipe = lastNatural;
                valid = true;
                armed = false;
                dirty = true;
            }
            else if (! valid)
            {
                armed = true;
            }
        }
        else if (! lockOn && engaged)
        {
            engaged = false;
            armed = false;
            explicitSet = false;
        }
    }

    /** A natural fill started (called from noteFillStarted, never for previews). */
    void noteStarted (const FillRecipe& r) noexcept
    {
        lastNatural = r;
        lastNatural.flags = (std::uint8_t) (lastNatural.flags & ~(FillRecipe::flagFromLock | FillRecipe::flagPreview));
        haveLast = true;

        if (engaged && armed)
        {
            recipe = lastNatural;
            valid = true;
            armed = false;
            dirty = true;
        }
    }

    /** LockRecipe command (History "Lock this", state restore). Invalid recipes are ignored. */
    void setExplicit (const FillRecipe& r) noexcept
    {
        if (! isValid (r))
            return;

        recipe = r;
        recipe.flags = (std::uint8_t) (recipe.flags & ~(FillRecipe::flagFromLock | FillRecipe::flagPreview));
        valid = true;
        explicitSet = true;
        armed = false;
        dirty = true;
    }

    /** LockLatest command: store the latest natural recipe now, or arm if there is none. */
    void lockLatest() noexcept
    {
        if (haveLast)
        {
            recipe = lastNatural;
            valid = true;
            explicitSet = true;
            armed = false;
            dirty = true;
        }
        else
        {
            armed = true;
        }
    }

    /** Unlock command: forget the locked recipe. While engaged the lock re-arms. */
    void clear() noexcept
    {
        valid = false;
        explicitSet = false;
        armed = engaged;
        dirty = true;
    }

    /** Engine reset (prepareToPlay, host reset): the latest-fill memory goes, the lock stays. */
    void resetRuntime() noexcept
    {
        haveLast = false;
        engaged = false;
        armed = false;
    }
};

} // namespace gapfill
