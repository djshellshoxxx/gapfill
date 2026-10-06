// GapFill
// Copyright © 2026 Sheldon Davidson.
// Licensed under AGPL-3.0-or-later. See LICENSE.
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace gapfill
{

namespace
{
    constexpr double kMaxLookaheadSeconds = 0.25;
    constexpr double kHistorySeconds      = 6.0;
    constexpr int    kSidechainSilenceSeconds = 3;
}

//==============================================================================
GapFillAudioProcessor::GapFillAudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
                        .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "GAPFILL", createParameterLayout())
{
    params.connect (apvts);
    bypassParameter = apvts.getParameter ("bypass");
    midiLearn.connect (apvts);

    for (const auto& id : allParameterIDs())
        apvts.addParameterListener (id, this);

    randomSource.setSeedRandomly();
    startTimerHz (15);
}

GapFillAudioProcessor::~GapFillAudioProcessor()
{
    stopTimer();

    for (const auto& id : allParameterIDs())
        apvts.removeParameterListener (id, this);
}

//==============================================================================
bool GapFillAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mainIn  = layouts.getMainInputChannelSet();
    const auto mainOut = layouts.getMainOutputChannelSet();

    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
        return false;

    if (mainIn != mainOut)
        return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto side = layouts.getChannelSet (true, 1);

        if (! side.isDisabled()
             && side != juce::AudioChannelSet::mono()
             && side != juce::AudioChannelSet::stereo())
            return false;
    }

    return true;
}

//==============================================================================
void GapFillAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    prepared.store (false);

    currentSampleRate = juce::jmax (8000.0, sampleRate);
    preparedBlockSize = juce::jlimit (16, 65536, samplesPerBlock > 0 ? samplesPerBlock : 512);
    mainChannels = juce::jlimit (1, 2, juce::jmax (1, getMainBusNumOutputChannels()));

    maxLookaheadSamples = (int) std::ceil (kMaxLookaheadSeconds * currentSampleRate) + 8;
    delayRingSize = maxLookaheadSamples + preparedBlockSize * 2 + 16;

    mainBuffer.setSize (2, preparedBlockSize, false, true, false);
    wetBuffer.setSize (2, preparedBlockSize, false, true, false);
    delayRing.setSize (2, delayRingSize, false, true, false);
    delayRing.clear();

    monoMain.assign ((size_t) preparedBlockSize, 0.0f);
    monoSide.assign ((size_t) preparedBlockSize, 0.0f);

    history.prepare (2, (int) std::ceil (kHistorySeconds * currentSampleRate));
    analysis.prepare (currentSampleRate, preparedBlockSize);
    engine.prepare (currentSampleRate, preparedBlockSize, mainChannels);
    soloSplitter.prepare (currentSampleRate, 2);

    for (auto& hp : lowProtect)
        hp.reset();

    lastLowProtectHz = -1.0f;
    lastSoloXoverLM = lastSoloXoverMH = -1.0f;

    for (auto* s : { &dryGain, &wetGain, &outGain, &centreProtect, &widthAmount, &bypassMix })
    {
        s->reset (currentSampleRate, 0.02);
        s->setCurrentAndTargetValue (0.0f);
    }

    dryGain.setCurrentAndTargetValue (pv (params.dry) * 0.01f);
    wetGain.setCurrentAndTargetValue (pv (params.wet) * 0.01f);
    outGain.setCurrentAndTargetValue (dbToGain (pv (params.output)));
    widthAmount.setCurrentAndTargetValue (pv (params.width) * 0.01f);

    delayWrite = 0;
    samplesProcessed = 0;
    sidechainSilentSamples = kSidechainSilenceSeconds * (int) currentSampleRate;
    sidechainHasSignal = false;

    currentLatency = targetLatency = juce::jlimit (0, maxLookaheadSamples,
                                                   (int) std::round (pv (params.lookahead) * 0.001 * currentSampleRate));
    latencyCrossfade = latencyCrossfadeLength = 0;

    setLatencySamples (currentLatency);
    latencyToReport.store (currentLatency);
    meters.latencySamples.store (currentLatency);

    viz.reset();
    prepared.store (true);
}

