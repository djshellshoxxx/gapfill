#pragma once

// Message-thread logs fed by the audio -> message `fillEvents` queue
// (docs/dev/02-FOUNDATIONS.md F0-4, revision R2). They live in the processor,
// not the editor, so history survives the editor being destroyed and recreated.
// Not thread-safe: message thread only.

#include "EngineEvents.h"

#include <array>
#include <atomic>

namespace gapfill
{

/** Where a recorded fill sits on the output timeline (absolute sample positions), so
    the exporter can find its audio (U-2). Zero length = unknown (restored from state). */
struct FillSpan
{
    std::int64_t startPos = 0;
    std::int32_t lengthSamples = 0;

    bool isKnown() const noexcept   { return lengthSamples > 0; }
};

/** The last 8 natural fills that started, newest first (docs/dev/05 U-1).

    Owned by the processor on the message thread. `generation` is the only member
    another thread may read: it changes whenever the contents do, so the editor
    can poll it at 15 Hz and repaint on change. */
class RecipeHistory
{
public:
    static constexpr int capacity = 8;

    void push (const FillRecipe& r, FillSpan span = {}, const RecipeExtras& extra = {}) noexcept
    {
        for (int i = capacity - 1; i > 0; --i)
        {
            items[(size_t) i] = items[(size_t) i - 1];
            spans[(size_t) i] = spans[(size_t) i - 1];
            extras[(size_t) i] = extras[(size_t) i - 1];
        }

        items[0] = r;
        spans[0] = span;
        extras[0] = normalizeRecipeExtrasForAlgorithm (r.algo, extra);
        count = count < capacity ? count + 1 : capacity;
        touch();
    }

    int size() const noexcept                       { return count; }

    /** 0 = newest. */
    const FillRecipe& get (int i) const noexcept    { return items[(size_t) juce::jlimit (0, capacity - 1, i)]; }
    const FillSpan& getSpan (int i) const noexcept  { return spans[(size_t) juce::jlimit (0, capacity - 1, i)]; }
    const RecipeExtras& getExtras (int i) const noexcept { return extras[(size_t) juce::jlimit (0, capacity - 1, i)]; }

    void clear() noexcept                           { count = 0; extras = {}; touch(); }

    /** Replaces the contents with `n` recipes (newest first), e.g. from saved state.
        Spans are unknown: positions from an earlier session mean nothing. */
    void restore (const FillRecipe* recipes, int n, const RecipeExtras* metadata = nullptr) noexcept
    {
        count = 0;
        extras = {};

        for (int i = 0; i < n && i < capacity; ++i)
        {
            items[(size_t) i] = recipes[i];
            spans[(size_t) i] = {};
            if (metadata != nullptr)
                extras[(size_t) i] = normalizeRecipeExtrasForAlgorithm (recipes[i].algo, metadata[i]);
            ++count;
        }

        touch();
    }

    std::uint32_t getGeneration() const noexcept    { return generation.load (std::memory_order_relaxed); }

    /** Message-thread notification; the owner publishes a copied audio-thread view. */
    void setChangeCallback (void (*fn) (void*) noexcept, void* context) noexcept
    {
        changed = fn;
        changedContext = context;
    }

private:
    void touch() noexcept
    {
        generation.fetch_add (1, std::memory_order_relaxed);
        if (changed != nullptr)
            changed (changedContext);
    }

    std::array<FillRecipe, (size_t) capacity> items {};
    std::array<FillSpan, (size_t) capacity> spans {};
    std::array<RecipeExtras, (size_t) capacity> extras {};
    int count = 0;
    std::atomic<std::uint32_t> generation { 0 };
    void (*changed) (void*) noexcept = nullptr;
    void* changedContext = nullptr;
};

/** The last 64 gap outcomes (started or rejected), newest first. */
class GapOutcomeLog
{
public:
    static constexpr int capacity = 64;

    void push (const FillEvent& e) noexcept
    {
        write = (write + capacity - 1) % capacity;      // newest at `write`
        items[(size_t) write] = e;
        count = count < capacity ? count + 1 : capacity;
    }

    int size() const noexcept                       { return count; }

    /** 0 = newest. */
    const FillEvent& get (int i) const noexcept
    {
        return items[(size_t) ((write + juce::jlimit (0, capacity - 1, i)) % capacity)];
    }

    void clear() noexcept                           { count = 0; write = 0; }

private:
    std::array<FillEvent, (size_t) capacity> items {};
    int write = 0;
    int count = 0;
};

} // namespace gapfill
