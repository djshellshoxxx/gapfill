#include "FxHooks.h"

#include "../Salts.h"
#include "../cloud/CloudMode.h"
#include "../swell/SwellMode.h"

#include <cmath>

namespace gapfill::fx
{

void gatherFx (EngineSettings& s, const ParamCache& p) noexcept
{
    auto& f = s.fx;

    f.swell.amount01 = pv (p.swellAmount) / 100.0f;
    f.swell.curve = juce::jlimit (0, 3, pi (p.swellCurve));
    f.swell.filter01 = pv (p.swellFilter) / 100.0f;
    f.swell.target = juce::jlimit (0, 2, pi (p.swellTarget));

    f.cloud.amount01 = pv (p.cloudAmount) / 100.0f;
    f.cloud.densityHz = pv (p.cloudDensity);
    f.cloud.sizeMs = pv (p.cloudSize);
    f.cloud.spanMs = pv (p.cloudSpan);
    f.cloud.jitter01 = pv (p.cloudJitter) / 100.0f;
    f.cloud.pitchCents = pv (p.cloudPitch);
    f.cloud.spread01 = pv (p.cloudSpread) / 100.0f;

    f.ratchet.amount01 = pv (p.ratAmount) / 100.0f;
    f.ratchet.division = juce::jlimit (0, (int) Division::NumDivisions - 1, pi (p.ratDiv));
    f.ratchet.repeats = juce::jlimit (2, 16, pi (p.ratRepeats));
    f.ratchet.accel = pv (p.ratAccel) / 100.0f;
    f.ratchet.gate01 = pv (p.ratGate) / 100.0f;

    f.rcrush.amount01 = pv (p.rcAmount) / 100.0f;
    f.rcrush.bits = juce::jlimit (1, 16, pi (p.rcBits));
    f.rcrush.rate = juce::jlimit (1, 64, pi (p.rcRate));
    f.rcrush.hold = juce::jlimit (0, (int) Division::NumDivisions, pi (p.rcHold));
    f.rcrush.tone = pv (p.rcTone) / 100.0f;

    f.tape.amount01 = pv (p.tdAmount) / 100.0f;
    f.tape.wow01 = pv (p.tdWow) / 100.0f;
    f.tape.flutter01 = pv (p.tdFlutter) / 100.0f;
    f.tape.sat01 = pv (p.tdSat) / 100.0f;
    f.tape.dropout01 = pv (p.tdDropout) / 100.0f;
    f.tape.splice01 = pv (p.tdSplice) / 100.0f;

    f.scrub.on = pb (p.scrubOn);
    f.scrub.pos01 = pv (p.scrubPos) / 100.0f;
    f.scrub.widthMs = pv (p.scrubWidth);

    f.resonator.amount01 = pv (p.resAmount) / 100.0f;
    f.resonator.note = juce::jlimit (24, 96, pi (p.resNote));
    f.resonator.mode = juce::jlimit (0, 1, pi (p.resMode));
    f.resonator.decay01 = pv (p.resDecay) / 100.0f;
    f.resonator.damp01 = pv (p.resDamp) / 100.0f;
    f.resonator.spread01 = pv (p.resSpread) / 100.0f;

    f.fsdelay.amount01 = pv (p.fsdAmount) / 100.0f;
    f.fsdelay.shiftHz = juce::jlimit (-2000.0f, 2000.0f, pv (p.fsdShift));
    f.fsdelay.time = juce::jlimit (0, (int) Division::NumDivisions - 1, pi (p.fsdTime));
    f.fsdelay.feedback01 = juce::jlimit (0.0f, 0.85f, pv (p.fsdFeedback) / 100.0f);
    f.fsdelay.damp01 = pv (p.fsdDamp) / 100.0f;
    f.fsdelay.stereo01 = pv (p.fsdStereo) / 100.0f;

    f.dub.amount01 = pv (p.dubAmount) / 100.0f;
    f.dub.div = juce::jlimit (0, (int) Division::NumDivisions - 1, pi (p.dubDiv));
    f.dub.feedback01 = juce::jlimit (0.0f, 0.9f, pv (p.dubFeedback) / 100.0f);
    f.dub.lowHz = pv (p.dubLow);
    f.dub.highHz = pv (p.dubHigh);
    f.dub.move01 = pv (p.dubMove) / 100.0f;
    f.dub.sat01 = pv (p.dubSat) / 100.0f;
    f.dub.ping = pb (p.dubPing);

    f.formant.amount01 = pv (p.fmtAmount) / 100.0f;
    f.formant.shiftSt = juce::jlimit (-12.0f, 12.0f, pv (p.fmtShift));
    f.formant.tilt01 = pv (p.fmtTilt) / 100.0f;
    f.formant.vowel = juce::jlimit (0, 5, pi (p.fmtVowel));

    f.spfreeze.amount01 = pv (p.sfzAmount) / 100.0f;
    f.spfreeze.lengthMs = pv (p.sfzLength);
    f.spfreeze.decay01 = pv (p.sfzDecay) / 100.0f;
    f.spfreeze.smear01 = pv (p.sfzSmear) / 100.0f;
    f.spfreeze.morph01 = pv (p.sfzMorph) / 100.0f;
    f.spfreeze.pitchSt = juce::jlimit (-12.0f, 12.0f, pv (p.sfzPitch));
    f.spfreeze.width01 = pv (p.sfzWidth) / 100.0f;

    f.convo.amount01 = pv (p.cvAmount) / 100.0f;
    f.convo.sizeMs = pv (p.cvSize);
    f.convo.damp01 = pv (p.cvDamp) / 100.0f;

    f.bandFx.low = pb (p.bandLowFx);
    f.bandFx.mid = pb (p.bandMidFx);
    f.bandFx.high = pb (p.bandHighFx);
}

//==============================================================================
namespace
{
    /** Probability roll with a salted sub-RNG; drawn only when 0 < amount < 1. */
    bool rollMode (float amount01, const GapContext& ctx, std::uint64_t salt) noexcept
    {
        if (amount01 <= kOffThreshold)
            return false;

        if (amount01 >= 0.9999f)
            return true;

        Rng sub;
        sub.seed (hashCombine (ctx.key, salt));
        return sub.chance (amount01);
    }
}

void decideRenderModes (FillPlan& plan, const GapContext& ctx, const EngineSettings& s) noexcept
{
    auto& r = plan.recipe;

    if ((r.flags & FillRecipe::flagFromLock) != 0)
        return;                                     // replay keeps the recorded flags

    const auto algo = (Algo) plan.algoIndex;

    if (algo == Algo::Reverse && rollMode (s.fx.swell.amount01, ctx, kSalt_Swell))
        r.flags |= FillRecipe::flagSwell;

    if (algo == Algo::Granular && rollMode (s.fx.cloud.amount01, ctx, kSalt_Cloud))
        r.flags |= FillRecipe::flagCloud;
}

//==============================================================================
void prepareVoiceSetup (VoiceSetup& setup, const FillPlan& plan, const GapContext& ctx,
                        const EngineSettings& s, FxMeters* meters) noexcept
{
    const bool havePpq = s.transportValid && s.samplesPerBeat > 1.0;
    const auto ppqNow = havePpq ? s.ppqAtBlockStart + (double) (ctx.outputPos - ctx.blockStartPos) / s.samplesPerBeat
                                : 0.0;

    setup.modes = {};

    // ---- render modes -------------------------------------------------------
    if ((plan.recipe.flags & FillRecipe::flagSwell) != 0 && setup.algo == Algo::Reverse)
    {
        SwellTransport tr;
        tr.valid = havePpq;
        tr.ppqNow = ppqNow;
        tr.samplesPerBeat = havePpq ? s.samplesPerBeat : 0.0;
        tr.tsNum = s.perf.tsNum > 0 ? s.perf.tsNum : 4;
        tr.tsDen = s.perf.tsDen > 0 ? s.perf.tsDen : 4;

        const auto landing = computeSwellLanding (s.fx.swell.target, setup.lengthSamples, setup.sampleRate, tr);

        setup.modes.swell = true;
        setup.modes.swellLanding = landing.landing;
        setup.modes.swellFade = landing.fade;
        setup.lengthSamples = landing.voiceLength;

        if (meters != nullptr)
        {
            meters->swellFills.fetch_add (1, std::memory_order_relaxed);
            meters->swellNoTransport.store (landing.noTransport, std::memory_order_relaxed);

            if (landing.landingPpq >= 0.0)
            {
                const auto barBeats = 4.0 * (double) tr.tsNum / (double) tr.tsDen;
                const auto bar = (int) std::floor (landing.landingPpq / barBeats) + 1;
                const auto beat = (int) std::floor (std::fmod (landing.landingPpq, barBeats)) + 1;
                meters->swellLandingBar.store (bar, std::memory_order_relaxed);
                meters->swellLandingBeat.store (beat, std::memory_order_relaxed);
            }
        }
    }

    if ((plan.recipe.flags & FillRecipe::flagCloud) != 0 && setup.algo == Algo::Granular)
    {
        setup.modes.cloud = true;

        if (meters != nullptr)
        {
            const auto d = limitCloudDensity (s.fx.cloud.densityHz, s.fx.cloud.sizeMs);
            meters->cloudGrains.store ((int) std::lround (d.overlap), std::memory_order_relaxed);
            meters->cloudLimited.store (d.limited, std::memory_order_relaxed);
        }
    }

    // ---- voice effects -------------------------------------------------------
    auto& st = setup.fxStart;
    st.settings = s.fx;
    st.samplesPerBeat = havePpq ? s.samplesPerBeat : 0.0;
    st.tempoFallback = ! havePpq;
    st.skipVoiceFx = ! s.fx.bandFx.forBand (plan.band);
    st.seed = setup.rngSeed;
    st.lengthSamples = setup.lengthSamples;
    setup.excludeFromBus = ! s.fx.bandFx.forBand (plan.band);
}

} // namespace gapfill::fx
