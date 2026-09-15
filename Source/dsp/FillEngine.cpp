#include "FillEngine.h"

namespace gapfill
{

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
    };

    constexpr float kEnterGapConf = 0.55f;
    constexpr float kExitGapConf  = 0.28f;
}

//==============================================================================
void FillEngine::prepare (double sampleRate, int /*maxBlockSize*/, int numChannels)
{
    sr = juce::jmax (8000.0, sampleRate);
    channels = juce::jlimit (1, 2, numChannels);

    for (auto& v : voices)
        v.prepare (sr, channels);

    spectral.prepare (sr, 2);

    const auto hopSeconds = (float) (AnalysisEngine::hopSize / sr);
    transDecay = std::exp (-hopSeconds / 0.09f);
    scDecay    = std::exp (-hopSeconds / 0.12f);

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
    currentDuck = 1.0f;
    lastRenderedAlgo = -1;
    fillsStarted = 0;
    gapsTooShort = 0;
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
                          VisualisationFifo* viz) noexcept
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
            controlUpdate (outputPos, outputStartPos, analysis, history, s, viz);

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
                                const EngineSettings& s, VisualisationFifo* viz) noexcept
{
    const auto& frame = analysis.frameAt (outputPos);

    // ---- protection envelopes -------------------------------------------
    const bool mainEventNow = (analysis.nextMainEvent (outputPos) == outputPos);
    const bool scEventNow   = s.sidechainActive && (analysis.nextSidechainEvent (outputPos) == outputPos);

    transEnv *= transDecay;
    scEnv    *= scDecay;

    if (mainEventNow)
    {
        transEnv = 1.0f;
        lastEventPos = outputPos;
        lastEventTransient = frame.mainTransient;
    }

    if (scEventNow)
    {
        scEnv = 1.0f;

        if (s.mode != DetMode::Transient || s.sidechainActive)
            lastEventPos = juce::jmax (lastEventPos, outputPos);
    }

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
        startedFill = trySchedule (outputPos, blockStartPos, frame, analysis, history, s);
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
bool FillEngine::trySchedule (std::int64_t outputPos, std::int64_t blockStartPos,
                              const AnalysisFrame& frame, const AnalysisEngine& analysis,
                              const SourceHistory& history, const EngineSettings& s) noexcept
{
    if (s.fill01 <= 0.001f)
        return false;

    // Never invent material out of nothing: the main input must have had some
    // musical content recently, otherwise there is nothing to build a fill from
    // and nothing for the listener to hear it against.
    if (frame.reference < 3.0e-4f)
        return false;

    const auto maxConcurrent = s.complexity01 > 0.65f ? 2 : 1;

    if (getActiveVoiceCount() >= maxConcurrent)
        return false;

    // Never start two fills on top of each other in the same musical breath.
    const auto minGapSamples = (std::int64_t) juce::jmax (16.0, s.minGapMs * 0.001 * sr);

    if (outputPos - lastFillStartPos < minGapSamples / 2)
        return false;

    // ---- how much room is there? ----------------------------------------
    const auto maxFillSamples = (std::int64_t) juce::jmax (32.0, s.maxFillMs * 0.001 * sr);
    const auto nextEvent = analysis.nextDetectionEvent (outputPos + 1);
    const auto nextProtected = analysis.nextProtectedEvent (outputPos + 1);

    auto boundary = nextEvent;

    if (nextProtected > outputPos && (boundary < 0 || nextProtected < boundary))
        boundary = nextProtected;

    // Leave a little air before the next hit even at zero protection.
    const auto preGuard = (std::int64_t) (sr * 0.001 * lerp (4.0f, 25.0f, s.protNext01));

    std::int64_t available = maxFillSamples;

    if (boundary > outputPos)
        available = juce::jmax ((std::int64_t) 0, boundary - outputPos - preGuard);

    if (available < minGapSamples)
    {
        ++gapsTooShort;
        return false;
    }

    auto usable = juce::jmin (available, maxFillSamples);

    // ---- deterministic decision key --------------------------------------
    std::uint64_t key;

    if (s.transportValid && s.samplesPerBeat > 1.0)
    {
        const auto ppq = s.ppqAtBlockStart + (double) (outputPos - blockStartPos) / s.samplesPerBeat;
        const auto tick = (std::int64_t) std::llround (ppq * 96.0);
        key = hashCombine ((std::uint64_t) s.seed * 0x9E3779B1ULL, (std::uint64_t) tick);
    }
    else
    {
        key = hashCombine ((std::uint64_t) s.seed * 0x9E3779B1ULL, (std::uint64_t) gapIndex);
    }

    Rng rng;
    rng.seed (key);

    // ---- should this gap be filled at all? -------------------------------
    const auto conf = juce::jlimit (0.0f, 1.0f, frame.conf);
    auto probability = s.fill01 * (0.4f + 0.6f * s.density01) * (0.6f + 0.4f * conf) * 1.15f;

    // ---- spectral lane ----------------------------------------------------
    int band = -1;

    if (s.mode == DetMode::Spectral)
    {
        band = chooseBand (frame, s, rng);

        if (band < 0)
            return false;

        probability *= s.bandProb[band];
    }

    if (! rng.chance (juce::jlimit (0.0f, 1.0f, probability)))
        return false;

    // ---- length ------------------------------------------------------------
    auto scale = lerp (0.5f, 1.0f, s.complexity01) * lerp (0.7f, 1.0f, conf);
    scale *= 1.0f + s.chaos01 * rng.nextBipolar() * 0.25f;

    auto lengthSamples = (std::int64_t) ((double) usable * juce::jlimit (0.2f, 1.2f, scale));
    lengthSamples = juce::jlimit (juce::jmin (minGapSamples, usable), usable, lengthSamples);

    const auto lengthMs = (float) (1000.0 * (double) lengthSamples / sr);

    // ---- algorithm ---------------------------------------------------------
    int algoIndex;

    if (s.fillTypeIndex > 0)
    {
        algoIndex = juce::jlimit (0, numAlgos - 1, s.fillTypeIndex - 1);

        // A manually chosen algorithm still has to fit the space.
        if (lengthMs < kProfiles[algoIndex].minMs * 0.6f)
            return false;
    }
    else
    {
        algoIndex = chooseAlgorithm (lengthMs, s, rng);

        if (algoIndex < 0)
            return false;
    }

    auto* voice = findFreeVoice();

    if (voice == nullptr)
        return false;

    // ---- build the setup ----------------------------------------------------
    VoiceSetup setup;
    setup.algo = (Algo) algoIndex;
    setup.sampleRate = sr;
    setup.lengthSamples = (int) juce::jlimit ((std::int64_t) 32, (std::int64_t) (sr * 3.0), lengthSamples);
    setup.fadeInSamples  = (int) (sr * 0.001 * lerp (8.0f, 2.0f, s.intensity01));
    setup.fadeOutSamples = (int) (sr * 0.001 * lerp (40.0f, 10.0f, s.intensity01));
    setup.intensity01  = s.intensity01;
    setup.complexity01 = s.complexity01;
    setup.chaos01      = s.chaos01;
    setup.sourceAnchor = history.getWritePosition() > 0 ? outputPos : 0;
    setup.sinceEventSamples = (int) juce::jlimit ((std::int64_t) 0, (std::int64_t) (sr * 0.3),
                                                  outputPos - lastEventPos);
    setup.band = band;
    setup.xoverLM = s.xoverLM;
    setup.xoverMH = s.xoverMH;
    setup.stutterSeconds = s.stutterSeconds;
    setup.delaySeconds = s.delaySeconds;
    setup.pingPong = s.pingPong;
    setup.rngSeed = hashCombine (key, (std::uint64_t) (algoIndex + 1) * 0x27D4EB2FULL);

    for (int b = 0; b < 3; ++b)
        setup.bandEnergy[b] = frame.bandEnergy[b];

    auto amp = 0.8f * (0.6f + 0.4f * conf);

    if (band >= 0)
    {
        amp *= s.bandAmt[band];
        setup.intensity01 = juce::jlimit (0.0f, 1.0f, s.bandInt[band]);
    }

    setup.amp = juce::jlimit (0.0f, 1.5f, amp);

    if (! voice->start (setup, history, &spectral))
    {
        // Spectral processor busy - fall back to a simpler algorithm.
        setup.algo = Algo::Granular;

        if (lengthMs < kProfiles[(int) Algo::Granular].minMs)
            setup.algo = Algo::Reverse;

        if (! voice->start (setup, history, &spectral))
            return false;

        algoIndex = (int) setup.algo;
    }

    recentAlgos[recentWrite] = algoIndex;
    recentWrite = (recentWrite + 1) % historyLength;
    lastFillStartPos = outputPos;
    ++fillsStarted;

    return true;
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
