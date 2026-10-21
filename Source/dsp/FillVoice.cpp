#include "FillVoice.h"
#include "../features/sync/Sync.h"

namespace gapfill
{

namespace
{
    /** Musical playback ratios used by the pitch-based algorithms. */
    constexpr float kRatios[] = { 0.25f, 0.5f, 0.594604f, 0.6674f, 0.75f, 0.8409f,
                                  1.0f, 1.0f, 1.0f, 1.1892f, 1.25f, 1.3348f, 1.5f, 2.0f };
    constexpr int kNumRatios = (int) (sizeof (kRatios) / sizeof (kRatios[0]));

    inline float softClip (float x) noexcept
    {
        return std::tanh (juce::jlimit (-8.0f, 8.0f, x));
    }

    inline float foldback (float x, float threshold) noexcept
    {
        if (threshold <= 0.0f)
            return 0.0f;

        while (std::abs (x) > threshold)
            x = std::abs (std::abs (std::fmod (x - threshold, threshold * 4.0f)) - threshold * 2.0f) - threshold;

        return x;
    }

    inline float quantiseBits (float x, int bits) noexcept
    {
        const auto levels = (float) ((1 << juce::jlimit (1, 16, bits)) - 1);
        return std::round (juce::jlimit (-1.0f, 1.0f, x) * levels) / levels;
    }
}

//==============================================================================
void FillVoice::prepare (double sampleRate, int numChannels)
{
    sr = juce::jmax (8000.0, sampleRate);
    channels = juce::jlimit (1, 2, numChannels);

    maxCaptureSamples = (int) std::ceil (sr * 0.32);
    capture.setSize (2, maxCaptureSamples + 8, false, true, false);

    combSize = (int) std::ceil (sr * 0.06) + 8;
    combBuffer.setSize (2, combSize, false, true, false);

    bandFilter.prepare (sr, 2);
    fxChain.prepare (sr, fx::kMaxVoiceBlock, channels);

    reset();
}

void FillVoice::reset()
{
    active = false;
    capture.clear();
    combBuffer.clear();
    elapsed = 0;
    totalLength = 0;
    killing = false;
    killGain = 1.0f;
    duck = duckTarget = 1.0f;
    filterL.reset();
    filterR.reset();
    modFilterL.reset();
    modFilterR.reset();

    for (auto& f : texL) f.reset();
    for (auto& f : texR) f.reset();

    for (auto& g : grains)
        g.active = false;

    combWrite = 0;
    combStateL = combStateR = 0.0f;
    bandFilter.reset();
    bandFilterActive = false;
    fxChain.reset();
    swellMode.reset();
    cloudMode.reset();

    if (spectralProcessor != nullptr)
    {
        spectralProcessor->release();
        spectralProcessor = nullptr;
    }
}

void FillVoice::requestStop (int fadeSamples) noexcept
{
    if (! active || killing)
        return;

    killing = true;
    const auto n = juce::jmax (8, fadeSamples);
    killStep = killGain / (float) n;
}

//==============================================================================
float FillVoice::readCapture (int channel, double index) const noexcept
{
    if (captureLength < 2)
        return 0.0f;

    const auto p = juce::jlimit (0.0, (double) (captureLength - 1), index);
    const auto i0 = (int) p;
    const auto i1 = juce::jmin (captureLength - 1, i0 + 1);
    const auto frac = (float) (p - (double) i0);

    const auto ch = juce::jlimit (0, capture.getNumChannels() - 1,
                                  channels > 1 ? channel : 0);
    const auto* data = capture.getReadPointer (ch);

    return data[i0] + (data[i1] - data[i0]) * frac;
}

void FillVoice::readFreezeLoop (double position, float& l, float& r) const noexcept
{
    const auto fadeRegion = (double) (freezeLen - freezeXfade);

    float gA = 1.0f, gB = 0.0f;
    double altP = position;

    if (position > fadeRegion && freezeXfade > 0)
    {
        const auto t = (float) ((position - fadeRegion) / (double) freezeXfade);
        equalPower (t, gA, gB);
        altP = position - (double) freezeLen;
    }

    l = readCapture (0, (double) freezeStart + position) * gA;
    r = readCapture (1, (double) freezeStart + position) * gA;

    if (gB > 0.0f)
    {
        l += readCapture (0, (double) freezeStart + altP) * gB;
        r += readCapture (1, (double) freezeStart + altP) * gB;
    }
}

float FillVoice::envelopeAt (int t) const noexcept
{
    if (totalLength <= 0)
        return 0.0f;

    float e = 1.0f;

    if (fadeIn > 0 && t < fadeIn)
        e = hann (0.5f * (float) t / (float) fadeIn);

    const auto tailStart = totalLength - fadeOut;

    if (fadeOut > 0 && t >= tailStart)
        e *= hann (0.5f + 0.5f * (float) (t - tailStart) / (float) fadeOut);

    return juce::jlimit (0.0f, 1.0f, e);
}

//==============================================================================
void FillVoice::captureSource (const SourceHistory& history, int lengthNeeded)
{
    captureLength = juce::jlimit (64, maxCaptureSamples, lengthNeeded);
    const auto start = setupData.sourceAnchor - (std::int64_t) captureLength;
    history.copyTo (capture, start, captureLength);

    // Fade the very first/last millisecond of the captured slice so that a slice
    // boundary can never click, whatever the algorithm does with it.
    const auto edge = juce::jmin (captureLength / 8, (int) (sr * 0.002));

    if (edge > 1)
    {
        for (int ch = 0; ch < capture.getNumChannels(); ++ch)
        {
            auto* d = capture.getWritePointer (ch);

            for (int i = 0; i < edge; ++i)
            {
                const auto g = (float) i / (float) edge;
                d[i] *= g;
                d[captureLength - 1 - i] *= g;
            }
        }
    }
}

//==============================================================================
bool FillVoice::start (const VoiceSetup& setup, const SourceHistory& history,
                       SpectralSmear* spectral)
{
    setupData = setup;
    rng.seed (setup.rngSeed);
    noiseRng.seed (hashCombine (setup.rngSeed, 0x51ED270Bu));

    totalLength = juce::jmax (32, setup.lengthSamples);
    fadeIn  = juce::jlimit (4, totalLength / 3, setup.fadeInSamples);
    fadeOut = juce::jlimit (4, totalLength / 2, setup.fadeOutSamples);

    elapsed = 0;
    envelopeStage.start (setup.envShape, setup.envAmount01, sr, setup.rngSeed);   // C-4
    killing = false;
    killGain = 1.0f;
    killStep = 0.0f;
    duck = duckTarget = 1.0f;
    duckCoeff = onePoleCoeff (sr, 3.0f);

    spectralProcessor = nullptr;

    if (setup.algo == Algo::Spectral)
    {
        if (spectral == nullptr || spectral->isClaimed())
            return false;                     // scheduler will choose another algorithm

        spectralProcessor = spectral;
        spectralProcessor->claim();
        spectralProcessor->reset();
    }

    // --- capture the material this algorithm needs ------------------------
    int needed = juce::jmin (totalLength, maxCaptureSamples);

    switch (setup.algo)
    {
        case Algo::Stutter:  needed = (int) (setup.stutterSeconds * sr * 1.5); break;
        case Algo::Delay:    needed = (int) (juce::jmin (setup.delaySeconds, 0.25f) * sr); break;
        case Algo::Freeze:   needed = (int) (sr * 0.28); break;
        // The resonator is excited by the hit that opened the gap, so its slice
        // has to reach back far enough to still contain that hit.
        case Algo::Comb:     needed = setup.sinceEventSamples + (int) (sr * 0.05); break;
        // Tape Stop plays forward from the hit; it travels at most one
        // fill-length of material while decelerating.
        case Algo::TapeStop: needed = juce::jmax (setup.sinceEventSamples, totalLength) + (int) (sr * 0.01); break;
        // Reverse Swell reads back to just before the hit that opened the gap (N-FX1).
        case Algo::Reverse:  if (setup.modes.swell) needed = setup.sinceEventSamples + (int) (sr * 0.015); break;
        case Algo::Granular: needed = 256; break;            // grains read the history directly
        case Algo::Texture:  needed = 256; break;            // synthesised, no source slice
        default: break;
    }

    captureSource (history, needed);

    filterL.reset();
    filterR.reset();
    modFilterL.reset();
    modFilterR.reset();
    combBuffer.clear();
    combWrite = 0;
    combStateL = combStateR = 0.0f;
    crushCounter = modCrushCounter = 0;
    crushHoldL = crushHoldR = modCrushHoldL = modCrushHoldR = 0.0f;

    for (auto& g : grains)
        g.active = false;

    for (auto& f : texL) f.reset();
    for (auto& f : texR) f.reset();

    configureAlgorithm();

    // --- C-3 controlled mistake (inactive unless the recipe carries one that fits this algorithm) ---
    {
        mistakes::StageConfig mc;
        mc.type = setup.mistakeType;
        mc.amount01 = setup.mistakeAmt01;
        mc.sampleRate = sr;
        mc.lengthSamples = totalLength;
        mc.algo = (int) setup.algo;
        mc.seed = setup.mistakeSeed;
        mc.mono = channels < 2;

        switch (setup.algo)
        {
            case Algo::Stutter:
                mc.repeatLen = stutterSliceLen;
                mc.repeats = stutterSliceLen > 0 ? totalLength / stutterSliceLen : 0;
                break;
            case Algo::Delay:
            {
                const auto spacing = juce::jmax (tapSliceLen, (int) (setup.delaySeconds * sr));
                mc.repeatLen = spacing;
                mc.repeats = juce::jmin (numTaps, spacing > 0 ? (totalLength + spacing - 1) / spacing : 0);
                break;
            }
            case Algo::Scramble:
                mc.repeatLen = sliceLen;
                mc.repeats = numSlices;
                break;
            default: break;
        }

        mistakeStage.start (mc);
    }

    // --- complexity decides how many extra transformations stack on top ----
    {
        const auto c = juce::jlimit (0.0f, 1.0f, setup.complexity01);
        int n = c < 0.25f ? 0 : (c < 0.5f ? 1 : (c < 0.78f ? 2 : 3));

        if (setup.chaos01 > 0.0f && rng.chance (setup.chaos01 * 0.5f))
            n = juce::jlimit (0, maxModifiers, n + (rng.chance (0.5f) ? 1 : -1));

        numModifiers = juce::jlimit (0, maxModifiers, n);

        Modifier pool[] = { Modifier::FilterSweep, Modifier::Crush, Modifier::Distort,
                            Modifier::Tremolo, Modifier::PanMotion };
        constexpr int poolSize = (int) (sizeof (pool) / sizeof (pool[0]));

        // Do not stack a modifier that merely repeats the base algorithm.
        bool blocked[poolSize] = { false, false, false, false, false };

        if (setup.algo == Algo::Filter)  blocked[0] = true;
        if (setup.algo == Algo::Crush)   blocked[1] = true;
        if (setup.algo == Algo::Distort) blocked[2] = true;

        int chosen = 0;

        for (int attempt = 0; attempt < 12 && chosen < numModifiers; ++attempt)
        {
            const auto idx = rng.nextInt (poolSize);

            if (blocked[idx])
                continue;

            blocked[idx] = true;
            modifiers[chosen++] = pool[idx];
        }

        numModifiers = chosen;

        for (int i = chosen; i < maxModifiers; ++i)
            modifiers[i] = Modifier::None;

        // A recipe may force the stack (after the draws above, so the RNG
        // sequence is identical whether or not an override is present).
        if (setup.modifierOverride != 0xFF)
        {
            numModifiers = 0;

            for (int i = 0; i < maxModifiers; ++i)
                modifiers[i] = Modifier::None;

            for (int bit = 0; bit < poolSize && numModifiers < maxModifiers; ++bit)
                if ((setup.modifierOverride & (1 << bit)) != 0)
                    modifiers[numModifiers++] = pool[bit];
        }

        const auto intensity = juce::jlimit (0.0f, 1.0f, setup.intensity01);

        modFilterMode  = rng.nextInt (3);
        modFilterStart = rng.range (200.0f, 1200.0f);
        modFilterEnd   = modFilterStart * rng.range (0.12f, 8.0f);
        modFilterQ     = lerp (0.7f, 4.5f, intensity);
        modCrushBits   = (int) std::round (lerp (10.0f, 3.0f, intensity));
        modCrushDecimate = juce::jmax (1, (int) std::round (lerp (1.0f, 14.0f, intensity * intensity)));
        modDrive       = lerp (1.2f, 9.0f, intensity);
        modDistortNorm = 1.0f / juce::jmax (0.2f, std::tanh (modDrive));
        modTremRate    = rng.range (4.0f, 26.0f) * lerp (0.5f, 1.6f, intensity);
        modTremDepth   = lerp (0.25f, 0.95f, intensity);
        modTremPhase   = 0.0f;
        modPanRate     = rng.range (0.6f, 5.0f);
        modPanDepth    = lerp (0.2f, 0.95f, intensity);
        modPanPhase    = rng.nextFloat();

        // E-2 Sync Mods: replace the random rates (after the draws, so the RNG
        // sequence is the same with the option on or off).
        if (setup.modSyncSeconds > 0.0f)
        {
            modTremRate = sync::modSyncTremoloHz (setup.modSyncSeconds);
            modPanRate  = sync::modSyncPanHz (setup.delaySeconds);
        }
    }

    // --- spectral-lane isolation -------------------------------------------
    bandFilterActive = (setup.band >= 0);

    if (bandFilterActive)
    {
        bandFilter.reset();
        bandFilter.setCrossovers (setup.xoverLM, setup.xoverMH);
    }

    // --- prime the spectral processor so the fill starts immediately -------
    if (spectralProcessor != nullptr)
    {
        for (int i = 0; i < SpectralSmear::latency(); ++i)
        {
            const auto p = std::fmod ((double) i, (double) juce::jmax (1, captureLength - 1));
            float dummyL = 0.0f, dummyR = 0.0f;
            spectralProcessor->process (readCapture (0, p), readCapture (1, p), dummyL, dummyR);
        }

        playPos = (double) SpectralSmear::latency();
    }

    // HOOK (WP8, FX-0): latch the effect cards for this fill.
    { auto fxs = setup.fxStart; fxs.lengthSamples = totalLength; fxChain.start (fxs); }

    active = true;
    return true;
}

//==============================================================================
void FillVoice::configureAlgorithm()
{
    const auto intensity  = juce::jlimit (0.0f, 1.0f, setupData.intensity01);
    const auto complexity = juce::jlimit (0.0f, 1.0f, setupData.complexity01);
    const auto chaos      = juce::jlimit (0.0f, 1.0f, setupData.chaos01);

    playPos = 0.0;
    playRate = 1.0;

    // Where the hit that opened this gap sits inside the captured slice. A few
    // milliseconds of pre-roll keeps the attack intact.
    sliceOffset = juce::jlimit (0, juce::jmax (0, captureLength - 64),
                                captureLength - setupData.sinceEventSamples - (int) (sr * 0.004));

    // Generic loop region used by the "play the slice forward" algorithms.
    freezeXfade = juce::jlimit (16, juce::jmax (17, captureLength / 4), (int) (sr * 0.008));
    freezeStart = freezeXfade;
    freezeLen   = juce::jmax (32, captureLength - freezeXfade);

    switch (setupData.algo)
    {
        case Algo::Reverse:
        {
            const auto semis = chaos > 0.35f ? (float) (rng.nextInt (3) - 1) * 12.0f * intensity : 0.0f;
            playRate = std::pow (2.0, (double) semis / 12.0);
            playRate = juce::jlimit (0.25, 4.0, playRate);
            reversePos = (double) (captureLength - 1);

            const auto natural = (int) ((double) captureLength / playRate);
            totalLength = juce::jlimit (32, totalLength, juce::jmax (32, natural));
            fadeIn  = juce::jlimit (4, totalLength / 3, fadeIn);
            fadeOut = juce::jlimit (4, totalLength / 2, fadeOut);

            // HOOK (WP8, N-FX1): Reverse Swell takes over length, fades and read position.
            swellMode.configure (setupData.modes.swell, setupData.fxStart.settings.swell,
                                 setupData.modes.swellLanding, setupData.modes.swellFade,
                                 captureLength, sr, totalLength, fadeIn, fadeOut);
            break;
        }

        case Algo::Stutter:
        {
            stutterSliceLen = juce::jlimit ((int) (sr * 0.01), (int) (sr * 0.3),
                                            (int) (setupData.stutterSeconds * sr));
            stutterCurrentLen = stutterSliceLen;
            stutterIndex = 0;
            stutterPos = 0;
            stutterRead = (double) sliceOffset;
            stutterGain = 1.0f;

            stutterRatio = 1.0f;

            if (complexity > 0.5f && rng.chance (0.6f))
                stutterRatio = rng.chance (0.5f) ? lerp (1.0f, 0.72f, complexity)
                                                 : lerp (1.0f, 1.35f, complexity);

            stutterRate = 1.0f;
            stutterRateStep = 1.0f;

            if (intensity > 0.45f && rng.chance (0.5f + chaos * 0.4f))
                stutterRateStep = std::pow (2.0f, (rng.chance (0.5f) ? 1.0f : -1.0f)
                                                    * lerp (0.0f, 3.0f, intensity) / 12.0f);

            stutterGainStep = rng.chance (0.35f) ? lerp (1.0f, 0.82f, intensity) : 1.0f;
            break;
        }

        case Algo::Delay:
        {
            numTaps = juce::jlimit (2, maxTaps, 2 + (int) std::round (complexity * 6.0f));
            tapSliceLen = juce::jmin (captureLength - sliceOffset,
                                      (int) (juce::jmin (setupData.delaySeconds, 0.25f) * sr));
            tapSliceLen = juce::jlimit (32, juce::jmax (32, captureLength), tapSliceLen);

            const auto spacing = juce::jmax (tapSliceLen, (int) (setupData.delaySeconds * sr));
            const auto feedback = lerp (0.42f, 0.86f, intensity);

            for (int i = 0; i < numTaps; ++i)
            {
                tapOffset[i] = i * spacing;
                tapGain[i] = std::pow (feedback, (float) i);

                if (setupData.pingPong && channels > 1)
                {
                    const auto leftSide = (i % 2) == 0;
                    tapPanL[i] = leftSide ? 1.0f : lerp (1.0f, 0.15f, intensity);
                    tapPanR[i] = leftSide ? lerp (1.0f, 0.15f, intensity) : 1.0f;
                }
                else
                {
                    tapPanL[i] = tapPanR[i] = 1.0f;
                }
            }
            break;
        }

        case Algo::Granular:
        {
            grainDuration = juce::jlimit ((int) (sr * 0.008), (int) (sr * 0.25),
                                          (int) (sr * lerp (0.09f, 0.022f, intensity)));
            const auto density = lerp (1.1f, 4.0f, complexity);
            grainInterval = juce::jmax (32, (int) ((float) grainDuration / density));
            grainCountdown = 0;
            grainSpread = lerp (0.15f, 1.0f, complexity * 0.5f + chaos * 0.5f);
            grainPitchSpread = lerp (0.0f, 1.0f, intensity * 0.6f + chaos * 0.4f);
            grainReverseProb = chaos * 0.45f;
            grainWindowSeconds = (double) lerp (0.15f, 1.1f, complexity);

            // HOOK (WP8, N-FX2): Granular Cloud replaces the derived grain parameters.
            cloudMode.configure (setupData.modes.cloud, setupData.fxStart.settings.cloud, chaos,
                                 setupData.sourceAnchor, setupData.rngSeed, sr);
            break;
        }

        case Algo::Freeze:
        {
            freezeLen = juce::jlimit ((int) (sr * 0.035), juce::jmax (64, captureLength / 2),
                                      (int) (sr * lerp (0.18f, 0.045f, intensity)));
            freezeXfade = juce::jlimit (16, freezeLen / 2, (int) (sr * 0.012));
            freezeStart = juce::jlimit (freezeXfade, juce::jmax (freezeXfade, captureLength - freezeLen - 1),
                                        captureLength - freezeLen - 1);
            playPos = 0.0;
            freezeDrift = 1.0 + (double) (chaos * rng.nextBipolar() * 0.02f);
            playRate = freezeDrift;
            break;
        }

        case Algo::PitchRise:
        case Algo::PitchFall:
        {
            const auto amount = lerp (5.0f, 26.0f, intensity) * (1.0f + chaos * rng.nextFloat() * 0.5f);
            const auto up = (setupData.algo == Algo::PitchRise);
            pitchRate = 1.0;
            pitchRateEnd = std::pow (2.0, (double) (up ? amount : -amount) / 12.0);
            pitchRateEnd = juce::jlimit (0.06, 16.0, pitchRateEnd);

            if (setupData.pitchOverride != 127)
                pitchRateEnd = juce::jlimit (0.06, 16.0, std::pow (2.0, (double) setupData.pitchOverride / 12.0));
            playPos = 0.0;
            pitchXfadePos = 0.0f;
            break;
        }

        case Algo::Filter:
        {
            filterMode = rng.nextInt (3);
            filterQ = lerp (0.7f, 6.0f, intensity);
            const auto lo = rng.range (90.0f, 500.0f);
            const auto hi = juce::jmin ((float) (sr * 0.45), lo * lerp (4.0f, 40.0f, intensity));
            const auto rising = rng.chance (0.5f);
            filterStart = rising ? lo : hi;
            filterEnd   = rising ? hi : lo;
            filterL.reset();
            filterR.reset();
            break;
        }

        case Algo::Texture:
        {
            const auto nyq = (float) (sr * 0.45);
            const float centres[3] =
            {
                juce::jlimit (25.0f, nyq, setupData.xoverLM * rng.range (0.35f, 0.85f)),
                juce::jlimit (60.0f, nyq, std::sqrt (setupData.xoverLM * setupData.xoverMH) * rng.range (0.7f, 1.5f)),
                juce::jlimit (200.0f, nyq, setupData.xoverMH * rng.range (1.1f, 2.6f))
            };

            const auto q = lerp (1.2f, 7.0f, intensity);

            for (int b = 0; b < 3; ++b)
            {
                texL[b].set (sr, centres[b], q);
                texR[b].set (sr, centres[b] * rng.range (0.97f, 1.03f), q);
                texGain[b] = 0.12f + juce::jlimit (0.0f, 1.0f, setupData.bandEnergy[b]);
            }

            // Normalise so the texture never gets louder as more bands are busy.
            const auto sum = texGain[0] + texGain[1] + texGain[2] + 1.0e-6f;

            for (auto& g : texGain)
                g /= sum;

            texAttack = lerp (0.03f, 0.002f, intensity);
            texDecay  = lerp (0.45f, 0.08f, intensity);
            break;
        }

        case Algo::Crush:
        {
            crushBits = (int) std::round (lerp (11.0f, 2.0f, intensity));
            crushDecimate = juce::jmax (1, (int) std::round (lerp (1.0f, 24.0f, intensity * intensity)));
            crushCounter = 0;
            playRate = 1.0;
            break;
        }

        case Algo::Distort:
        {
            driveAmount = lerp (1.5f, 18.0f, intensity);
            distortNorm = 1.0f / juce::jmax (0.2f, std::tanh (driveAmount));
            playRate = 1.0;
            break;
        }

        case Algo::Scramble:
        {
            {
                const auto usable = juce::jmax (128, captureLength - sliceOffset);
                numSlices = juce::jlimit (4, maxSlices, 4 + (int) std::round (complexity * 20.0f));
                sliceLen = juce::jmax (48, usable / numSlices);

                // E-2 Sync Mods: slices land on the stutter grid (at most half the material).
                if (setupData.modSyncSeconds > 0.0f)
                    sliceLen = juce::jlimit (48, juce::jmax (48, usable / 2),
                                             (int) std::lround (setupData.modSyncSeconds * sr));

                numSlices = juce::jlimit (2, maxSlices, juce::jmax (2, usable / sliceLen));
            }

            for (int i = 0; i < numSlices; ++i)
                sliceOrder[i] = i;

            // Musical scrambling: keep the downbeat, then swap and repeat pairs.
            const auto swaps = 1 + (int) std::round (chaos * (float) numSlices * 0.6f);

            for (int i = 0; i < swaps; ++i)
            {
                const auto a = 1 + rng.nextInt (juce::jmax (1, numSlices - 1));
                const auto b = 1 + rng.nextInt (juce::jmax (1, numSlices - 1));
                std::swap (sliceOrder[a], sliceOrder[b]);
            }

            const auto repeats = (int) std::round (complexity * 3.0f);

            for (int i = 0; i < repeats; ++i)
            {
                const auto a = rng.nextInt (numSlices);
                const auto b = juce::jmin (numSlices - 1, a + 1);
                sliceOrder[b] = sliceOrder[a];
            }

            sliceIndex = 0;
            slicePos = 0;
            break;
        }

        case Algo::Spectral:
        {
            if (spectralProcessor != nullptr)
                spectralProcessor->setCharacter (lerp (0.35f, 1.0f, complexity),
                                                 chaos * 0.6f + intensity * 0.15f,
                                                 setupData.rngSeed);
            playPos = 0.0;
            break;
        }

        case Algo::Comb:
        {
            // Tune the resonator to a musical pitch rather than an arbitrary one.
            static constexpr float semitones[] = { 0.0f, 3.0f, 5.0f, 7.0f, 10.0f, 12.0f, 15.0f, 19.0f };
            const auto note = semitones[rng.nextInt ((int) (sizeof (semitones) / sizeof (semitones[0])))];
            const auto freq = 110.0f * std::pow (2.0f, (note + 12.0f * (float) rng.nextInt (3)) / 12.0f);

            combDelay = juce::jlimit (8, combSize - 4, (int) (sr / juce::jmax (30.0f, freq)));

            const auto decaySeconds = juce::jmax (0.05f, (float) totalLength / (float) sr * lerp (0.5f, 1.1f, intensity));
            const auto repeats = decaySeconds * (float) sr / (float) combDelay;
            combFeedback = juce::jlimit (0.0f, 0.985f, std::pow (0.001f, 1.0f / juce::jmax (1.0f, repeats)));
            combDamp = lerp (0.5f, 0.06f, intensity);
            combExcite = juce::jlimit (16, captureLength, (int) (sr * lerp (0.006f, 0.03f, 1.0f - intensity)));
            break;
        }

        case Algo::TapeStop:
        {
            // Higher intensity brakes harder: the platter loses speed early.
            tapeCurve = lerp (0.7f, 2.6f, intensity) * (1.0f + chaos * rng.nextBipolar() * 0.2f);
            tapeWow = chaos * 0.012f;
            tapeWowPhase = rng.nextFloat();

            // Average speed over the fill is 1 / (curve + 1); start where that
            // much material still lies ahead, preferring the opening hit.
            const auto travel = (double) totalLength / (1.0 + (double) tapeCurve);
            tapePos = juce::jlimit (0.0, juce::jmax (0.0, (double) captureLength - 2.0),
                                    juce::jmin ((double) sliceOffset, (double) captureLength - travel));
            tapeLpL = tapeLpR = 0.0f;
            break;
        }

        case Algo::NumAlgos:
        default:
            break;
    }
}

//==============================================================================
void FillVoice::render (juce::AudioBuffer<float>& wet, int startSample, int numSamples,
                        const SourceHistory& history) noexcept
{
    if (! active || numSamples <= 0)
        return;

    const auto wetChannels = wet.getNumChannels();

    if (wetChannels <= 0)
        return;

    // HOOK (WP8, FX-0): fills with an active effect card take the chained path.
    if (fxChain.isActive())
    {
        renderWithFx (wet, startSample, numSamples, history);
        return;
    }

    auto* outL = wet.getWritePointer (0, startSample);
    auto* outR = wetChannels > 1 ? wet.getWritePointer (1, startSample) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        if (! active)
            break;

        float l = 0.0f, r = 0.0f;
        generateSample (l, r, history);

        outL[i] += l;

        if (outR != nullptr)
            outR[i] += r;
    }
}