void GapFillAudioProcessor::releaseResources()
{
    prepared.store (false);
    reset();
}

void GapFillAudioProcessor::reset()
{
    // Hosts call this on transport jumps and when re-arming a track: every
    // piece of running DSP state has to go, without reallocating anything.
    engine.reset();
    analysis.reset();
    history.clear();

    delayRing.clear();
    wetBuffer.clear();
    mainBuffer.clear();
    delayWrite = 0;
    samplesProcessed = 0;
    latencyCrossfade = latencyCrossfadeLength = 0;
    currentLatency = targetLatency;

    for (auto& hp : lowProtect)
        hp.reset();

    soloSplitter.reset();
    lastLowProtectHz = -1.0f;
    meterIn = meterOut = meterWet = 0.0f;
    lastPpqPosition = 0.0;
    lastTransportPlaying = false;
    sidechainSilentSamples = kSidechainSilenceSeconds * (int) currentSampleRate;
}

//==============================================================================
void GapFillAudioProcessor::timerCallback()
{
    const auto wanted = latencyToReport.load();

    if (wanted != getLatencySamples())
        setLatencySamples (wanted);

    // Applies CC moves and completed learns on the thread that owns them.
    // MIDI learn is intentionally bounded so the plugin cannot stay armed
    // indefinitely after the context menu closes.
    const bool learnChanged = midiLearn.applyPendingChanges();
    const bool learnTimedOut = midiLearn.advanceLearningTimeout();

    if (learnChanged || learnTimedOut)
        updateHostDisplay();
}

void GapFillAudioProcessor::parameterChanged (const juce::String&, float)
{
    if (! programChangeInProgress.load (std::memory_order_relaxed))
        currentProgram.store (-1, std::memory_order_relaxed);
}

//==============================================================================
AnalysisSettings GapFillAudioProcessor::gatherAnalysisSettings (bool sidechainActive) const
{
    AnalysisSettings s;
    s.mode           = (DetMode) juce::jlimit (0, 2, pi (params.detMode));
    s.source         = (DetSource) juce::jlimit (0, 2, pi (params.detSource));
    s.sensitivity01  = pv (params.sensitivity) * 0.01f;
    s.gapThresh01    = pv (params.gapThresh) * 0.01f;
    s.protectHoldMs  = pv (params.protectHold);
    s.retrigGuardMs  = pv (params.retrigGuard);
    s.gapReleaseMs   = pv (params.gapRelease);
    s.xoverLowMid    = pv (params.xoverLM);
    s.xoverMidHigh   = juce::jmax (s.xoverLowMid * 1.5f, pv (params.xoverMH));
    s.sidechainActive = sidechainActive;
    return s;
}

EngineSettings GapFillAudioProcessor::gatherEngineSettings() const
{
    EngineSettings s;
    s.mode         = (DetMode) juce::jlimit (0, 2, pi (params.detMode));
    s.fill01       = pv (params.fill) * 0.01f;
    s.intensity01  = pv (params.intensity) * 0.01f;
    s.complexity01 = pv (params.complexity) * 0.01f;
    s.chaos01      = pv (params.chaos) * 0.01f;
    s.density01    = pv (params.density) * 0.01f;
    s.fillTypeIndex = pi (params.fillType);
    s.seed         = pi (params.seed);

    for (int i = 0; i < numAlgos; ++i)
        s.weights[i] = pv (params.algoWeights[i]) * 0.01f;

    s.minGapMs      = pv (params.minGap);
    s.maxFillMs     = pv (params.maxFill);
    s.protectHoldMs = pv (params.protectHold);
    s.lookaheadMs   = pv (params.lookahead);

    s.protTrans01 = pv (params.protTrans) * 0.01f;
    s.protNext01  = pv (params.protNext) * 0.01f;
    s.protSC01    = pv (params.protSC) * 0.01f;

    s.pingPong = pb (params.pingPong);

    s.xoverLM = pv (params.xoverLM);
    s.xoverMH = juce::jmax (s.xoverLM * 1.5f, pv (params.xoverMH));

    s.bandOn[0] = pb (params.bandLowOn);
    s.bandOn[1] = pb (params.bandMidOn);
    s.bandOn[2] = pb (params.bandHighOn);
    s.bandAmt[0] = pv (params.bandLowAmt) * 0.01f;
    s.bandAmt[1] = pv (params.bandMidAmt) * 0.01f;
    s.bandAmt[2] = pv (params.bandHighAmt) * 0.01f;
    s.bandInt[0] = pv (params.bandLowInt) * 0.01f;
    s.bandInt[1] = pv (params.bandMidInt) * 0.01f;
    s.bandInt[2] = pv (params.bandHighInt) * 0.01f;
    s.bandProb[0] = pv (params.bandLowProb) * 0.01f;
    s.bandProb[1] = pv (params.bandMidProb) * 0.01f;
    s.bandProb[2] = pv (params.bandHighProb) * 0.01f;

    return s;
}

