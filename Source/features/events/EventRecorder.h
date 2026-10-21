#pragma once

// F-D02 Event capture and replay (no audio) - docs/dev/12 F-D02.
//
// Opt-in diagnostics for bug reports and regression: records parameter changes,
// commands, transport snapshots (on change), the block-size sequence and the
// outcome of every evaluated gap, plus (in the file header) the sample rate,
// plugin/state version and the initial parameter state. Never audio, never paths.
//
// Threading: ONE producer (the audio thread) writes fixed 24-byte records into a
// preallocated SPSC ring; the message thread drains the ring into a bounded
// store. Parameter edits are sampled on the audio thread at each block start (a
// diff against the last seen values), which is block-exact and avoids a second
// producer (host automation may call listeners on either thread). While not
// recording, every hook costs one relaxed atomic load.

#include "../../dsp/EngineEvents.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <cstring>
#include <memory>
#include <vector>

namespace gapfill::events
{

enum class RecType : std::uint8_t
{
    None = 0,
    ParamChange = 1,   ///< index = parameter index, p0 = real value (float bits)
    Command = 2,       ///< flags = CommandType, index = Command::index, p0 = velocity
    Transport = 3,     ///< flags bit0 = valid, index = tsNum << 8 | tsDen, p0 = bpm (float bits), pos = ppq * 1e6
    BlockSize = 4,     ///< p0 = host block size in samples (written when it changes)
    Outcome = 5        ///< flags bit0 = started, index = reason | algo << 8, pos = decision sample, p0/p1 = avail/need ms
};

inline constexpr std::uint8_t kLastRecType = (std::uint8_t) RecType::Outcome;

/** Exactly 24 bytes; serialised little-endian field by field (never as raw memory). */
struct Record
{
    std::uint8_t type = 0;
    std::uint8_t flags = 0;
    std::uint16_t index = 0;
    std::uint32_t block = 0;      ///< host block counter since recording started
    std::int64_t pos = 0;
    std::uint32_t p0 = 0;
    std::uint32_t p1 = 0;
};

static_assert (std::is_trivially_copyable_v<Record>);

inline constexpr int kRecordBytes = 24;
inline constexpr int kRingCapacity = 65536;
/** The recording stops here so a saved file always stays below 2 MiB
    (60,000 x 24 B x 4/3 base64 = 1.92 MB plus the header). */
inline constexpr int kRecordCap = 60000;
inline constexpr std::size_t kMaxFileBytes = 2u * 1024u * 1024u;
inline constexpr int kMaxParams = 1024;

void toBytes (const Record&, std::uint8_t* out24) noexcept;
Record fromBytes (const std::uint8_t* in24) noexcept;

inline std::uint32_t floatBits (float f) noexcept   { std::uint32_t u; std::memcpy (&u, &f, 4); return u; }
inline float bitsFloat (std::uint32_t u) noexcept   { float f; std::memcpy (&f, &u, 4); return f; }

//==============================================================================
/** Everything a .gfevents file holds. */
struct ReplayData
{
    juce::String pluginVersion;
    int stateVersion = 0;
    double sampleRate = 48000.0;
    int preparedBlockSize = 512;
    int numParams = 0;
    int numBlocks = 0;
    bool incomplete = false;          ///< the recording hit its cap
    juce::String initialState;        ///< the APVTS root as XML text
    std::vector<Record> records;
};

/** Serialises to the .gfevents text (XML header + base64 records). */
juce::String toFileText (const ReplayData&);

/** Parses and validates; never throws, never trusts sizes. */
juce::Result fromFileText (const juce::String& text, ReplayData& out);

juce::Result saveToFile (const ReplayData&, const juce::File&);
juce::Result loadFromFile (const juce::File&, ReplayData& out);

//==============================================================================
class EventRecorder : public FillEventTap
{
public:
    EventRecorder();
    ~EventRecorder() override;

    /** Constructor-time / prepare-time (message thread, before the audio thread
        runs): the raw parameter atomics to diff, in the processor's parameter order. */
    void attachParameters (std::vector<std::atomic<float>*> raw);
    int getNumParameters() const noexcept   { return (int) params.size(); }

    //--- message thread ------------------------------------------------------
    /** Begins a recording. Returns false while one is running. */
    bool start (const juce::String& initialStateXml, double sampleRate, int preparedBlockSize,
                const juce::String& pluginVersion, int stateVersion);
    void stop() noexcept;

    bool isRecording() const noexcept       { return active.load (std::memory_order_relaxed); }
    bool isFull() const noexcept            { return full.load (std::memory_order_relaxed); }

    /** Moves records from the audio ring into the store. Timer / save / stop. */
    void drain();

    int getNumRecords() const noexcept      { return (int) store.size(); }
    std::size_t getEstimatedFileBytes() const noexcept;

    /** Drains and returns everything recorded so far. */
    ReplayData snapshot();

    //--- audio thread --------------------------------------------------------
    struct Chunk
    {
        int numSamples = 0;
        bool blockStart = true;        ///< first chunk of a host block
        bool transportValid = false;
        double ppq = 0.0;
        double bpm = 120.0;
        int tsNum = 0, tsDen = 0;
    };

    void onChunk (const Chunk&) noexcept;
    void onCommand (const Command&) noexcept;

    bool wantsAll() const noexcept override   { return isRecording(); }
    void onFillEvent (const FillEvent&) noexcept override;

private:
    void push (const Record&) noexcept;

    struct Ring;
    std::unique_ptr<Ring> ring;
    std::vector<Record> store;

    std::vector<std::atomic<float>*> params;
    std::vector<float> lastParam;                     // audio thread while recording

    std::atomic<bool> active { false };
    std::atomic<bool> full { false };
    std::atomic<std::uint32_t> produced { 0 };
    std::atomic<std::uint32_t> blockIndex { 0 };

    // audio-thread-only state while recording (reset by start() before activation)
    int lastBlockSize = -1;
    bool lastValid = false;
    double lastBpm = 0.0, lastPpq = 0.0;
    int lastTs = -1;
    int lastChunkSamples = 0;

    // header, written by start()
    juce::String headerState, headerVersion;
    int headerStateVersion = 0, headerBlock = 512;
    double headerRate = 48000.0;
};

} // namespace gapfill::events
