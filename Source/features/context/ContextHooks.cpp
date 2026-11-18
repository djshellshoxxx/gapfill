#include "ContextHooks.h"
#include "../../dsp/ScrambleTiming.h"

#include "../groove/GrooveWarp.h"
#include "../harmonic/HarmonicHooks.h"
#include "../scale/ScaleHooks.h"

namespace gapfill::context
{

void prepareVoiceSetup (VoiceSetup& setup, const FillPlan& plan, const GapContext& ctx,
                        const EngineSettings& s, const SourceHistory& history, fx::FxMeters* meters) noexcept
{
    scale::prepareVoiceSetup (setup, plan, s);
    harmonic::decide (setup, plan, ctx, s, history, meters);       // F-X01 (Granular fills only)

    // ---- F-L09 Groove: tempo-synced subdivisions; Scramble is an explicit model2 upgrade ----------
    const auto& g = s.perf.groove;

    if (! g.active() || ! s.transportValid || s.samplesPerBeat <= 1.0)
        return;

    const bool scramble = setup.algo == Algo::Scramble && g.modelVersion == 2 && setup.modSyncSeconds > 0.0f;
    if (setup.algo != Algo::Stutter && setup.algo != Algo::Delay && ! scramble)
        return;

    const auto periodSeconds = setup.algo == Algo::Stutter ? setup.stutterSeconds : setup.delaySeconds;
    const auto periodBeats = scramble
        ? (double) voiceTiming::syncedScrambleSliceForPlan (setup.lengthSamples, setup.sinceEventSamples,
                                                          setup.modSyncSeconds, setup.sampleRate) / s.samplesPerBeat
        : (double) periodSeconds * setup.sampleRate / s.samplesPerBeat;
    const auto fillPpq = s.ppqAtBlockStart + (double) (ctx.outputPos - ctx.blockStartPos) / s.samplesPerBeat;

    groove::makeShifts (g.snap, g.grid, fillPpq, periodBeats, s.samplesPerBeat, g.amount01, setup.grooveShifts);
}

} // namespace gapfill::context