void FillVoice::generateSample (float& left, float& right, const SourceHistory& history) noexcept
{
    float l = 0.0f, r = 0.0f;

    // C-3: source read-rate multiplier of an active mistake (octave / brake); exactly 1 otherwise.
    mistakeRate = mistakeStage.isActive() ? mistakeStage.rateMultiplier ((float) elapsed / (float) totalLength) : 1.0f;

    switch (setupData.algo)
    {
        case Algo::Reverse:   genReverse (l, r); break;
        case Algo::Stutter:   genStutter (l, r); break;
        case Algo::Delay:     genDelay (l, r); break;
        case Algo::Granular:  genGranular (l, r, history); break;
        case Algo::Freeze:    genFreeze (l, r); break;
        case Algo::PitchRise:
        case Algo::PitchFall: genPitch (l, r); break;
        case Algo::Filter:    genFilter (l, r); break;
        case Algo::Texture:   genTexture (l, r); break;
        case Algo::Crush:     genCrush (l, r); break;
        case Algo::Distort:   genDistort (l, r); break;
        case Algo::Scramble:  genScramble (l, r); break;
        case Algo::Spectral:  genSpectral (l, r); break;
        case Algo::Comb:      genComb (l, r); break;
        case Algo::TapeStop:  genTapeStop (l, r); break;
        case Algo::NumAlgos:
        default: break;
    }

    applyModifiers (l, r);

    if (bandFilterActive)
    {
        float lo = 0.0f, mi = 0.0f, hi = 0.0f;
        bandFilter.processStereo (0, l, lo, mi, hi);
        l = setupData.band == 0 ? lo : (setupData.band == 1 ? mi : hi);

        bandFilter.processStereo (1, r, lo, mi, hi);
        r = setupData.band == 0 ? lo : (setupData.band == 1 ? mi : hi);
    }

    // C-3: output-side part of a mistake (wobble, dropped repeat, brake gain, stereo swing).
    if (mistakeStage.isActive())
        mistakeStage.process (l, r);

    // C-4: shape the level / brightness over the fill's life, before fade and duck.
    if (envelopeStage.isActive())
        envelopeStage.process (l, r, (float) elapsed / (float) totalLength);

    auto gain = envelopeAt (elapsed) * setupData.amp;
    const auto preFxGain = gain;

    duck += duckCoeff * (duckTarget - duck);
    gain *= duck;
    auto postFxGain = duck;

    if (killing)
    {
        killGain -= killStep;

        if (killGain <= 0.0f)
        {
            killGain = 0.0f;
            deactivate();
        }

        gain *= killGain;
        postFxGain *= killGain;
    }

    // HOOK (WP8, FX-0): with an active effect chain the duck and the panic fade
    // are applied AFTER the cards, so repeats and tails stay protected.
    if (fxChain.isActive())
    {
        left  = sanitise (l * preFxGain);
        right = sanitise (r * preFxGain);
        fxPostGainNow = postFxGain;
    }
    else
    {
        left  = sanitise (l * gain);
        right = sanitise (r * gain);
    }

    if (++elapsed >= totalLength)
        deactivate();
}