void GapFillAudioProcessor::updateTempoInfo (EngineSettings& s, int startSample, int numSamples)
{
    double bpm = 120.0;
    bool tempoValid = false;
    bool playing = false;
    double ppq = 0.0;
    bool ppqValid = false;

    if (auto* ph = getPlayHead())
    {
        if (const auto pos = ph->getPosition())
        {
            if (const auto b = pos->getBpm())
            {
                if (*b > 1.0 && *b < 1000.0)
                {
                    bpm = *b;
                    tempoValid = true;
                }
            }

            if (const auto p = pos->getPpqPosition())
            {
                ppq = *p;
                ppqValid = std::isfinite (ppq);
            }

            playing = pos->getIsPlaying();
        }
    }

    const auto samplesPerBeat = 60.0 / bpm * currentSampleRate;

    s.samplesPerBeat = samplesPerBeat;
    // The host reports the position of the whole block, so offset it for the
    // sub-chunk we are actually rendering.
    s.ppqAtBlockStart = ppq + (double) startSample / samplesPerBeat;
    s.transportValid = playing && ppqValid && tempoValid;

    // A transport jump (or a fresh start) restarts the deterministic sequence.
    if (playing && startSample == 0)
    {
        const auto expected = lastPpqPosition + (double) numSamples / samplesPerBeat;

        if (! lastTransportPlaying || ! ppqValid || std::abs (ppq - expected) > 0.25)
        {
            engine.noteTransportJump();
            engine.panic();
        }

        lastPpqPosition = ppq;
    }
    else if (lastTransportPlaying && ! playing && startSample == 0)
    {
        engine.panic();
    }

    if (startSample == 0)
        lastTransportPlaying = playing;

    // Tempo-synced lengths, with a musical fallback when the host has no tempo.
    const auto beatSeconds = tempoValid ? 60.0 / bpm : 0.5;

    if (pb (params.syncEnable))
    {
        s.stutterSeconds = (float) (divisionInBeats (pi (params.stutterDiv)) * beatSeconds);
        s.delaySeconds   = (float) (divisionInBeats (pi (params.delayDiv)) * beatSeconds);
    }
    else
    {
        s.stutterSeconds = 0.0625f;
        s.delaySeconds   = 0.125f;
    }

    s.stutterSeconds = juce::jlimit (0.008f, 0.5f, s.stutterSeconds);
    s.delaySeconds   = juce::jlimit (0.01f, 1.0f, s.delaySeconds);

    if (pb (params.maxFillSync))
    {
        const auto ms = (float) (divisionInBeats (pi (params.maxFillDiv)) * beatSeconds * 1000.0);
        s.maxFillMs = juce::jlimit (20.0f, 2000.0f, ms);
    }
}

//==============================================================================
void GapFillAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    midiLearn.processMidi (midi);

    const auto numSamples = buffer.getNumSamples();

    // Clear any output channel that has no matching input.
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    if (! prepared.load() || numSamples <= 0)
        return;

    const bool bypassed = pb (params.bypass);

    int offset = 0;

    while (offset < numSamples)
    {
        const auto n = juce::jmin (preparedBlockSize, numSamples - offset);
        processChunk (buffer, offset, n, bypassed);
        offset += n;
    }
}

void GapFillAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    midiLearn.processMidi (midi);

    const auto numSamples = buffer.getNumSamples();

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    if (! prepared.load() || numSamples <= 0)
        return;

    int offset = 0;

    while (offset < numSamples)
    {
        const auto n = juce::jmin (preparedBlockSize, numSamples - offset);
        processChunk (buffer, offset, n, true);
        offset += n;
    }
}

//==============================================================================
void GapFillAudioProcessor::processChunk (juce::AudioBuffer<float>& buffer,
                                          int startSample, int numSamples, bool bypassed)
{
    auto mainBus = getBusBuffer (buffer, true, 0);
    auto outBus  = getBusBuffer (buffer, false, 0);

    if (mainBus.getNumChannels() <= 0 || outBus.getNumChannels() <= 0)
        return;

    const auto inChannels = juce::jmax (1, mainBus.getNumChannels());

    // ---- copy the main input somewhere stable ---------------------------
    {
        const auto* inL = mainBus.getReadPointer (0, startSample);
        const auto* inR = inChannels > 1 ? mainBus.getReadPointer (1, startSample) : inL;

        auto* dstL = mainBuffer.getWritePointer (0);
        auto* dstR = mainBuffer.getWritePointer (1);

        for (int i = 0; i < numSamples; ++i)
        {
            dstL[i] = sanitise (inL[i], 32.0f);
            dstR[i] = sanitise (inR[i], 32.0f);
        }
    }

    // ---- sidechain -------------------------------------------------------
    sidechainBusEnabled = (getBusCount (true) > 1 && getBus (true, 1) != nullptr
                            && getBus (true, 1)->isEnabled());

    bool sidechainSeen = false;

    if (sidechainBusEnabled)
    {
        auto scBus = getBusBuffer (buffer, true, 1);

        if (scBus.getNumChannels() > 0)
        {
            const auto* scL = scBus.getReadPointer (0, startSample);
            const auto* scR = scBus.getNumChannels() > 1 ? scBus.getReadPointer (1, startSample) : scL;

            for (int i = 0; i < numSamples; ++i)
            {
                const auto v = 0.5f * (sanitise (scL[i], 32.0f) + sanitise (scR[i], 32.0f));
                monoSide[(size_t) i] = v;

                if (std::abs (v) > 1.0e-6f)
                    sidechainSeen = true;
            }
        }
        else
        {
            std::fill_n (monoSide.begin(), numSamples, 0.0f);
        }
    }
    else
    {
        std::fill_n (monoSide.begin(), numSamples, 0.0f);
    }

    if (sidechainSeen)
        sidechainSilentSamples = 0;
    else
        sidechainSilentSamples = juce::jmin (sidechainSilentSamples + numSamples,
                                             kSidechainSilenceSeconds * (int) currentSampleRate + 1);

    sidechainHasSignal = sidechainSilentSamples < kSidechainSilenceSeconds * (int) currentSampleRate;

    const auto detSource = (DetSource) juce::jlimit (0, 2, pi (params.detSource));
    const bool sidechainActive = sidechainBusEnabled && sidechainHasSignal
                                   && detSource != DetSource::Main;

    meters.sidechainConnected.store (sidechainBusEnabled && sidechainHasSignal);
    meters.sidechainRequested.store (detSource != DetSource::Main);

    // ---- mono detection signal ------------------------------------------
    {
        const auto* l = mainBuffer.getReadPointer (0);
        const auto* r = mainBuffer.getReadPointer (1);

        for (int i = 0; i < numSamples; ++i)
            monoMain[(size_t) i] = inChannels > 1 ? 0.5f * (l[i] + r[i]) : l[i];
    }

    // ---- history + analysis ---------------------------------------------
    history.write (mainBuffer, 0, numSamples);

    const auto analysisSettings = gatherAnalysisSettings (sidechainActive);
    analysis.processBlock (monoMain.data(), sidechainActive ? monoSide.data() : nullptr,
                           numSamples, analysisSettings);

    // ---- latency / lookahead --------------------------------------------
    const auto wantedLatency = juce::jlimit (0, maxLookaheadSamples,
                                             (int) std::round (pv (params.lookahead) * 0.001 * currentSampleRate));

    if (wantedLatency != targetLatency)
    {
        pendingLatencyFrom = currentLatency;
        targetLatency = wantedLatency;
        latencyCrossfadeLength = juce::jmin (numSamples, 256);
        latencyCrossfade = 0;
        latencyToReport.store (targetLatency);
        meters.latencySamples.store (targetLatency);
    }

    // ---- generate the wet bus -------------------------------------------
    wetBuffer.clear (0, numSamples);

    auto engineSettings = gatherEngineSettings();
    engineSettings.sidechainActive = sidechainActive;
    updateTempoInfo (engineSettings, startSample, numSamples);

    if (bypassed)
    {
        // Keep analysing (so the visualiser still shows what the detector sees)
        // but stop scheduling and fade out anything still playing.
        engineSettings.fill01 = 0.0f;
        engine.panic();
    }

    const auto outputStart = samplesProcessed - (std::int64_t) targetLatency;
    const auto skip = (int) juce::jlimit ((std::int64_t) 0, (std::int64_t) numSamples, -outputStart);

    analysis.discardEventsBefore (juce::jmax ((std::int64_t) 0, outputStart));

    if (skip < numSamples)
    {
        engine.process (wetBuffer, skip, numSamples - skip, outputStart + skip,
                        analysis, history, engineSettings, &viz);
    }

    // ---- dry path: lookahead delay with click-free latency changes -------
    {
        auto* ringL = delayRing.getWritePointer (0);
        auto* ringR = delayRing.getWritePointer (1);
        const auto* srcL = mainBuffer.getReadPointer (0);
        const auto* srcR = mainBuffer.getReadPointer (1);
        auto* dstL = mainBuffer.getWritePointer (0);
        auto* dstR = mainBuffer.getWritePointer (1);

        for (int i = 0; i < numSamples; ++i)
        {
            ringL[delayWrite] = srcL[i];
            ringR[delayWrite] = srcR[i];

            const auto readNew = (delayWrite - targetLatency + delayRingSize) % delayRingSize;

            float outL, outR;

            if (currentLatency != targetLatency && latencyCrossfadeLength > 0)
            {
                const auto readOld = (delayWrite - pendingLatencyFrom + delayRingSize) % delayRingSize;
                const auto t = juce::jlimit (0.0f, 1.0f, (float) latencyCrossfade / (float) latencyCrossfadeLength);
                float ga, gb;
                equalPower (t, ga, gb);
                outL = ringL[readOld] * ga + ringL[readNew] * gb;
                outR = ringR[readOld] * ga + ringR[readNew] * gb;

                if (++latencyCrossfade >= latencyCrossfadeLength)
                    currentLatency = targetLatency;
            }
            else
            {
                currentLatency = targetLatency;
                outL = ringL[readNew];
                outR = ringR[readNew];
            }

            dstL[i] = outL;
            dstR[i] = outR;
            delayWrite = (delayWrite + 1) % delayRingSize;
        }
    }

    // ---- wet post-processing --------------------------------------------
    {
        auto* wl = wetBuffer.getWritePointer (0);
        auto* wr = wetBuffer.getWritePointer (1);

        // Low Protect: keep the generated material out of the kick/sub region.
        const auto lowHz = pv (params.protLow);

        if (lowHz > 20.5f)
        {
            if (std::abs (lowHz - lastLowProtectHz) > 0.5f)
            {
                lastLowProtectHz = lowHz;

                for (auto& hp : lowProtect)
                    hp.set (currentSampleRate, lowHz);
            }

            for (int i = 0; i < numSamples; ++i)
            {
                wl[i] = lowProtect[0].process (wl[i]);
                wr[i] = lowProtect[1].process (wr[i]);
            }
        }
        else
        {
            lastLowProtectHz = -1.0f;
        }

        centreProtect.setTargetValue (pv (params.protCenter) * 0.01f);
        widthAmount.setTargetValue (pv (params.width) * 0.01f);
        const bool monoWet = pb (params.wetMono);

        for (int i = 0; i < numSamples; ++i)
        {
            const auto cp = centreProtect.getNextValue();
            auto width = widthAmount.getNextValue();

            auto mid  = 0.5f * (wl[i] + wr[i]);
            auto side = 0.5f * (wl[i] - wr[i]);

            // Center Protect pushes generated material out to the sides so that
            // centred kick/snare/bass/vocal stays clear.
            mid  *= (1.0f - cp);
            side *= (1.0f + 0.7f * cp);

            if (monoWet)
            {
                width = 0.0f;
                mid = 0.5f * (wl[i] + wr[i]) * (1.0f - cp * 0.5f);
            }

            side *= width;

            wl[i] = sanitise (mid + side);
            wr[i] = sanitise (mid - side);
        }
    }

    // ---- mix -------------------------------------------------------------
    dryGain.setTargetValue (pv (params.dry) * 0.01f);
    wetGain.setTargetValue (pv (params.wet) * 0.01f);
    outGain.setTargetValue (dbToGain (pv (params.output)));
    bypassMix.setTargetValue (bypassed ? 1.0f : 0.0f);

    const bool audition = pb (params.audition);

    auto* outL = outBus.getWritePointer (0, startSample);
    auto* outR = outBus.getNumChannels() > 1 ? outBus.getWritePointer (1, startSample) : nullptr;

    const auto* dryL = mainBuffer.getReadPointer (0);
    const auto* dryR = mainBuffer.getReadPointer (1);
    const auto* wetL = wetBuffer.getReadPointer (0);
    const auto* wetR = wetBuffer.getReadPointer (1);

    float peakIn = 0.0f, peakOut = 0.0f, peakWet = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto byp = bypassMix.getNextValue();
        const auto d = lerp (dryGain.getNextValue(), 1.0f, byp);
        const auto w = wetGain.getNextValue() * (1.0f - byp);
        const auto o = lerp (outGain.getNextValue(), 1.0f, byp);

        float l, r;

        if (audition && ! bypassed)
        {
            // Monitor exactly what the detector is listening to.
            const auto mono = sidechainActive ? monoSide[(size_t) i] : monoMain[(size_t) i];
            l = r = mono;
        }
        else
        {
            l = dryL[i] * d + wetL[i] * w;
            r = dryR[i] * d + wetR[i] * w;
        }

        l = sanitise (l * o, 16.0f);
        r = sanitise (r * o, 16.0f);

        peakIn  = juce::jmax (peakIn,  std::abs (dryL[i]), std::abs (dryR[i]));
        peakWet = juce::jmax (peakWet, std::abs (wetL[i] * w), std::abs (wetR[i] * w));
        peakOut = juce::jmax (peakOut, std::abs (l), std::abs (r));

        outL[i] = l;

        if (outR != nullptr)
            outR[i] = r;
    }

    // ---- band solo / audition of the spectral lanes ----------------------
    const auto solo = juce::jlimit (0, 3, pi (params.bandSolo));

    if (solo > 0 && ! bypassed)
    {
        const auto lm = pv (params.xoverLM);
        const auto mh = juce::jmax (lm * 1.5f, pv (params.xoverMH));

        if (std::abs (lm - lastSoloXoverLM) > 0.5f || std::abs (mh - lastSoloXoverMH) > 0.5f)
        {
            lastSoloXoverLM = lm;
            lastSoloXoverMH = mh;
            soloSplitter.setCrossovers (lm, mh);
        }

        for (int i = 0; i < numSamples; ++i)
        {
            float lo, mi, hi;
            soloSplitter.processStereo (0, outL[i], lo, mi, hi);
            outL[i] = solo == 1 ? lo : (solo == 2 ? mi : hi);

            if (outR != nullptr)
            {
                soloSplitter.processStereo (1, outR[i], lo, mi, hi);
                outR[i] = solo == 1 ? lo : (solo == 2 ? mi : hi);
            }
        }
    }

    // ---- metering --------------------------------------------------------
    {
        constexpr float decay = 0.82f;
        meterIn  = juce::jmax (peakIn,  meterIn  * decay);
        meterOut = juce::jmax (peakOut, meterOut * decay);
        meterWet = juce::jmax (peakWet, meterWet * decay);

        meters.inputLevel.store (meterIn);
        meters.outputLevel.store (meterOut);
        meters.wetLevel.store (meterWet);
        meters.confidence.store (analysis.getCurrentConfidence());

        const auto& frame = analysis.frameAt (juce::jmax ((std::int64_t) 0, outputStart));
        meters.bandConf0.store (frame.bandConf[0]);
        meters.bandConf1.store (frame.bandConf[1]);
        meters.bandConf2.store (frame.bandConf[2]);
        meters.activeVoices.store (engine.getActiveVoiceCount());
        meters.currentAlgo.store (engine.getCurrentAlgo());
    }

    samplesProcessed += numSamples;
}

