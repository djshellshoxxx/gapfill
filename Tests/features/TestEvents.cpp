// F-D02 Event capture and replay (docs/dev/12 F-D02).

#include "../support/AllocGuard.h"
#include "../support/TestRig.h"

#include "features/events/EventRecorder.h"
#include "features/events/Replay.h"

#include <thread>

using namespace gapfill;
using namespace gapfill::events;
using namespace gftest;

namespace
{
    juce::String stateXmlOf (GapFillAudioProcessor& proc)
    {
        juce::MemoryBlock block;
        proc.getStateInformation (block);
        const auto xml = juce::AudioProcessor::getXmlFromBinary (block.getData(), (int) block.getSize());
        return xml != nullptr ? xml->toString() : juce::String();
    }

    struct Session
    {
        ReplayData data;
        juce::AudioBuffer<float> audio;
        int fills = 0;
    };

    /** Records a real session: parameter edits, a command and a tempo change mid-way. */
    Session recordSession (int blockSize = 512, int bars = 6)
    {
        Session s;
        GapFillAudioProcessor proc;
        setParams (proc, { { "fill", 70.0f }, { "density", 80.0f } });
        s.audio = drumLoop (goldenSampleRate, goldenBpm, bars);

        auto& rec = proc.getExplainState().recorder;
        rec.start (stateXmlOf (proc), goldenSampleRate, blockSize, "test", 2);

        TestPlayHead ph;
        ph.bpm = goldenBpm;
        RenderOptions o;
        o.playHead = &ph;
        o.onBlock = [&] (int block, int)
        {
            if (block == 120)
                setParams (proc, { { "fill", 100.0f }, { "minGap", 40.0f } });

            if (block == 200)
            {
                Command c;
                c.type = CommandType::Preview;
                proc.postCommand (c);
            }

            if (block == 260)
            {
                ph.bpm = 120.0;                  // a tempo change shows up as a Transport record
            }

            if (block == 300)
                setParams (proc, { { "density", 20.0f } });
        };

        renderOffline (proc, s.audio, blockSize, goldenSampleRate, o);
        rec.stop();
        s.data = rec.snapshot();
        s.fills = proc.getFillEngine().getFillsStarted();
        return s;
    }

    int countType (const ReplayData& d, RecType t)
    {
        int n = 0;

        for (const auto& r : d.records)
            n += r.type == (std::uint8_t) t;

        return n;
    }
}

GF_TEST (Events, record_layout_is_24_bytes_and_serialises_little_endian)
{
    static_assert (sizeof (Record) == 24);
    Record r;
    r.type = 5;
    r.flags = 1;
    r.index = 0x0A0B;
    r.block = 0x01020304;
    r.pos = -2;
    r.p0 = 0xAABBCCDD;
    r.p1 = 7;

    std::uint8_t bytes[kRecordBytes];
    toBytes (r, bytes);
    GF_EXPECT (bytes[2] == 0x0B && bytes[3] == 0x0A && bytes[4] == 0x04 && bytes[7] == 0x01);
    GF_EXPECT (bytes[8] == 0xFE && bytes[15] == 0xFF);

    const auto back = fromBytes (bytes);
    GF_EXPECT (back.type == 5 && back.flags == 1 && back.index == 0x0A0B && back.block == 0x01020304
               && back.pos == -2 && back.p0 == 0xAABBCCDD && back.p1 == 7);
}

GF_TEST (Events, record_off_zero_cost)
{
    GapFillAudioProcessor proc;
    auto& rec = proc.getExplainState().recorder;
    GF_EXPECT (! rec.isRecording() && ! rec.wantsAll());

    // Hooks while off: nothing is stored, nothing allocates.
    EventRecorder::Chunk chunk;
    chunk.numSamples = 512;
    Command cmd;
    cmd.type = CommandType::Preview;
    FillEvent ev;
    ev.type = FillEventType::Rejected;

    AllocGuard guard;

    for (int i = 0; i < 1000; ++i)
    {
        rec.onChunk (chunk);
        rec.onCommand (cmd);
        rec.onFillEvent (ev);
    }

    GF_EXPECT (guard.count() == 0);
    rec.drain();
    GF_EXPECT (rec.getNumRecords() == 0);

    // A whole session with the recorder off produces no records either.
    renderGoldenScenarioA (proc);
    rec.drain();
    GF_EXPECT (rec.getNumRecords() == 0);
}