void FillVoice::deactivate() noexcept
{
    active = false;

    if (spectralProcessor != nullptr)
    {
        spectralProcessor->release();
        spectralProcessor = nullptr;
    }
}

//==============================================================================
void FillVoice::renderWithFx (juce::AudioBuffer<float>& wet, int startSample, int numSamples,
                              const SourceHistory& history) noexcept
{
    const auto wetChannels = wet.getNumChannels();
    auto* outL = wet.getWritePointer (0, startSample);
    auto* outR = wetChannels > 1 ? wet.getWritePointer (1, startSample) : nullptr;

    int done = 0;

    while (done < numSamples && active)
    {
        const auto n = juce::jmin (numSamples - done, fx::kMaxVoiceBlock);
        int produced = 0;

        for (; produced < n && active; ++produced)
        {
            generateSample (fxScratchL[produced], fxScratchR[produced], history);
            fxPostGain[produced] = fxPostGainNow;
        }

        if (produced == 0)
            break;

        fxChain.process (fxScratchL, fxScratchR, produced, juce::jmax (0, totalLength - elapsed), totalLength);

        for (int i = 0; i < produced; ++i)
        {
            outL[done + i] += sanitise (fxScratchL[i] * fxPostGain[i]);

            if (outR != nullptr)
                outR[done + i] += sanitise (fxScratchR[i] * fxPostGain[i]);
        }

        done += produced;
    }
}

