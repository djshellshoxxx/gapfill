#include "CommandMap.h"

namespace gapfill::commands
{

//==============================================================================
bool isKnownCommand (CommandType c) noexcept
{
    const auto v = (int) c;
    return v >= 1 && v <= 30;
}

const char* commandName (CommandType c) noexcept
{
    static const char* const names[] =
    {
        "None", "Preview", "Like latest", "Like recipe", "Lock latest", "Lock recipe", "Unlock", "Recapture",
        "Release capture", "Panic", "Recover", "Go to scene", "Next scene", "Previous scene", "Cancel scene change",
        "Next preset", "Previous preset", "Stutter Storm", "Glitch Roulette", "Fill Now", "Momentary stutter",
        "Momentary freeze", "Momentary reverse", "Momentary tape stop", "Spectral capture", "Spectral release",
        "Groove learn", "Groove reset", "A/B swap", "Event recording", "Preview recipe"
    };

    return isKnownCommand (c) ? names[(int) c] : "Unknown";
}

bool isValid (const Binding& b) noexcept
{
    if (! isKnownCommand (b.cmd))
        return false;

    if ((int) b.type > (int) MsgType::ProgramChange || (int) b.mode > (int) BindMode::Toggle)
        return false;

    return b.channel <= 16 && b.number <= 127 && b.threshold >= 9 && b.threshold <= 127;
}

bool sameMessage (const Binding& a, const Binding& b) noexcept
{
    if (a.type != b.type || a.number != b.number)
        return false;

    return a.channel == 0 || b.channel == 0 || a.channel == b.channel;
}

//==============================================================================
BindingModel::BindingModel()
{
    pub.publish (table);
}

int BindingModel::findOwner (const Binding& b) const noexcept
{
    for (int i = 0; i < table.count; ++i)
        if (sameMessage (table.bindings[i], b))
            return i;

    return -1;
}

int BindingModel::findCommand (CommandType cmd, int arg) const noexcept
{
    for (int i = 0; i < table.count; ++i)
        if (table.bindings[i].cmd == cmd && table.bindings[i].arg == arg)
            return i;

    return -1;
}

BindingModel::AddResult BindingModel::add (const Binding& b, int* ownerIndex)
{
    if (! isValid (b))
        return AddResult::Invalid;

    if (table.count >= kMaxBindings)
        return AddResult::Full;

    if (const auto owner = findOwner (b); owner >= 0)
    {
        if (ownerIndex != nullptr)
            *ownerIndex = owner;

        return AddResult::Duplicate;
    }

    table.bindings[table.count++] = b;
    publish();
    return AddResult::Ok;
}

void BindingModel::removeAt (int index)
{
    if (index < 0 || index >= table.count)
        return;

    for (int i = index; i < table.count - 1; ++i)
        table.bindings[i] = table.bindings[i + 1];

    table.bindings[--table.count] = Binding {};
    publish();
}

void BindingModel::clear()
{
    const auto pcOn = table.pcSceneEnabled;
    const auto pcCh = table.pcSceneChannel;
    profileName.clear();
    table = BindingTable {};
    table.pcSceneEnabled = pcOn;
    table.pcSceneChannel = pcCh;
    publish();
}

void BindingModel::setProgramChangeScenes (bool enabled, int channel) noexcept
{
    table.pcSceneEnabled = enabled ? 1 : 0;
    table.pcSceneChannel = (std::uint8_t) juce::jlimit (0, 16, channel);
    publish();
}

//------------------------------------------------------------------------------
juce::ValueTree BindingModel::toValueTree() const
{
    juce::ValueTree t (subtreeName);
    t.setProperty ("v", 1, nullptr);
    t.setProperty ("pcScene", (int) table.pcSceneEnabled, nullptr);
    t.setProperty ("pcCh", (int) table.pcSceneChannel, nullptr);

    if (! feedbackDevice.empty())
        t.setProperty ("dev", juce::String (feedbackDevice), nullptr);

    if (! profileName.empty())
        t.setProperty ("profile", juce::String (profileName), nullptr);

    for (int i = 0; i < table.count; ++i)
    {
        const auto& b = table.bindings[i];
        juce::ValueTree n ("B");
        n.setProperty ("c", (int) b.cmd, nullptr);
        n.setProperty ("a", (int) b.arg, nullptr);
        n.setProperty ("t", (int) b.type, nullptr);
        n.setProperty ("ch", (int) b.channel, nullptr);
        n.setProperty ("n", (int) b.number, nullptr);
        n.setProperty ("m", (int) b.mode, nullptr);
        n.setProperty ("th", (int) b.threshold, nullptr);
        t.appendChild (n, nullptr);
    }

    return t;
}

bool BindingModel::validateTree (const juce::ValueTree& t)
{
    // Per-binding problems are handled by dropping the binding alone; the
    // subtree is only rejected when it is not ours or absurdly large.
    return t.hasType (subtreeName) && t.getNumChildren() <= 4 * kMaxBindings;
}

void BindingModel::restore (const juce::ValueTree& t)
{
    BindingTable fresh;

    if (t.hasType (subtreeName))
    {
        fresh.pcSceneEnabled = (std::uint8_t) (((int) t.getProperty ("pcScene", 0)) != 0 ? 1 : 0);
        fresh.pcSceneChannel = (std::uint8_t) juce::jlimit (0, 16, (int) t.getProperty ("pcCh", 0));
        feedbackDevice = t.getProperty ("dev", "").toString().substring (0, 96).toStdString();
        profileName = t.getProperty ("profile", "").toString().substring (0, 48).toStdString();

        for (const auto& n : t)
        {
            if (fresh.count >= kMaxBindings)
                break;

            if (! n.hasType ("B"))
                continue;

            Binding b;
            b.cmd = (CommandType) juce::jlimit (0, 255, (int) n.getProperty ("c", 0));
            b.arg = (std::int16_t) juce::jlimit (-32768, 32767, (int) n.getProperty ("a", 0));
            b.type = (MsgType) juce::jlimit (0, 255, (int) n.getProperty ("t", 0));
            b.channel = (std::uint8_t) juce::jlimit (0, 255, (int) n.getProperty ("ch", 0));
            b.number = (std::uint8_t) juce::jlimit (0, 255, (int) n.getProperty ("n", 0));
            b.mode = (BindMode) juce::jlimit (0, 255, (int) n.getProperty ("m", 0));
            b.threshold = (std::uint8_t) juce::jlimit (0, 255, (int) n.getProperty ("th", 64));

            if (! isValid (b))
                continue;

            bool duplicate = false;

            for (int i = 0; i < fresh.count; ++i)
                duplicate = duplicate || sameMessage (fresh.bindings[i], b);

            if (! duplicate)
                fresh.bindings[fresh.count++] = b;
        }
    }
    else
    {
        feedbackDevice.clear();
        profileName.clear();
    }

    table = fresh;
    publish();
}

//==============================================================================
void Scanner::fire (int i, bool on, int offset, Event* out, int& n, int maxOut) const noexcept
{
    if (n >= maxOut)
        return;

    auto& e = out[n++];
    e.type = table.bindings[i].cmd;
    e.arg = table.bindings[i].arg;
    e.on = on;
    e.sampleOffset = offset;
}

int Scanner::adoptTable (const BindingTable& t, Event* out, int maxOut) noexcept
{
    int n = 0;

    // A momentary command that was held must not stay stuck after an edit.
    for (int i = 0; i < table.count; ++i)
        if (held[i])
            fire (i, false, 0, out, n, maxOut);

    table = t;

    for (int i = 0; i < kMaxBindings; ++i)
    {
        armed[i] = true;
        held[i] = false;
        toggled[i] = false;
    }

    return n;
}

int Scanner::releaseAll (Event* out, int maxOut) noexcept
{
    int n = 0;

    for (int i = 0; i < table.count; ++i)
    {
        if (held[i])
            fire (i, false, 0, out, n, maxOut);

        held[i] = false;
        toggled[i] = false;
    }

    return n;
}

void Scanner::armLearn (const LearnTarget& t) noexcept
{
    const std::uint32_t packed = 0x80000000u | ((std::uint32_t) t.cmd << 16)
                               | ((std::uint32_t) t.mode << 24) | (std::uint16_t) t.arg;
    learnPacked.store (packed, std::memory_order_release);
}

void Scanner::cancelLearn() noexcept
{
    learnPacked.store (0, std::memory_order_release);
}

int Scanner::process (const juce::MidiBuffer& midi, Event* out, int maxOut) noexcept
{
    int n = 0;
    int inspected = 0;

    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        const bool isCC = msg.isController();
        const bool isNoteOn = msg.isNoteOn();
        const bool isNoteOff = msg.isNoteOff();
        const bool isPC = msg.isProgramChange();

        if (! (isCC || isNoteOn || isNoteOff || isPC))
            continue;

        if (++inspected > kMaxMidiEventsPerBlock)
        {
            droppedMidi.fetch_add (1, std::memory_order_relaxed);
            continue;
        }

        const int offset = meta.samplePosition;
        const int channel = msg.getChannel();
        const int number = isCC ? msg.getControllerNumber() : isPC ? msg.getProgramChangeNumber() : msg.getNoteNumber();
        const int value = isCC ? msg.getControllerValue() : isNoteOn ? (int) msg.getVelocity() : 0;

        // Learn captures the next message and swallows it.
        if (const auto packed = learnPacked.load (std::memory_order_acquire); packed != 0
            && (isCC || isNoteOn || isPC))
        {
            Binding b;
            b.cmd = (CommandType) ((packed >> 16) & 0xFFu);
            b.mode = (BindMode) ((packed >> 24) & 0x3u);
            b.arg = (std::int16_t) (packed & 0xFFFFu);
            b.type = isCC ? MsgType::CC : isPC ? MsgType::ProgramChange : MsgType::Note;
            b.channel = (std::uint8_t) channel;
            b.number = (std::uint8_t) number;
            b.threshold = 64;
            learned.push (b);
            learnPacked.store (0, std::memory_order_release);
            continue;
        }

        // Program Change -> scene n (N-VAL).
        if (isPC && table.pcSceneEnabled != 0 && number < 64
            && (table.pcSceneChannel == 0 || table.pcSceneChannel == channel))
        {
            if (n < maxOut)
            {
                auto& e = out[n++];
                e.type = CommandType::SceneGoto;
                e.arg = (std::int16_t) number;
                e.on = true;
                e.sampleOffset = offset;
            }
        }

        for (int i = 0; i < table.count; ++i)
        {
            const auto& b = table.bindings[i];

            if (b.number != number || (b.channel != 0 && b.channel != channel))
                continue;

            if (b.type == MsgType::CC && isCC)
            {
                if (value >= b.threshold)
                {
                    if (armed[i])
                    {
                        armed[i] = false;

                        if (b.mode == BindMode::Toggle)
                        {
                            toggled[i] = ! toggled[i];
                            fire (i, toggled[i], offset, out, n, maxOut);
                        }
                        else
                        {
                            held[i] = b.mode == BindMode::Momentary;
                            fire (i, true, offset, out, n, maxOut);
                        }
                    }
                }
                else
                {
                    if (held[i])
                    {
                        held[i] = false;
                        fire (i, false, offset, out, n, maxOut);
                    }

                    if (value < (int) b.threshold - 8)
                        armed[i] = true;
                }
            }
            else if (b.type == MsgType::Note && (isNoteOn || isNoteOff))
            {
                const bool press = isNoteOn && value > 0;

                if (press)
                {
                    if (b.mode == BindMode::Toggle)
                    {
                        toggled[i] = ! toggled[i];
                        fire (i, toggled[i], offset, out, n, maxOut);
                    }
                    else
                    {
                        held[i] = b.mode == BindMode::Momentary;
                        fire (i, true, offset, out, n, maxOut);
                    }
                }
                else if (held[i])
                {
                    held[i] = false;
                    fire (i, false, offset, out, n, maxOut);
                }
            }
            else if (b.type == MsgType::ProgramChange && isPC)
            {
                fire (i, true, offset, out, n, maxOut);
            }
        }
    }

    return n;
}

} // namespace gapfill::commands
