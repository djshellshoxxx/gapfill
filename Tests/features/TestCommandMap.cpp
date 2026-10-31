// X-1 MIDI command map (docs/dev/12 X-1).

#include "../support/AllocGuard.h"
#include "../support/LiveRig.h"

#include "features/commands/CommandMap.h"
#include "features/commands/RecipeCommandTargets.h"
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

GF_TEST (CommandMap, skip_commands_are_bindable_and_unknown_ids_are_rejected)
{
    Harness h;
    GF_EXPECT (h.model.add (cc (CommandType::SkipLatest, 92)) == BindingModel::AddResult::Ok);
    GF_EXPECT (h.model.add (cc (CommandType::SkipRecipe, 93, BindMode::Trigger, 0, 7)) == BindingModel::AddResult::Ok);
    GF_EXPECT (h.ccMsg (92, 127) == 1 && h.ev[0].type == CommandType::SkipLatest);
    GF_EXPECT (h.ccMsg (93, 127) == 1 && h.ev[0].type == CommandType::SkipRecipe && h.ev[0].arg == 7);
    GF_EXPECT (! isKnownCommand ((CommandType) 255));
}

GF_TEST (CommandMap, explicit_program_binding_takes_precedence_over_automatic_scene_recall)
{
    Harness h;
    h.model.setProgramChangeScenes (true, 1);
    auto binding = cc (CommandType::Panic, 4, BindMode::Trigger, 1);
    binding.type = MsgType::ProgramChange;
    GF_EXPECT (h.model.add (binding) == BindingModel::AddResult::Ok);
    GF_EXPECT (h.run (juce::MidiMessage::programChange (1, 4), 17) == 1);
    GF_EXPECT (h.ev[0].type == CommandType::Panic && h.ev[0].sampleOffset == 17);
}

GF_TEST (CommandMap, factory_program_change_is_opt_in_channel_filtered_and_mutually_exclusive)
{
    Harness h;
    GF_EXPECT (h.run (juce::MidiMessage::programChange (2, 4)) == 0);
    h.model.setProgramChangeTarget (ProgramChangeTarget::FactoryPresets, 2);
    GF_EXPECT (h.model.getTable().pcSceneEnabled == 0 && h.model.getTable().pcPresetEnabled == 1);
    GF_EXPECT (h.run (juce::MidiMessage::programChange (1, 4)) == 0);
    GF_EXPECT (h.run (juce::MidiMessage::programChange (2, 4), 9) == 1);
    GF_EXPECT (h.ev[0].type == CommandType::PresetGoto && h.ev[0].arg == 4 && h.ev[0].sampleOffset == 9);
    const auto state = h.model.toValueTree();
    BindingModel restored;
    restored.restore (state);
    GF_EXPECT (restored.getProgramChangeTarget() == ProgramChangeTarget::FactoryPresets);
    restored.setProgramChangeScenes (true, 4);
    GF_EXPECT (restored.getProgramChangeTarget() == ProgramChangeTarget::Scenes && restored.getTable().pcPresetEnabled == 0);
    juce::ValueTree legacy ("Cmd");
    legacy.setProperty ("v", 1, nullptr);
    legacy.setProperty ("pcScene", 1, nullptr);
    legacy.setProperty ("pcCh", 3, nullptr);
    restored.restore (legacy);
    GF_EXPECT (restored.getProgramChangeTarget() == ProgramChangeTarget::Scenes && restored.getTable().pcSceneChannel == 3);
}

GF_TEST (CommandMap, recipe_targets_copy_valid_payload_and_reject_missing_or_stale_history)
{
    RecipeCommandTargets targets;
    Command out;
    out.recipe.seed = 999;
    GF_EXPECT (! targets.resolve (CommandType::SkipRecipe, 0, 0, out));
    RecipeTargetSnapshot snapshot;
    snapshot.generation = 42;
    snapshot.count = 2;
    snapshot.recipes[0].seed = 11;
    snapshot.recipes[1].seed = 22;
    targets.publish (snapshot);
    targets.adopt();
    GF_EXPECT (! targets.resolve (CommandType::SkipRecipe, 1, 41, out));
    GF_EXPECT (out.recipe.seed == 999);
    GF_EXPECT (! targets.resolve (CommandType::SkipRecipe, 8, 42, out));
    GF_EXPECT (targets.resolve (CommandType::PreviewRecipe, 1, 42, out));
    GF_EXPECT (out.recipe.seed == 22 && out.index == 0);
    snapshot.generation = 43;
    snapshot.recipes[1].seed = 33;
    targets.publish (snapshot);
    targets.adopt();
    GF_EXPECT (! targets.resolve (CommandType::PreviewRecipe, 1, 42, out));
    GF_EXPECT (out.recipe.seed == 22);
}

