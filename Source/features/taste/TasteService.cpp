#include "TasteService.h"

#include <algorithm>
#include <cmath>

namespace gapfill::taste
{

TasteService::TasteService (juce::File libraryFile) : library (std::move (libraryFile))
{
    for (auto& q : pubQ)
        q.store (0, std::memory_order_relaxed);
}

void TasteService::useLibraryFile (juce::File f)
{
    library = RatingStore (std::move (f));
    libraryLoaded = false;
}

TasteBias TasteService::currentBias() const noexcept
{
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        const auto a = seq.load (std::memory_order_acquire);

        if ((a & 1u) != 0)
            continue;

        std::int8_t q[kFeatures];

        for (int i = 0; i < kFeatures; ++i)
            q[i] = pubQ[i].load (std::memory_order_relaxed);

        const bool ready = pubReady.load (std::memory_order_relaxed);

        if (seq.load (std::memory_order_acquire) == a)
            return dequantise (q, ready);
    }

    return {};                                           // torn read: neutral for this block
}

void TasteService::publish()
{
    seq.fetch_add (1, std::memory_order_acq_rel);

    for (int i = 0; i < kFeatures; ++i)
        pubQ[i].store (profile.q[i], std::memory_order_relaxed);

    pubReady.store (profile.ready, std::memory_order_relaxed);
    seq.fetch_add (1, std::memory_order_release);
}

void TasteService::apply (EngineSettings& s) const noexcept
{
    const auto& t = s.perf.taste;

    if (! t.on || s.perf.lockOn || s.fillTypeIndex > 0)
        return;                                          // ignored while Fill Lock is on or the type is forced

    const auto bias = currentBias();

    if (! bias.ready)
        return;

    for (int a = 0; a < kAlgoFeatures; ++a)
        s.weights[a] *= bias.algo[a];                    // a zero weight stays zero

    for (int b = 0; b < kBandFeatures; ++b)
        s.bandProb[b] = std::min (1.0f, s.bandProb[b] * bias.band[b]);
}

void TasteService::onCommand (const Command& c) noexcept
{
    switch (c.type)
    {
        case CommandType::LikeLatest:
        case CommandType::LikeRecipe:
        case CommandType::SkipLatest:
        case CommandType::SkipRecipe:
        {
            RatingEvent e;
            e.type = c.type;
            e.recipe = c.recipe;
            events.push (e);
            break;
        }

        default:
            break;
    }
}

void TasteService::onFillEvent (const FillEvent& e) noexcept
{
    if (e.type == FillEventType::Started && (e.recipe.flags & FillRecipe::flagPreview) == 0)
    {
        latest = e.recipe;
        haveLatest = true;
    }
}

void TasteService::recompute()
{
    if (! libraryLoaded)
    {
        library.load();
        libraryLoaded = true;
    }

    displayCounts = library.counts();
    const auto bias = biasFrom (displayCounts, lastStrength);
    profile.likes = displayCounts.likes;
    profile.skips = displayCounts.skips;
    profile.ready = bias.ready;
    quantise (bias, profile.q);
    publish();
}

void TasteService::rate (const FillRecipe& r, int rating)
{
    if (! libraryLoaded)
    {
        library.load();
        libraryLoaded = true;
    }

    library.add (r, rating);
    dirty = true;
    recompute();
}

void TasteService::rateLatest (int rating)
{
    if (haveLatest)
        rate (latest, rating);
}

bool TasteService::undoLast()
{
    if (! libraryLoaded)
    {
        library.load();
        libraryLoaded = true;
    }

    if (! library.removeLast())
        return false;

    dirty = true;
    recompute();
    return true;
}

void TasteService::rebuildFromLibrary()
{
    library.load();
    libraryLoaded = true;
    recompute();
}

void TasteService::resetProfile (bool alsoClearLibrary)
{
    profile = {};
    displayCounts = {};
    publish();

    if (alsoClearLibrary)
    {
        library.clear();
        dirty = true;
    }
}

void TasteService::setProjectProfile (const ProjectProfile& p)
{
    profile = p;
    displayCounts = {};
    displayCounts.likes = p.likes;
    displayCounts.skips = p.skips;
    publish();
}

void TasteService::timerTick (std::uint32_t nowMs, bool on, float strength01)
{
    RatingEvent batch[8];

    for (;;)
    {
        const auto n = events.pop (batch, 8);

        if (n <= 0)
            break;

        for (int i = 0; i < n; ++i)
        {
            const int rating = (batch[i].type == CommandType::LikeLatest || batch[i].type == CommandType::LikeRecipe) ? 1 : -1;
            const bool latestKind = batch[i].type == CommandType::LikeLatest || batch[i].type == CommandType::SkipLatest;

            if (! on)
                continue;                                // ratings are only collected with "Learn my taste" on

            if (latestKind)
                rateLatest (rating);
            else
                rate (batch[i].recipe, rating);

            if (dirty && dirtySinceMs == 0)
                dirtySinceMs = nowMs;
        }
    }

    if (std::abs (strength01 - lastStrength) > 1.0e-4f)
    {
        lastStrength = strength01;

        if (libraryLoaded && (displayCounts.likes + displayCounts.skips) > 0)
            recompute();
    }

    if (dirty)
    {
        if (dirtySinceMs == 0)
            dirtySinceMs = nowMs;

        if (nowMs - dirtySinceMs >= 500)                 // debounced, atomic file write
        {
            library.save();
            dirty = false;
            dirtySinceMs = 0;
        }
    }
}

} // namespace gapfill::taste
