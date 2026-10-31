#include "ControllerProfile.h"
#include "../controller/FeedbackAdapter.h"

#include <cmath>
#include <map>

namespace gapfill::commands
{

namespace
{
    const char* const kCommandKeys[] =
    {
        "None", "Preview", "LikeLatest", "LikeRecipe", "LockLatest", "LockRecipe", "Unlock", "Recapture",
        "ReleaseCapture", "Panic", "Recover", "SceneGoto", "SceneNext", "ScenePrev", "SceneCancel",
        "PresetNext", "PresetPrev", "StutterStorm", "Roulette", "FillNow", "MomentaryStutter",
        "MomentaryFreeze", "MomentaryReverse", "MomentaryTape", "SpectralCapture", "SpectralRelease",
        "GrooveLearn", "GrooveReset", "AbSwap", "EventRecordToggle", "PreviewRecipe",
        "SkipLatest", "SkipRecipe", "PresetGoto"
    };

    const char* typeKey (MsgType t)
    {
        return t == MsgType::CC ? "cc" : t == MsgType::Note ? "note" : "program";
    }

    const char* modeKey (BindMode m)
    {
        return m == BindMode::Momentary ? "momentary" : m == BindMode::Toggle ? "toggle" : "trigger";
    }

    const char* sourceKey (FeedbackSource s)
    {
        switch (s)
        {
            case FeedbackSource::Panic: return "panic";
            case FeedbackSource::SceneCurrent: return "scene.current";
            case FeedbackSource::ScenePending: return "scene.pending";
            case FeedbackSource::Bypass: return "bypass";
            case FeedbackSource::Capture: return "capture";
            case FeedbackSource::Hold: return "hold";
            case FeedbackSource::Safe: return "safe";
        }
        return "panic";
    }

    bool isNumber (const juce::var& v)
    {
        return v.isInt() || v.isInt64() || v.isDouble();
    }

    bool readInt (const juce::var& obj, const char* key, int lo, int hi, int& out, bool required, int fallback = 0)
    {
        const auto v = obj.getProperty (key, juce::var());

        if (v.isVoid())
        {
            out = fallback;
            return ! required;
        }

        if (! isNumber (v))
            return false;

        const double d = (double) v;

        if (! std::isfinite (d) || ! juce::exactlyEqual (d, std::floor (d)) || d < lo || d > hi)
            return false;

        out = (int) d;
        return true;
    }

    bool isPrintable (const juce::String& s)
    {
        for (auto c : s)
            if (c < 32 || c == 127)
                return false;

        return true;
    }

    ProfileParse fail (const juce::String& why)
    {
        ProfileParse p;
        p.error = why;
        return p;
    }