GF_TEST (Events, recording_does_not_change_the_sound)
{
    GapFillAudioProcessor plain, recorded;
    setParams (plain, { { "fill", 70.0f } });
    setParams (recorded, { { "fill", 70.0f } });
    recorded.getExplainState().recorder.start (stateXmlOf (recorded), goldenSampleRate, goldenBlockSize, "test", 2);

    const auto a = renderGoldenScenarioA (plain);
    const auto b = renderGoldenScenarioA (recorded);
    GF_EXPECT (compareBuffers (a.out, b.out, 0.0).firstBad < 0);
    GF_EXPECT (recorded.getExplainState().recorder.snapshot().records.size() > 5);
}

GF_TEST (Events, session_contains_params_commands_transport_blocksize_and_outcomes)
{
    const auto s = recordSession();
    GF_EXPECT (countType (s.data, RecType::ParamChange) >= 3);       // fill+minGap at 120, density at 300
    GF_EXPECT (countType (s.data, RecType::Command) == 1);
    GF_EXPECT (countType (s.data, RecType::Transport) >= 2);         // start + tempo change
    GF_EXPECT (countType (s.data, RecType::BlockSize) == 1);         // constant 512
    GF_EXPECT (countType (s.data, RecType::Outcome) > 5);
    GF_EXPECT (s.data.numBlocks > 300 && ! s.data.incomplete);
    GF_EXPECT (s.data.numParams > 60);

    // Records are ordered by block.
    for (size_t i = 1; i < s.data.records.size(); ++i)
        GF_EXPECT (s.data.records[i].block >= s.data.records[i - 1].block);

    // The outcome of every started fill is in the recording.
    int started = 0;

    for (const auto& r : s.data.records)
        started += r.type == (std::uint8_t) RecType::Outcome && (r.flags & 1) != 0;

    GF_EXPECT (started == s.fills);
}

GF_TEST (Events, ring_full_stops_with_flag)
{
    GapFillAudioProcessor proc;
    auto& rec = proc.getExplainState().recorder;
    rec.start ("<s/>", 48000.0, 512, "t", 2);

    Command cmd;
    cmd.type = CommandType::Roulette;

    for (int i = 0; i < kRecordCap + 500; ++i)
        rec.onCommand (cmd);

    GF_EXPECT (rec.isFull() && ! rec.isRecording());
    rec.drain();
    GF_EXPECT (rec.getNumRecords() == kRecordCap);
    GF_EXPECT (rec.snapshot().incomplete);

    // The stored maximum still fits the 2 MiB file limit.
    const auto text = toFileText (rec.snapshot());
    GF_EXPECT ((size_t) text.getNumBytesAsUTF8() < kMaxFileBytes);
    ReplayData back;
    GF_EXPECT (fromFileText (text, back).wasOk());
    GF_EXPECT ((int) back.records.size() == kRecordCap && back.incomplete);

    // Stopping and starting again begins a clean recording.
    GF_EXPECT (rec.start ("<s/>", 48000.0, 512, "t", 2));
    GF_EXPECT (rec.isRecording() && ! rec.isFull() && rec.getNumRecords() == 0);
    GF_EXPECT (! rec.start ("<s/>", 48000.0, 512, "t", 2));       // already running
}

GF_TEST (Events, file_roundtrip)
{
    const auto s = recordSession();
    const auto text = toFileText (s.data);

    ReplayData back;
    GF_EXPECT (fromFileText (text, back).wasOk());
    GF_EXPECT (back.records.size() == s.data.records.size());

    for (size_t i = 0; i < back.records.size(); ++i)
    {
        const auto& a = back.records[i];
        const auto& b = s.data.records[i];
        GF_EXPECT (a.type == b.type && a.flags == b.flags && a.index == b.index && a.block == b.block
                   && a.pos == b.pos && a.p0 == b.p0 && a.p1 == b.p1);
    }

    GF_EXPECT (back.sampleRate == s.data.sampleRate && back.preparedBlockSize == s.data.preparedBlockSize
               && back.numBlocks == s.data.numBlocks && back.numParams == s.data.numParams
               && back.stateVersion == s.data.stateVersion && back.pluginVersion == s.data.pluginVersion);
    GF_EXPECT (back.initialState.trim() == s.data.initialState.trim());

    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("gapfill_events_test.gfevents");
    GF_EXPECT (saveToFile (s.data, file).wasOk());
    ReplayData loaded;
    GF_EXPECT (loadFromFile (file, loaded).wasOk());
    GF_EXPECT (loaded.records.size() == s.data.records.size());
    file.deleteFile();
}

