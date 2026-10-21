#include "EventRecorder.h"
#include "../../dsp/SpscQueue.h"

#include <cmath>
#include <cstring>

namespace gapfill::events
{

//==============================================================================
struct EventRecorder::Ring
{
    SpscQueue<Record, kRingCapacity> queue;
};

namespace
{
    void put16 (std::uint8_t* p, std::uint16_t v) noexcept { p[0] = (std::uint8_t) v; p[1] = (std::uint8_t) (v >> 8); }

    void put32 (std::uint8_t* p, std::uint32_t v) noexcept
    {
        for (int i = 0; i < 4; ++i)
            p[i] = (std::uint8_t) (v >> (8 * i));
    }

    void put64 (std::uint8_t* p, std::uint64_t v) noexcept
    {
        for (int i = 0; i < 8; ++i)
            p[i] = (std::uint8_t) (v >> (8 * i));
    }

    std::uint16_t get16 (const std::uint8_t* p) noexcept { return (std::uint16_t) (p[0] | (p[1] << 8)); }

    std::uint32_t get32 (const std::uint8_t* p) noexcept
    {
        std::uint32_t v = 0;

        for (int i = 0; i < 4; ++i)
            v |= (std::uint32_t) p[i] << (8 * i);

        return v;
    }

    std::uint64_t get64 (const std::uint8_t* p) noexcept
    {
        std::uint64_t v = 0;

        for (int i = 0; i < 8; ++i)
            v |= (std::uint64_t) p[i] << (8 * i);

        return v;
    }
}

void toBytes (const Record& r, std::uint8_t* out) noexcept
{
    out[0] = r.type;
    out[1] = r.flags;
    put16 (out + 2, r.index);
    put32 (out + 4, r.block);
    put64 (out + 8, (std::uint64_t) r.pos);
    put32 (out + 16, r.p0);
    put32 (out + 20, r.p1);
}

Record fromBytes (const std::uint8_t* in) noexcept
{
    Record r;
    r.type = in[0];
    r.flags = in[1];
    r.index = get16 (in + 2);
    r.block = get32 (in + 4);
    r.pos = (std::int64_t) get64 (in + 8);
    r.p0 = get32 (in + 16);
    r.p1 = get32 (in + 20);
    return r;
}

//==============================================================================
EventRecorder::EventRecorder() : ring (std::make_unique<Ring>()) {}
EventRecorder::~EventRecorder() = default;

void EventRecorder::attachParameters (std::vector<std::atomic<float>*> raw)
{
    if (raw.size() > (size_t) kMaxParams)
        raw.resize ((size_t) kMaxParams);

    params = std::move (raw);
    lastParam.assign (params.size(), 0.0f);
}

bool EventRecorder::start (const juce::String& initialStateXml, double sampleRate, int preparedBlockSize,
                           const juce::String& pluginVersion, int stateVersion)
{
    if (isRecording())
        return false;

    ring->queue.reset();
    store.clear();
    store.reserve ((size_t) kRecordCap);

    // One canonical single-line form, so the text survives the file round trip byte for byte.
    if (const auto parsed = juce::parseXML (initialStateXml))
        headerState = parsed->toString (juce::XmlElement::TextFormat().singleLine());
    else
        headerState = initialStateXml.trim();

    headerVersion = pluginVersion;
    headerStateVersion = stateVersion;
    headerBlock = preparedBlockSize;
    headerRate = sampleRate;

    for (size_t i = 0; i < params.size(); ++i)
        lastParam[i] = params[i] != nullptr ? params[i]->load (std::memory_order_relaxed) : 0.0f;

    lastBlockSize = -1;
    lastValid = false;
    lastBpm = lastPpq = 0.0;
    lastTs = -1;
    lastChunkSamples = 0;

    produced.store (0, std::memory_order_relaxed);
    blockIndex.store (0, std::memory_order_relaxed);
    full.store (false, std::memory_order_relaxed);
    active.store (true, std::memory_order_release);
    return true;
}

void EventRecorder::stop() noexcept
{
    active.store (false, std::memory_order_release);
}

void EventRecorder::drain()
{
    Record batch[256];

    for (;;)
    {
        const auto n = ring->queue.pop (batch, 256);

        if (n <= 0)
            break;

        for (int i = 0; i < n && (int) store.size() < kRecordCap; ++i)
            store.push_back (batch[i]);
    }
}

std::size_t EventRecorder::getEstimatedFileBytes() const noexcept
{
    return ((store.size() * (size_t) kRecordBytes + 2) / 3) * 4 + headerState.getNumBytesAsUTF8() + 512;
}

ReplayData EventRecorder::snapshot()
{
    drain();

    ReplayData d;
    d.pluginVersion = headerVersion;
    d.stateVersion = headerStateVersion;
    d.sampleRate = headerRate;
    d.preparedBlockSize = headerBlock;
    d.numParams = (int) params.size();
    d.numBlocks = (int) blockIndex.load (std::memory_order_relaxed);
    d.incomplete = isFull();
    d.initialState = headerState;
    d.records = store;
    return d;
}

//==============================================================================
void EventRecorder::push (const Record& r) noexcept
{
    if (produced.load (std::memory_order_relaxed) >= (std::uint32_t) kRecordCap)
    {
        full.store (true, std::memory_order_relaxed);
        active.store (false, std::memory_order_relaxed);
        return;
    }

    if (ring->queue.push (r))
        produced.fetch_add (1, std::memory_order_relaxed);
    else
    {
        full.store (true, std::memory_order_relaxed);
        active.store (false, std::memory_order_relaxed);
    }
}

void EventRecorder::onChunk (const Chunk& c) noexcept
{
    if (! active.load (std::memory_order_relaxed))
        return;

    if (! c.blockStart)
        return;      // parameters and transport are sampled once per host block

    const auto block = blockIndex.load (std::memory_order_relaxed);
    blockIndex.store (block + 1, std::memory_order_relaxed);

    // Parameter edits since the previous block (the edit becomes audible at this block).
    for (size_t i = 0; i < params.size(); ++i)
    {
        if (params[i] == nullptr)
            continue;

        const auto v = params[i]->load (std::memory_order_relaxed);

        if (floatBits (v) != floatBits (lastParam[i]) && std::isfinite (v))
        {
            lastParam[i] = v;
            Record r;
            r.type = (std::uint8_t) RecType::ParamChange;
            r.index = (std::uint16_t) i;
            r.block = block;
            r.p0 = floatBits (v);
            push (r);
        }
    }

    if (c.numSamples != lastBlockSize)
    {
        lastBlockSize = c.numSamples;
        Record r;
        r.type = (std::uint8_t) RecType::BlockSize;
        r.block = block;
        r.p0 = (std::uint32_t) c.numSamples;
        push (r);
    }

    // Transport: on change of validity / tempo / signature, or a discontinuity of the position.
    const int ts = ((juce::jlimit (0, 255, c.tsNum)) << 8) | juce::jlimit (0, 255, c.tsDen);
    bool changed = c.transportValid != lastValid || ts != lastTs
                   || (c.transportValid && std::abs (c.bpm - lastBpm) > 1.0e-6);

    if (! changed && c.transportValid)
    {
        const auto expected = lastPpq + (double) lastChunkSamples * (c.bpm / 60.0) / juce::jmax (1.0, headerRate);
        changed = std::abs (c.ppq - expected) > 1.0e-3;
    }

    if (changed)
    {
        Record r;
        r.type = (std::uint8_t) RecType::Transport;
        r.flags = c.transportValid ? 1 : 0;
        r.index = (std::uint16_t) ts;
        r.block = block;
        r.p0 = floatBits ((float) c.bpm);
        r.pos = (std::int64_t) std::llround (c.ppq * 1.0e6);
        push (r);
    }

    lastValid = c.transportValid;
    lastTs = ts;
    lastBpm = c.bpm;
    lastPpq = c.ppq;
    lastChunkSamples = c.numSamples;
}

void EventRecorder::onCommand (const Command& cmd) noexcept
{
    if (! active.load (std::memory_order_relaxed))
        return;

    Record r;
    r.type = (std::uint8_t) RecType::Command;
    r.flags = (std::uint8_t) cmd.type;
    r.index = (std::uint16_t) cmd.index;
    r.block = blockIndex.load (std::memory_order_relaxed);    // dispatched at the top of the upcoming block
    r.p0 = cmd.velocity;
    push (r);
}

void EventRecorder::onFillEvent (const FillEvent& e) noexcept
{
    if (! isGapOutcome (e.type) || ! active.load (std::memory_order_relaxed))
        return;

    const auto b = blockIndex.load (std::memory_order_relaxed);

    Record r;
    r.type = (std::uint8_t) RecType::Outcome;
    r.flags = e.type == FillEventType::Started ? 1 : 0;
    r.index = (std::uint16_t) ((std::uint16_t) e.reason | ((std::uint16_t) e.recipe.algo << 8));
    r.block = b > 0 ? b - 1 : 0;
    r.pos = e.pos;
    r.p0 = floatBits (e.availMs);
    r.p1 = floatBits (e.needMs);
    push (r);
}

//==============================================================================
juce::String toFileText (const ReplayData& d)
{
    juce::XmlElement root ("GapFillEvents");
    root.setAttribute ("format", 1);
    root.setAttribute ("pluginVersion", d.pluginVersion);
    root.setAttribute ("stateVersion", d.stateVersion);
    root.setAttribute ("sampleRate", d.sampleRate);
    root.setAttribute ("blockSize", d.preparedBlockSize);
    root.setAttribute ("params", d.numParams);
    root.setAttribute ("blocks", d.numBlocks);
    root.setAttribute ("records", (int) d.records.size());
    root.setAttribute ("incomplete", d.incomplete ? 1 : 0);

    root.createNewChildElement ("State")->addTextElement (d.initialState);

    juce::MemoryBlock bytes;
    bytes.setSize (d.records.size() * (size_t) kRecordBytes);

    for (size_t i = 0; i < d.records.size(); ++i)
        toBytes (d.records[i], static_cast<std::uint8_t*> (bytes.getData()) + i * (size_t) kRecordBytes);

    root.createNewChildElement ("Records")->addTextElement (juce::Base64::toBase64 (bytes.getData(), bytes.getSize()));
    return root.toString (juce::XmlElement::TextFormat().singleLine());
}

juce::Result fromFileText (const juce::String& text, ReplayData& out)
{
    if ((std::size_t) text.getNumBytesAsUTF8() > kMaxFileBytes)
        return juce::Result::fail ("File is larger than 2 MiB");

    const auto xml = juce::parseXML (text);

    if (xml == nullptr || ! xml->hasTagName ("GapFillEvents"))
        return juce::Result::fail ("Not a GapFill event file");

    if (xml->getIntAttribute ("format", 0) != 1)
        return juce::Result::fail ("Unsupported event file format");

    ReplayData d;
    d.pluginVersion = xml->getStringAttribute ("pluginVersion").substring (0, 32);
    d.stateVersion = xml->getIntAttribute ("stateVersion", 0);
    d.sampleRate = xml->getDoubleAttribute ("sampleRate", 0.0);
    d.preparedBlockSize = xml->getIntAttribute ("blockSize", 0);
    d.numParams = xml->getIntAttribute ("params", -1);
    d.numBlocks = xml->getIntAttribute ("blocks", -1);
    d.incomplete = xml->getIntAttribute ("incomplete", 0) != 0;
    const auto declared = xml->getIntAttribute ("records", -1);

    if (! (d.sampleRate >= 8000.0 && d.sampleRate <= 384000.0) || d.preparedBlockSize < 1 || d.preparedBlockSize > 65536
         || d.numParams < 0 || d.numParams > kMaxParams || d.numBlocks < 0 || declared < 0 || declared > kRecordCap)
        return juce::Result::fail ("Event file header is invalid");

    const auto* state = xml->getChildByName ("State");
    const auto* recs = xml->getChildByName ("Records");

    if (state == nullptr || recs == nullptr)
        return juce::Result::fail ("Event file is missing a section");

    d.initialState = state->getAllSubText();

    juce::MemoryOutputStream decoded;

    if (! juce::Base64::convertFromBase64 (decoded, recs->getAllSubText().trim()))
        return juce::Result::fail ("Event records are not valid base64");

    if (decoded.getDataSize() != (size_t) declared * (size_t) kRecordBytes)
        return juce::Result::fail ("Event record count does not match the data");

    d.records.reserve ((size_t) declared);
    std::uint32_t lastBlock = 0;

    for (int i = 0; i < declared; ++i)
    {
        auto r = fromBytes (static_cast<const std::uint8_t*> (decoded.getData()) + (size_t) i * (size_t) kRecordBytes);

        if (r.type < 1 || r.type > kLastRecType)
            return juce::Result::fail ("Event file contains an unknown record type");

        if (r.block < lastBlock)
            return juce::Result::fail ("Event records are out of order");

        if (r.type == (std::uint8_t) RecType::ParamChange
             && (r.index >= d.numParams || ! std::isfinite (bitsFloat (r.p0))))
            return juce::Result::fail ("Event file contains an invalid parameter change");

        if (r.type == (std::uint8_t) RecType::BlockSize && (r.p0 < 1 || r.p0 > 65536))
            return juce::Result::fail ("Event file contains an invalid block size");

        if (r.type == (std::uint8_t) RecType::Command && r.flags > (std::uint8_t) CommandType::PreviewRecipe)
            return juce::Result::fail ("Event file contains an unknown command");

        lastBlock = r.block;
        d.records.push_back (r);
    }

    if (! d.records.empty() && (int) d.records.back().block > d.numBlocks)
        return juce::Result::fail ("Event records run past the recorded block count");

    out = std::move (d);
    return juce::Result::ok();
}

juce::Result saveToFile (const ReplayData& d, const juce::File& f)
{
    const auto text = toFileText (d);

    if ((std::size_t) text.getNumBytesAsUTF8() > kMaxFileBytes)
        return juce::Result::fail ("Recording is larger than 2 MiB");

    return f.replaceWithText (text) ? juce::Result::ok() : juce::Result::fail ("Could not write the file");
}

juce::Result loadFromFile (const juce::File& f, ReplayData& out)
{
    if (! f.existsAsFile() || (std::size_t) f.getSize() > kMaxFileBytes)
        return juce::Result::fail ("File is missing or larger than 2 MiB");

    return fromFileText (f.loadFileAsString(), out);
}

} // namespace gapfill::events