void FillVoice::genReverse (float& l, float& r) noexcept
{
    // HOOK (WP8, N-FX1): Reverse Swell render mode.
    if (swellMode.generate (l, r, elapsed, [this] (int ch, double pos) { return readCapture (ch, pos); }))
        return;

    l = readCapture (0, reversePos);
    r = readCapture (1, reversePos);

    reversePos -= playRate * (double) mistakeRate;   // C-3 (mistakeRate == 1 unless a mistake is active)

    if (reversePos < 0.0)
    {
        reversePos = 0.0;

        if (! killing)
            requestStop (juce::jmax (8, fadeOut));
    }
}

void FillVoice::genStutter (float& l, float& r) noexcept
{
    if (stutterPos >= stutterCurrentLen)
    {
        ++stutterIndex;
        stutterPos = 0;
        stutterRead = (double) sliceOffset;
        stutterCurrentLen = juce::jlimit ((int) (sr * 0.006), (int) (sr * 0.5),
                                          (int) ((float) stutterCurrentLen * stutterRatio));
        stutterRate = juce::jlimit (0.25f, 4.0f, stutterRate * stutterRateStep);
        stutterGain = juce::jlimit (0.05f, 1.0f, stutterGain * stutterGainStep);
    }

    const auto edge = juce::jmin (stutterCurrentLen / 6, (int) (sr * 0.0015) + 2);
    float w = 1.0f;

    if (edge > 0)
    {
        if (stutterPos < edge)
            w = (float) stutterPos / (float) edge;
        else if (stutterPos > stutterCurrentLen - edge)
            w = (float) (stutterCurrentLen - stutterPos) / (float) edge;
    }

    w = juce::jlimit (0.0f, 1.0f, w) * stutterGain;

    // Wrap inside the slice window so a long repeat loops its own material.
    const auto window = (double) juce::jmax (1, captureLength - 1 - sliceOffset);
    const auto readPos = (double) sliceOffset + std::fmod (stutterRead - (double) sliceOffset, window);
    l = readCapture (0, readPos) * w;
    r = readCapture (1, readPos) * w;

    stutterRead += stutterRate * mistakeRate;
    ++stutterPos;
}