GF_TEST (Events, replay_reproduces_outcomes)
{
    const auto s = recordSession();
    const auto recorded = outcomesOf (s.data);
    GF_EXPECT (recorded.size() > 5);

    const auto r = replay (s.data, s.audio);
    GF_EXPECT_MSG (r.ran, r.error);
    GF_EXPECT (r.blocksReplayed == s.data.numBlocks || r.blocksReplayed >= s.data.numBlocks - 1);
    GF_EXPECT_MSG (r.firstDivergence < 0, r.divergenceText);
    GF_EXPECT (r.outcomes == recorded);

    // Determinism: replaying the same recording again gives the same sequence.
    const auto again = replay (s.data, s.audio);
    GF_EXPECT (again.outcomes == r.outcomes);
}

GF_TEST (Events, replay_reports_the_first_divergence)
{
    auto s = recordSession();

    // Drop the mid-session parameter edits: the replayed sound is different from the recorded one.
    auto altered = s.data;
    altered.records.erase (std::remove_if (altered.records.begin(), altered.records.end(),
                                           [] (const Record& r) { return r.type == (std::uint8_t) RecType::ParamChange; }),
                           altered.records.end());

    const auto r = replay (altered, s.audio);
    GF_EXPECT (r.ran);
    GF_EXPECT (r.firstDivergence >= 0);
    GF_EXPECT (r.divergenceText.startsWith ("Gap "));

    // Different audio also diverges.
    auto silence = s.audio;
    silence.clear();
    const auto q = replay (s.data, silence);
    GF_EXPECT (q.firstDivergence >= 0 || q.outcomes.empty());
}

GF_TEST (Events, replay_with_variable_block_sizes)
{
    // The recorded block-size sequence is honoured: a 64-sample session replays identically.
    const auto s = recordSession (64, 2);
    const auto r = replay (s.data, s.audio);
    GF_EXPECT_MSG (r.ran, r.error);
    GF_EXPECT_MSG (r.firstDivergence < 0, r.divergenceText);
}

GF_TEST (Events, rejects_oversize_malformed)
{
    ReplayData d;
    GF_EXPECT (! fromFileText ("", d).wasOk());
    GF_EXPECT (! fromFileText ("not xml at all", d).wasOk());
    GF_EXPECT (! fromFileText ("<Other/>", d).wasOk());
    GF_EXPECT (! fromFileText (juce::String::repeatedString ("x", 3 * 1024 * 1024), d).wasOk());

    const auto good = toFileText (recordSession (512, 2).data);
    GF_EXPECT (fromFileText (good, d).wasOk());

    auto mutate = [&] (std::function<void (juce::XmlElement&)> f)
    {
        auto xml = juce::parseXML (good);
        f (*xml);
        ReplayData out;
        return fromFileText (xml->toString(), out).wasOk();
    };

    GF_EXPECT (! mutate ([] (juce::XmlElement& x) { x.setAttribute ("format", 2); }));
    GF_EXPECT (! mutate ([] (juce::XmlElement& x) { x.setAttribute ("sampleRate", 5.0); }));
    GF_EXPECT (! mutate ([] (juce::XmlElement& x) { x.setAttribute ("blockSize", 0); }));
    GF_EXPECT (! mutate ([] (juce::XmlElement& x) { x.setAttribute ("params", 100000); }));
    GF_EXPECT (! mutate ([] (juce::XmlElement& x) { x.setAttribute ("records", 100000); }));
    GF_EXPECT (! mutate ([] (juce::XmlElement& x) { x.setAttribute ("records", 3); }));            // count mismatch
    GF_EXPECT (! mutate ([] (juce::XmlElement& x) { x.removeChildElement (x.getChildByName ("Records"), true); }));
    GF_EXPECT (! mutate ([] (juce::XmlElement& x) { x.getChildByName ("Records")->deleteAllTextElements();
                                                     x.getChildByName ("Records")->addTextElement ("!!!not base64!!!"); }));

    // Unknown record type / out-of-order blocks / bad parameter index / absurd block size.
    auto withRecords = [&] (std::function<void (std::vector<Record>&)> f)
    {
        ReplayData x = recordSession (512, 2).data;
        f (x.records);
        ReplayData out;
        return fromFileText (toFileText (x), out).wasOk();
    };

    GF_EXPECT (! withRecords ([] (std::vector<Record>& r) { r.push_back ({ 99, 0, 0, 1000000, 0, 0, 0 }); }));
    GF_EXPECT (! withRecords ([] (std::vector<Record>& r) { r.back().block = 0; r.insert (r.begin(), Record { 4, 0, 0, 5, 0, 512, 0 }); }));
    GF_EXPECT (! withRecords ([] (std::vector<Record>& r) { r.push_back ({ (std::uint8_t) RecType::ParamChange, 0, 60000, 100000, 0, 0, 0 }); }));
    GF_EXPECT (! withRecords ([] (std::vector<Record>& r) { r.push_back ({ (std::uint8_t) RecType::BlockSize, 0, 0, 100000, 0, 0, 0 }); }));
    GF_EXPECT (! withRecords ([] (std::vector<Record>& r) { r.push_back ({ (std::uint8_t) RecType::Command, 200, 0, 100000, 0, 0, 0 }); }));

    // And replay of a rejected/empty recording does not run.
    ReplayData empty;
    GF_EXPECT (! replay (empty, juce::AudioBuffer<float> (2, 100)).ran);
}

