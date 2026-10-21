#include "HarmonicHooks.h"

#include "../Salts.h"
#include "Yin.h"

#include <cmath>

namespace gapfill::harmonic
{

void decide (VoiceSetup& setup, const FillPlan& plan, const GapContext& ctx, const EngineSettings& s,
             const SourceHistory& history, fx::FxMeters* meters) noexcept
{
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