void FillVoice::genDelay (float& l, float& r) noexcept
{
    const auto edge = juce::jmin (tapSliceLen / 8, (int) (sr * 0.003) + 2);

    for (int i = 0; i < numTaps; ++i)
    {
        const auto d = elapsed - tapOffset[i];

        if (d < 0 || d >= tapSliceLen)
            continue;

        float w = 1.0f;

        if (edge > 0)
        {
            if (d < edge)
                w = (float) d / (float) edge;
            else if (d > tapSliceLen - edge)
                w = (float) (tapSliceLen - d) / (float) edge;
        }

        w = juce::jlimit (0.0f, 1.0f, w) * tapGain[i];

        l += readCapture (0, (double) (sliceOffset + d)) * w * tapPanL[i];
        r += readCapture (1, (double) (sliceOffset + d)) * w * tapPanR[i];
    }
}

void FillVoice::genGranular (float& l, float& r, const SourceHistory& history) noexcept
{
    // HOOK (WP8, N-FX2): Granular Cloud render mode.
    if (cloudMode.generate (l, r, history, channels))
        return;

    if (--grainCountdown <= 0)
    {
        for (auto& g : grains)
        {
            if (g.active)
                continue;

            const auto back = (double) rng.range (0.02f, (float) grainWindowSeconds) * sr;
            g.dur = juce::jmax (32, (int) ((float) grainDuration * rng.range (1.0f - grainSpread * 0.5f,
                                                                              1.0f + grainSpread * 0.5f)));
            const auto baseRatio = kRatios[juce::jlimit (0, kNumRatios - 1,
                                                          (int) (kNumRatios * 0.5f)
                                                            + (int) std::round (rng.nextBipolar() * grainPitchSpread * 5.0f))];
            g.rate = (double) baseRatio * (1.0 + (double) (rng.nextBipolar() * grainPitchSpread * 0.03f));

            const auto reverse = rng.chance (grainReverseProb);
            g.pos = (double) setupData.sourceAnchor - back;

            if (reverse)
            {
                g.pos += (double) g.dur * g.rate;
                g.rate = -g.rate;
            }

            const auto pan = rng.nextBipolar() * grainSpread;
            g.panL = std::sqrt (juce::jlimit (0.0f, 1.0f, 0.5f - pan * 0.5f)) * 1.4142f;
            g.panR = std::sqrt (juce::jlimit (0.0f, 1.0f, 0.5f + pan * 0.5f)) * 1.4142f;
            g.amp = rng.range (0.6f, 1.0f);
            g.age = 0;
            g.active = true;
            break;
        }

        const auto jitter = 1.0f + setupData.chaos01 * rng.nextBipolar() * 0.6f;
        grainCountdown = juce::jmax (16, (int) ((float) grainInterval * juce::jmax (0.2f, jitter)));
    }

    for (auto& g : grains)
    {
        if (! g.active)
            continue;

        const auto w = hann ((float) g.age / (float) g.dur);
        const auto s0 = history.read (0, g.pos);
        const auto s1 = history.read (channels > 1 ? 1 : 0, g.pos);

        l += s0 * w * g.amp * g.panL;
        r += s1 * w * g.amp * g.panR;

        g.pos += g.rate * (double) mistakeRate;

        if (++g.age >= g.dur)
            g.active = false;
    }

    l *= 0.7f;
    r *= 0.7f;
}

