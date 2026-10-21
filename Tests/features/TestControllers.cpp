// F-L24 Controller profiles and feedback (docs/dev/12 F-L24, X-1).

#include "../support/LiveRig.h"

#include "features/commands/ControllerProfile.h"

#include <cmath>

using namespace gapfill;
using namespace gapfill::commands;
using namespace gftest;

namespace
{
    constexpr const char* kMinimal = R"json({ "name": "Mini", "version": 1,
        "bindings": [ { "command": "Panic", "type": "note", "channel": 1, "number": 36 } ],
        "feedback": [ { "source": "panic", "type": "note", "channel": 1, "number": 36, "on": 127, "off": 0 } ] })json";

    struct MockSink : MidiSink
    {
        std::vector<juce::MidiMessage> sent;
        bool connected = true;
        bool canReconnect = false;
        int reconnects = 0;

        bool send (const juce::MidiMessage& m) override
        {
            if (! connected)
                return false;

            sent.push_back (m);
            return true;
        }

        bool isConnected() override    { return connected; }

        bool tryReconnect() override
        {
            ++reconnects;

            if (canReconnect)
                connected = true;

            return connected;
        }
    };

    /** A sink owned by the test while the engine owns a forwarding wrapper. */
    struct Forward : MidiSink
    {
        explicit Forward (MockSink& t) : target (t) {}
        bool send (const juce::MidiMessage& m) override  { return target.send (m); }
        bool isConnected() override                      { return target.isConnected(); }
        bool tryReconnect() override                     { return target.tryReconnect(); }
        MockSink& target;
    };

    int velocityOf (const juce::MidiMessage& m)  { return m.isNoteOn() ? (int) m.getVelocity() : m.getControllerValue(); }
    int numberOf (const juce::MidiMessage& m)    { return m.isNoteOn() ? m.getNoteNumber() : m.getControllerNumber(); }
}

GF_TEST (Controllers, built_in_profiles_pass_the_same_validator_as_user_files)
{
    const auto& list = builtInProfiles();
    GF_EXPECT (list.size() >= 2);

    for (const auto& p : list)
    {
        GF_EXPECT_MSG (! p.profile.bindings.empty(), p.name);
        GF_EXPECT_MSG ((int) p.profile.bindings.size() <= kMaxBindings, p.name);

        // round trip through JSON
        const auto back = parseProfile (profileToJson (p.profile));
        GF_EXPECT_MSG (back.ok, p.name + ": " + back.error);
        GF_EXPECT (back.profile.bindings.size() == p.profile.bindings.size());
        GF_EXPECT (back.profile.feedback.size() == p.profile.feedback.size());
        GF_EXPECT (profileToJson (p.profile).getNumBytesAsUTF8() < (size_t) kMaxProfileBytes);
    }
}

