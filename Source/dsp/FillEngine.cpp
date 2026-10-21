#include "FillEngine.h"
#include "../features/effective/Effective.h"
#include "../features/sync/Sync.h"
#include "../features/breathing/Breathing.h"
#include "../features/budget/Budget.h"
#include "../features/mistakes/Mistakes.h"
#include "../features/phrase/Phrase.h"
#include "../features/timing/BarTiming.h"

namespace gapfill
{

// FillRecipe validates against literal counts so it needs no dependency on Parameters.h.
static_assert (numAlgos == 15, "FillRecipe isValid() algo range must track Algo");
static_assert ((int) Division::NumDivisions == 12, "FillRecipe isValid() division range must track Division");

namespace
{
    /** Per-algorithm scheduling character.

        minMs      - below this the algorithm simply cannot say anything useful
        idealMs    - length at which the algorithm is at its best
        complexity - how "advanced" it sounds (matched against the macro)
        transient  - how much it likes following a hard hit */
    struct AlgoProfile
    {
        float minMs, idealMs, complexity, transient;
    };

    constexpr AlgoProfile kProfiles[numAlgos] =
    {
        /* Reverse   */ {  25.0f, 180.0f, 0.15f, 0.60f },
        /* Stutter   */ {  40.0f, 250.0f, 0.35f, 0.90f },
        /* Delay     */ {  70.0f, 350.0f, 0.25f, 0.60f },
        /* Granular  */ { 110.0f, 500.0f, 0.70f, 0.30f },
        /* Freeze    */ { 130.0f, 600.0f, 0.55f, 0.25f },
        /* PitchRise */ {  50.0f, 250.0f, 0.50f, 0.50f },
        /* PitchFall */ {  50.0f, 250.0f, 0.50f, 0.50f },
        /* Filter    */ {  45.0f, 300.0f, 0.30f, 0.40f },
        /* Texture   */ {  35.0f, 200.0f, 0.50f, 0.35f },
        /* Crush     */ {  20.0f, 120.0f, 0.45f, 0.80f },
        /* Distort   */ {  20.0f, 120.0f, 0.40f, 0.80f },
        /* Scramble  */ {  55.0f, 250.0f, 0.65f, 0.80f },
        /* Spectral  */ { 180.0f, 700.0f, 0.85f, 0.15f },
        /* Comb      */ {  80.0f, 400.0f, 0.60f, 0.50f },
        /* TapeStop  */ {  90.0f, 350.0f, 0.30f, 0.75f },
    };

    constexpr float kEnterGapConf = 0.55f;
    constexpr float kExitGapConf  = 0.28f;

    /** How long protection takes to release once the safe region has passed. */
    constexpr double kProtectReleaseSeconds = 0.12;