    const char* const kBuiltInPadProfile = R"json(
{ "name": "Generic pad controller (notes 36-51, channel 10)", "version": 1,
  "bindings": [
    { "command": "Panic", "type": "note", "channel": 10, "number": 36 },
    { "command": "Recover", "type": "note", "channel": 10, "number": 37 },
    { "command": "ScenePrev", "type": "note", "channel": 10, "number": 38 },
    { "command": "SceneNext", "type": "note", "channel": 10, "number": 39 },
    { "command": "SceneGoto", "arg": 0, "type": "note", "channel": 10, "number": 40 },
    { "command": "SceneGoto", "arg": 1, "type": "note", "channel": 10, "number": 41 },
    { "command": "SceneGoto", "arg": 2, "type": "note", "channel": 10, "number": 42 },
    { "command": "SceneGoto", "arg": 3, "type": "note", "channel": 10, "number": 43 },
    { "command": "SceneGoto", "arg": 4, "type": "note", "channel": 10, "number": 44 },
    { "command": "SceneGoto", "arg": 5, "type": "note", "channel": 10, "number": 45 },
    { "command": "SceneGoto", "arg": 6, "type": "note", "channel": 10, "number": 46 },
    { "command": "SceneGoto", "arg": 7, "type": "note", "channel": 10, "number": 47 },
    { "command": "MomentaryStutter", "type": "note", "channel": 10, "number": 48, "mode": "momentary" },
    { "command": "MomentaryFreeze", "type": "note", "channel": 10, "number": 49, "mode": "momentary" },
    { "command": "MomentaryReverse", "type": "note", "channel": 10, "number": 50, "mode": "momentary" },
    { "command": "MomentaryTape", "type": "note", "channel": 10, "number": 51, "mode": "momentary" }
  ],
  "feedback": [
    { "source": "panic", "type": "note", "channel": 10, "number": 36, "on": 127, "off": 0 },
    { "source": "scene.current", "arg": 0, "type": "note", "channel": 10, "number": 40, "on": 127, "off": 8 },
    { "source": "scene.current", "arg": 1, "type": "note", "channel": 10, "number": 41, "on": 127, "off": 8 },
    { "source": "scene.current", "arg": 2, "type": "note", "channel": 10, "number": 42, "on": 127, "off": 8 },
    { "source": "scene.current", "arg": 3, "type": "note", "channel": 10, "number": 43, "on": 127, "off": 8 },
    { "source": "scene.current", "arg": 4, "type": "note", "channel": 10, "number": 44, "on": 127, "off": 8 },
    { "source": "scene.current", "arg": 5, "type": "note", "channel": 10, "number": 45, "on": 127, "off": 8 },
    { "source": "scene.current", "arg": 6, "type": "note", "channel": 10, "number": 46, "on": 127, "off": 8 },
    { "source": "scene.current", "arg": 7, "type": "note", "channel": 10, "number": 47, "on": 127, "off": 8 },
    { "source": "scene.pending", "arg": 0, "type": "note", "channel": 10, "number": 40, "on": 64, "off": 8 },
    { "source": "scene.pending", "arg": 1, "type": "note", "channel": 10, "number": 41, "on": 64, "off": 8 },
    { "source": "scene.pending", "arg": 2, "type": "note", "channel": 10, "number": 42, "on": 64, "off": 8 },
    { "source": "scene.pending", "arg": 3, "type": "note", "channel": 10, "number": 43, "on": 64, "off": 8 },
    { "source": "scene.pending", "arg": 4, "type": "note", "channel": 10, "number": 44, "on": 64, "off": 8 },
    { "source": "scene.pending", "arg": 5, "type": "note", "channel": 10, "number": 45, "on": 64, "off": 8 },
    { "source": "scene.pending", "arg": 6, "type": "note", "channel": 10, "number": 46, "on": 64, "off": 8 },
    { "source": "scene.pending", "arg": 7, "type": "note", "channel": 10, "number": 47, "on": 64, "off": 8 }
  ] }
)json";

    const char* const kBuiltInCcProfile = R"json(
{ "name": "Generic CC buttons (CC 20-29, channel 1)", "version": 1,
  "bindings": [
    { "command": "Panic", "type": "cc", "channel": 1, "number": 20 },
    { "command": "Recover", "type": "cc", "channel": 1, "number": 21 },
    { "command": "ScenePrev", "type": "cc", "channel": 1, "number": 22 },
    { "command": "SceneNext", "type": "cc", "channel": 1, "number": 23 },
    { "command": "SceneCancel", "type": "cc", "channel": 1, "number": 24 },
    { "command": "Roulette", "type": "cc", "channel": 1, "number": 25 },
    { "command": "PresetPrev", "type": "cc", "channel": 1, "number": 26 },
    { "command": "PresetNext", "type": "cc", "channel": 1, "number": 27 },
    { "command": "SceneGoto", "arg": -1, "type": "cc", "channel": 1, "number": 28 }
  ],
  "feedback": [
    { "source": "panic", "type": "cc", "channel": 1, "number": 20, "on": 127, "off": 0 }
  ] }
)json";
}