//==============================================================================
int GapFillAudioProcessor::getNumPrograms()
{
    return (int) getFactoryPresets().size();
}

void GapFillAudioProcessor::setCurrentProgram (int index)
{
    const auto& presets = getFactoryPresets();

    if (index < 0 || index >= (int) presets.size())
        return;

    programChangeInProgress.store (true, std::memory_order_relaxed);
    applyPreset (apvts, index);
    currentProgram.store (index, std::memory_order_relaxed);
    programChangeInProgress.store (false, std::memory_order_relaxed);
}

const juce::String GapFillAudioProcessor::getProgramName (int index)
{
    const auto& presets = getFactoryPresets();

    if (index < 0 || index >= (int) presets.size())
        return {};

    return presets[(size_t) index].name;
}

juce::String GapFillAudioProcessor::getCurrentPresetName() const
{
    const auto& presets = getFactoryPresets();

    const auto program = currentProgram.load (std::memory_order_relaxed);

    if (program < 0 || program >= (int) presets.size())
        return "Custom";

    return presets[(size_t) program].name;
}

void GapFillAudioProcessor::selectPreset (int index)
{
    setCurrentProgram (index);
    updateHostDisplay();
}

void GapFillAudioProcessor::resetAllParameters()
{
    programChangeInProgress.store (true, std::memory_order_relaxed);
    resetToDefaults (apvts);
    currentProgram.store (0, std::memory_order_relaxed);
    programChangeInProgress.store (false, std::memory_order_relaxed);
    updateHostDisplay();
}

