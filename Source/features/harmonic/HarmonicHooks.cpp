#include "HarmonicHooks.h"

#include "../Salts.h"
#include "Yin.h"
#include "psola/PitchSourceRouter.h"
#include "psola/IntervalSets.h"

#include <cmath>

namespace gapfill::harmonic
{

void decide (VoiceSetup& setup, const FillPlan& plan, const GapContext& ctx, const EngineSettings& s,
             const SourceHistory& history, fx::FxMeters* meters) noexcept
{
    using namespace psola;
    const auto& incoming = setup.recipeExtras.harmonic;
    const bool recorded = incoming.version == 1 && applicableToAlgorithm (incoming, (std::uint8_t) setup.algo);
    const bool detailed = setup.algo == Algo::Granular
        && (recorded || (! setup.replayRecipe && s.perf.pitchSync.enabled));
    if (detailed)
    {
        auto& selected = setup.pitchSync;
        selected.settings = s.perf.pitchSync;
        if (s.perf.extendedMacroRuntime == nullptr)
            selected.settings.mix01 *= s.perf.fxSend01;
        selected.settings.enabled = true;
        if (s.scaleLock.active())
            selected.scale = absoluteScale (s.scaleLock.set.root, s.scaleLock.set.mask);
        auto& meta = setup.recipeExtras.harmonic;
        if (recorded)
        {
            selected.settings.intervals = (IntervalSet) incoming.intervals;
            selected.settings.voices = incoming.voices;
            selected.f0Hz = incoming.f0Hz;
            selected.clarity = incoming.clarity;
            selected.voiced = incoming.outcome == Outcome::Used;
        }
        else
        {
            meta = { 1, Outcome::Bypassed, (std::uint8_t) selected.settings.intervals,
                (std::uint8_t) selected.settings.voices, 0.0f, 0.0f };
            if (selected.settings.mix01 > 0.0f)
            {
                const auto availableFrom = juce::jmax ((std::int64_t) 0, history.getReadFloor(),
                    history.getWritePosition() - history.getSize() + 2);
                const auto first = juce::jmax (availableFrom,
                    setup.sourceAnchor - (std::int64_t) std::ceil (0.32 * setup.sampleRate));
                const auto query = s.perf.pitchTracker != nullptr
                    ? s.perf.pitchTracker->query (first, setup.sourceAnchor - 1) : PitchSourceRouter::Query {};
                selected.voiced = query.voiced;
                selected.f0Hz = query.voiced ? query.f0Hz : 0.0f;
                selected.clarity = query.clarity;
                meta.outcome = query.voiced ? Outcome::Used : Outcome::UnvoicedFallback;
                meta.f0Hz = selected.f0Hz;
                meta.clarity = selected.clarity;
            }
        }
        setup.modes.cloud = false;
        return;
    }

    const auto& hp = s.perf.harmonic;

    if (hp.amount01 <= 0.0f || setup.algo != Algo::Granular)
        return;

    if (hp.amount01 < 0.9999f)
    {
        Rng sub;
        sub.seed (hashCombine (ctx.key, kSalt_HGran));

        if (! sub.chance (hp.amount01))
            return;
    }

    // ---- pitch step: the last 1024 samples of the decimated source ending at the anchor -------------
    const double sr = setup.sampleRate;
    const int decim = yinDecimation (sr);
    float x[kYinWindow];
    const double start = (double) setup.sourceAnchor - (double) (kYinWindow * decim);

    for (int i = 0; i < kYinWindow; ++i)
    {
        float acc = 0.0f;

        for (int k = 0; k < decim; ++k)
        {
            const double pos = start + (double) (i * decim + k);
            acc += 0.5f * (history.read (0, pos) + history.read (history.getChannels() > 1 ? 1 : 0, pos));
        }

        x[i] = acc / (float) decim;
    }

    const auto r = yinDetectDecimated (x, sr / (double) decim);

    if (meters != nullptr)
    {
        meters->hgF0.store (r.voiced ? r.f0Hz : 0.0f, std::memory_order_relaxed);
        meters->hgConfidence.store (r.confidence, std::memory_order_relaxed);
        meters->hgState.store (r.voiced ? 1 : 2, std::memory_order_relaxed);          // 1 used, 2 fell back to Granular
    }

    if (! r.voiced)
        return;

    auto& h = setup.harmonicSetup;
    h.enabled = true;
    h.f0Hz = r.f0Hz;
    h.confidence = r.confidence;
    h.voices = hp.voices;
    h.motion01 = hp.motion01;
    h.tone = hp.tone;
    h.minorThird = s.scaleLock.active() && s.scaleLock.set.hasDegree (3) && ! s.scaleLock.set.hasDegree (4);
}

} // namespace gapfill::harmonic