//==============================================================================
const char* commandKey (CommandType c) noexcept
{
    return isKnownCommand (c) ? kCommandKeys[(int) c] : "None";
}

ProfileParse parseProfile (const juce::String& json)
{
    if (json.getNumBytesAsUTF8() > (size_t) kMaxProfileBytes)
        return fail ("The profile is larger than 8 KiB.");

    juce::var root;

    if (const auto r = juce::JSON::parse (json, root); r.failed())
        return fail ("The profile is not valid JSON.");

    if (root.getDynamicObject() == nullptr)
        return fail ("The profile must be a JSON object.");

    Profile profile;
    const auto name = root.getProperty ("name", juce::var());

    if (! name.isString() || name.toString().isEmpty() || name.toString().length() > 48 || ! isPrintable (name.toString()))
        return fail ("\"name\" must be 1 to 48 printable characters.");

    profile.name = name.toString();

    int version = 0;

    if (! readInt (root, "version", 1, 3, version, true))
        return fail ("\"version\" must be 1, 2 or 3.");

    profile.extended = version == 3;
    const auto params = root.getProperty ("params", juce::var());
    if (version < 3 && (! params.isVoid() || root.hasProperty ("parameterFeedback") || root.hasProperty ("txChannel")))
        return fail ("Parameter mappings and feedback configuration require version 3.");
    if (! params.isVoid() && ! params.isArray())
        return fail ("\"params\" must be a list.");
    if (version == 3)
    {
        const auto enabled = root.getProperty ("parameterFeedback", false);
        if (! enabled.isBool()) return fail ("\"parameterFeedback\" must be true or false.");
        profile.parameterFeedback = (bool) enabled;
        int channel = 1;
        if (! readInt (root, "txChannel", 1, 16, channel, false, 1))
            return fail ("\"txChannel\" must be 1 to 16.");
        profile.txChannel = (std::uint8_t) channel;
    }
    if (auto* arr = params.getArray())
    {
        if (arr->size() > controller::kMaxParameterBindings)
            return fail ("A profile can hold at most 64 parameter mappings.");
        for (const auto& item : *arr)
        {
            if (item.getDynamicObject() == nullptr) return fail ("Each parameter mapping must be an object.");
            const auto idValue = item.getProperty ("id", juce::var());
            const auto id = idValue.toString();
            if (! idValue.isString() || id.isEmpty() || id.length() > 32
                || ! id.containsOnly ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_"))
                return fail ("Parameter id must be 1 to 32 identifier characters.");
            int cc = 0, channel = 0;
            if (! readInt (item, "cc", 0, 127, cc, true) || ! readInt (item, "channel", 0, 16, channel, false))
                return fail ("Parameter CC must be 0 to 127 and channel 0 to 16.");
            const auto modeValue = item.getProperty ("takeover", "jump");
            const auto mode = modeValue.toString();
            if (! modeValue.isString() || (mode != "jump" && mode != "pickup"))
                return fail ("Parameter takeover must be jump or pickup.");
            for (const auto& other : profile.params)
                if (other.id == id.toStdString() || other.cc == cc)
                    return fail ("Parameter mappings repeat an id or CC.");
            profile.params.push_back ({ id.toStdString(), (std::uint8_t) cc, (std::uint8_t) channel,
                mode == "pickup" ? controller::TakeoverMode::Pickup : controller::TakeoverMode::Jump });
        }
    }

    const auto bindings = root.getProperty ("bindings", juce::var());
    const auto feedback = root.getProperty ("feedback", juce::var());

    if (! bindings.isVoid() && ! bindings.isArray())
        return fail ("\"bindings\" must be a list.");

    if (! feedback.isVoid() && ! feedback.isArray())
        return fail ("\"feedback\" must be a list.");

    if (auto* arr = bindings.getArray())
    {
        if (arr->size() > kMaxBindings)
            return fail ("A profile can hold at most 48 bindings.");

        int index = 0;

        for (const auto& item : *arr)
        {
            const auto where = "bindings[" + juce::String (index++) + "]: ";

            if (item.getDynamicObject() == nullptr)
                return fail (where + "must be an object.");

            Binding b;
            const auto cmdName = item.getProperty ("command", juce::var()).toString();
            int cmdIndex = -1;

            for (int i = 1; i <= (version == 1 ? 30 : 33); ++i)
                if (cmdName == kCommandKeys[i])
                    cmdIndex = i;

            if (cmdIndex < 0)
                return fail (where + "unknown command \"" + cmdName + "\".");

            b.cmd = (CommandType) cmdIndex;

            int v = 0;

            if (! readInt (item, "arg", -1, 127, v, false, 0))
                return fail (where + "\"arg\" must be an integer from -1 to 127.");

            b.arg = (std::int16_t) v;

            const auto type = item.getProperty ("type", juce::var()).toString();

            if (type == "cc")            b.type = MsgType::CC;
            else if (type == "note")     b.type = MsgType::Note;
            else if (type == "program")  b.type = MsgType::ProgramChange;
            else return fail (where + "\"type\" must be cc, note or program.");

            if (! readInt (item, "channel", 0, 16, v, false, 0))
                return fail (where + "\"channel\" must be 0 (any) to 16.");

            b.channel = (std::uint8_t) v;

            if (! readInt (item, "number", 0, 127, v, true))
                return fail (where + "\"number\" must be 0 to 127.");

            b.number = (std::uint8_t) v;

            const auto mode = item.getProperty ("mode", "trigger").toString();

            if (mode == "trigger")        b.mode = BindMode::Trigger;
            else if (mode == "momentary") b.mode = BindMode::Momentary;
            else if (mode == "toggle")    b.mode = BindMode::Toggle;
            else return fail (where + "\"mode\" must be trigger, momentary or toggle.");

            if (! readInt (item, "threshold", 9, 127, v, false, 64))
                return fail (where + "\"threshold\" must be 9 to 127.");

            b.threshold = (std::uint8_t) v;

            if (! isValid (b))
                return fail (where + "is not a valid binding.");

            for (const auto& other : profile.bindings)
                if (sameMessage (other, b))
                    return fail (where + "reacts to the same MIDI message as an earlier binding.");

            profile.bindings.push_back (b);
        }
    }

    if (auto* arr = feedback.getArray())
    {
        if (arr->size() > (version == 3 ? controller::kMaxIndicators : kMaxFeedbackRules))
            return fail (version == 3 ? "A schema 3 profile can hold at most 16 indicators."
                                      : "A profile can hold at most 64 feedback rules.");

        int index = 0;

        for (const auto& item : *arr)
        {
            const auto where = "feedback[" + juce::String (index++) + "]: ";

            if (item.getDynamicObject() == nullptr)
                return fail (where + "must be an object.");

            FeedbackRule r;
            const auto source = item.getProperty ("source", juce::var()).toString();

            if (source == "panic")               r.source = FeedbackSource::Panic;
            else if (source == "scene.current")  r.source = FeedbackSource::SceneCurrent;
            else if (source == "scene.pending")  r.source = FeedbackSource::ScenePending;
            else if (version == 3 && source == "bypass") r.source = FeedbackSource::Bypass;
            else if (version == 3 && source == "capture") r.source = FeedbackSource::Capture;
            else if (version == 3 && source == "hold") r.source = FeedbackSource::Hold;
            else if (version == 3 && source == "safe") r.source = FeedbackSource::Safe;
            else return fail (where + "unknown feedback source for this profile version.");

            int v = 0;

            if (! readInt (item, "arg", 0, 63, v, false, 0))
                return fail (where + "\"arg\" must be a scene index from 0 to 63.");

            r.arg = v;

            const auto type = item.getProperty ("type", juce::var()).toString();

            if (type == "note")      r.type = MsgType::Note;
            else if (type == "cc")   r.type = MsgType::CC;
            else return fail (where + "\"type\" must be note or cc.");

            if (! readInt (item, "channel", 1, 16, v, true))
                return fail (where + "\"channel\" must be 1 to 16.");

            r.channel = (std::uint8_t) v;

            if (! readInt (item, "number", 0, 127, v, true))
                return fail (where + "\"number\" must be 0 to 127.");

            r.number = (std::uint8_t) v;

            if (! readInt (item, "on", 0, 127, v, false, 127))
                return fail (where + "\"on\" must be 0 to 127.");

            r.onValue = (std::uint8_t) v;

            if (! readInt (item, "off", 0, 127, v, false, 0))
                return fail (where + "\"off\" must be 0 to 127.");

            r.offValue = (std::uint8_t) v;
            profile.feedback.push_back (r);
        }
    }

    for (const auto& parameter : profile.params)
        for (const auto& binding : profile.bindings)
            if (binding.type == MsgType::CC && binding.number == parameter.cc
                && (binding.channel == 0 || parameter.channel == 0 || binding.channel == parameter.channel))
                return fail ("A CC cannot control both a parameter and a command on overlapping channels.");

    if (profile.parameterFeedback)
        for (const auto& parameter : profile.params)
            for (const auto& indicator : profile.feedback)
                if (indicator.type == MsgType::CC && indicator.number == parameter.cc && indicator.channel == profile.txChannel)
                    return fail ("A feedback CC cannot carry both a parameter value and an indicator.");

    ProfileParse out;
    out.ok = true;
    out.profile = std::move (profile);
    return out;
}

juce::String profileToJson (const Profile& p)
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("name", p.name);
    bool extended = false;
    for (const auto& b : p.bindings)
        extended = extended || (int) b.cmd > 30;
    const bool schema3 = p.extended || ! p.params.empty() || p.parameterFeedback || p.txChannel != 1;
    root->setProperty ("version", schema3 ? 3 : extended ? 2 : 1);
    if (schema3)
    {
        juce::Array<juce::var> params;
        for (const auto& binding : p.params)
        {
            auto* item = new juce::DynamicObject();
            item->setProperty ("id", juce::String (binding.id));
            item->setProperty ("cc", (int) binding.cc);
            item->setProperty ("channel", (int) binding.channel);
            item->setProperty ("takeover", binding.takeover == controller::TakeoverMode::Pickup ? "pickup" : "jump");
            params.add (juce::var (item));
        }
        root->setProperty ("params", params);
        root->setProperty ("parameterFeedback", p.parameterFeedback);
        root->setProperty ("txChannel", (int) p.txChannel);
    }

    juce::Array<juce::var> bindings, feedback;

    for (const auto& b : p.bindings)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("command", commandKey (b.cmd));

        if (b.arg != 0)
            o->setProperty ("arg", (int) b.arg);

        o->setProperty ("type", typeKey (b.type));
        o->setProperty ("channel", (int) b.channel);
        o->setProperty ("number", (int) b.number);
        o->setProperty ("mode", modeKey (b.mode));

        if (b.threshold != 64)
            o->setProperty ("threshold", (int) b.threshold);

        bindings.add (juce::var (o));
    }

    for (const auto& f : p.feedback)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("source", sourceKey (f.source));

        if (f.arg != 0)
            o->setProperty ("arg", f.arg);

        o->setProperty ("type", f.type == MsgType::CC ? "cc" : "note");
        o->setProperty ("channel", (int) f.channel);
        o->setProperty ("number", (int) f.number);
        o->setProperty ("on", (int) f.onValue);
        o->setProperty ("off", (int) f.offValue);
        feedback.add (juce::var (o));
    }

    root->setProperty ("bindings", bindings);
    root->setProperty ("feedback", feedback);
    return juce::JSON::toString (juce::var (root), schema3);
}