void GapFillAudioProcessor::randomise (bool everything)
{
    randomiseParameters (apvts, everything, randomSource);
    updateHostDisplay();
}

//==============================================================================
void GapFillAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree root ("GAPFILL_STATE");
    root.setProperty ("version", 1, nullptr);
    root.setProperty ("program", currentProgram.load (std::memory_order_relaxed), nullptr);
    root.setProperty ("editorWidth", savedEditorWidth.load (std::memory_order_relaxed), nullptr);
    root.setProperty ("editorHeight", savedEditorHeight.load (std::memory_order_relaxed), nullptr);
    root.setProperty ("tooltips", tooltipsEnabled.load (std::memory_order_relaxed), nullptr);
    root.appendChild (apvts.copyState(), nullptr);
    root.appendChild (midiLearn.toValueTree(), nullptr);

    if (auto xml = root.createXml())
        copyXmlToBinary (*xml, destData);
}

bool GapFillAudioProcessor::isParameterStateValid (const juce::ValueTree& state) const
{
    if (! state.isValid() || ! state.hasType (apvts.state.getType()))
        return false;

    juce::StringArray seenKnownParameters;

    for (int i = 0; i < state.getNumChildren(); ++i)
    {
        const auto child = state.getChild (i);
        const auto id = child.getProperty ("id").toString();

        if (id.isEmpty() || apvts.getParameter (id) == nullptr)
            continue; // forward-compatible unknown metadata/parameters

        if (seenKnownParameters.contains (id))
            return false;

        seenKnownParameters.add (id);

        if (! child.hasProperty ("value"))
            return false;

        const auto value = child.getProperty ("value");

        if (! (value.isDouble() || value.isInt() || value.isInt64() || value.isBool()))
            return false;

        const auto normalised = (double) value;

        if (! std::isfinite (normalised) || normalised < 0.0 || normalised > 1.0)
            return false;
    }

    return true;
}

