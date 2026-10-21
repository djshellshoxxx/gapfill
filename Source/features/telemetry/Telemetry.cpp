#include "Telemetry.h"

namespace gapfill::telemetry
{

juce::String formatLatency (int latencySamples, double sampleRate)
{
    const auto samples = juce::jmax (0, latencySamples);
    const auto ms = sampleRate > 0.0 ? 1000.0 * (double) samples / sampleRate : 0.0;
    return "PDC " + juce::String (samples) + " smp (" + juce::String (ms, 1) + " ms)";
}

juce::String formatCpu (float cpu01, bool measured)
{
    if (! measured)
        return "CPU: not measured";

    const auto pct = (int) std::lround (juce::jlimit (0.0f, 9.99f, std::isfinite (cpu01) ? cpu01 : 0.0f) * 100.0f);
    return "CPU ~" + juce::String (pct) + "% (estimated)";
}

juce::String describeTelemetry (int latencySamples, double sampleRate, float cpu01, bool measured)
{
    return "Latency reported to host: " + formatLatency (latencySamples, sampleRate) + "\n"
         + formatCpu (cpu01, measured)
         + "  - GapFill's own processing time per block; not the host's CPU meter.";
}

} // namespace gapfill::telemetry