//==============================================================================
const std::vector<ProfileInfo>& builtInProfiles()
{
    static const std::vector<ProfileInfo> list = []
    {
        std::vector<ProfileInfo> out;

        for (const char* text : { kBuiltInPadProfile, kBuiltInCcProfile })
        {
            auto parsed = parseProfile (juce::String (text));
            jassert (parsed.ok);

            if (parsed.ok)
            {
                ProfileInfo info;
                info.name = parsed.profile.name;
                info.builtIn = true;
                info.profile = std::move (parsed.profile);
                out.push_back (std::move (info));
            }
        }

        return out;
    }();

    return list;
}

juce::File defaultUserDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("GapFill").getChildFile ("Controllers");
}

LibraryScan scanLibrary (const juce::File& userDirectory)
{
    LibraryScan scan;
    scan.profiles = builtInProfiles();

    if (! userDirectory.isDirectory())
        return scan;

    auto files = userDirectory.findChildFiles (juce::File::findFiles, false, "*.json");
    files.sort();

    for (const auto& f : files)
    {
        if (f.getSize() > kMaxProfileBytes)
        {
            scan.problems.add (f.getFileName() + ": larger than 8 KiB");
            continue;
        }

        const auto parsed = parseProfile (f.loadFileAsString());

        if (! parsed.ok)
        {
            scan.problems.add (f.getFileName() + ": " + parsed.error);
            continue;
        }

        bool clash = false;

        for (const auto& existing : scan.profiles)
            clash = clash || existing.name == parsed.profile.name;

        if (clash)
        {
            scan.problems.add (f.getFileName() + ": a profile named \"" + parsed.profile.name + "\" already exists");
            continue;
        }

        ProfileInfo info;
        info.name = parsed.profile.name;
        info.builtIn = false;
        info.file = f;
        info.profile = parsed.profile;
        scan.profiles.push_back (std::move (info));
    }

    return scan;
}

