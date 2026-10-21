#include "ContextState.h"

#include <algorithm>
#include <cmath>

namespace gapfill::context
{

void gatherContext (EngineSettings& s, const ParamCache& p) noexcept
{
    s.perf.energy.adapt01 = juce::jlimit (0.0f, 1.0f, pv (p.energyAdapt) / 100.0f);
    s.perf.energy.target = juce::jlimit (0, 2, pi (p.energyTarget));
    s.perf.energy.level = 1;                    // Mid until the tracker has learned the range

    auto& g = s.perf.groove;
    g.amount01 = juce::jlimit (0.0f, 1.0f, pv (p.fl09_amount) / 100.0f);
    g.grid = juce::jlimit (0, 1, pi (p.fl09_grid));
    g.hold = pb (p.fl09_hold);
    g.syncOn = pb (p.syncEnable);
    g.snap = {};

    auto& c = s.perf.call;
    c.mode = juce::jlimit (0, 2, pi (p.fl10_mode));
    c.strength01 = juce::jlimit (0.0f, 1.0f, pv (p.fl10_strength) / 100.0f);
    c.calls = juce::jlimit (2, 4, pi (p.fl10_calls));
    c.replies = juce::jlimit (1, 4, pi (p.fl10_replies));

    s.perf.taste.on = pb (p.fx05_on);
    s.perf.taste.strength01 = juce::jlimit (0.0f, 1.0f, pv (p.fx05_strength) / 100.0f);

    auto& a = s.perf.arr;
    a.on = pb (p.fx04_on);
    a.calm01 = juce::jlimit (0.0f, 1.0f, pv (p.fx04_calm) / 100.0f);
    a.lift01 = juce::jlimit (0.0f, 1.0f, pv (p.fx04_lift) / 100.0f);
    a.settle01 = juce::jlimit (0.0f, 1.0f, pv (p.fx04_settle) / 100.0f);
    a.sensitivity01 = juce::jlimit (0.0f, 1.0f, pv (p.fx04_sensitivity) / 100.0f);

    auto& t = s.perf.tension;
    t.on = pb (p.fm21_on);
    t.bars = 2 << juce::jlimit (0, 4, pi (p.fm21_bars));
    t.startBar = juce::jlimit (0, 255, pi (p.fm21_start));
    t.shape = juce::jlimit (0, tension::NumShapes - 1, pi (p.fm21_shape));
    t.end = juce::jlimit (0, 2, pi (p.fm21_end));
    t.depth[tension::Density] = juce::jlimit (0.0f, 1.0f, pv (p.fm21_depthDensity) / 100.0f);
    t.depth[tension::Intensity] = juce::jlimit (0.0f, 1.0f, pv (p.fm21_depthIntensity) / 100.0f);
    t.depth[tension::Width] = juce::jlimit (0.0f, 1.0f, pv (p.fm21_depthWidth) / 100.0f);
    t.depth[tension::FxSend] = juce::jlimit (0.0f, 1.0f, pv (p.fm21_depthFx) / 100.0f);

    auto& k = s.perf.key;
    k.autoOn = pb (p.scaleFollow);
    k.windowSeconds = juce::jlimit (4.0f, 30.0f, pv (p.fx03_window));
    k.minConfidence01 = juce::jlimit (0.0f, 1.0f, pv (p.fx03_minConf) / 100.0f);
    k.hold = pb (p.fx03_hold);
}

void ContextState::prepare (double sampleRate) noexcept
{
    energyTracker.prepare (sampleRate);
    groove.prepare (sampleRate);
    key.prepare (sampleRate);
    section.reset();
    reset();
}

void ContextState::reset() noexcept
{
    energyTracker.reset();
    groove.reset();
    key.reset();
    section.reset();
    arrRunning = false;
    publishArrangement();
    energyRunning = false;
    displayLevel.store (-1, std::memory_order_relaxed);
}

void ContextState::onChunk (const float* mono, int numSamples, const AnalysisEngine& analysis,
                            std::int64_t chunkInputStart, EngineSettings& s) noexcept
{
    taste.apply (s);                                                  // F-X05 (identity while off / not ready / locked)
    groove.onChunk (analysis, chunkInputStart, numSamples, s);       // F-L09 (returns at once while off)

    // F-X03: detect the key (nothing runs while Auto key is off) and let the resolver
    // decide which key the scale lock uses: manual, detected or held.
    key.process (mono, numSamples, s.perf.key);

    if (s.perf.key.autoOn)
    {
        const auto resolved = keydetect::resolveKey (s.scaleLock.set.root, s.scaleLock.set.type, true, s.perf.key.hold,
                                                     key.heldKey(), key.estimate());

        if (resolved.source == keydetect::KeySource::Auto || resolved.source == keydetect::KeySource::Held)
            s.scaleLock.set = scale::makeScaleSet (resolved.rootPc, resolved.scaleType);
    }

    // F-M21: custom curves for the decision stage, and the playhead for the editor.
    if (s.perf.tension.on)
    {
        if (s.perf.tension.shape == tension::Custom)
            s.perf.tension.curves = tensionSvc.curves();

        const bool valid = s.transportValid && s.samplesPerBeat > 1.0;
        tensionSvc.setStatus (tension::tensionAt (s.ppqAtBlockStart + (valid ? 0.5 * numSamples / s.samplesPerBeat : 0.0), valid,
                                                  s.perf.tsNum, s.perf.tsDen, s.perf.tension));
    }
    else
        tensionSvc.setStatus ({});

    // F-L22 energy and F-X04 arrangement share the 0.5 s frames. Nothing runs while both are off.
    const bool energyWanted = s.perf.energy.adapt01 > 0.0f;
    const bool arrangementWanted = s.perf.arr.on;

    if (! energyWanted && ! arrangementWanted)
    {
        if (energyRunning || arrRunning)
        {
            energyTracker.reset();
            section.reset();
            energyRunning = arrRunning = false;
            displayLevel.store (-1, std::memory_order_relaxed);
            publishArrangement();
        }

        return;
    }

    if (! energyWanted)
        displayLevel.store (-1, std::memory_order_relaxed);

    energyRunning = true;

    if (arrRunning && ! arrangementWanted)
    {
        section.reset();
        publishArrangement();
    }

    arrRunning = arrangementWanted;

    // Feed in pieces that end exactly at frame boundaries, so every frame is pushed once whatever the block size.
    for (int remaining = numSamples; remaining > 0;)
    {
        const int step = std::min (remaining, energyTracker.samplesUntilFrameEnd());
        const int before = energyTracker.framesCompleted();
        energyTracker.process (mono + (numSamples - remaining), step);
        remaining -= step;

        if (arrangementWanted && energyTracker.framesCompleted() != before)
            section.pushFrame (energyTracker.lastFrame(), s.perf.arr.sensitivity01);
    }

    if (energyWanted && energyTracker.warmedUp())
    {
        s.perf.energy.level = (int) energyTracker.level();
        displayLevel.store ((int) energyTracker.level(), std::memory_order_relaxed);
    }

    if (arrangementWanted)
    {
        s.perf.arr.state = section.state();
        publishArrangement();
    }
}

float ContextState::tensionWidthDelta (const EngineSettings& s, float baseWidth01, int numSamples) const noexcept
{
    const auto& t = s.perf.tension;

    if (! t.active() || t.depth[tension::Width] <= 0.0f)
        return 0.0f;

    const bool valid = s.transportValid && s.samplesPerBeat > 1.0;
    const double mid = s.ppqAtBlockStart + (valid ? 0.5 * (double) numSamples / s.samplesPerBeat : 0.0);
    return tension::widthDelta (t, baseWidth01, mid, valid, s.perf.tsNum, s.perf.tsDen);
}

void ContextState::publishArrangement() noexcept
{
    const auto& a = section.state();
    const auto q8 = [] (float v, float lo, float hi) { return (std::uint64_t) std::lround (255.0f * (std::clamp (v, lo, hi) - lo) / (hi - lo)); };

    arrWord0.store (((std::uint64_t) a.sectionIndex << 32) | ((std::uint64_t) a.framesInto << 16) | (std::uint64_t) (std::uint16_t) a.expectedIn,
                    std::memory_order_relaxed);
    arrWord1.store (q8 (a.energy01, 0.0f, 1.0f) | (q8 (a.trend, -1.0f, 1.0f) << 8) | (q8 (a.confidence, 0.0f, 1.0f) << 16)
                      | ((a.learning ? 1ull : 0ull) << 24),
                    std::memory_order_relaxed);
    arrFrames.store (section.frames(), std::memory_order_relaxed);
}

arrangement::ArrangementState ContextState::arrangementState() const noexcept
{
    const auto w0 = arrWord0.load (std::memory_order_relaxed), w1 = arrWord1.load (std::memory_order_relaxed);
    arrangement::ArrangementState a;
    a.sectionIndex = (std::uint32_t) (w0 >> 32);
    a.framesInto = (std::uint16_t) ((w0 >> 16) & 0xFFFF);
    a.expectedIn = (std::int16_t) (std::uint16_t) (w0 & 0xFFFF);
    a.energy01 = (float) (w1 & 0xFF) / 255.0f;
    a.trend = (float) ((w1 >> 8) & 0xFF) / 255.0f * 2.0f - 1.0f;
    a.confidence = (float) ((w1 >> 16) & 0xFF) / 255.0f;
    a.learning = ((w1 >> 24) & 1ull) != 0;
    return a;
}

} // namespace gapfill::context
