#include "ContextHooks.h"

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

    // ---- F-L09 Groove: only tempo-synced Stutter repeats and Delay taps move ----------
    const auto& g = s.perf.groove;

    if (! g.active() || ! s.transportValid || s.samplesPerBeat <= 1.0)
        return;

    if (setup.algo != Algo::Stutter && setup.algo != Algo::Delay)
        return;

    const auto periodSeconds = setup.algo == Algo::Stutter ? setup.stutterSeconds : setup.delaySeconds;
    const auto periodBeats = (double) periodSeconds * setup.sampleRate / s.samplesPerBeat;
    const auto fillPpq = s.ppqAtBlockStart + (double) (ctx.outputPos - ctx.blockStartPos) / s.samplesPerBeat;

    groove::makeShifts (g.snap, g.grid, fillPpq, periodBeats, s.samplesPerBeat, g.amount01, setup.grooveShifts);
}

} // namespace gapfill::context