//==============================================================================
namespace
{
    class DeviceSink : public MidiSink
    {
    public:
        explicit DeviceSink (const juce::String& n) : deviceName (n) { open(); }

        bool send (const juce::MidiMessage& m) override
        {
            if (output == nullptr)
                return false;

            output->sendMessageNow (m);
            return true;
        }

        bool isConnected() override
        {
            if (output == nullptr)
                return false;

            for (const auto& d : juce::MidiOutput::getAvailableDevices())
                if (d.identifier == identifier)
                    return true;

            output.reset();          // the device vanished
            return false;
        }

        bool tryReconnect() override  { return open(); }
        bool isOpen() const           { return output != nullptr; }

    private:
        bool open()
        {
            for (const auto& d : juce::MidiOutput::getAvailableDevices())
            {
                if (d.name == deviceName)
                {
                    output = juce::MidiOutput::openDevice (d.identifier);
                    identifier = d.identifier;
                    return output != nullptr;
                }
            }

            return false;
        }

        juce::String deviceName, identifier;
        std::unique_ptr<juce::MidiOutput> output;
    };
}

std::unique_ptr<MidiSink> makeDeviceSink (const juce::String& deviceName, juce::String& error)
{
    auto sink = std::make_unique<DeviceSink> (deviceName);

    if (! sink->isOpen())
    {
        error = "MIDI output device \"" + deviceName + "\" was not found. It will be retried automatically.";
        // The sink is still returned: it keeps trying to reconnect, bindings are unaffected.
    }

    return sink;
}

