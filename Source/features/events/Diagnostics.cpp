#include "Diagnostics.h"

#ifndef GAPFILL_VERSION
 #define GAPFILL_VERSION "0.0.0"
#endif

#include "../../PluginProcessor.h"
#include "../explain/ExplainState.h"
#include "../state/StateSubtrees.h"

namespace gapfill::events
{

const char* const kPrivacyNote =
    "A recording stores parameter changes, commands, transport position, the block size, the sample rate, "
    "the plug-in and state version, the starting parameter values and the outcome of every gap. "
    "It never stores audio, file names or paths.";

namespace
{
    juce::String currentStateXml (GapFillAudioProcessor& proc)
    {
        juce::MemoryBlock block;
        proc.getStateInformation (block);
        const auto xml = juce::AudioProcessor::getXmlFromBinary (block.getData(), (int) block.getSize());
        return xml != nullptr ? xml->toString (juce::XmlElement::TextFormat().singleLine()) : juce::String();
    }
}

bool isRecording (const GapFillAudioProcessor& proc)
{
    return proc.getExplainState().recorder.isRecording();
}

juce::Result startRecording (GapFillAudioProcessor& proc)
{
    auto& rec = proc.getExplainState().recorder;

    if (rec.isRecording())
        return juce::Result::fail ("A recording is already running");

    if (! rec.start (currentStateXml (proc), proc.getExplainState().sampleRate.load(),
                     juce::jmax (16, proc.getBlockSize()), GAPFILL_VERSION, state::kStateVersion))
        return juce::Result::fail ("Could not start the recording");

    return juce::Result::ok();
}

void stopRecording (GapFillAudioProcessor& proc)
{
    auto& rec = proc.getExplainState().recorder;
    rec.stop();
    rec.drain();
}

void toggleRecording (GapFillAudioProcessor& proc)
{
    if (isRecording (proc))
        stopRecording (proc);
    else
        startRecording (proc);
}

juce::String describeRecording (GapFillAudioProcessor& proc)
{
    auto& rec = proc.getExplainState().recorder;
    rec.drain();
    const auto n = rec.getNumRecords();
    const auto mb = (double) rec.getEstimatedFileBytes() / (1024.0 * 1024.0);

    if (rec.isRecording())
        return "Recording: " + juce::String (n) + " events (" + juce::String (mb, 2) + " MB)";

    if (rec.isFull())
        return "Recording stopped: the buffer is full (" + juce::String (n) + " events, " + juce::String (mb, 2) + " MB). Save it now.";

    if (n > 0)
        return "Recorded " + juce::String (n) + " events (" + juce::String (mb, 2) + " MB), not saved yet";

    return "Not recording";
}

juce::Result saveRecording (GapFillAudioProcessor& proc, const juce::File& file)
{
    stopRecording (proc);
    return saveToFile (proc.getExplainState().recorder.snapshot(), file);
}

} // namespace gapfill::events
