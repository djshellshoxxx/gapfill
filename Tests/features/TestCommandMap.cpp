// X-1 MIDI command map (docs/dev/12 X-1).

#include "../support/AllocGuard.h"
#include "../support/LiveRig.h"

#include "features/commands/CommandMap.h"
#include "features/state/StateSubtrees.h"

using namespace gapfill;
using namespace gapfill::commands;
using namespace gftest;

namespace
{
    Binding cc (CommandType cmd, int number, BindMode mode = BindMode::Trigger, int channel = 0, int arg = 0)
    {
        Binding b;
        b.cmd = cmd;
        b.type = MsgType::CC;
        b.number = (std::uint8_t) number;
        b.mode = mode;
        b.channel = (std::uint8_t) channel;
        b.arg = (std::int16_t) arg;
        return b;
    }

    Binding note (CommandType cmd, int number, BindMode mode = BindMode::Trigger)
    {
        Binding b = cc (cmd, number, mode);
        b.type = MsgType::Note;
        return b;
    }

    struct Harness
    {
        BindingModel model;
        Scanner scanner;
        Event ev[kMaxCommandEventsPerBlock];

        Harness()
        {
            sync();
        }

        void sync()
        {
            BindingTable t;

            if (model.publisher().consume (t))
                scanner.adoptTable (t, ev, kMaxCommandEventsPerBlock);
        }

        int run (const juce::MidiMessage& m, int offset = 0)
        {
            sync();
            juce::MidiBuffer buf;
            buf.addEvent (m, offset);
            return scanner.process (buf, ev, kMaxCommandEventsPerBlock);
        }

        int ccMsg (int number, int value, int channel = 1)
        {
            return run (juce::MidiMessage::controllerEvent (channel, number, value));
        }
    };
}

GF_TEST (CommandMap, trigger_fires_on_rising_edge_and_rearms_below_threshold_minus_8)
{
    Harness h;
    GF_EXPECT (h.model.add (cc (CommandType::SceneNext, 20)) == BindingModel::AddResult::Ok);

    GF_EXPECT (h.ccMsg (20, 127) == 1);
    GF_EXPECT (h.ev[0].type == CommandType::SceneNext && h.ev[0].on);
    GF_EXPECT (h.ccMsg (20, 127) == 0);          // held: no repeat
    GF_EXPECT (h.ccMsg (20, 60) == 0);           // 60 >= 56: not re-armed yet
    GF_EXPECT (h.ccMsg (20, 127) == 0);
    GF_EXPECT (h.ccMsg (20, 40) == 0);           // below 56: re-armed
    GF_EXPECT (h.ccMsg (20, 100) == 1);
}

GF_TEST (CommandMap, momentary_cc_is_on_while_high_and_releases_below_threshold)
{
    Harness h;
    h.model.add (cc (CommandType::MomentaryStutter, 21, BindMode::Momentary));

    GF_EXPECT (h.ccMsg (21, 127) == 1 && h.ev[0].on);
    GF_EXPECT (h.ccMsg (21, 90) == 0);
    GF_EXPECT (h.ccMsg (21, 10) == 1 && ! h.ev[0].on);
    GF_EXPECT (h.ccMsg (21, 10) == 0);
}

GF_TEST (CommandMap, toggle_flips_on_each_rising_edge)
{
    Harness h;
    h.model.add (cc (CommandType::StutterStorm, 22, BindMode::Toggle));

    GF_EXPECT (h.ccMsg (22, 127) == 1 && h.ev[0].on);
    h.ccMsg (22, 0);
    GF_EXPECT (h.ccMsg (22, 127) == 1 && ! h.ev[0].on);
    h.ccMsg (22, 0);
    GF_EXPECT (h.ccMsg (22, 127) == 1 && h.ev[0].on);
}

GF_TEST (CommandMap, note_trigger_and_momentary_release_on_note_off)
{
    Harness h;
    h.model.add (note (CommandType::Panic, 36));
    h.model.add (note (CommandType::MomentaryFreeze, 38, BindMode::Momentary));

    GF_EXPECT (h.run (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100)) == 1);
    GF_EXPECT (h.ev[0].type == CommandType::Panic);
    GF_EXPECT (h.run (juce::MidiMessage::noteOff (1, 36)) == 0);       // trigger ignores note-off

    GF_EXPECT (h.run (juce::MidiMessage::noteOn (1, 38, (juce::uint8) 100)) == 1 && h.ev[0].on);
    GF_EXPECT (h.run (juce::MidiMessage::noteOff (1, 38)) == 1 && ! h.ev[0].on);
    GF_EXPECT (h.run (juce::MidiMessage::noteOn (1, 38, (juce::uint8) 100)) == 1 && h.ev[0].on);
    GF_EXPECT (h.run (juce::MidiMessage::noteOn (1, 38, (juce::uint8) 0)) == 1 && ! h.ev[0].on);   // vel 0 = off
}