//==============================================================================
FeedbackEngine::FeedbackEngine() : extended (std::make_unique<controller::FeedbackAdapter> (*this)) {}
FeedbackEngine::~FeedbackEngine() = default;

void FeedbackEngine::setRules (std::vector<FeedbackRule> r)
{
    rules = std::move (r);
    lastValue.assign (rules.size(), -1);
}

void FeedbackEngine::setSink (std::unique_ptr<MidiSink> s)
{
    sink = std::move (s);
    connected = sink == nullptr || sink->isConnected();
    std::fill (lastValue.begin(), lastValue.end(), -1);
    extended->sinkChanged();
}

bool FeedbackEngine::isOn (const FeedbackRule& r, const FeedbackState& s) noexcept
{
    switch (r.source)
    {
        case FeedbackSource::Panic:        return s.panicked;
        case FeedbackSource::SceneCurrent: return s.currentScene == r.arg;
        case FeedbackSource::ScenePending: return s.pendingScene == r.arg;
        case FeedbackSource::Bypass: return s.bypassed;
        case FeedbackSource::Capture: return s.captured;
        case FeedbackSource::Hold: return s.holding;
        case FeedbackSource::Safe: return s.safe;
    }

    return false;
}

int FeedbackEngine::sendAll (const FeedbackState& state, bool onlyChanged)
{
    if (sink == nullptr || rules.empty())
        return 0;

    // Rules that drive the same LED: the first rule's "off" is the resting value, the last rule that is
    // "on" wins (so a pending scene can show a dimmer light than the current one).
    std::map<int, int> value;
    std::map<int, size_t> firstRule;

    for (size_t i = 0; i < rules.size(); ++i)
    {
        const auto& r = rules[i];
        const int key = ((int) r.type << 16) | ((int) r.channel << 8) | r.number;

        if (value.find (key) == value.end())
        {
            value[key] = r.offValue;
            firstRule[key] = i;
        }
    }

    for (const auto& r : rules)
    {
        const int key = ((int) r.type << 16) | ((int) r.channel << 8) | r.number;

        if (isOn (r, state))
            value[key] = r.onValue;
    }

    int sent = 0;

    for (const auto& [key, v] : value)
    {
        const auto idx = firstRule[key];

        if (onlyChanged && lastValue[idx] == v)
            continue;

        const auto& r = rules[idx];
        const auto msg = r.type == MsgType::CC ? juce::MidiMessage::controllerEvent ((int) r.channel, (int) r.number, v)
                                               : juce::MidiMessage::noteOn ((int) r.channel, (int) r.number, (juce::uint8) v);

        if (! sink->send (msg))
        {
            connected = false;               // keep the cached state so a reconnect refreshes everything
            return sent;
        }

        lastValue[idx] = v;
        ++sent;
        ++totalSent;
    }

    return sent;
}