void FillVoice::genFreeze (float& l, float& r) noexcept
{
    readFreezeLoop (playPos, l, r);

    playPos += playRate * (double) mistakeRate;

    if (playPos >= (double) freezeLen)
        playPos -= (double) freezeLen;
}

void FillVoice::genPitch (float& l, float& r) noexcept
{
    const auto t = totalLength > 1 ? (double) elapsed / (double) (totalLength - 1) : 0.0;
    const auto rate = pitchRate * std::pow (pitchRateEnd / pitchRate, t);

    const auto p = playPos;
    const auto fadeRegion = (double) (freezeLen - freezeXfade);

    float gA = 1.0f, gB = 0.0f;
    double altP = p;

    if (p > fadeRegion && freezeXfade > 0)
    {
        const auto x = (float) ((p - fadeRegion) / (double) freezeXfade);
        equalPower (x, gA, gB);
        altP = p - (double) freezeLen;
    }

    l = readCapture (0, (double) freezeStart + p) * gA;
    r = readCapture (1, (double) freezeStart + p) * gA;

    if (gB > 0.0f)
    {
        l += readCapture (0, (double) freezeStart + altP) * gB;
        r += readCapture (1, (double) freezeStart + altP) * gB;
    }

    playPos += juce::jlimit (0.03, 16.0, rate * (double) mistakeRate);

    while (playPos >= (double) freezeLen)
        playPos -= (double) freezeLen;
}

