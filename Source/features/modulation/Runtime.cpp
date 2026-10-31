#include "Runtime.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace gapfill::modulation
{
Runtime::Runtime() noexcept { std::fill (std::begin (available), std::end (available), true); }

void Runtime::resetReadout() noexcept
{
    std::fill (std::begin (effective01), std::end (effective01), 0.0f);
    std::fill (std::begin (active), std::end (active), false);
}

bool Runtime::hasTarget (int target) const noexcept
{
    if (target < 0 || target >= kTargetCount || ! available[target]) return false;
    for (int macro = 0; macro < kMacroCount; ++macro)
        for (int index = 0; index < std::min<int> (cfg.count[macro], macros::kMaxMappings); ++index)
            if (cfg.map[macro][index].target == target) return true;
    return false;
}

void Runtime::claimOwnership (EngineSettings& settings) const noexcept
{
    settings.perf.macroOwnedMask = 0;
    settings.perf.advancedMacroOwnedMask = 0;
    for (int macro = 0; macro < kMacroCount; ++macro)
        for (int index = 0; index < std::min<int> (cfg.count[macro], macros::kMaxMappings); ++index)
        {
            const auto target = cfg.map[macro][index].target;
            if (target >= kTargetCount || ! available[target]) continue;
            settings.perf.macroOwnedMask |= coreOwnership (target);
            settings.perf.advancedMacroOwnedMask |= advancedOwnership (target);
        }
}

Runtime::Values Runtime::valuesAt (const DecisionInputs& at) const noexcept
{
    bool needed[kLfoCount] {};
    for (int macro = 0; macro < kMacroCount; ++macro)
        if (cfg.count[macro] > 0)
        {
            needed[0] |= inputs.source[macro] == MacroSource::LfoA;
            needed[1] |= inputs.source[macro] == MacroSource::LfoB;
        }
    const float lfoA = needed[0] ? lfoValue (inputs.lfo[0], at, 0) : 0;
    const float lfoB = needed[1] ? lfoValue (inputs.lfo[1], at, 1) : 0;
    Values result;
    bool claimed[kTargetCount] {};
    for (int macro = 0; macro < kMacroCount; ++macro)
    {
        const auto value = macroValue (inputs.source[macro], inputs.knob01[macro], inputs.depth[macro], lfoA, lfoB, at.energy01);
        for (int index = 0; index < std::min<int> (cfg.count[macro], macros::kMaxMappings); ++index)
        {
            const auto& mapping = cfg.map[macro][index];
            const auto target = mapping.target;
            if (target >= kTargetCount || claimed[target] || ! available[target]) continue;
            claimed[target] = true;
            // Automation can select Energy after the GUI refused it. Keep width inert.
            if (target == 5 && inputs.source[macro] == MacroSource::Energy) continue;
            result.active[target] = true;
            result.value[target] = macros::mappingValue (mapping, value);
        }
    }
    return result;
}

TargetOutputs Runtime::project (EngineSettings& settings, const Values& values, Projection mode) const noexcept
{
    TargetOutputs outputs;
    const auto base = captureTargetBase (settings);
    // Selector first, regardless of which macro/mapping supplied it. All claims
    // and values have already been resolved with lower-index ownership.
    if (mode != Projection::Context && values.active[47]) applyTarget (settings, 47, values.value[47], base, outputs);
    for (int target = 0; target < kTargetCount - 1; ++target)
    {
        if (! values.active[target]) continue;
        if (mode == Projection::Continuous && ! isContinuousTarget (target)) continue;
        if (mode == Projection::Context && (target < 32 || target > 35)) continue;
        applyTarget (settings, target, values.value[target], base, outputs);
    }
    return outputs;
}

TargetOutputs Runtime::evaluateDecision (EngineSettings& settings, const DecisionInputs& at) const noexcept
{
    const auto values = valuesAt (at);
    std::copy (std::begin (values.value), std::end (values.value), std::begin (effective01));
    std::copy (std::begin (values.active), std::end (values.active), std::begin (active));
    return project (settings, values, Projection::Decision);
}

TargetOutputs Runtime::evaluateContinuous (EngineSettings& settings, const DecisionInputs& at) const noexcept
{
    const auto values = valuesAt (at);
    for (int target = 0; target < kTargetCount; ++target)
        if (isContinuousTarget (target))
        {
            active[target] = values.active[target];
            effective01[target] = values.value[target];
        }
    return project (settings, values, Projection::Continuous);
}

ContextBase Runtime::projectContextTargets (EngineSettings& settings, const DecisionInputs& at) const noexcept
{
    const ContextBase base { captureTargetBase (settings), settings.perf.energy.adapt01,
                            settings.scaleLock.amount01, settings.perf.groove.amount01 };
    (void) project (settings, valuesAt (at), Projection::Context);
    return base;
}

void Runtime::restoreContextBase (EngineSettings& settings, const ContextBase& base) noexcept
{
    for (int lane = 0; lane < tension::kLanes; ++lane) settings.perf.tension.depth[lane] = base.targets.tensionDepth[lane];
    settings.perf.energy.adapt01 = base.energyAdapt;
    settings.scaleLock.amount01 = base.scaleAmount;
    settings.perf.groove.amount01 = base.grooveAmount;
}

WidthValue Runtime::widthAt (const DecisionInputs& at) const noexcept
{
    const auto values = valuesAt (at);
    return { values.active[5], values.active[5] ? targetRealValue (5, values.value[5]) * .01f : 1.0f };
}

bool Runtime::needsControlGrid() const noexcept
{
    for (int macro = 0; macro < kMacroCount; ++macro)
        if (inputs.source[macro] != MacroSource::Knob)
            for (int index = 0; index < std::min<int> (cfg.count[macro], macros::kMaxMappings); ++index)
            {
                const auto target = cfg.map[macro][index].target;
                if (target < kTargetCount && available[target] && isContinuousTarget (target)
                    && ! (target == 5 && inputs.source[macro] == MacroSource::Energy)) return true;
            }
    return false;
}

int Runtime::samplesUntilControlBoundary (std::int64_t outputPos, int maxSamples) noexcept
{
    const auto remainder = (int) ((outputPos % kControlCellSamples + kControlCellSamples) % kControlCellSamples);
    return std::max (0, std::min (maxSamples, kControlCellSamples - remainder));
}

DecisionInputs Runtime::controlCellMidpoint (DecisionInputs at, double samplesPerBeat) noexcept
{
    const auto remainder = (int) ((at.outputPos % kControlCellSamples + kControlCellSamples) % kControlCellSamples);
    const auto delta = kControlCellSamples / 2 - remainder;
    at.outputPos += delta;
    if (at.transportValid && std::isfinite (at.ppq) && std::isfinite (samplesPerBeat) && samplesPerBeat > 1)
        at.ppq += (double) delta / samplesPerBeat;
    else
        at.transportValid = false;
    return at;
}
void applyCombinedEffectSend (EngineSettings& settings, float send) noexcept
{
    if (send >= 1) return;
    send = std::isfinite (send) ? std::clamp (send, 0.0f, 1.0f) : 0.0f;
    const auto tapeDepth = settings.fx.tape.amount01;
    fx::applyEffectSend (settings.fx, send);
    if (settings.fx.tape.mixOverride01 >= 0)
    {
        // A V3 independent wet target keeps its nominal distortion depth.
        settings.fx.tape.amount01 = tapeDepth;
        settings.fx.tape.mixOverride01 *= send;
    }
    settings.fx.tapeExtra.mix01 *= send;
    settings.voiceFormant.amount01 *= send;
    settings.voiceFormant.enabled = settings.voiceFormant.enabled && settings.voiceFormant.amount01 > 0;
    settings.perf.harmonic.amount01 *= send;
}
} // namespace gapfill::modulation