int FeedbackEngine::update (const FeedbackState& state)
{
    if (isExtended()) return 0;
    if (sink == nullptr)
        return 0;

    if (! connected)
        return 0;                            // pollConnection() brings the device back

    return sendAll (state, true);
}

int FeedbackEngine::pollConnection (const FeedbackState& state)
{
    if (isExtended()) return 0;
    if (sink == nullptr)
        return 0;

    if (! connected || ! sink->isConnected())
    {
        connected = false;

        if (! sink->tryReconnect())
            return 0;

        connected = true;
        std::fill (lastValue.begin(), lastValue.end(), -1);        // the device lost its LEDs: refresh all
        return sendAll (state, false);
    }

    return 0;
}

bool FeedbackEngine::configureExtended (const Profile& p, juce::AudioProcessorValueTreeState& state,
                                         MidiLearnManager* learn, std::function<FeedbackState()> source)
{ return extended->configure (p, state, learn, std::move (source)); }
void FeedbackEngine::disableExtended() { extended->disable(); }
bool FeedbackEngine::isExtended() const noexcept { return extended->isConfigured(); }
const Profile& FeedbackEngine::extendedProfile() const noexcept { return extended->profile(); }
void FeedbackEngine::requestResync() { extended->resync(); }
void FeedbackEngine::setHostOutputEnabled (bool enabled) { extended->enableHostOutput (enabled); }
int FeedbackEngine::popOutput (controller::OutEvent* events, int maximum) noexcept { return extended->pop (events, maximum); }
bool FeedbackEngine::takeDeviceLost() noexcept { return extended->takeDeviceLost(); }
void FeedbackEngine::tickExtendedForTests (double nowMs) { extended->tick (nowMs); }

} // namespace gapfill::commands