void FillVoice::genTapeStop (float& l, float& r) noexcept
{
    const auto t = totalLength > 1 ? (float) elapsed / (float) (totalLength - 1) : 1.0f;
    const auto speed = std::pow (juce::jmax (0.0f, 1.0f - t), tapeCurve);

    l = readCapture (0, tapePos);
    r = readCapture (1, tapePos);

    // The capstan loses high end as it slows, like a real deck running down.
    const auto cutoff = juce::jlimit (60.0f, (float) (sr * 0.45), 180.0f + 17000.0f * speed * speed);
    const auto a = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * cutoff / (float) sr);
    tapeLpL += a * (l - tapeLpL);
    tapeLpR += a * (r - tapeLpR);
    l = tapeLpL;
    r = tapeLpR;

    tapeWowPhase += 5.5f / (float) sr;
    tapeWowPhase -= std::floor (tapeWowPhase);
    const auto wow = 1.0f + tapeWow * std::sin (juce::MathConstants<float>::twoPi * tapeWowPhase);

    tapePos = juce::jmin ((double) (captureLength - 1), tapePos + (double) (speed * wow));
}

void FillVoice::genFilter (float& l, float& r) noexcept
{
    const auto t = totalLength > 1 ? (float) elapsed / (float) (totalLength - 1) : 0.0f;
    const auto cutoff = filterStart * std::pow (filterEnd / juce::jmax (1.0f, filterStart), t);

    filterL.set (sr, cutoff, filterQ);
    filterR.copyCoefficientsFrom (filterL);

    float src0 = 0.0f, src1 = 0.0f;
    readFreezeLoop (std::fmod (playPos, (double) freezeLen), src0, src1);

    float lp = 0.0f, bp = 0.0f, hp = 0.0f;
    filterL.process (src0, lp, bp, hp);
    l = filterMode == 0 ? lp : (filterMode == 1 ? hp : bp);

    filterR.process (src1, lp, bp, hp);
    r = filterMode == 0 ? lp : (filterMode == 1 ? hp : bp);

    playPos += 1.0;
}

void FillVoice::genTexture (float& l, float& r) noexcept
{
    const auto t = (float) elapsed / (float) sr;
    const auto attack = juce::jmax (0.0005f, texAttack);
    const auto shape = t < attack ? (t / attack)
                                  : std::exp (-(t - attack) / juce::jmax (0.01f, texDecay));

    const auto nL = noiseRng.nextBipolar();
    const auto nR = noiseRng.nextBipolar();

    float sumL = 0.0f, sumR = 0.0f;

    for (int b = 0; b < 3; ++b)
    {
        float lp = 0.0f, bp = 0.0f, hp = 0.0f;
        texL[b].process (nL, lp, bp, hp);
        sumL += bp * texGain[b];

        texR[b].process (nR, lp, bp, hp);
        sumR += bp * texGain[b];
    }

    l = sumL * shape * 1.6f;
    r = sumR * shape * 1.6f;
}

void FillVoice::genCrush (float& l, float& r) noexcept
{
    float s0 = 0.0f, s1 = 0.0f;
    readFreezeLoop (std::fmod (playPos, (double) freezeLen), s0, s1);

    if (--crushCounter <= 0)
    {
        crushCounter = crushDecimate;
        crushHoldL = quantiseBits (s0, crushBits);
        crushHoldR = quantiseBits (s1, crushBits);
    }

    l = crushHoldL;
    r = crushHoldR;

    playPos += playRate;
}