    /** Full protection for `hold` samples after the event, then a smooth
        release. `distance` is how long ago the event was, in samples. */
    inline float protectEnvelope (std::int64_t distance, float hold, float release) noexcept
    {
        if (distance < 0)
            return 0.0f;

        return 1.0f - smoothstep (hold, hold + juce::jmax (1.0f, release), (float) distance);
    }
}

//==============================================================================
void FillEngine::prepare (double sampleRate, int /*maxBlockSize*/, int numChannels)
{
    sr = juce::jmax (8000.0, sampleRate);
    channels = juce::jlimit (1, 2, numChannels);

    for (auto& v : voices)
        v.prepare (sr, channels);

    spectral.prepare (sr, 2);

    // Keep the visualiser feed at a roughly constant rate across sample rates.
    vizDecim = juce::jmax (1, (int) std::round (sr / (double) AnalysisEngine::hopSize / 700.0));

    reset();
}

void FillEngine::reset()
{
    for (auto& v : voices)
        v.reset();

    spectral.reset();
    spectral.release();

    inGap = false;
    gapEvaluated = false;
    gapStartPos = 0;
    lastFillStartPos = -1000000;
    lastEventPos = 0;
    lastEventTransient = 0.5f;
    gapIndex = 0;
    transEnv = scEnv = 0.0f;
    lastMainEventPos = lastScEventPos = std::numeric_limits<std::int64_t>::min() / 4;
    currentDuck = 1.0f;
    lastRenderedAlgo = -1;
    fillsStarted = 0;
    gapsTooShort = 0;
    rejectCounts.fill (0);
    telemetry.reset();
    recentFills.clear();
    vizCounter = 0;
    vizAccum = {};

    for (auto& a : recentAlgos)
        a = -1;

    recentWrite = 0;
}

void FillEngine::panic() noexcept
{
    for (auto& v : voices)
        if (v.isActive())
            v.requestStop (juce::jmax (16, (int) (sr * 0.004)));
}

void FillEngine::sourceChanged() noexcept
{
    for (auto& v : voices)
        if (v.isActive() && v.getAlgo() == Algo::Granular)
            v.requestStop (juce::jmax (16, (int) (sr * 0.010)));
}

int FillEngine::getActiveVoiceCount() const noexcept
{
    int n = 0;

    for (const auto& v : voices)
        if (v.isActive())
            ++n;

    return n;
}

FillVoice* FillEngine::findFreeVoice() noexcept
{
    for (auto& v : voices)
        if (! v.isActive())
            return &v;

    return nullptr;
}

//==============================================================================
void FillEngine::process (juce::AudioBuffer<float>& wet, int wetStartSample, int numSamples,
                          std::int64_t outputStartPos,
                          const AnalysisEngine& analysis,
                          const SourceHistory& history,
                          const EngineSettings& s,
                          VisualisationFifo* viz,
                          FillEventQueue* events) noexcept
{
    if (numSamples <= 0)
        return;

    constexpr auto hop = AnalysisEngine::hopSize;
    int offset = 0;

    while (offset < numSamples)
    {
        const auto outputPos = outputStartPos + offset;
        const auto phase = (int) (outputPos % hop);

        if (phase == 0)
            controlUpdate (outputPos, outputStartPos, analysis, history, s, viz, events);

        const auto chunk = juce::jmin (numSamples - offset, hop - phase);

        for (auto& v : voices)
            if (v.isActive())
                v.render (wet, wetStartSample + offset, chunk, history);

        offset += chunk;
    }

    int algo = -1;

    for (const auto& v : voices)
        if (v.isActive())
            algo = (int) v.getAlgo();

    lastRenderedAlgo = algo;
}

//==============================================================================
void FillEngine::controlUpdate (std::int64_t outputPos, std::int64_t blockStartPos,
                                const AnalysisEngine& analysis, const SourceHistory& history,
                                const EngineSettings& s, VisualisationFifo* viz,
                                FillEventQueue* events) noexcept
{
    const auto& frame = analysis.frameAt (outputPos);

    // ---- protection envelopes -------------------------------------------
    const bool mainEventNow = (analysis.nextMainEvent (outputPos) == outputPos);
    const bool scEventNow   = s.sidechainActive && (analysis.nextSidechainEvent (outputPos) == outputPos);

    if (mainEventNow)
    {
        lastMainEventPos = outputPos;
        lastEventPos = outputPos;
        lastEventTransient = frame.mainTransient;
    }

    if (scEventNow)
    {
        lastScEventPos = outputPos;

        if (s.mode != DetMode::Transient || s.sidechainActive)
            lastEventPos = juce::jmax (lastEventPos, outputPos);
    }

    const auto holdSamples = (float) (juce::jmax (0.0f, s.protectHoldMs) * 0.001 * sr);
    const auto releaseSamples = (float) (kProtectReleaseSeconds * sr);

    transEnv = protectEnvelope (outputPos - lastMainEventPos, holdSamples, releaseSamples);
    scEnv    = protectEnvelope (outputPos - lastScEventPos, holdSamples, releaseSamples);

    // ---- next-event protection ------------------------------------------
    const auto lookaheadSamples = (int) (s.lookaheadMs * 0.001 * sr);
    const auto nextProtected = analysis.nextProtectedEvent (outputPos + 1);

    float nextDuck = 1.0f;
    std::int64_t distanceToNext = -1;

    if (nextProtected > outputPos)
    {
        distanceToNext = nextProtected - outputPos;
        const auto window = (std::int64_t) juce::jmax ((int) (sr * 0.025), lookaheadSamples);

        if (distanceToNext < window)
        {
            const auto t = smoothstep (0.0f, (float) window, (float) distanceToNext);
            nextDuck = 1.0f - s.protNext01 * (1.0f - t);
        }
    }

    auto duck = nextDuck * (1.0f - s.protTrans01 * transEnv);

    if (s.sidechainActive)
        duck *= (1.0f - s.protSC01 * scEnv);

    currentDuck = juce::jlimit (0.0f, 1.0f, duck);

    for (auto& v : voices)
    {
        if (! v.isActive())
            continue;

        v.setDuck (currentDuck);

        // If a protected event is imminent and the user asked for strong
        // protection, fade the fill out so it lands silent on the hit.
        if (s.protNext01 > 0.35f && distanceToNext > 0
             && distanceToNext < (std::int64_t) juce::jmax (64, (int) (sr * 0.03 * s.protNext01)))
        {
            v.requestStop ((int) juce::jmax ((std::int64_t) 48, distanceToNext));
        }
    }

    // ---- gap state machine ----------------------------------------------
    if (! inGap)
    {
        if (frame.conf >= kEnterGapConf)
        {
            inGap = true;
            gapEvaluated = false;
            gapStartPos = outputPos;
            ++gapIndex;
        }
    }
    else if (frame.conf < kExitGapConf)
    {
        inGap = false;
    }

    bool startedFill = false;

    if (inGap && ! gapEvaluated)
    {
        gapEvaluated = true;
        const auto result = trySchedule (outputPos, blockStartPos, frame, analysis, history, s);
        startedFill = result.started;

        // Recipes are always reported; rejections only while a visualiser is listening.
        if (events != nullptr && (result.started || (viz != nullptr && viz->isEnabled())))
        {
            FillEvent e;
            e.type = result.started ? FillEventType::Started : FillEventType::Rejected;
            e.reason = result.reason;
            e.recipe = result.recipe;
            e.availMs = result.availableMs;
            e.needMs = result.neededMs;
            e.pos = outputPos;
            events->push (e);
        }
    }

    // ---- visualiser feed -------------------------------------------------
    if (viz != nullptr && viz->isEnabled())
    {
        vizAccum.peak = juce::jmax (vizAccum.peak, frame.peak);
        vizAccum.conf = juce::jmax (vizAccum.conf, frame.conf);
        vizAccum.energy = juce::jmax (vizAccum.energy, frame.energy);
        vizAccum.duck = juce::jmin (vizAccum.duck, currentDuck);

        for (int b = 0; b < 3; ++b)
            vizAccum.bandConf[b] = juce::jmax (vizAccum.bandConf[b], frame.bandConf[b]);

        if (mainEventNow || scEventNow)
            vizAccum.flags |= VizFrame::flagEvent;

        if (inGap)
            vizAccum.flags |= VizFrame::flagGapEligible;

        if (transEnv > 0.25f || currentDuck < 0.9f)
            vizAccum.flags |= VizFrame::flagProtected;

        int activeAlgo = -1;
        int activeBand = -1;

        for (const auto& v : voices)
        {
            if (v.isActive())
            {
                activeAlgo = (int) v.getAlgo();
                activeBand = v.getBand();
            }
        }

        if (activeAlgo >= 0 || startedFill)
        {
            vizAccum.flags |= VizFrame::flagFillActive;
            vizAccum.algo = (std::uint8_t) juce::jlimit (0, numAlgos - 1, activeAlgo >= 0 ? activeAlgo : 0);
            vizAccum.band = activeBand >= 0 ? (std::uint8_t) activeBand : (std::uint8_t) 0xFF;
        }

        if (++vizCounter >= vizDecim)
        {
            vizCounter = 0;
            viz->push (vizAccum);
            vizAccum = {};
        }
    }
}

//==============================================================================
void FillEngine::publishDecisionTelemetry (const EngineSettings& base, const EngineSettings& decided, double ppq,
                                           const Effective::TransportInfo& transport) noexcept
{
    const auto breath = breathing::phaseAt (base.perf.breath, ppq, transport);
    telemetry.breathPhase.store ((float) breath, std::memory_order_relaxed);
    telemetry.breathOffset.store (breath >= 0.0 ? decided.density01 - base.density01 : 0.0f, std::memory_order_relaxed);

    telemetry.phrasePosition.store ((float) phrase::positionAt (base.perf.phrase, ppq, transport), std::memory_order_relaxed);
    telemetry.phraseMultiplier.store (decided.perf.probabilityMul, std::memory_order_relaxed);
    const auto bpb = timing::beatsPerBar (transport);
    telemetry.barFills.store (bpb > 0.0 ? recentFills.countInBar (timing::barIndex (ppq, bpb), bpb) : 0,
                              std::memory_order_relaxed);
    telemetry.decisions.fetch_add (1, std::memory_order_relaxed);
}

ScheduleResult FillEngine::trySchedule (std::int64_t outputPos, std::int64_t blockStartPos,
                                       const AnalysisFrame& frame, const AnalysisEngine& analysis,
                                       const SourceHistory& history, const EngineSettings& base) noexcept
{
    // Decision stage of the effective-settings pipeline (F0-5): every later
    // stage reads the local copy `s`, evaluated at the PPQ of the decision sample.
    const bool havePpq = base.transportValid && base.samplesPerBeat > 1.0;
    const auto ppq = havePpq ? base.ppqAtBlockStart + (double) (outputPos - blockStartPos) / base.samplesPerBeat
                             : 0.0;

    EngineSettings s = base;
    const Effective::TransportInfo transport { havePpq, ppq, base.perf.tsNum, base.perf.tsDen,
                                               havePpq ? 60.0 * sr / base.samplesPerBeat : 0.0 };
    Effective::applyAtDecision (s, ppq, transport);
    publishDecisionTelemetry (base, s, ppq, transport);

    GapContext ctx;
    ctx.outputPos = outputPos;
    ctx.blockStartPos = blockStartPos;
    ctx.frame = &frame;
    ctx.history = &history;
    ctx.sourceAnchor = juce::jlimit ((std::int64_t) 0, history.getWritePosition(), outputPos - s.historyOffset);

    ScheduleResult result;
    FillPlan plan;
    float neededMs = 0.0f;

    auto reject = [&] (RejectReason r)
    {
        noteRejected (r);
        result.reason = r;
        result.availableMs = (float) (1000.0 * (double) ctx.available / sr);
        result.neededMs = r == RejectReason::NoAlgoFits ? plan.neededMs : neededMs;
        return result;
    };

    if (auto r = checkEligibility (ctx, analysis, s, neededMs); r != RejectReason::None)
        return reject (r);

    // ---- deterministic decision key --------------------------------------
    const auto seedMix = (std::uint64_t) s.seed * 0x9E3779B1ULL;
    ctx.key = havePpq ? hashCombine (seedMix, (std::uint64_t) std::llround (ppq * 96.0))
                      : hashCombine (seedMix, (std::uint64_t) gapIndex);

    Rng rng;
    rng.seed (ctx.key);

    int band = -1;

    if (auto r = rollDecision (ctx, rng, s, band); r != RejectReason::None)
        return reject (r);

    if (auto r = buildRecipe (ctx, rng, s, band, plan); r != RejectReason::None)
        return reject (r);

    if (auto r = adjustRecipe (plan, ctx, s); r != RejectReason::None)
        return reject (r);

    auto* voice = findFreeVoice();

    if (voice == nullptr)
        return reject (RejectReason::NoVoice);

    auto setup = toVoiceSetup (plan, ctx, s, history);

    if (! startVoice (*voice, setup, plan, history))
        return reject (RejectReason::SourceUnavailable);

    noteFillStarted (plan, outputPos);

    if (havePpq)
        recentFills.push (budget::tickFor (ppq));       // F-D01 bar counting (every started fill counts)

    result.started = true;
    result.recipe = plan.recipe;
    return result;
}

//==============================================================================
RejectReason FillEngine::checkEligibility (GapContext& ctx, const AnalysisEngine& analysis,
                                           const EngineSettings& s, float& neededMs) noexcept
{
    if (s.fill01 <= 0.001f)
        return RejectReason::FillOff;

    // Never invent material out of nothing: the main input must have had some
    // musical content recently, otherwise there is nothing to build a fill from
    // and nothing for the listener to hear it against.
    if (ctx.frame->reference < 3.0e-4f)
        return RejectReason::NoSource;

    const auto maxConcurrent = s.complexity01 > 0.65f ? 2 : 1;

    if (getActiveVoiceCount() >= maxConcurrent)
        return RejectReason::NoVoice;

    // Never start two fills on top of each other in the same musical breath.
    ctx.minGapSamples = (std::int64_t) juce::jmax (16.0, s.minGapMs * 0.001 * sr);

    if (ctx.outputPos - lastFillStartPos < ctx.minGapSamples / 2)
        return RejectReason::TooSoon;

    // ---- how much room is there? ----------------------------------------
    const auto outputPos = ctx.outputPos;
    ctx.maxFillSamples = (std::int64_t) juce::jmax (32.0, s.maxFillMs * 0.001 * sr);
    const auto nextEvent = analysis.nextDetectionEvent (outputPos + 1);
    const auto nextProtected = analysis.nextProtectedEvent (outputPos + 1);

    auto boundary = nextEvent;

    if (nextProtected > outputPos && (boundary < 0 || nextProtected < boundary))
        boundary = nextProtected;

    // Leave a little air before the next hit even at zero protection.
    ctx.preGuard = (std::int64_t) (sr * 0.001 * lerp (4.0f, 25.0f, s.protNext01));

    ctx.available = ctx.maxFillSamples;

    if (boundary > outputPos)
        ctx.available = juce::jmax ((std::int64_t) 0, boundary - outputPos - ctx.preGuard);

    if (ctx.available < ctx.minGapSamples)
    {
        ++gapsTooShort;
        neededMs = (float) (1000.0 * (double) ctx.minGapSamples / sr);

        // The gap itself was long enough, the pre-event guard ate the room.
        return noRoomReason (boundary, outputPos, ctx.minGapSamples);
    }

    ctx.usable = juce::jmin (ctx.available, ctx.maxFillSamples);

    // F-D01 per-bar budget and quiet zones: decided before any RNG draw, neutral when Off
    // or without a valid transport / time signature.
    if (s.perf.budget.perBar > 0 || s.perf.budget.zone > 0)
    {
        const bool havePpq = s.transportValid && s.samplesPerBeat > 1.0;
        const auto ppq = havePpq ? s.ppqAtBlockStart + (double) (ctx.outputPos - ctx.blockStartPos) / s.samplesPerBeat
                                 : 0.0;
        bool needsTransport = false;
        const auto verdict = budget::evaluate (s.perf.budget, s.perf.phrase, ppq,
                                               { havePpq, ppq, s.perf.tsNum, s.perf.tsDen, 0.0 },
                                               recentFills, &needsTransport);
        telemetry.budgetNeedsTransport.store (needsTransport, std::memory_order_relaxed);

        if (verdict != RejectReason::None)
            return verdict;
    }
    else
    {
        telemetry.budgetNeedsTransport.store (false, std::memory_order_relaxed);
    }

    return RejectReason::None;
}

RejectReason FillEngine::rollDecision (GapContext& ctx, Rng& rng, const EngineSettings& s,
                                       int& band) noexcept
{
    // ---- should this gap be filled at all? -------------------------------
    ctx.conf = juce::jlimit (0.0f, 1.0f, ctx.frame->conf);
    auto probability = s.fill01 * (0.4f + 0.6f * s.density01) * (0.6f + 0.4f * ctx.conf) * 1.15f;
    probability *= s.perf.probabilityMul;   // C-7 phrase ramp (exactly 1.0 when neutral)

    // ---- spectral lane ----------------------------------------------------
    band = -1;

    if (s.mode == DetMode::Spectral)
    {
        band = chooseBand (*ctx.frame, s, rng);

        if (band < 0)
            return RejectReason::NoBand;

        probability *= s.bandProb[band];
    }

    if (! rng.chance (juce::jlimit (0.0f, 1.0f, probability)))
        return RejectReason::DensityRoll;

    return RejectReason::None;
}

RejectReason FillEngine::buildRecipe (const GapContext& ctx, Rng& rng, const EngineSettings& s,
                                      int band, FillPlan& plan) noexcept
{
    // ---- length ------------------------------------------------------------
    auto scale = lerp (0.5f, 1.0f, s.complexity01) * lerp (0.7f, 1.0f, ctx.conf);
    scale *= 1.0f + s.chaos01 * rng.nextBipolar() * 0.25f;

    auto lengthSamples = (std::int64_t) ((double) ctx.usable * juce::jlimit (0.2f, 1.2f, scale));
    lengthSamples = juce::jlimit (juce::jmin (ctx.minGapSamples, ctx.usable), ctx.usable, lengthSamples);

    // E-2: optional fill-length grid (never lengthens, never exceeds usable).
    if (s.perf.quantDivision >= 0 && s.transportValid && s.samplesPerBeat > 1.0)
        lengthSamples = sync::quantiseFillLength (lengthSamples,
                                                  sync::fillQuantBeats (s.perf.quantDivision + 1) * s.samplesPerBeat,
                                                  ctx.minGapSamples, ctx.usable);

    const auto lengthMs = (float) (1000.0 * (double) lengthSamples / sr);

    // ---- algorithm ---------------------------------------------------------
    int algoIndex;

    // C-9: in Spectral mode a lane may force its own fill type; a global Fill Type wins over it.
    const int laneType = (s.fillTypeIndex == 0 && band >= 0 && band < 3) ? s.perf.bandType[band] : 0;

    if (s.fillTypeIndex > 0 || laneType > 0)
    {
        algoIndex = juce::jlimit (0, numAlgos - 1, (s.fillTypeIndex > 0 ? s.fillTypeIndex : laneType) - 1);

        // A manually chosen algorithm still has to fit the space (no silent substitution).
        if (lengthMs < kProfiles[algoIndex].minMs * 0.6f)
        {
            plan.lengthMs = lengthMs;
            plan.neededMs = kProfiles[algoIndex].minMs * 0.6f;
            return RejectReason::NoAlgoFits;
        }
    }
    else
    {
        algoIndex = chooseAlgorithm (lengthMs, s, rng);

        if (algoIndex < 0)
        {
            // Report the shortest fill any enabled algorithm could have played.
            float shortest = 0.0f;

            for (int i = 0; i < numAlgos; ++i)
                if (s.weights[i] > 0.001f && (shortest <= 0.0f || kProfiles[i].minMs < shortest))
                    shortest = kProfiles[i].minMs;

            plan.lengthMs = lengthMs;
            plan.neededMs = shortest;
            return RejectReason::NoAlgoFits;
        }
    }

    plan.lengthSamples = lengthSamples;
    plan.lengthMs = lengthMs;
    plan.algoIndex = algoIndex;
    plan.band = band;

    auto& r = plan.recipe;
    r = {};
    r.algo = (std::uint8_t) algoIndex;
    r.band = (std::int8_t) band;
    r.lengthClass = lengthClassFor (lengthMs, s.maxFillMs);
    r.lengthMs = (std::uint16_t) juce::jlimit (0, 65535, (int) std::lround (lengthMs));
    r.seed = (std::uint32_t) hashCombine (ctx.key, (std::uint64_t) (algoIndex + 1) * 0x27D4EB2FULL);
    return RejectReason::None;
}

RejectReason FillEngine::adjustRecipe (FillPlan& plan, const GapContext& ctx, const EngineSettings& s) noexcept
{
    // HOOK: Fill Lock (C-5) substitutes the locked recipe here and may return
    // LockSkipped. Features draw from their own salted sub-RNG
    // (Source/features/Salts.h), never from the scheduler's Rng.

    // C-3 Controlled Mistakes: a fresh roll for every fill (a locked recipe's mistake field is
    // ignored). With mistakes at 0 nothing runs and no RNG is touched.
    if (s.perf.mistakes01 > 0.0f)
        rollMistake (plan, ctx, s);

    return RejectReason::None;
}

void FillEngine::rollMistake (FillPlan& plan, const GapContext& ctx, const EngineSettings& s) noexcept
{
    plan.recipe.mistakeType = 0;
    plan.recipe.mistakeAmt = 0;

    mistakes::Roller roller (s.perf.mistakes01, ctx.key);

    if (! roller.hit())
        return;

    mistakes::Facts f;
    f.algo = plan.algoIndex;
    f.lengthSamples = plan.lengthSamples;
    f.sampleRate = sr;
    f.mono = channels < 2;
    f.sourceAvailable = ctx.sourceAnchor;
    f.seed = plan.recipe.seed;
    f.repeats = mistakes::estimateRepeats (plan.algoIndex, plan.lengthSamples, sr, s.stutterSeconds,
                                           s.delaySeconds, s.complexity01);

    // Is the capture effectively mono? Estimated from at most 512 evenly spaced samples, and only
    // when a stereo swing could be chosen at all.
    if (! f.mono && ctx.history != nullptr
         && 1000.0 * (double) plan.lengthSamples / sr >= mistakes::kMinBounceMs)
    {
        const auto span = juce::jmin ((std::int64_t) (sr * 0.32), plan.lengthSamples);
        double diff = 0.0, energy = 0.0;

        for (int i = 0; i < 512; ++i)
        {
            const auto p = (double) (ctx.sourceAnchor - span) + (double) span * (double) i / 512.0;
            const auto a = (double) ctx.history->read (0, p);
            const auto b = (double) ctx.history->read (1, p);
            diff += (a - b) * (a - b);
            energy += a * a + b * b;
        }

        f.stereoCoherent = energy < 1.0e-12 || diff <= 1.0e-6 * energy;        // < -60 dB difference
    }

    const auto roll = roller.pick (f);
    plan.recipe.mistakeType = roll.type;
    plan.recipe.mistakeAmt = roll.amount;
}

VoiceSetup FillEngine::toVoiceSetup (const FillPlan& plan, const GapContext& ctx,
                                     const EngineSettings& s, const SourceHistory& history) const noexcept
{
    const auto outputPos = ctx.outputPos;
    const auto& frame = *ctx.frame;

    VoiceSetup setup;
    setup.algo = (Algo) plan.algoIndex;
    setup.sampleRate = sr;
    setup.lengthSamples = (int) juce::jlimit ((std::int64_t) 32, (std::int64_t) (sr * 3.0), plan.lengthSamples);
    setup.fadeInSamples  = (int) (sr * 0.001 * lerp (8.0f, 2.0f, s.intensity01));
    setup.fadeOutSamples = (int) (sr * 0.001 * lerp (40.0f, 10.0f, s.intensity01));
    setup.intensity01  = s.intensity01;
    setup.complexity01 = s.complexity01;
    setup.chaos01      = s.chaos01;
    setup.sourceAnchor = juce::jlimit ((std::int64_t) 0, history.getWritePosition(),
                                       outputPos - s.historyOffset);
    setup.sinceEventSamples = (int) juce::jlimit ((std::int64_t) 0, (std::int64_t) (sr * 0.3),
                                                  outputPos - lastEventPos);
    setup.band = plan.band;
    setup.xoverLM = s.xoverLM;
    setup.xoverMH = s.xoverMH;
    setup.stutterSeconds = s.stutterSeconds;
    setup.delaySeconds = s.delaySeconds;
    setup.pingPong = s.pingPong;
    setup.modSyncSeconds = s.perf.modSyncSeconds;   // E-2 (0 = off)
    setup.envShape = s.perf.envShape;               // C-4
    setup.envAmount01 = s.perf.envAmount01;
    setup.mistakeType = plan.recipe.mistakeType;    // C-3
    setup.mistakeAmt01 = (float) plan.recipe.mistakeAmt * 0.01f;
    setup.mistakeSeed = plan.recipe.seed;
    setup.rngSeed = hashCombine (ctx.key, (std::uint64_t) (plan.algoIndex + 1) * 0x27D4EB2FULL);

    for (int b = 0; b < 3; ++b)
        setup.bandEnergy[b] = frame.bandEnergy[b];

    auto amp = 0.8f * (0.6f + 0.4f * ctx.conf);

    if (plan.band >= 0)
    {
        amp *= s.bandAmt[plan.band];
        setup.intensity01 = juce::jlimit (0.0f, 1.0f, s.bandInt[plan.band]);
    }

    setup.amp = juce::jlimit (0.0f, 1.5f, amp);

    // A recipe replayed from Lock forces the choices the voice would otherwise derive.
    if ((plan.recipe.flags & FillRecipe::flagFromLock) != 0)
    {
        setup.modifierOverride = plan.recipe.modifierMask;
        setup.pitchOverride = plan.recipe.pitchSemis;
    }

    return setup;
}

bool FillEngine::startVoice (FillVoice& voice, VoiceSetup& setup, FillPlan& plan,
                             const SourceHistory& history) noexcept
{
    if (voice.start (setup, history, &spectral))
        return true;

    // Spectral processor busy - fall back to a simpler algorithm.
    setup.algo = Algo::Granular;

    if (plan.lengthMs < kProfiles[(int) Algo::Granular].minMs)
        setup.algo = Algo::Reverse;

    if (! voice.start (setup, history, &spectral))
        return false;

    plan.algoIndex = (int) setup.algo;
    plan.recipe.algo = (std::uint8_t) plan.algoIndex;
    return true;
}

void FillEngine::noteFillStarted (const FillPlan& plan, std::int64_t outputPos) noexcept
{
    recentAlgos[recentWrite] = plan.algoIndex;
    recentWrite = (recentWrite + 1) % historyLength;
    lastFillStartPos = outputPos;
    ++fillsStarted;
}

void FillEngine::noteRejected (RejectReason r) noexcept
{
    ++rejectCounts[(size_t) r];
}

//==============================================================================
int FillEngine::chooseAlgorithm (float lengthMs, const EngineSettings& s, Rng& rng) noexcept
{
    float weights[numAlgos];
    float total = 0.0f;

    const auto chaosFlatten = 1.0f - 0.65f * juce::jlimit (0.0f, 1.0f, s.chaos01);

    for (int i = 0; i < numAlgos; ++i)
    {
        weights[i] = 0.0f;

        auto w = juce::jlimit (0.0f, 1.0f, s.weights[i]);

        if (w <= 0.001f)
            continue;

        const auto& p = kProfiles[i];

        if (lengthMs < p.minMs)
            continue;

        if (i == (int) Algo::Spectral && spectral.isClaimed())
            continue;

        // Gaussian fit in the log-length domain: short gaps get short fills.
        const auto lr = std::log (juce::jmax (1.0f, lengthMs) / p.idealMs) / 0.9f;
        w *= std::exp (-0.5f * lr * lr) + 0.08f;

        // Complexity affinity.
        w *= 1.0f - 0.75f * std::abs (juce::jlimit (0.0f, 1.0f, s.complexity01) - p.complexity);

        // How hard was the hit that opened this gap?
        w *= 1.0f - 0.5f * std::abs (juce::jlimit (0.0f, 1.0f, lastEventTransient) - p.transient);

        // Avoid immediate repetition unless chaos is low (where repetition is a
        // musical choice rather than an accident).
        if (recentAlgos[(recentWrite + historyLength - 1) % historyLength] == i)
            w *= 0.22f;
        else
            for (int h = 0; h < historyLength; ++h)
                if (recentAlgos[h] == i)
                {
                    w *= 0.6f;
                    break;
                }

        w = std::pow (juce::jmax (1.0e-6f, w), chaosFlatten);

        weights[i] = w;
        total += w;
    }

    if (total <= 0.0f)
        return -1;

    auto pick = rng.nextFloat() * total;

    for (int i = 0; i < numAlgos; ++i)
    {
        pick -= weights[i];

        if (pick <= 0.0f && weights[i] > 0.0f)
            return i;
    }

    for (int i = numAlgos - 1; i >= 0; --i)
        if (weights[i] > 0.0f)
            return i;

    return -1;
}

int FillEngine::chooseBand (const AnalysisFrame& frame, const EngineSettings& s, Rng& rng) noexcept
{
    float weights[3] { 0.0f, 0.0f, 0.0f };
    float total = 0.0f;

    for (int b = 0; b < 3; ++b)
    {
        if (! s.bandOn[b])
            continue;

        if (frame.bandConf[b] < 0.45f)
            continue;

        const auto w = frame.bandConf[b] * juce::jmax (0.02f, s.bandProb[b]) * juce::jmax (0.02f, s.bandAmt[b]);
        weights[b] = w;
        total += w;
    }

    if (total <= 0.0f)
        return -1;

    auto pick = rng.nextFloat() * total;

    for (int b = 0; b < 3; ++b)
    {
        pick -= weights[b];

        if (pick <= 0.0f && weights[b] > 0.0f)
            return b;
    }

    for (int b = 2; b >= 0; --b)
        if (weights[b] > 0.0f)
            return b;

    return -1;
}

} // namespace gapfill