GF_TEST (Controllers, schema_rejects_bad_profiles_with_a_reason)
{
    auto rejects = [] (const juce::String& json, const char* needle)
    {
        const auto p = parseProfile (json);
        return ! p.ok && p.error.containsIgnoreCase (needle);
    };

    GF_EXPECT (rejects ("not json", "JSON"));
    GF_EXPECT (rejects ("[1,2,3]", "object"));
    GF_EXPECT (! parseProfile ("{}").ok && parseProfile ("{}").error.isNotEmpty());
    GF_EXPECT (rejects ("{\"name\":\"\",\"version\":1}", "name"));
    GF_EXPECT (rejects ("{\"name\":\"" + juce::String::repeatedString ("x", 49) + "\",\"version\":1}", "name"));
    GF_EXPECT (rejects ("{\"name\":\"a\"}", "version"));
    GF_EXPECT (rejects ("{\"name\":\"a\",\"version\":2}", "version"));
    GF_EXPECT (rejects ("{\"name\":\"a\",\"version\":1,\"bindings\":5}", "list"));
    GF_EXPECT (rejects ("{\"name\":\"a\",\"version\":1,\"bindings\":[5]}", "object"));
    GF_EXPECT (rejects ("{\"name\":\"a\",\"version\":1,\"bindings\":[{\"command\":\"Explode\",\"type\":\"cc\",\"number\":1}]}", "unknown command"));
    GF_EXPECT (rejects ("{\"name\":\"a\",\"version\":1,\"bindings\":[{\"command\":\"Panic\",\"type\":\"sysex\",\"number\":1}]}", "type"));
    GF_EXPECT (rejects ("{\"name\":\"a\",\"version\":1,\"bindings\":[{\"command\":\"Panic\",\"type\":\"cc\",\"number\":200}]}", "number"));
    GF_EXPECT (rejects ("{\"name\":\"a\",\"version\":1,\"bindings\":[{\"command\":\"Panic\",\"type\":\"cc\",\"number\":1.5}]}", "number"));
    GF_EXPECT (rejects ("{\"name\":\"a\",\"version\":1,\"bindings\":[{\"command\":\"Panic\",\"type\":\"cc\"}]}", "number"));
    GF_EXPECT (rejects ("{\"name\":\"a\",\"version\":1,\"bindings\":[{\"command\":\"Panic\",\"type\":\"cc\",\"number\":1,\"channel\":17}]}", "channel"));
    GF_EXPECT (rejects ("{\"name\":\"a\",\"version\":1,\"bindings\":[{\"command\":\"Panic\",\"type\":\"cc\",\"number\":1,\"mode\":\"hold\"}]}", "mode"));
    GF_EXPECT (rejects ("{\"name\":\"a\",\"version\":1,\"bindings\":[{\"command\":\"Panic\",\"type\":\"cc\",\"number\":1,\"threshold\":3}]}", "threshold"));
    GF_EXPECT (rejects ("{\"name\":\"a\",\"version\":1,\"bindings\":[{\"command\":\"Panic\",\"type\":\"cc\",\"number\":1},"
                        "{\"command\":\"Recover\",\"type\":\"cc\",\"number\":1}]}", "same MIDI message"));
    GF_EXPECT (rejects ("{\"name\":\"a\",\"version\":1,\"feedback\":[{\"source\":\"nothing\",\"type\":\"note\",\"channel\":1,\"number\":1}]}", "source"));
    GF_EXPECT (rejects ("{\"name\":\"a\",\"version\":1,\"feedback\":[{\"source\":\"panic\",\"type\":\"program\",\"channel\":1,\"number\":1}]}", "type"));
    GF_EXPECT (rejects ("{\"name\":\"a\",\"version\":1,\"feedback\":[{\"source\":\"panic\",\"type\":\"note\",\"number\":1}]}", "channel"));
    GF_EXPECT (rejects ("{\"name\":\"a\",\"version\":1,\"feedback\":[{\"source\":\"panic\",\"type\":\"note\",\"channel\":1,\"number\":1,\"on\":300}]}", "on"));

    // size and count caps
    GF_EXPECT (rejects (juce::String (kMinimal) + juce::String::repeatedString (" ", kMaxProfileBytes), "8 KiB"));

    juce::String many = "{\"name\":\"a\",\"version\":1,\"bindings\":[";

    for (int i = 0; i < 49; ++i)
        many << (i > 0 ? "," : "") << "{\"command\":\"Panic\",\"type\":\"cc\",\"number\":" << i << "}";

    GF_EXPECT (rejects (many + "]}", "48"));

    GF_EXPECT (parseProfile (kMinimal).ok);
}

GF_TEST (Controllers, user_directory_scan_lists_good_profiles_and_reports_each_rejected_file)
{
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("gapfill_profiles_test");
    dir.deleteRecursively();
    dir.createDirectory();

    dir.getChildFile ("good.json").replaceWithText (kMinimal);
    dir.getChildFile ("broken.json").replaceWithText ("{ not json");
    dir.getChildFile ("huge.json").replaceWithText (juce::String (kMinimal) + juce::String::repeatedString ("\n", 9000));
    dir.getChildFile ("duplicate_name.json").replaceWithText (juce::String (kMinimal).replace ("Mini", builtInProfiles()[0].name));
    dir.getChildFile ("ignored.txt").replaceWithText (kMinimal);

    const auto scan = scanLibrary (dir);
    GF_EXPECT (scan.profiles.size() == builtInProfiles().size() + 1);
    GF_EXPECT (scan.profiles.back().name == "Mini" && ! scan.profiles.back().builtIn);
    GF_EXPECT (scan.problems.size() == 3);

    // a directory that does not exist is fine
    GF_EXPECT (scanLibrary (dir.getChildFile ("missing")).profiles.size() == builtInProfiles().size());
    dir.deleteRecursively();
}

