#pragma once

// F-L24 Controller profiles and feedback (docs/dev/12 F-L24, X-1 "Feedback").
//
// A profile is a small JSON document (<= 8 KiB, schema-validated) that carries a
// set of command bindings and LED feedback rules for a hardware controller:
//
//   { "name": "Pad controller", "version": 1,
//     "bindings": [ { "command": "Panic", "type": "note", "channel": 1, "number": 36, "mode": "trigger" },
//                   { "command": "SceneGoto", "arg": 0, "type": "note", "channel": 1, "number": 40 } ],
//     "feedback": [ { "source": "panic", "type": "note", "channel": 1, "number": 36, "on": 127, "off": 0 },
//                   { "source": "scene.current", "arg": 0, "type": "note", "channel": 1, "number": 40, "on": 127, "off": 8 } ] }
//
// Built-in profiles are compiled in (parsed by the same validator); user profiles
// are read from <Documents>/GapFill/Controllers/*.json. Feedback is OUTPUT only
// and only in the Standalone application: the plugin emits no MIDI. Everything
// here runs on the message thread; a disconnected device never blocks audio and
// never deletes bindings.

#include "CommandMap.h"

#include <juce_core/juce_core.h>
#include <juce_audio_devices/juce_audio_devices.h>

#include <memory>
#include <vector>

namespace gapfill::commands
{

inline constexpr int kMaxProfileBytes = 8 * 1024;
inline constexpr int kMaxFeedbackRules = 64;

enum class FeedbackSource : std::uint8_t { Panic = 0, SceneCurrent, ScenePending };

struct FeedbackRule
{
    FeedbackSource source = FeedbackSource::Panic;
    int arg = 0;                        ///< scene index for the scene sources
    MsgType type = MsgType::Note;       ///< Note or CC (program change cannot light an LED)
    std::uint8_t channel = 1;           ///< 1..16
    std::uint8_t number = 0;
    std::uint8_t onValue = 127;
    std::uint8_t offValue = 0;
};

struct Profile
{
    juce::String name;
    std::vector<Binding> bindings;
    std::vector<FeedbackRule> feedback;
};

struct ProfileParse
{
    bool ok = false;
    juce::String error;                 ///< user-facing reason when !ok
    Profile profile;
};

/** Command identifier used in profile files (the enum name, e.g. "SceneGoto"). */
const char* commandKey (CommandType) noexcept;

/** Pure parse + schema validation. Never throws. */
ProfileParse parseProfile (const juce::String& json);

/** JSON text for a profile (used by tests and for sharing a user profile). */
juce::String profileToJson (const Profile&);

struct ProfileInfo
{
    juce::String name;
    bool builtIn = true;
    juce::File file;                    ///< user profiles only
    Profile profile;
};

/** The compiled-in profiles (always valid). */
const std::vector<ProfileInfo>& builtInProfiles();

struct LibraryScan
{
    std::vector<ProfileInfo> profiles;  ///< built-ins first, then valid user profiles
    juce::StringArray problems;         ///< "<file>: <reason>" for each rejected file
};

/** Built-ins plus every valid profile in `userDirectory` (files > 8 KiB are rejected unread). */
LibraryScan scanLibrary (const juce::File& userDirectory);

/** <Documents>/GapFill/Controllers */
juce::File defaultUserDirectory();

//==============================================================================
/** Where LED messages go. The production sink wraps a MIDI output device. */
class MidiSink
{
public:
    virtual ~MidiSink() = default;
    virtual bool send (const juce::MidiMessage&) = 0;       ///< false: the device is gone
    virtual bool isConnected() = 0;
    virtual bool tryReconnect() = 0;
};

/** Opens a MIDI output device by name (Standalone only). */
std::unique_ptr<MidiSink> makeDeviceSink (const juce::String& deviceName, juce::String& error);

struct FeedbackState
{
    bool panicked = false;
    int currentScene = -1;
    int pendingScene = -1;
};

/** Turns application state into LED messages. Sends only changes (plus a full
    refresh after a reconnect). Message thread only. */
class FeedbackEngine
{
public:
    void setRules (std::vector<FeedbackRule>);
    void setSink (std::unique_ptr<MidiSink>);
    bool hasSink() const noexcept                       { return sink != nullptr; }
    int  getNumRules() const noexcept                   { return (int) rules.size(); }

    /** Sends the LEDs that changed since the last call. Returns how many messages went out. */
    int update (const FeedbackState&);

    /** Called ~every second: reconnects a vanished device and refreshes the LEDs. */
    int pollConnection (const FeedbackState&);

    bool isConnected() const noexcept                   { return connected; }
    int  getTotalSent() const noexcept                  { return totalSent; }

private:
    static bool isOn (const FeedbackRule&, const FeedbackState&) noexcept;
    int sendAll (const FeedbackState&, bool onlyChanged);

    std::vector<FeedbackRule> rules;
    std::vector<int> lastValue;                         // per rule, -1 = unknown
    std::unique_ptr<MidiSink> sink;
    bool connected = true;
    int totalSent = 0;
};

} // namespace gapfill::commands