GF_TEST (CommandMap, channel_filter_and_program_change)
{
    Harness h;
    h.model.add (cc (CommandType::ScenePrev, 30, BindMode::Trigger, 3));

    GF_EXPECT (h.ccMsg (30, 127, 1) == 0);
    GF_EXPECT (h.ccMsg (30, 127, 3) == 1);

    Binding pc;
    pc.cmd = CommandType::PresetNext;
    pc.type = MsgType::ProgramChange;
    pc.number = 5;
    h.model.add (pc);
    GF_EXPECT (h.run (juce::MidiMessage::programChange (1, 5)) == 1);
    GF_EXPECT (h.run (juce::MidiMessage::programChange (1, 6)) == 0);
}

GF_TEST (CommandMap, program_change_selects_scene_when_enabled_on_channel)
{
    Harness h;
    h.model.setProgramChangeScenes (true, 2);
    GF_EXPECT (h.run (juce::MidiMessage::programChange (1, 7)) == 0);          // wrong channel
    GF_EXPECT (h.run (juce::MidiMessage::programChange (2, 7)) == 1);
    GF_EXPECT (h.ev[0].type == CommandType::SceneGoto && h.ev[0].arg == 7);
    GF_EXPECT (h.run (juce::MidiMessage::programChange (2, 64)) == 0);         // only 0..63
    h.model.setProgramChangeScenes (false, 0);
    GF_EXPECT (h.run (juce::MidiMessage::programChange (2, 7)) == 0);
}

GF_TEST (CommandMap, duplicate_message_refused_and_owner_reported)
{
    BindingModel m;
    GF_EXPECT (m.add (cc (CommandType::Panic, 64)) == BindingModel::AddResult::Ok);

    int owner = -1;
    GF_EXPECT (m.add (cc (CommandType::Recover, 64), &owner) == BindingModel::AddResult::Duplicate);
    GF_EXPECT (owner == 0);

    // "any channel" collides with a specific channel on the same number
    GF_EXPECT (m.add (cc (CommandType::Recover, 64, BindMode::Trigger, 5)) == BindingModel::AddResult::Duplicate);
    GF_EXPECT (m.add (cc (CommandType::Recover, 65)) == BindingModel::AddResult::Ok);
    GF_EXPECT (m.size() == 2);
}

GF_TEST (CommandMap, capacity_is_48_and_invalid_bindings_are_refused)
{
    BindingModel m;

    for (int i = 0; i < kMaxBindings; ++i)
        GF_EXPECT (m.add (cc (CommandType::SceneNext, i)) == BindingModel::AddResult::Ok);

    GF_EXPECT (m.add (cc (CommandType::SceneNext, 100)) == BindingModel::AddResult::Full);

    BindingModel m2;
    GF_EXPECT (m2.add (cc ((CommandType) 0, 1)) == BindingModel::AddResult::Invalid);
    GF_EXPECT (m2.add (cc ((CommandType) 31, 1)) == BindingModel::AddResult::Invalid);
    auto bad = cc (CommandType::Panic, 200);
    GF_EXPECT (m2.add (bad) == BindingModel::AddResult::Invalid);
}

GF_TEST (CommandMap, state_round_trip_and_bad_bindings_are_dropped_alone)
{
    BindingModel a;
    a.add (cc (CommandType::Panic, 10));
    a.add (note (CommandType::MomentaryReverse, 40, BindMode::Momentary));
    a.setProgramChangeScenes (true, 4);
    a.setFeedbackDevice ("Launchpad");

    const auto tree = a.toValueTree();
    GF_EXPECT (BindingModel::validateTree (tree));

    BindingModel b;
    b.restore (tree);
    GF_EXPECT (b.size() == 2);
    GF_EXPECT (b.get (0).cmd == CommandType::Panic && b.get (1).mode == BindMode::Momentary);
    GF_EXPECT (b.getTable().pcSceneEnabled == 1 && b.getTable().pcSceneChannel == 4);
    GF_EXPECT (b.getFeedbackDevice() == "Launchpad");

    // one bad binding, one duplicate: both dropped, the good ones survive
    auto damaged = tree.createCopy();
    juce::ValueTree junk ("B");
    junk.setProperty ("c", 99, nullptr);
    damaged.addChild (junk, 0, nullptr);
    damaged.appendChild (tree.getChild (0).createCopy(), nullptr);       // duplicate of binding 0

    BindingModel c;
    c.restore (damaged);
    GF_EXPECT (c.size() == 2);
}