GF_TEST (Controllers, installing_a_profile_makes_the_bindings_fire)
{
    GapFillAudioProcessor proc;
    auto& live = proc.getLiveControl();
    juce::String message;
    GF_EXPECT (live.applyProfile (builtInProfiles()[0].profile, true, message));
    GF_EXPECT (message.contains ("installed") && live.bindings().getProfileName() == builtInProfiles()[0].name.toStdString());
    GF_EXPECT (live.bindings().size() == (int) builtInProfiles()[0].profile.bindings.size());

    prepareProcessor (proc, 48000.0, 256, false);
    juce::AudioBuffer<float> work (2, 256);
    juce::MidiBuffer midi;
    work.clear();
    midi.addEvent (juce::MidiMessage::noteOn (10, 36, (juce::uint8) 100), 5);      // pad 36 = Panic
    proc.processBlock (work, midi);
    GF_EXPECT (live.panic().isPanicked());
}

GF_TEST (Controllers, existing_bindings_and_parameter_cc_win_over_profile_bindings)
{
    GapFillAudioProcessor proc;
    auto& live = proc.getLiveControl();
    live.bindings().add ({ CommandType::Roulette, 0, MsgType::Note, 10, 36 });             // the user already owns note 36
    proc.getMidiLearnManager().setMapping ("wet", 20);                                     // CC 20 belongs to a parameter

    juce::String message;
    live.applyProfile (builtInProfiles()[1].profile, false, message);                      // CC profile: Panic is on CC 20
    GF_EXPECT (message.contains ("skipped"));

    bool panicOnCc20 = false;

    for (int i = 0; i < live.bindings().size(); ++i)
        panicOnCc20 = panicOnCc20 || (live.bindings().get (i).cmd == CommandType::Panic && live.bindings().get (i).number == 20);

    GF_EXPECT (! panicOnCc20);
    GF_EXPECT (live.bindings().findCommand (CommandType::Roulette, 0) >= 0);              // untouched

    live.applyProfile (builtInProfiles()[0].profile, false, message);                      // merge: note 36 stays Roulette
    GF_EXPECT (live.bindings().get (live.bindings().findCommand (CommandType::Roulette, 0)).number == 36);
}

//==============================================================================
GF_TEST (Controllers, feedback_sends_only_changes_after_a_full_first_refresh)
{
    MockSink sink;
    FeedbackEngine fb;
    fb.setRules (builtInProfiles()[0].profile.feedback);
    fb.setSink (std::make_unique<Forward> (sink));

    FeedbackState s;
    const auto first = fb.update (s);
    GF_EXPECT (first == 9);                                           // panic LED + 8 scene LEDs (pending shares the LED)
    GF_EXPECT (fb.update (s) == 0);                                   // no change, no traffic

    s.panicked = true;
    sink.sent.clear();
    GF_EXPECT (fb.update (s) == 1);
    GF_EXPECT (numberOf (sink.sent[0]) == 36 && velocityOf (sink.sent[0]) == 127 && sink.sent[0].getChannel() == 10);

    s.panicked = false;
    sink.sent.clear();
    fb.update (s);
    GF_EXPECT (velocityOf (sink.sent[0]) == 0);

    // scene LEDs: current is bright, pending is dim, a pending scene on a current LED shows the later rule
    sink.sent.clear();
    s.currentScene = 2;
    GF_EXPECT (fb.update (s) == 1 && numberOf (sink.sent[0]) == 42 && velocityOf (sink.sent[0]) == 127);

    sink.sent.clear();
    s.pendingScene = 5;
    GF_EXPECT (fb.update (s) == 1 && numberOf (sink.sent[0]) == 45 && velocityOf (sink.sent[0]) == 64);

    sink.sent.clear();
    s.currentScene = 5;                                               // the pending scene became current; scene 2 goes dark
    s.pendingScene = -1;
    GF_EXPECT (fb.update (s) == 2);
    int bright = 0, dark = 0;

    for (const auto& m : sink.sent)
    {
        if (numberOf (m) == 45 && velocityOf (m) == 127) ++bright;
        if (numberOf (m) == 42 && velocityOf (m) == 8) ++dark;
    }

    GF_EXPECT (bright == 1 && dark == 1);
}

GF_TEST (Controllers, feedback_uses_cc_messages_for_cc_rules_and_nothing_without_rules_or_sink)
{
    MockSink sink;
    FeedbackEngine fb;
    fb.setRules (builtInProfiles()[1].profile.feedback);
    GF_EXPECT (fb.update ({}) == 0);                                  // no sink yet
    fb.setSink (std::make_unique<Forward> (sink));
    FeedbackState s;
    s.panicked = true;
    GF_EXPECT (fb.update (s) == 1 && sink.sent[0].isController() && sink.sent[0].getControllerNumber() == 20);

    FeedbackEngine empty;
    MockSink sink2;
    empty.setSink (std::make_unique<Forward> (sink2));
    GF_EXPECT (empty.update (s) == 0 && sink2.sent.empty());
}

