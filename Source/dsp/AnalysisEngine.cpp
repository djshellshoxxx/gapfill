#include "AnalysisEngine.h"

namespace gapfill
{

//==============================================================================
// LaneDetector
//==============================================================================

void LaneDetector::prepare (double sampleRateIn, int hopSizeIn)
{
    sampleRate = juce::jmax (8000.0, sampleRateIn);
    hopSize    = juce::jmax (1, hopSizeIn);
    hopSeconds = (float) ((double) hopSize / sampleRate);

    configure (currentMode);
    reset();
}

void LaneDetector::configure (DetMode mode)
{
    currentMode = mode;

    switch (mode)
    {
        case DetMode::Phrase:
            fast.setTimes (sampleRate, 6.0f, 90.0f);
            slow.setTimes (sampleRate, 120.0f, 500.0f);
            refDecay = std::exp (-hopSeconds / 1.6f);
            break;

        case DetMode::Spectral:
            fast.setTimes (sampleRate, 0.8f, 25.0f);
            slow.setTimes (sampleRate, 40.0f, 200.0f);
            refDecay = std::exp (-hopSeconds / 0.9f);
            break;

        case DetMode::Transient:
        case DetMode::NumModes:
        default:
            fast.setTimes (sampleRate, 0.4f, 18.0f);
            slow.setTimes (sampleRate, 30.0f, 160.0f);
            refDecay = std::exp (-hopSeconds / 0.8f);
            break;
    }
}

void LaneDetector::reset()
{
    fast.reset();
    slow.reset();
    floorEnv  = 0.0f;
    reference = 0.0f;
    referenceLevel = 0.0f;
    confValue = 0.0f;
    hopPeak = hopSum = 0.0f;
    hopCount = 0;
    sinceEventMs = sinceOnsetMs = 10000.0f;
    riseHoldMs = 0.0f;
    armed = true;
    conf = 0.0f;
    energy = 0.0f;
    peak = 0.0f;
    transientStrength = 0.0f;
    eventThisHop = false;
}

void LaneDetector::updateHop (const AnalysisSettings& s, float flux) noexcept
{
    const auto hopMs   = hopSeconds * 1000.0f;
    const auto hopRate = 1.0f / juce::jmax (1.0e-6f, hopSeconds);

    peak   = hopPeak;
    const auto rms = hopCount > 0 ? std::sqrt (hopSum / (float) hopCount) : 0.0f;
    hopPeak = hopSum = 0.0f;
    hopCount = 0;

    sinceEventMs += hopMs;
    sinceOnsetMs += hopMs;

    const auto f  = fast.value();
    const auto sl = juce::jmax (slow.value(), rms * 0.25f);

    // -- adaptive noise/energy floor: falls quickly, rises slowly ------------
    {
        static constexpr float downMs = 60.0f, upMs = 4000.0f;
        const auto c = sl < floorEnv ? onePoleCoeff (hopRate, downMs)
                                     : onePoleCoeff (hopRate, upMs);
        floorEnv += c * (sl - floorEnv);
        floorEnv = juce::jmax (0.0f, floorEnv);

        if (! std::isfinite (floorEnv))
            floorEnv = 0.0f;
    }

    // -- local musical reference level (peak hold with decay) ---------------
    reference = juce::jmax (f, reference * refDecay);

    if (! std::isfinite (reference))
        reference = 0.0f;

    // -- onset score --------------------------------------------------------
    const auto ratioDb = gainToDb (f + kTinyLevel) - gainToDb (sl + kTinyLevel);

    float levelScore = 0.0f, fluxWeight = 0.0f;

    switch (currentMode)
    {
        case DetMode::Phrase:
            levelScore = smoothstep (1.5f, 9.0f, ratioDb);
            fluxWeight = 0.55f;
            break;

        case DetMode::Spectral:
            levelScore = smoothstep (2.0f, 13.0f, ratioDb);
            fluxWeight = 0.30f;
            break;

        case DetMode::Transient:
        case DetMode::NumModes:
        default:
            levelScore = smoothstep (2.0f, 14.0f, ratioDb);
            fluxWeight = 0.30f;
            break;
    }

    if (! useFlux)
        fluxWeight = 0.0f;

    const auto score = (1.0f - fluxWeight) * levelScore + fluxWeight * juce::jlimit (0.0f, 1.0f, flux);

    transientStrength = juce::jlimit (0.0f, 1.0f, levelScore);

    const auto threshold = lerp (0.72f, 0.10f, juce::jlimit (0.0f, 1.0f, s.sensitivity01));

    // Absolute gate: never fire on the noise floor or on digital silence.
    const auto audible = f > juce::jmax (floorEnv * 2.5f, 3.0e-5f);

    auto guardMs = juce::jmax (hopMs, s.retrigGuardMs);

    if (currentMode == DetMode::Phrase)
        guardMs = juce::jmax (guardMs, 120.0f);

    eventThisHop = false;

    if (score > threshold && audible && armed && sinceEventMs >= guardMs)
    {
        eventThisHop = true;
        sinceEventMs = 0.0f;
        sinceOnsetMs = 0.0f;
        armed = false;
        reference = juce::jmax (reference, f);
    }
    else if (score < threshold * 0.55f)
    {
        armed = true;
    }

    // -- gap confidence -----------------------------------------------------
    const auto dropDb = gainToDb (reference + kTinyLevel) - gainToDb (sl + kTinyLevel);
    const auto requiredDrop = lerp (30.0f, 7.0f, juce::jlimit (0.0f, 1.0f, s.gapThresh01));

    auto levelConf = smoothstep (requiredDrop - 7.0f, requiredDrop + 3.0f, dropDb);

    // A region sitting on the adaptive floor is space regardless of the drop.
    const auto nearFloor = 1.0f - smoothstep (floorEnv * 1.4f + 1.0e-6f,
                                              floorEnv * 4.0f + 4.0e-6f, sl);
    levelConf = juce::jmax (levelConf, nearFloor * 0.95f);

    // Absolute silence is always usable space.
    if (sl < 1.0e-5f)
        levelConf = 1.0f;

    const auto holdMs = juce::jmax (0.0f, s.protectHoldMs);
    const auto timeConf = smoothstep (holdMs * 0.5f, holdMs + 15.0f, sinceEventMs);

    const auto raw = juce::jlimit (0.0f, 1.0f, levelConf * timeConf);

    // Rising toward "this is a gap" takes as long as Gap Confirm; collapsing
    // back to "busy" is immediate so that a new hit always wins.
    const auto cRise = onePoleCoeff (hopRate, juce::jmax (1.0f, s.gapReleaseMs));
    const auto cFall = onePoleCoeff (hopRate, 4.0f);
    confValue += (raw > confValue ? cRise : cFall) * (raw - confValue);

    if (! std::isfinite (confValue))
        confValue = 0.0f;

    conf   = juce::jlimit (0.0f, 1.0f, confValue);
    energy = juce::jlimit (0.0f, 1.0f, (gainToDb (sl + kTinyLevel) + 60.0f) / 60.0f);
    referenceLevel = reference;

    juce::ignoreUnused (riseHoldMs);
}

//==============================================================================
// FluxAnalyser
//==============================================================================

void FluxAnalyser::prepare (double)
{
    fifo.assign ((size_t) fftSize, 0.0f);
    scratch.assign ((size_t) fftSize * 2, 0.0f);
    prevMag.assign ((size_t) (fftSize / 2), 0.0f);
    window.resize ((size_t) fftSize);

    for (int i = 0; i < fftSize; ++i)
        window[(size_t) i] = hann ((float) i / (float) (fftSize - 1));

    reset();
}

void FluxAnalyser::reset()
{
    std::fill (fifo.begin(), fifo.end(), 0.0f);
    std::fill (prevMag.begin(), prevMag.end(), 0.0f);
    fifoPos = 0;
    hopCounter = 0;
    fluxValue = 0.0f;
}

void FluxAnalyser::computeFrame() noexcept
{
    if (scratch.size() < (size_t) fftSize * 2)
        return;

    // Unwrap the circular fifo into the (windowed) scratch buffer.
    for (int i = 0; i < fftSize; ++i)
        scratch[(size_t) i] = fifo[(size_t) ((fifoPos + i) % fftSize)] * window[(size_t) i];

    std::fill (scratch.begin() + fftSize, scratch.end(), 0.0f);

    fft.performFrequencyOnlyForwardTransform (scratch.data(), true);

    float flux = 0.0f, total = 0.0f;
    const int bins = fftSize / 2;

    for (int i = 1; i < bins; ++i)
    {
        const auto m = scratch[(size_t) i];
        const auto d = m - prevMag[(size_t) i];

        if (d > 0.0f)
            flux += d;

        total += m;
        prevMag[(size_t) i] = m;
    }

    const auto normalised = flux / (total + 1.0e-6f);
    fluxValue = smoothstep (0.08f, 0.45f, normalised);

    if (! std::isfinite (fluxValue))
        fluxValue = 0.0f;
}

//==============================================================================
// BandSplitter
//==============================================================================

void BandSplitter::prepare (double sampleRate, int numChannels)
{
    sr = juce::jmax (8000.0, sampleRate);
    stages.resize ((size_t) juce::jmax (1, numChannels));

    juce::dsp::ProcessSpec spec { sr, 512, 1 };

    for (auto& s : stages)
    {
        // Note: the two-output processSample() always yields the LR low/high
        // pair whose sum reconstructs the input, regardless of the filter type.
        s.lowSplit.prepare (spec);
        s.highSplit.prepare (spec);
    }

    lastLowMid = lastMidHigh = 0.0f;
    setCrossovers (180.0f, 3500.0f);
    reset();
}

void BandSplitter::reset()
{
    for (auto& s : stages)
    {
        s.lowSplit.reset();
        s.highSplit.reset();
    }
}

void BandSplitter::setCrossovers (float lowMid, float midHigh)
{
    const auto nyq = (float) (sr * 0.5);
    const auto lm = juce::jlimit (30.0f, nyq * 0.45f, lowMid);
    const auto mh = juce::jlimit (lm * 1.5f, nyq * 0.9f, midHigh);

    if (std::abs (lm - lastLowMid) > 0.01f || std::abs (mh - lastMidHigh) > 0.01f)
    {
        lastLowMid  = lm;
        lastMidHigh = mh;

        for (auto& s : stages)
        {
            s.lowSplit.setCutoffFrequency (lm);
            s.highSplit.setCutoffFrequency (mh);
        }
    }
}

void BandSplitter::processStereo (int channel, float x, float& low, float& mid, float& high) noexcept
{
    const auto idx = (size_t) juce::jlimit (0, (int) stages.size() - 1, channel);
    auto& s = stages[idx];

    float rest = 0.0f;
    s.lowSplit.processSample (0, x, low, rest);
    s.highSplit.processSample (0, rest, mid, high);

    low  = sanitise (low, 32.0f);
    mid  = sanitise (mid, 32.0f);
    high = sanitise (high, 32.0f);
}

void BandSplitter::processMono (float x, float& low, float& mid, float& high) noexcept
{
    processStereo (0, x, low, mid, high);
}

//==============================================================================
// AnalysisEngine
//==============================================================================

void AnalysisEngine::prepare (double sampleRateIn, int /*maxBlockSize*/)
{
    sampleRate = juce::jmax (8000.0, sampleRateIn);

    frames.assign ((size_t) frameCapacity, AnalysisFrame {});

    mainLane.prepare (sampleRate, hopSize);
    scLane.prepare (sampleRate, hopSize);

    for (auto& l : mainBandLanes) { l.useFlux = false; l.prepare (sampleRate, hopSize); }
    for (auto& l : scBandLanes)   { l.useFlux = false; l.prepare (sampleRate, hopSize); }

    mainSplit.prepare (sampleRate, 1);
    scSplit.prepare (sampleRate, 1);

    mainFlux.prepare (sampleRate);
    scFlux.prepare (sampleRate);

    reset();
}

void AnalysisEngine::reset()
{
    std::fill (frames.begin(), frames.end(), AnalysisFrame {});
    pending = {};

    mainLane.reset();
    scLane.reset();

    for (auto& l : mainBandLanes) l.reset();
    for (auto& l : scBandLanes)   l.reset();

    mainSplit.reset();
    scSplit.reset();
    mainFlux.reset();
    scFlux.reset();

    detectionEvents.clear();
    mainEvents.clear();
    scEvents.clear();

    for (auto& q : bandEvents)
        q.clear();

    samplePos = 0;
    hopCounter = 0;
    lastConfidence = 0.0f;
}

void AnalysisEngine::processBlock (const float* mainMono, const float* sidechainMono,
                                   int numSamples, const AnalysisSettings& s) noexcept
{
    if (mainMono == nullptr || numSamples <= 0)
        return;

    const bool modeChanged   = (s.mode != lastMode);
    const bool sourceChanged = (s.source != lastSource) || (s.sidechainActive != lastSidechain);

    needsScLane    = (s.source != DetSource::Main) && s.sidechainActive;
    const bool needsScBands = needsScLane && (s.mode == DetMode::Spectral);

    if (modeChanged)
    {
        mainLane.configure (s.mode);
        scLane.configure (s.mode);

        for (auto& l : mainBandLanes) l.configure (s.mode);
        for (auto& l : scBandLanes)   l.configure (s.mode);
    }

    if (sourceChanged && needsScLane)
    {
        scLane.reset();

        for (auto& l : scBandLanes)
            l.reset();

        scSplit.reset();
        scFlux.reset();
    }

    lastMode = s.mode;
    lastSource = s.source;
    lastSidechain = s.sidechainActive;
    needsBandLanes = (s.mode == DetMode::Spectral);

    mainSplit.setCrossovers (s.xoverLowMid, s.xoverMidHigh);
    scSplit.setCrossovers (s.xoverLowMid, s.xoverMidHigh);

    float mainFluxValue = mainFlux.value();
    float scFluxValue   = scFlux.value();

    for (int i = 0; i < numSamples; ++i)
    {
        const auto m = sanitise (mainMono[i], 32.0f);
        const auto sc = (sidechainMono != nullptr) ? sanitise (sidechainMono[i], 32.0f) : 0.0f;

        mainLane.pushSample (m);
        mainFluxValue = mainFlux.push (m);

        float lo = 0.0f, mi = 0.0f, hi = 0.0f;
        mainSplit.processMono (m, lo, mi, hi);
        mainBandLanes[0].pushSample (lo);
        mainBandLanes[1].pushSample (mi);
        mainBandLanes[2].pushSample (hi);

        if (needsScLane)
        {
            scLane.pushSample (sc);
            scFluxValue = scFlux.push (sc);

            if (needsScBands)
            {
                float slo = 0.0f, smi = 0.0f, shi = 0.0f;
                scSplit.processMono (sc, slo, smi, shi);
                scBandLanes[0].pushSample (slo);
                scBandLanes[1].pushSample (smi);
                scBandLanes[2].pushSample (shi);
            }
        }

        ++samplePos;

        if (++hopCounter >= hopSize)
        {
            hopCounter = 0;

            mainLane.updateHop (s, mainFluxValue);

            // Band lanes deliberately ignore broadband flux so that activity in
            // one region cannot mark another region as busy.
            for (auto& l : mainBandLanes)
                l.updateHop (s, 0.0f);

            if (needsScLane)
            {
                scLane.updateHop (s, scFluxValue);

                if (needsScBands)
                    for (auto& l : scBandLanes)
                        l.updateHop (s, 0.0f);
            }

            writeFrame (s);
        }
    }
}

void AnalysisEngine::writeFrame (const AnalysisSettings& s) noexcept
{
    const auto hopStart = samplePos - hopSize;
    const auto frameIndex = hopStart / hopSize;

    AnalysisFrame frame;

    const bool scUsable = needsScLane;

    // ---- broadband detection lane ----
    bool detectionEvent = false;

    if (s.source == DetSource::Main || ! scUsable)
    {
        frame.conf = mainLane.conf;
        frame.transient = mainLane.transientStrength;
        detectionEvent = mainLane.eventThisHop;
    }
    else if (s.source == DetSource::Sidechain)
    {
        frame.conf = scLane.conf;
        frame.transient = scLane.transientStrength;
        detectionEvent = scLane.eventThisHop;
    }
    else // Hybrid: the sidechain says where the space is, the main track still
         // protects its own important events.
    {
        frame.conf = juce::jmin (scLane.conf, mainLane.conf);
        frame.transient = juce::jmax (scLane.transientStrength, mainLane.transientStrength);
        detectionEvent = scLane.eventThisHop || mainLane.eventThisHop;
    }

    // ---- per band lanes ----
    const bool useScBands = scUsable && needsBandLanes && (s.source != DetSource::Main);

    for (int b = 0; b < 3; ++b)
    {
        const auto& mainBand = mainBandLanes[(size_t) b];
        frame.bandEnergy[b] = mainBand.energy;

        bool bandEvent = mainBand.eventThisHop;
        float bandConf = mainBand.conf;

        if (useScBands)
        {
            const auto& scBand = scBandLanes[(size_t) b];

            if (s.source == DetSource::Sidechain)
            {
                bandConf = scBand.conf;
                bandEvent = scBand.eventThisHop;
            }
            else
            {
                bandConf = juce::jmin (scBand.conf, mainBand.conf);
                bandEvent = scBand.eventThisHop || mainBand.eventThisHop;
            }
        }

        frame.bandConf[b] = bandConf;

        if (bandEvent)
            bandEvents[(size_t) b].push (hopStart);
    }

    frame.energy = mainLane.energy;
    frame.reference = mainLane.referenceLevel;
    frame.peak = mainLane.peak;
    frame.mainTransient = mainLane.transientStrength;
    frame.scTransient = scUsable ? scLane.transientStrength : 0.0f;

    if (detectionEvent)
        detectionEvents.push (hopStart);

    if (mainLane.eventThisHop)
        mainEvents.push (hopStart);

    if (scUsable && scLane.eventThisHop)
        scEvents.push (hopStart);

    frames[(size_t) (frameIndex % frameCapacity)] = frame;
    lastConfidence = frame.conf;
}

} // namespace gapfill
