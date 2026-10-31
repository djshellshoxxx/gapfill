#include "TraceQueue.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>

namespace gapfill::transportprobe
{
class Probe final : public juce::AudioProcessor, private juce::Thread
{
public:
    using juce::AudioProcessor::processBlock;
    Probe() : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                            .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
              Thread ("GapFill timing trace writer")
    {
        hostId = juce::SystemStats::getEnvironmentVariable ("GAPFILL_TRANSPORT_TRACE_HOST_ID", "unknown").substring (0, 128);
        hostVersion = juce::SystemStats::getEnvironmentVariable ("GAPFILL_TRANSPORT_TRACE_HOST_VERSION", "unknown").substring (0, 128);
        scenario = juce::SystemStats::getEnvironmentVariable ("GAPFILL_TRANSPORT_TRACE_SCENARIO", "unspecified").substring (0, 128);
        const auto directory = juce::SystemStats::getEnvironmentVariable ("GAPFILL_TRANSPORT_TRACE_DIR", {});
        if (directory.isEmpty()) return;
        if (! juce::File::isAbsolutePath (directory)) { status.store (Error); return; }
        const juce::File folder (directory);
        if (folder.createDirectory().failed()) { status.store (Error); return; }
        csvFile = folder.getChildFile ("transport-" + juce::Uuid().toString() + ".csv");
        started = juce::Time::getCurrentTime().toISO8601 (true);
        gate.open();
        status.store (Recording);
        if (! startThread()) { gate.close(); status.store (Error); }
    }
    ~Probe() override { finishCapture(); stopThread (-1); }
    const juce::String getName() const override { return "GapFill Transport Probe"; }
    void prepareToPlay (double rate, int) override { sampleRate = rate; }
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layout) const override
    {
        const auto input = layout.getMainInputChannelSet();
        return input == layout.getMainOutputChannelSet()
            && (input == juce::AudioChannelSet::mono() || input == juce::AudioChannelSet::stereo());
    }
    void processBlock (juce::AudioBuffer<float>& audio, juce::MidiBuffer&) override
    {
        if (! gate.enter()) return;
        Row row;
        row.sequence = sequence++;
        row.blockSamples = (std::uint32_t) audio.getNumSamples();
        row.sampleRate = sampleRate;
        row.nonRealtime = isNonRealtime();
        if (auto* hostPlayHead = getPlayHead())
            if (const auto position = hostPlayHead->getPosition())
            {
                row.playing = position->getIsPlaying();
                if (const auto value = position->getPpqPosition(); value && std::isfinite (*value))
                { row.ppq = *value; row.ppqValid = true; }
                if (const auto value = position->getBpm(); value && std::isfinite (*value) && *value > 1 && *value < 1000)
                { row.bpm = *value; row.tempoValid = true; }
                if (const auto value = position->getTimeInSamples())
                { row.samplePosition = *value; row.samplePositionValid = true; }
            }
        (void) queue.push (row);                  // bounded/drop-counted; never retries or wakes file I/O
        callbacks.store (sequence, std::memory_order_relaxed);
        gate.leave();
        // Audio and MIDI buffers remain byte-for-byte untouched.
    }
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Trace"; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& state) override { state.reset(); }
    void setStateInformation (const void*, int) override {}
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;
    void finishCapture()
    {
        if (status.load() == Recording) status.store (Writing);
        gate.close();
        notify();                               // non-realtime UI/writer/destructor only
    }
    juce::String displayStatus() const
    {
        switch (status.load())
        {
            case Disabled: return "Disabled: set GAPFILL_TRANSPORT_TRACE_DIR before launching your host.";
            case Recording: return "Recording timing only. Audio is unchanged. Press Finish capture before analyzing.";
            case Writing: return "Finalizing the timing trace...";
            case Complete: return limited.load() ? "Finalized at the 64 MiB limit; review actual duration." : "Finalized. Retain the CSV and JSON together.";
            case Error: return "Trace write failed. Check the configured directory and retained files.";
        }
        return {};
    }
    const juce::File& traceFile() const { return csvFile; }
private:
    enum Status { Disabled, Recording, Writing, Complete, Error };
    void writeMetadata (bool complete, std::uint64_t written, bool ioError)
    {
        auto* data = new juce::DynamicObject;
        data->setProperty ("schema", 1); data->setProperty ("producer", "GapFillTransportProbe");
        data->setProperty ("source_sha", GAPFILL_PROBE_HEAD); data->setProperty ("source_dirty", GAPFILL_PROBE_DIRTY != 0);
        data->setProperty ("juce_source_sha", GAPFILL_PROBE_JUCE_HEAD); data->setProperty ("compiler", GAPFILL_PROBE_COMPILER);
        data->setProperty ("juce_source_dirty", GAPFILL_PROBE_JUCE_DIRTY != 0);
        data->setProperty ("host_id", hostId); data->setProperty ("host_version", hostVersion);
        data->setProperty ("scenario", scenario); data->setProperty ("platform", juce::SystemStats::getOperatingSystemName());
        data->setProperty ("started_utc", started); data->setProperty ("complete", complete);
        data->setProperty ("finished_utc", complete ? juce::Time::getCurrentTime().toISO8601 (true) : juce::String());
        data->setProperty ("wall_clock_scope", "plugin open to finalize, including startup and stopped time");
        data->setProperty ("rows_written", (juce::int64) written); data->setProperty ("callbacks_seen", (juce::int64) callbacks.load());
        data->setProperty ("dropped_rows", (juce::int64) queue.droppedRows()); data->setProperty ("io_error", ioError);
        data->setProperty ("size_limit_reached", limited.load());
        juce::TemporaryFile temporary (csvFile.withFileExtension ("json"));
        if (! temporary.getFile().replaceWithText (juce::JSON::toString (juce::var (data), true))
            || ! temporary.overwriteTargetFileWithTemporary()) status.store (Error);
    }
    void run() override
    {
        juce::FileOutputStream output (csvFile);
        if (! output.openedOk()) { gate.close(); status.store (Error); writeMetadata (false, 0, true); return; }
        constexpr char header[] = "sequence,playing,ppq_valid,tempo_valid,ppq,bpm,block_samples,sample_rate,sample_position_valid,sample_position,non_realtime\n";
        bool okay = output.write (header, sizeof (header) - 1);
        std::uint64_t written = 0;
        std::ostringstream line;
        line.imbue (std::locale::classic()); // ASCII decimals regardless of the host's locale; worker only
        line << std::setprecision (std::numeric_limits<double>::max_digits10);
        writeMetadata (false, 0, false);
        while (okay)
        {
            bool consumed = false;
            Row row;
            for (int index = 0; index < 4096 && queue.pop (row); ++index)
            {
                consumed = true;
                line.str ({}); line.clear();
                line << row.sequence << ',' << (int) row.playing << ',' << (int) row.ppqValid << ',' << (int) row.tempoValid
                     << ',' << row.ppq << ',' << row.bpm << ',' << row.blockSamples << ',' << row.sampleRate
                     << ',' << (int) row.samplePositionValid << ',' << row.samplePosition << ',' << (int) row.nonRealtime << '\n';
                const auto text = line.str();
                okay = output.write (text.data(), text.size());
                if (! okay) break;
                ++written;
                if (output.getPosition() >= 64 * 1024 * 1024 && ! limited.exchange (true)) finishCapture();
            }
            if (! okay || (! consumed && gate.closed())) break;
            if (! consumed) wait (20);             // file-writer thread only
        }
        gate.close();
        output.flush();
        okay = okay && output.getStatus().wasOk();
        if (! okay) status.store (Error);
        writeMetadata (okay && gate.closed(), written, ! okay);
        if (status.load() != Error) status.store (Complete);
    }
    Queue<> queue;
    Gate gate;
    double sampleRate = 48000;
    std::uint64_t sequence = 0;
    std::atomic<std::uint64_t> callbacks { 0 };
    std::atomic<int> status { Disabled };
    std::atomic<bool> limited { false };
    juce::File csvFile;
    juce::String hostId, hostVersion, scenario, started;
};

class Editor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit Editor (Probe& owner) : AudioProcessorEditor (owner), probe (owner)
    {
        addAndMakeVisible (status); addAndMakeVisible (path); addAndMakeVisible (finish);
        status.setJustificationType (juce::Justification::centredLeft);
        path.setText (probe.traceFile().getFullPathName(), juce::dontSendNotification);
        finish.onClick = [this] { probe.finishCapture(); };
        setSize (600, 160); timerCallback(); startTimerHz (2);
    }
    ~Editor() override { stopTimer(); }
    void resized() override
    {
        auto area = getLocalBounds().reduced (12);
        status.setBounds (area.removeFromTop (60)); path.setBounds (area.removeFromTop (28));
        finish.setBounds (area.removeFromBottom (28).removeFromLeft (160));
    }
    void paint (juce::Graphics& graphics) override { graphics.fillAll (juce::Colours::darkgrey); }
private:
    void timerCallback() override { status.setText (probe.displayStatus(), juce::dontSendNotification); }
    Probe& probe;
    juce::Label status, path;
    juce::TextButton finish { "Finish capture" };
};
juce::AudioProcessorEditor* Probe::createEditor() { return new Editor (*this); }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new gapfill::transportprobe::Probe; }