GF_TEST (Controllers, a_disconnected_device_never_blocks_and_reconnecting_refreshes_every_led)
{
    MockSink sink;
    FeedbackEngine fb;
    fb.setRules (builtInProfiles()[0].profile.feedback);
    fb.setSink (std::make_unique<Forward> (sink));

    FeedbackState s;
    s.currentScene = 3;
    fb.update (s);
    GF_EXPECT (fb.isConnected());

    sink.connected = false;                                           // cable pulled
    sink.sent.clear();
    s.panicked = true;
    GF_EXPECT (fb.update (s) == 0);                                   // fails soft: no exception, no traffic
    GF_EXPECT (! fb.isConnected());
    GF_EXPECT (fb.update (s) == 0);

    GF_EXPECT (fb.pollConnection (s) == 0 && sink.reconnects >= 1);  // still gone
    sink.canReconnect = true;                                         // cable back
    const auto refreshed = fb.pollConnection (s);
    GF_EXPECT (refreshed == 9 && fb.isConnected());
    bool sawPanicOn = false, sawScene3 = false;

    for (const auto& m : sink.sent)
    {
        sawPanicOn = sawPanicOn || (numberOf (m) == 36 && velocityOf (m) == 127);
        sawScene3 = sawScene3 || (numberOf (m) == 43 && velocityOf (m) == 127);
    }

    GF_EXPECT (sawPanicOn && sawScene3);
}

GF_TEST (Controllers, bindings_survive_a_missing_device_and_the_state_remembers_the_profile)
{
    GapFillAudioProcessor a;
    juce::String message;
    a.getLiveControl().applyProfile (builtInProfiles()[0].profile, true, message);
    a.getLiveControl().setFeedbackDevice ("Definitely Not A Real Device");
    const auto count = a.getLiveControl().bindings().size();

    juce::MemoryBlock blob;
    a.getStateInformation (blob);

    GapFillAudioProcessor b;
    b.setStateInformation (blob.getData(), (int) blob.getSize());
    GF_EXPECT (b.getLiveControl().bindings().size() == count);
    GF_EXPECT (b.getLiveControl().bindings().getProfileName() == builtInProfiles()[0].name.toStdString());
    GF_EXPECT (b.getLiveControl().bindings().getFeedbackDevice() == "Definitely Not A Real Device");
    GF_EXPECT (b.getLiveControl().feedbackEngine().getNumRules() == (int) builtInProfiles()[0].profile.feedback.size());

    // an unknown profile name keeps the bindings and simply has no feedback rules
    auto xml = juce::AudioProcessor::getXmlFromBinary (blob.getData(), (int) blob.getSize());
    xml->getChildByName ("Cmd")->setAttribute ("profile", "A profile that was deleted");
    juce::MemoryBlock edited;
    juce::AudioProcessor::copyXmlToBinary (*xml, edited);

    GapFillAudioProcessor c;
    c.setStateInformation (edited.getData(), (int) edited.getSize());
    GF_EXPECT (c.getLiveControl().bindings().size() == count);
    GF_EXPECT (c.getLiveControl().feedbackEngine().getNumRules() == 0);
}

GF_TEST (Controllers, feedback_is_standalone_only_and_the_processor_never_emits_midi)
{
    GapFillAudioProcessor proc;
    GF_EXPECT (! proc.producesMidi());

    // The compiled test host is a Standalone-style build, so the device path is allowed here...
    GF_EXPECT (gapfill::live::LiveControl::feedbackAvailableInThisHost() == juce::JUCEApplicationBase::isStandaloneApp());

    // ...and process blocks never write into the host MIDI buffer.
    prepareProcessor (proc, 48000.0, 256, false);
    proc.getLiveControl().setFeedbackSink (std::make_unique<MockSink>());
    juce::AudioBuffer<float> work (2, 256);
    juce::MidiBuffer midi;
    work.clear();
    proc.getLiveControl().panic().requestPanic();
    proc.processBlock (work, midi);
    proc.flushLatencyReportForTests();
    GF_EXPECT (midi.getNumEvents() == 0);
}
