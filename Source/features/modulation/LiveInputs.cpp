#include "../live/LiveControl.h"

namespace gapfill::live
{
void LiveControl::preflightModulationInputs (const Effective::ModulationInputs& raw, int seed,
                                            const scenes::TransportSnap& transport) noexcept
{
    modulation::ConfigSnapshot fresh;
    if (macroModel.extendedPublisher().consume (fresh))
    {
        extendedMacros = fresh.enabled != 0;
        extendedMacroRuntime.cfg = fresh.config;
        extendedMacroRuntime.resetReadout();
        macroRuntime.cfg = fresh.config.retainedLegacy;
        for (auto& smoothing : macroRuntime.hasSmoothed) smoothing = false;
    }
    pinnedModulation = raw;
    macroRuntime.seed = seed;
    pinnedModulation.macros.runtime = extendedMacros ? nullptr : &macroRuntime;
    pinnedModulation.macros.extendedRuntime = extendedMacros ? &extendedMacroRuntime : nullptr;
    const auto& scene = sceneView();
    if (scene.appliesAt (transport.ppq, transport.valid)) scene.applyModulationTo (pinnedModulation);
    extendedMacroRuntime.inputs = pinnedModulation.macros.extendedInputs;
    extendedMacroRuntime.inputs.knob01[0] = pinnedModulation.macros.activity * 0.01f;
    extendedMacroRuntime.inputs.knob01[1] = pinnedModulation.macros.chaos * 0.01f;
    extendedMacroRuntime.inputs.knob01[2] = pinnedModulation.macros.space * 0.01f;
    extendedMacroRuntime.inputs.knob01[3] = pinnedModulation.macros.destruction * 0.01f;
}

int LiveControl::modulationSegmentLength (std::int64_t outputPos, int maxSamples) const noexcept
{
    return extendedMacros && pinnedModulation.macros.on && extendedMacroRuntime.needsControlGrid()
        ? modulation::Runtime::samplesUntilControlBoundary (outputPos, maxSamples) : maxSamples;
}
}