bool GapFillAudioProcessor::applyParameterState (const juce::ValueTree& state)
{
    if (! isParameterStateValid (state))
        return false;

    programChangeInProgress.store (true, std::memory_order_relaxed);
    apvts.replaceState (state.createCopy());
    currentProgram.store (-1, std::memory_order_relaxed);
    programChangeInProgress.store (false, std::memory_order_relaxed);
    updateHostDisplay();
    return true;
}

//==============================================================================
void GapFillAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr)
        return;

    auto root = juce::ValueTree::fromXml (*xml);

    if (! root.isValid())
        return;

    juce::ValueTree paramState;
    juce::ValueTree mapState;
    int restoredProgram = -1;
    int restoredEditorWidth = savedEditorWidth.load (std::memory_order_relaxed);
    int restoredEditorHeight = savedEditorHeight.load (std::memory_order_relaxed);
    bool restoredTooltips = tooltipsEnabled.load (std::memory_order_relaxed);
    bool wrappedState = false;

    if (root.hasType ("GAPFILL_STATE"))
    {
        wrappedState = true;

        const auto savedProgram = (int) root.getProperty ("program", 0);
        const auto presetCount = (int) getFactoryPresets().size();
        restoredProgram = (savedProgram >= 0 && savedProgram < presetCount) ? savedProgram : -1;
        restoredEditorWidth = (int) root.getProperty ("editorWidth", 1100);
        restoredEditorHeight = (int) root.getProperty ("editorHeight", 700);
        restoredTooltips = (bool) root.getProperty ("tooltips", true);

        if (root.getNumChildren() > 0)
            paramState = root.getChild (0);

        mapState = root.getChildWithName (MidiLearnManager::stateType);
    }
    else if (root.hasType (apvts.state.getType()))
    {
        // Tolerate a bare APVTS tree (older or externally written state).
        paramState = root;
    }

    // Validate every sound-affecting value before mutating any part of the
    // current instance. This keeps a malformed preset/project load atomic:
    // parameters, UI metadata and MIDI mappings all remain untouched on error.
    if (! isParameterStateValid (paramState))
        return;

    programChangeInProgress.store (true, std::memory_order_relaxed);
    apvts.replaceState (paramState.createCopy());
    currentProgram.store (restoredProgram, std::memory_order_relaxed);
    programChangeInProgress.store (false, std::memory_order_relaxed);

    if (wrappedState)
    {
        savedEditorWidth.store (juce::jlimit (860, 3200, restoredEditorWidth),
                                std::memory_order_relaxed);
        savedEditorHeight.store (juce::jlimit (560, 2200, restoredEditorHeight),
                                 std::memory_order_relaxed);
        tooltipsEnabled.store (restoredTooltips, std::memory_order_relaxed);

        // Absent in state written before MIDI learn existed; leave the current
        // mappings alone in that case rather than wiping them.
        if (mapState.isValid())
            midiLearn.restoreFromValueTree (mapState);
    }
}

//==============================================================================
juce::AudioProcessorEditor* GapFillAudioProcessor::createEditor()
{
    return new GapFillAudioProcessorEditor (*this);
}

//==============================================================================
MidiLearnManager* findMidiLearnManager (juce::AudioProcessorValueTreeState& state)
{
    // The test harness drives a bare APVTS, so a failed cast is expected there
    // and simply means the MIDI items are left out of the menu.
    if (auto* processor = dynamic_cast<GapFillAudioProcessor*> (&state.processor))
        return &processor->getMidiLearnManager();

    return nullptr;
}

} // namespace gapfill

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new gapfill::GapFillAudioProcessor();
}
