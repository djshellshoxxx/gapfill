#include "WavExport.h"

namespace gapfill::exporting
{

juce::String defaultFileName (const juce::String& algoName, double bpm, juce::Time when)
{
    const auto algo = algoName.removeCharacters (" /\\:*?\"<>|");
    juce::String name = "GapFill_" + (algo.isEmpty() ? juce::String ("Fill") : algo);

    if (bpm > 1.0)
        name << "_" << juce::String (juce::roundToInt (bpm)) << "bpm";

    name << "_" << when.formatted ("%Y%m%d-%H%M%S") << ".wav";
    return name;
}

bool writeWav24 (const juce::File& file, const juce::AudioBuffer<float>& audio, double sampleRate, juce::String& error)
{
    if (audio.getNumSamples() <= 0 || audio.getNumChannels() <= 0)
    {
        error = "There is nothing to export.";
        return false;
    }

    if (! file.getParentDirectory().createDirectory())
    {
        error = "Couldn't create the folder " + file.getParentDirectory().getFullPathName();
        return false;
    }

    file.deleteFile();
    auto stream = std::make_unique<juce::FileOutputStream> (file);

    if (! stream->openedOk())
    {
        error = "Couldn't open " + file.getFullPathName() + " for writing.";
        return false;
    }

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), sampleRate,
                                                                          (unsigned int) audio.getNumChannels(), 24,
                                                                          juce::StringPairArray(), 0));

    if (writer == nullptr)
    {
        error = "Couldn't create a WAV writer for " + file.getFileName();
        return false;
    }

    stream.release();           // the writer owns the stream now

    if (! writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()))
    {
        error = "Writing " + file.getFileName() + " failed (is the disk full?).";
        writer.reset();
        file.deleteFile();
        return false;
    }

    writer.reset();             // flushes and finalises the header

    if (! file.existsAsFile() || file.getSize() < 44 + (juce::int64) audio.getNumSamples() * audio.getNumChannels() * 3)
    {
        error = "The file " + file.getFileName() + " was not written completely.";
        return false;
    }

    return true;
}

juce::String failureText (Failure f)
{
    switch (f)
    {
        case Failure::TooOld:       return "That fill is too old";
        case Failure::NotRecorded:  return "That fill was played before the export buffer was recording";
        case Failure::TooLong:      return "That fill is too long to export";
        case Failure::None:
        default:                    return "Export failed";
    }
}

juce::String requestText (RequestResult r)
{
    switch (r)
    {
        case RequestResult::Busy:         return "Another export is still running";
        case RequestResult::NoSpan:       return "That fill is too old";
        case RequestResult::NotPrepared:  return "Start playback before exporting";
        case RequestResult::Accepted:
        default:                          return {};
    }
}

} // namespace gapfill::exporting