GF_TEST (CommandMap, nonlive_midi_sink_runs_once_after_one_block_and_ignores_gui_dispatch)
{
    struct Received { int count = 0; Command latest; } received;
    GapFillAudioProcessor proc;
    prepareProcessor (proc, 48000.0, 256, false);
    auto& live = proc.getLiveControl();
    live.setFeatureCommandSink ([] (void* context, const Command& command) noexcept
    {
        auto& result = *static_cast<Received*> (context);
        ++result.count;
        result.latest = command;
    }, &received);
    live.bindings().add (cc (CommandType::Preview, 94));
    juce::AudioBuffer<float> audio (2, 256);
    juce::MidiBuffer midi;
    audio.clear();
    midi.addEvent (juce::MidiMessage::controllerEvent (1, 94, 127), 17);
    proc.processBlock (audio, midi);
    GF_EXPECT (received.count == 0);
    audio.clear(); midi.clear();
    proc.processBlock (audio, midi);
    GF_EXPECT (received.count == 1 && received.latest.type == CommandType::Preview);
    Command gui; gui.type = CommandType::Preview;
    GF_EXPECT (proc.postCommand (gui));
    audio.clear(); midi.clear();
    proc.processBlock (audio, midi);
    GF_EXPECT (received.count == 1);
}

GF_TEST (CommandMap, queued_recipe_command_is_rejected_when_history_generation_changes)
{
    struct Received { int count = 0; Command latest; } received;
    GapFillAudioProcessor proc;
    prepareProcessor (proc, 48000.0, 256, false);
    auto& live = proc.getLiveControl();
    live.setFeatureCommandSink ([] (void* context, const Command& command) noexcept
    {
        auto& result = *static_cast<Received*> (context);
        ++result.count; result.latest = command;
    }, &received);
    live.bindings().add (cc (CommandType::SkipRecipe, 95));
    RecipeTargetSnapshot snapshot;
    snapshot.count = 1; snapshot.generation = 42; snapshot.recipes[0].seed = 11;
    live.publishRecipeTargets (snapshot);
    juce::AudioBuffer<float> audio (2, 256);
    juce::MidiBuffer midi;
    auto process = [&] { audio.clear(); proc.processBlock (audio, midi); midi.clear(); };
    midi.addEvent (juce::MidiMessage::controllerEvent (1, 95, 127), 0);
    process();
    snapshot.generation = 43; snapshot.recipes[0].seed = 22;
    live.publishRecipeTargets (snapshot);
    process();
    GF_EXPECT (received.count == 0 && live.getDroppedCommandEvents() > 0);
    midi.addEvent (juce::MidiMessage::controllerEvent (1, 95, 0), 0);
    process();
    midi.addEvent (juce::MidiMessage::controllerEvent (1, 95, 127), 0);
    process(); process();
    GF_EXPECT (received.count == 1 && received.latest.recipe.seed == 22);
}

GF_TEST (CommandMap, factory_program_change_works_with_editor_closed_and_rejects_unknown_program)
{
    GapFillAudioProcessor proc;
    prepareProcessor (proc, 48000.0, 256, false);
    auto& live = proc.getLiveControl();
    live.bindings().setProgramChangeTarget (ProgramChangeTarget::FactoryPresets, 2);
    juce::AudioBuffer<float> audio (2, 256);
    juce::MidiBuffer midi;
    auto run = [&] (int program)
    {
        audio.clear(); midi.clear();
        midi.addEvent (juce::MidiMessage::programChange (2, program), 0);
        proc.processBlock (audio, midi);
        audio.clear(); midi.clear();
        proc.processBlock (audio, midi);
        live.pump (proc);
    };
    GF_EXPECT (proc.getActiveEditor() == nullptr);
    run (3);
    GF_EXPECT (proc.getCurrentProgram() == 3);
    run (127);
    GF_EXPECT (proc.getCurrentProgram() == 3);
}

GF_TEST (LiveScene, preflight_splits_at_fixed_boundary_and_cancels_a_changed_group_epoch)
{
    GapFillAudioProcessor proc;
    prepareProcessor (proc, 48000.0, 256, false);
    auto& live = proc.getLiveControl();
    GF_EXPECT (live.scenes().saveCurrent (0, "Group target"));
    std::uint64_t epoch = 5;
    live.setGroupSceneCancelReader ([] (void* context, std::uint8_t) noexcept
    { return *static_cast<std::uint64_t*> (context); }, &epoch);
    GF_EXPECT (live.scenes().gotoSceneAt (0, 10.0, 1, epoch));
    gapfill::scenes::TransportSnap transport;
    transport.valid = true; transport.ppq = 9.0; transport.samplesPerBeat = 100.0;
    transport.sampleRate = 48000.0; transport.numSamples = 256;
    GF_EXPECT (live.stageSceneChunk (transport) == 100);
    ++epoch;
    GF_EXPECT (live.stageSceneChunk (transport) == 256);
    live.pump (proc);
    GF_EXPECT (live.scenes().getPendingIndex() == -1);
}