GF_TEST (Events, contains_no_audio_or_paths)
{
    const auto s = recordSession();
    const auto text = toFileText (s.data);

    for (const char* forbidden : { "/home", "/Users", "/tmp", "C:\\", "\\\\", ".wav", ".aif", ".flac", ".mp3", "file://" })
        GF_EXPECT_MSG (! text.containsIgnoreCase (forbidden), forbidden);

    // Audio would be megabytes; this is parameters, a handful of commands and outcomes.
    const auto audioBytes = (size_t) s.audio.getNumSamples() * 2 * sizeof (float);
    GF_EXPECT ((size_t) text.getNumBytesAsUTF8() < audioBytes / 20);

    // Only these record types exist; none of them can carry samples.
    for (const auto& r : s.data.records)
        GF_EXPECT (r.type >= 1 && r.type <= kLastRecType);

    // The header stores parameters, not file locations.
    GF_EXPECT (! s.data.initialState.containsIgnoreCase ("path") || ! s.data.initialState.containsIgnoreCase ("/"));
}

GF_TEST (Events, records_ordered_across_threads)
{
    // One producer (the audio role) fills the ring while the message thread drains it.
    EventRecorder rec;
    rec.start ("<s/>", 48000.0, 512, "t", 2);

    constexpr int total = 40000;
    std::atomic<bool> done { false };

    std::thread producer ([&]
    {
        Command cmd;
        cmd.type = CommandType::Roulette;

        for (int i = 0; i < total; ++i)
        {
            cmd.index = (std::int16_t) (i & 0x7FFF);
            rec.onCommand (cmd);
        }

        done.store (true);
    });

    while (! done.load())
    {
        rec.drain();
        std::this_thread::yield();
    }

    producer.join();
    const auto d = rec.snapshot();
    GF_EXPECT ((int) d.records.size() == total);

    for (int i = 0; i < total; ++i)
        GF_EXPECT (d.records[(size_t) i].index == (std::uint16_t) (i & 0x7FFF));
}

GF_TEST (Events, command_29_toggles_recording_on_the_message_thread)
{
    GapFillAudioProcessor proc;
    auto& rec = proc.getExplainState().recorder;
    GF_EXPECT (! rec.isRecording());

    Command toggle;
    toggle.type = CommandType::EventRecordToggle;
    GF_EXPECT (proc.postCommand (toggle));

    renderOffline (proc, drumLoop (goldenSampleRate, goldenBpm, 1), 512, goldenSampleRate);   // drains the command on the audio thread
    GF_EXPECT (! rec.isRecording());                    // the audio thread only raised the request
    proc.flushLatencyReportForTests();                  // the timer acts on it
    GF_EXPECT (rec.isRecording());

    GF_EXPECT (proc.postCommand (toggle));
    renderOffline (proc, drumLoop (goldenSampleRate, goldenBpm, 1), 512, goldenSampleRate);
    proc.flushLatencyReportForTests();
    GF_EXPECT (! rec.isRecording());
    GF_EXPECT (rec.getNumRecords() > 0 || rec.snapshot().records.size() > 0);
}