GF_TEST (CommandMap, more_than_48_bindings_in_state_are_truncated_and_size_is_under_the_cap)
{
    BindingModel full;

    for (int i = 0; i < kMaxBindings; ++i)
        full.add (cc (CommandType::SceneNext, i));

    GF_EXPECT (gapfill::state::serialisedSize (full.toValueTree()) <= 3 * 1024);

    auto tree = full.toValueTree();

    for (int i = 0; i < 20; ++i)
        tree.appendChild (full.toValueTree().getChild (i).createCopy(), nullptr);

    BindingModel again;
    again.restore (tree);
    GF_EXPECT (again.size() == kMaxBindings);
}

GF_TEST (CommandMap, learn_captures_the_next_message_and_does_not_fire_it)
{
    Harness h;
    h.model.add (cc (CommandType::SceneNext, 20));
    h.sync();

    LearnTarget t;
    t.cmd = CommandType::Panic;
    t.mode = BindMode::Momentary;
    t.arg = 3;
    h.scanner.armLearn (t);
    GF_EXPECT (h.scanner.isLearning());

    GF_EXPECT (h.ccMsg (20, 127, 2) == 0);       // swallowed: SceneNext did not fire
    GF_EXPECT (! h.scanner.isLearning());

    Binding got;
    GF_EXPECT (h.scanner.popLearned (got));
    GF_EXPECT (got.cmd == CommandType::Panic && got.mode == BindMode::Momentary && got.arg == 3);
    GF_EXPECT (got.type == MsgType::CC && got.number == 20 && got.channel == 2);
    GF_EXPECT (! h.scanner.popLearned (got));

    // cancel disarms
    h.scanner.armLearn (t);
    h.scanner.cancelLearn();
    GF_EXPECT (h.ccMsg (21, 127) == 0 && ! h.scanner.popLearned (got));
}

GF_TEST (CommandMap, table_change_releases_a_held_momentary_binding)
{
    Harness h;
    h.model.add (cc (CommandType::MomentaryStutter, 21, BindMode::Momentary));
    h.ccMsg (21, 127);

    h.model.clear();
    BindingTable t;
    GF_EXPECT (h.model.publisher().consume (t));
    const auto n = h.scanner.adoptTable (t, h.ev, kMaxCommandEventsPerBlock);
    GF_EXPECT (n == 1 && ! h.ev[0].on && h.ev[0].type == CommandType::MomentaryStutter);
}

GF_TEST (CommandMap, at_most_128_messages_per_block_are_inspected_and_the_rest_counted)
{
    Harness h;
    h.model.add (cc (CommandType::SceneNext, 7));
    h.sync();

    juce::MidiBuffer buf;

    for (int i = 0; i < 200; ++i)
        buf.addEvent (juce::MidiMessage::controllerEvent (1, 50, i % 100), i);

    h.scanner.process (buf, h.ev, kMaxCommandEventsPerBlock);
    GF_EXPECT (h.scanner.getDroppedMidi() == 72);
}

GF_TEST (CommandMap, scan_does_not_allocate)
{
    Harness h;

    for (int i = 0; i < 10; ++i)
        h.model.add (cc (CommandType::SceneNext, i, i % 2 ? BindMode::Toggle : BindMode::Momentary));

    h.sync();
    juce::MidiBuffer buf;

    for (int i = 0; i < 100; ++i)
        buf.addEvent (juce::MidiMessage::controllerEvent (1, i % 10, (i * 37) % 128), i);

    AllocGuard guard;
    h.scanner.process (buf, h.ev, kMaxCommandEventsPerBlock);
    GF_EXPECT (guard.count() == 0);
}

GF_TEST (CommandMap, midi_command_reaches_the_live_control_one_block_later_and_panic_immediately)
{
    GapFillAudioProcessor proc;
    prepareProcessor (proc, 48000.0, 256, false);

    auto& live = proc.getLiveControl();
    live.bindings().add (cc (CommandType::Panic, 90));
    live.bindings().add (cc (CommandType::SceneNext, 91));

    juce::AudioBuffer<float> work (2, 256);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::controllerEvent (1, 90, 127), 100);
    midi.addEvent (juce::MidiMessage::controllerEvent (1, 91, 127), 120);
    work.clear();
    proc.processBlock (work, midi);

    // Panic acts in the same block through the direct path; the scene command is queued.
    GF_EXPECT (live.panic().isPanicked());
}
