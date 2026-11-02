#include "DisplayText.h"

#include "../events/EventRecorder.h"
#include "../fixit/Fixit.h"
#include "../i18n/Strings.h"

#include <cmath>

namespace gapfill::health
{

Description displayDescription (HealthCode code, const DescribeContext& context)
{
    auto display = describe (code, context);
    const auto bpm = juce::String (juce::roundToInt (context.fallbackBpm));
    const auto percentage = juce::String ((int) std::lround (juce::jlimit (0.0f, 9.99f, context.cpu01) * 100.0f));
    switch (code)
    {
        case HealthCode::NoTransport:
            display.message = i18n::format ("Transport unavailable - free timing at {0} BPM", { bpm });
            break;
        case HealthCode::FreeTimingFallback:
            display.message = i18n::format ("Free timing at {0} BPM", { bpm });
            break;
        case HealthCode::OverloadWarn:
            display.message = i18n::format ("CPU high ({0}%) - try Quality: Balanced", { percentage });
            break;
        case HealthCode::OverloadCritical:
            display.message = i18n::format ("CPU critical ({0}%) - audio may drop out", { percentage });
            break;
        default:
            display.message = i18n::text (display.message);
            break;
    }
    display.explanation = i18n::text (display.explanation);
    display.action = i18n::text (display.action);
    return display;
}

juce::String proposalText (const fixit::Proposal& proposal)
{
    if (proposal.changes.empty())
        return i18n::text (proposal.text);
    juce::StringArray changes;
    for (const auto& change : proposal.changes)
    {
        const auto label = i18n::text (change.label);
        if (change.toName.isNotEmpty())
            changes.add (i18n::format ("{0} -> {1}", { label, i18n::text (change.toName) }));
        else
            changes.add (i18n::format ("{0} {1} -> {2}{3}",
                         { label, juce::String ((int) std::lround (change.from)),
                           juce::String ((int) std::lround (change.to)), change.unit }));
    }
    return changes.joinIntoString (", ");
}

juce::String proposalSpoken (const fixit::Proposal& proposal)
{
    const auto display = proposalText (proposal);
    if (display.isEmpty())
        return {};
    return proposal.changes.empty()
               ? i18n::format ("Suggestion: {0}. Press Apply to do it.", { display })
               : i18n::format ("Suggestion: change {0}. Press Apply to accept.", { display });
}

juce::String telemetryText (int latencySamples, double sampleRate, float cpu01, bool measured)
{
    const auto samples = juce::jmax (0, latencySamples);
    const auto ms = sampleRate > 0.0 ? 1000.0 * (double) samples / sampleRate : 0.0;
    const auto latency = i18n::format ("PDC {0} smp ({1} ms)", { juce::String (samples), juce::String (ms, 1) });
    const auto percentage = (int) std::lround (juce::jlimit (0.0f, 9.99f, std::isfinite (cpu01) ? cpu01 : 0.0f) * 100.0f);
    const auto cpu = measured ? i18n::format ("CPU ~{0}% (estimated)", { juce::String (percentage) })
                              : i18n::text ("CPU: not measured");
    return i18n::format ("Latency reported to host: {0}\n{1}  - GapFill's own processing time per block; not the host's CPU meter.", { latency, cpu });
}

juce::String recordingText (events::EventRecorder& recorder)
{
    recorder.drain();
    const auto count = juce::String (recorder.getNumRecords());
    const auto megabytes = juce::String ((double) recorder.getEstimatedFileBytes() / (1024.0 * 1024.0), 2);
    if (recorder.isRecording())
        return i18n::format ("Recording: {0} events ({1} MB)", { count, megabytes });
    if (recorder.isFull())
        return i18n::format ("Recording stopped: the buffer is full ({0} events, {1} MB). Save it now.", { count, megabytes });
    if (recorder.getNumRecords() > 0)
        return i18n::format ("Recorded {0} events ({1} MB), not saved yet", { count, megabytes });
    return i18n::text ("Not recording");
}

} // namespace gapfill::health
