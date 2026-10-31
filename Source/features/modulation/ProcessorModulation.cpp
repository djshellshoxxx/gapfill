#include "../../PluginProcessor.h"

namespace gapfill
{
modulation::DecisionInputs GapFillAudioProcessor::modulationPosition (const EngineSettings& settings,
    std::int64_t outputPos, bool canonical) const noexcept
{
    modulation::DecisionInputs at { settings.ppqAtBlockStart, settings.transportValid, outputPos,
        currentSampleRate, 0.0f, (std::uint64_t) (std::uint32_t) settings.seed,
        settings.perf.tsNum, settings.perf.tsDen };
    const auto start = samplesProcessed - (std::int64_t) targetLatency;
    if (at.transportValid && settings.samplesPerBeat > 1.0)
        at.ppq += (double) (outputPos - start) / settings.samplesPerBeat;
    if (canonical) at = modulation::Runtime::controlCellMidpoint (at, settings.samplesPerBeat);
    at.energy01 = analysis.frameAt (at.outputPos).energy;
    return at;
}

void GapFillAudioProcessor::updateContextForChunk (EngineSettings& settings, int numSamples) noexcept
{
    const auto* runtime = settings.perf.extendedMacroRuntime;
    if (runtime == nullptr)
    {
        contextState.onChunk (monoMain.data(), numSamples, analysis, samplesProcessed, settings);
        return;
    }
    const auto at = modulationPosition (settings, samplesProcessed - (std::int64_t) targetLatency,
                                         runtime->needsControlGrid());
    const auto nominal = runtime->projectContextTargets (settings, at);
    contextState.onChunk (monoMain.data(), numSamples, analysis, samplesProcessed, settings);
    modulation::Runtime::restoreContextBase (settings, nominal);
}

void GapFillAudioProcessor::prepareModulatedBus (EngineSettings& bus, const EngineSettings& raw, int numSamples) const noexcept
{
    bus = raw;
    const auto& runtime = *raw.perf.extendedMacroRuntime;
    const auto outputStart = samplesProcessed - (std::int64_t) targetLatency;
    const auto at = modulationPosition (raw, outputStart + (runtime.needsControlGrid() ? 0 : numSamples / 2),
                                         runtime.needsControlGrid());
    const auto send = runtime.evaluateContinuous (bus, at).fxSend01;
    // Both continuous Tension and width now refer to this same fixed sample.
    bus.ppqAtBlockStart = at.ppq;
    if (! runtime.needsControlGrid() && bus.transportValid && bus.samplesPerBeat > 1.0)
        bus.ppqAtBlockStart = raw.ppqAtBlockStart + 0.5 * (double) numSamples / bus.samplesPerBeat;
    bus.perf.previewTensionSend01 = contextState.tensionFxSend (bus, 0);
    modulation::applyCombinedEffectSend (bus, send * bus.perf.previewTensionSend01);
}
}