void FillVoice::genDistort (float& l, float& r) noexcept
{
    float s0 = 0.0f, s1 = 0.0f;
    readFreezeLoop (std::fmod (playPos, (double) freezeLen), s0, s1);

    if (setupData.intensity01 > 0.75f)
    {
        l = foldback (s0 * driveAmount * 0.5f, 0.85f);
        r = foldback (s1 * driveAmount * 0.5f, 0.85f);
    }
    else
    {
        l = softClip (s0 * driveAmount) * distortNorm;
        r = softClip (s1 * driveAmount) * distortNorm;
    }

    playPos += playRate;
}

void FillVoice::genScramble (float& l, float& r) noexcept
{
    if (slicePos >= sliceLen)
    {
        slicePos = 0;
        sliceIndex = (sliceIndex + 1) % juce::jmax (1, numSlices);
    }

    const auto base = sliceOffset + sliceOrder[juce::jlimit (0, maxSlices - 1, sliceIndex)] * sliceLen;
    const auto edge = juce::jmin (sliceLen / 8, (int) (sr * 0.0015) + 2);

    float w = 1.0f;

    if (edge > 0)
    {
        if (slicePos < edge)
            w = (float) slicePos / (float) edge;
        else if (slicePos > sliceLen - edge)
            w = (float) (sliceLen - slicePos) / (float) edge;
    }

    w = juce::jlimit (0.0f, 1.0f, w);

    l = readCapture (0, (double) (base + slicePos)) * w;
    r = readCapture (1, (double) (base + slicePos)) * w;

    ++slicePos;
}

void FillVoice::genSpectral (float& l, float& r) noexcept
{
    if (spectralProcessor == nullptr)
        return;

    float in0 = 0.0f, in1 = 0.0f;
    readFreezeLoop (std::fmod (playPos, (double) juce::jmax (1, freezeLen)), in0, in1);

    spectralProcessor->process (in0, in1, l, r);
    playPos += 1.0;
}

void FillVoice::genComb (float& l, float& r) noexcept
{
    const auto excite = elapsed < combExcite ? 1.0f : 0.0f;
    const auto inL = readCapture (0, (double) (sliceOffset + elapsed)) * excite;
    const auto inR = readCapture (1, (double) (sliceOffset + elapsed)) * excite;

    const auto readIdx = ((combWrite - combDelay) % combSize + combSize) % combSize;

    auto* bufL = combBuffer.getWritePointer (0);
    auto* bufR = combBuffer.getWritePointer (1);

    const auto dL = bufL[readIdx];
    const auto dR = bufR[readIdx];

    combStateL += combDamp * (dL - combStateL);
    combStateR += combDamp * (dR - combStateR);

    const auto fbL = juce::jlimit (-4.0f, 4.0f, combStateL * combFeedback);
    const auto fbR = juce::jlimit (-4.0f, 4.0f, combStateR * combFeedback);

    bufL[combWrite] = sanitise (inL + fbL, 4.0f);
    bufR[combWrite] = sanitise (inR + fbR, 4.0f);

    combWrite = (combWrite + 1) % combSize;

    l = bufL[readIdx] * 0.8f;
    r = bufR[readIdx] * 0.8f;
}

//==============================================================================
void FillVoice::applyModifiers (float& l, float& r) noexcept
{
    for (int i = 0; i < numModifiers; ++i)
    {
        switch (modifiers[i])
        {
            case Modifier::FilterSweep:
            {
                const auto t = totalLength > 1 ? (float) elapsed / (float) (totalLength - 1) : 0.0f;
                const auto cutoff = modFilterStart * std::pow (juce::jmax (0.02f, modFilterEnd / juce::jmax (1.0f, modFilterStart)), t);
                modFilterL.set (sr, cutoff, modFilterQ);
                modFilterR.copyCoefficientsFrom (modFilterL);

                float lp = 0.0f, bp = 0.0f, hp = 0.0f;
                modFilterL.process (l, lp, bp, hp);
                l = modFilterMode == 0 ? lp : (modFilterMode == 1 ? hp : bp);

                modFilterR.process (r, lp, bp, hp);
                r = modFilterMode == 0 ? lp : (modFilterMode == 1 ? hp : bp);
                break;
            }

            case Modifier::Crush:
            {
                if (--modCrushCounter <= 0)
                {
                    modCrushCounter = modCrushDecimate;
                    modCrushHoldL = quantiseBits (l, modCrushBits);
                    modCrushHoldR = quantiseBits (r, modCrushBits);
                }

                l = modCrushHoldL;
                r = modCrushHoldR;
                break;
            }

            case Modifier::Distort:
            {
                l = softClip (l * modDrive) * modDistortNorm;
                r = softClip (r * modDrive) * modDistortNorm;
                break;
            }

            case Modifier::Tremolo:
            {
                const auto g = 1.0f - modTremDepth * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * modTremPhase));
                l *= g;
                r *= g;
                modTremPhase += modTremRate / (float) sr;

                if (modTremPhase >= 1.0f)
                    modTremPhase -= 1.0f;

                break;
            }

            case Modifier::PanMotion:
            {
                const auto pan = modPanDepth * std::sin (juce::MathConstants<float>::twoPi * modPanPhase);
                const auto gl = std::sqrt (juce::jlimit (0.0f, 1.0f, 0.5f - pan * 0.5f)) * 1.4142f;
                const auto gr = std::sqrt (juce::jlimit (0.0f, 1.0f, 0.5f + pan * 0.5f)) * 1.4142f;
                const auto mono = 0.5f * (l + r);
                l = lerp (l, mono * gl, 0.85f);
                r = lerp (r, mono * gr, 0.85f);
                modPanPhase += modPanRate / (float) sr;

                if (modPanPhase >= 1.0f)
                    modPanPhase -= 1.0f;

                break;
            }

            case Modifier::None:
            case Modifier::NumModifiers:
            default:
                break;
        }
    }
}

} // namespace gapfill
