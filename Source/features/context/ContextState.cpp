#include "ContextState.h"

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
    reset();
}

void ContextState::reset() noexcept
{
    energyTracker.reset();
    groove.reset();
    key.reset();
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

    // F-L22: no per-sample work while Follow is 0; the range is relearned when it is switched on again.
    if (s.perf.energy.adapt01 <= 0.0f)
    {
        if (energyRunning)
        {
            energyTracker.reset();
            energyRunning = false;
            displayLevel.store (-1, std::memory_order_relaxed);
        }

        return;
    }

    energyRunning = true;
    energyTracker.process (mono, numSamples);

    if (energyTracker.warmedUp())
    {
        s.perf.energy.level = (int) energyTracker.level();
        displayLevel.store ((int) energyTracker.level(), std::memory_order_relaxed);
    }
}

} // namespace gapfill::context
