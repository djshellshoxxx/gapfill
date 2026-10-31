#include "../TestFramework.h"
#include "../support/AllocGuard.h"
#include "features/events/EventRecorder.h"
#include "features/events/Replay.h"

#include <thread>
#include <limits>

using namespace gapfill;
using namespace gapfill::events;
using namespace gftest;

namespace
{
    FillRecipe eventRecipe()
    {
        FillRecipe r;
        r.algo = 8;
        r.band = 2;
        r.lengthClass = 3;
        r.division = 7;
        r.pitchSemis = -12;
        r.modifierMask = 0x15;
        r.stereoMode = 3;
        r.mistakeType = 9;
        r.lengthMs = 1234;
        r.mistakeAmt = 81;
        r.flags = FillRecipe::flagFromLock | FillRecipe::flagResponse;
        r.seed = 0xEFCDAB89u;
        return r;
    }

    RecipeExtras eventExtras()
    {
        RecipeExtras extras;
        extras.harmonic = { 1, harmonic::psola::Outcome::Used, 3, 2, 220.0f, 0.9f };
        return extras;
    }

    ReplayData recordedCommand (CommandType type)
    {
        EventRecorder recorder;
        recorder.start ("<s/>", 48000.0, 512, "test", 2);
        Command command;
        command.type = type;
        command.index = -1234;
        command.velocity = 97;
        command.recipe = eventRecipe();
        recorder.onCommand (command);
        recorder.stop();
        return recorder.snapshot();
    }
}

GF_TEST (EventsCodec, legacy_record_bytes_and_format_remain_unchanged)
{
    const auto data = recordedCommand (CommandType::Preview);
    GF_EXPECT (data.records.size() == 1);
    const auto xml = juce::parseXML (toFileText (data));
    GF_EXPECT (xml != nullptr && xml->getIntAttribute ("format") == 1);
    std::uint8_t bytes[kRecordBytes];
    toBytes (data.records[0], bytes);
    const std::uint8_t expected[kRecordBytes] = { 2, 1, 0x2E, 0xFB, 0, 0, 0, 0,
                                                0, 0, 0, 0, 0, 0, 0, 0,
                                                97, 0, 0, 0, 0, 0, 0, 0 };
    GF_EXPECT (std::memcmp (bytes, expected, sizeof bytes) == 0);
}

GF_TEST (EventsCodec, recipe_commands_roundtrip_complete_payload_without_allocation)
{
    for (const auto type : { CommandType::LikeRecipe, CommandType::LockRecipe,
                             CommandType::PreviewRecipe, CommandType::SkipRecipe })
    {
        EventRecorder recorder;
        recorder.start ("<s/>", 48000.0, 512, "test", 2);
        Command command;
        command.type = type;
        command.recipe = eventRecipe();
        command.index = -1234;
        command.velocity = 97;
        {
            AllocGuard guard;
            recorder.onCommand (command);
            GF_EXPECT (guard.count() == 0);
        }
        recorder.stop();
        const auto data = recorder.snapshot();
        GF_EXPECT (data.records.size() == 2);
        GF_EXPECT (data.records[0].flags == (std::uint8_t) type
                   && data.records[0].index == (std::uint16_t) command.index
                   && data.records[0].p0 == 97);
        GF_EXPECT (data.records[1].type == 6 && data.records[1].block == data.records[0].block);
        std::uint8_t bytes[kRecordBytes], recipe[FillRecipe::serialisedBytes];
        toBytes (data.records[1], bytes);
        gapfill::toBytes (command.recipe, recipe);
        GF_EXPECT (std::memcmp (bytes + 8, recipe, sizeof recipe) == 0);
        const auto text = toFileText (data);
        const auto xml = juce::parseXML (text);
        GF_EXPECT (xml != nullptr && xml->getIntAttribute ("format") == 2);
        ReplayData parsed;
        GF_EXPECT (fromFileText (text, parsed).wasOk() && parsed.records.size() == 2);
        Command restored;
        GF_EXPECT (commandAt (parsed, 0, restored));
        GF_EXPECT (restored.type == command.type && restored.index == command.index
                   && restored.velocity == command.velocity && restored.issuedMs == 0);
        gapfill::toBytes (restored.recipe, recipe);
        GF_EXPECT (std::memcmp (bytes + 8, recipe, sizeof recipe) == 0);
    }
}

GF_TEST (EventsCodec, legacy_recipe_record_can_be_loaded_and_saved_as_version_one)
{
    ReplayData legacy;
    legacy.initialState = "<s/>";
    legacy.records.push_back ({ 2, (std::uint8_t) CommandType::PreviewRecipe, 0, 0, 0, 255, 0 });
    const auto xml = juce::parseXML (toFileText (legacy));
    GF_EXPECT (xml != nullptr && xml->getIntAttribute ("format") == 1);
    ReplayData parsed;
    GF_EXPECT (fromFileText (xml->toString(), parsed).wasOk());
    Command command;
    GF_EXPECT (commandAt (parsed, 0, command) && command.velocity == 255);
    GF_EXPECT (juce::parseXML (toFileText (parsed))->getIntAttribute ("format") == 1);
}

GF_TEST (EventsCodec, appended_commands_require_version_two_and_unknown_future_versions_reject)
{
    for (const auto type : { CommandType::SkipLatest, CommandType::SkipRecipe, CommandType::PresetGoto })
    {
        const auto data = recordedCommand (type);
        auto xml = juce::parseXML (toFileText (data));
        GF_EXPECT (xml != nullptr && xml->getIntAttribute ("format") == 2);
        ReplayData parsed;
        GF_EXPECT (fromFileText (xml->toString(), parsed).wasOk());
        xml->setAttribute ("format", 1);
        GF_EXPECT (! fromFileText (xml->toString(), parsed).wasOk());
        xml->setAttribute ("format", 4);
        GF_EXPECT (! fromFileText (xml->toString(), parsed).wasOk());
    }
}

GF_TEST (EventsCodec, malformed_or_truncated_recipe_pairs_reject_atomically)
{
    const auto complete = recordedCommand (CommandType::PreviewRecipe);
    GF_EXPECT (complete.records.size() == 2);
    auto invalid = complete;
    invalid.records.pop_back();
    ReplayData out;
    out.pluginVersion = "unchanged";
    GF_EXPECT (! fromFileText (toFileText (invalid), out).wasOk());
    GF_EXPECT (out.pluginVersion == "unchanged");
    invalid = complete;
    invalid.records.erase (invalid.records.begin());
    GF_EXPECT (! fromFileText (toFileText (invalid), out).wasOk());
    invalid = complete;
    invalid.records[1].block = 1;
    invalid.numBlocks = 1;
    GF_EXPECT (! fromFileText (toFileText (invalid), out).wasOk());
    invalid = complete;
    invalid.records[1].pos |= 0xFF; // invalid algorithm in the recipe's first byte
    GF_EXPECT (! fromFileText (toFileText (invalid), out).wasOk());
}

GF_TEST (EventsCodec, record_cap_never_publishes_half_a_recipe_pair)
{
    EventRecorder recorder;
    recorder.start ("<s/>", 48000.0, 512, "test", 2);
    Command command;
    command.type = CommandType::Preview;
    for (int i = 0; i < kRecordCap - 1; ++i)
        recorder.onCommand (command);
    command.type = CommandType::PreviewRecipe;
    command.recipe = eventRecipe();
    recorder.onCommand (command);
    recorder.stop();
    const auto data = recorder.snapshot();
    GF_EXPECT (data.incomplete && data.records.size() == (size_t) kRecordCap - 1);
    ReplayData parsed;
    GF_EXPECT (fromFileText (toFileText (data), parsed).wasOk());
}

GF_TEST (EventsCodec, concurrent_drain_observes_only_complete_recipe_pairs)
{
    EventRecorder recorder;
    recorder.start ("<s/>", 48000.0, 512, "test", 2);
    Command initial;
    initial.type = CommandType::Preview;
    recorder.onCommand (initial); // exercise a pair that straddles the 256-record drain batch
    std::atomic<bool> done { false };
    std::thread producer ([&]
    {
        Command command;
        command.type = CommandType::SkipRecipe;
        command.recipe = eventRecipe();
        for (int i = 0; i < 4000; ++i)
        {
            command.index = (std::int16_t) i;
            recorder.onCommand (command);
        }
        done.store (true, std::memory_order_release);
    });
    bool sawHalfPair = false;
    while (! done.load (std::memory_order_acquire))
    {
        recorder.drain();
        sawHalfPair = sawHalfPair || (recorder.getNumRecords() - 1) % 2 != 0;
        std::this_thread::yield();
    }
    producer.join();
    recorder.stop();
    const auto data = recorder.snapshot();
    GF_EXPECT (! sawHalfPair && data.records.size() == 8001 && ! data.incomplete);
    for (size_t i = 1; i < data.records.size(); i += 2)
    {
        Command command;
        GF_EXPECT (commandAt (data, i, command));
        GF_EXPECT (command.index == (std::int16_t) ((i - 1) / 2));
    }
}

GF_TEST (EventsCodec, detailed_recipe_commands_use_format_three_and_keep_every_decision)
{
    EventRecorder recorder;
    recorder.start ("<s/>", 48000.0, 512, "test", 2);
    Command command;
    command.type = CommandType::PreviewRecipe;
    command.recipe = eventRecipe();
    command.extras = eventExtras();
    command.velocity = 91;
    command.index = -123;
    { AllocGuard guard; recorder.onCommand (command); GF_EXPECT (guard.count() == 0); }
    command.extras = {};
    recorder.onCommand (command); // a later legacy recipe must not downgrade the recording
    recorder.stop();
    const auto data = recorder.snapshot();
    GF_EXPECT (data.formatVersion == 3 && data.records.size() == 7);
    ReplayData parsed;
    const auto text = toFileText (data);
    GF_EXPECT (juce::parseXML (text)->getIntAttribute ("format") == 3);
    GF_EXPECT (fromFileText (text, parsed).wasOk());
    Command restored;
    GF_EXPECT (commandAt (parsed, 0, restored) && restored.extras == eventExtras());
    GF_EXPECT (restored.recipe.seed == command.recipe.seed && restored.velocity == 91 && restored.index == -123);
    GF_EXPECT (commandAt (parsed, 5, restored) && ! hasRecipeExtras (restored.extras));
}

GF_TEST (EventsCodec, detailed_markers_reject_missing_or_malformed_extra_chunks_atomically)
{
    EventRecorder recorder;
    recorder.start ("<s/>", 48000.0, 512, "test", 2);
    Command command;
    command.type = CommandType::LockRecipe;
    command.recipe = eventRecipe(); command.extras = eventExtras();
    recorder.onCommand (command); recorder.stop();
    const auto data = recorder.snapshot();
    GF_EXPECT (data.records.size() == 5);
    for (size_t count = 1; count < 5; ++count)
    {
        auto truncated = data;
        truncated.records.resize (count);
        ReplayData untouched;
        untouched.pluginVersion = "unchanged";
        GF_EXPECT (! fromFileText (toFileText (truncated), untouched).wasOk());
        GF_EXPECT (untouched.pluginVersion == "unchanged");
    }
    for (int mutation = 0; mutation < 4; ++mutation)
    {
        auto malformed = data;
        if (mutation == 0) malformed.records[2].flags = 2;
        if (mutation == 1) malformed.records[3].index = 15;
        if (mutation == 2) malformed.records[4].p1 = 1; // noncanonical unused bytes
        if (mutation == 3) malformed.records[2].pos |= 255; // unknown extras envelope schema
        ReplayData parsed;
        GF_EXPECT (! fromFileText (toFileText (malformed), parsed).wasOk());
    }
    auto xml = juce::parseXML (toFileText (data));
    xml->setAttribute ("format", 2);
    ReplayData parsed;
    GF_EXPECT (! fromFileText (xml->toString(), parsed).wasOk());
}

GF_TEST (EventsCodec, natural_outcomes_record_the_actual_detailed_decision)
{
    EventRecorder recorder;
    recorder.start ("<s/>", 48000.0, 512, "test", 2);
    FillEvent event;
    event.type = FillEventType::Started;
    event.recipe = eventRecipe(); event.extras = eventExtras(); event.pos = 123456;
    { AllocGuard guard; recorder.onFillEvent (event); GF_EXPECT (guard.count() == 0); }
    recorder.stop();
    const auto data = recorder.snapshot();
    GF_EXPECT (data.formatVersion == 3 && data.records.size() == 5);
    ReplayData parsed;
    GF_EXPECT (fromFileText (toFileText (data), parsed).wasOk());
    FillRecipe recipe;
    RecipeExtras extras;
    GF_EXPECT (outcomeDetailsAt (parsed, 0, recipe, extras));
    GF_EXPECT (recipe.seed == event.recipe.seed && extras == event.extras);
    const auto outcomes = outcomesOf (parsed);
    GF_EXPECT (outcomes.size() == 1 && outcomes[0].detailed && outcomes[0].extras == event.extras);
    auto different = outcomes;
    different[0].extras.harmonic.f0Hz += 1.0f;
    GF_EXPECT (findFirstDivergence (outcomes, different) == 0);
    different = outcomes;
    ++different[0].recipe.seed;
    GF_EXPECT (findFirstDivergence (outcomes, different) == 0);
    GF_EXPECT (findFirstDivergence (outcomes, outcomes) == -1);
    auto mismatched = data;
    mismatched.records[0].index = 0; // outcome algorithm must agree with its complete recipe
    GF_EXPECT (! fromFileText (toFileText (mismatched), parsed).wasOk());
}

GF_TEST (EventsCodec, cap_never_publishes_a_partial_detailed_command_group)
{
    EventRecorder recorder;
    recorder.start ("<s/>", 48000.0, 512, "test", 2);
    Command command;
    command.type = CommandType::Preview;
    for (int i = 0; i < kRecordCap - 4; ++i)
        recorder.onCommand (command);
    command.type = CommandType::PreviewRecipe;
    command.recipe = eventRecipe(); command.extras = eventExtras();
    recorder.onCommand (command); recorder.stop();
    const auto data = recorder.snapshot();
    GF_EXPECT (data.incomplete && data.records.size() == (size_t) kRecordCap - 4);
    ReplayData parsed;
    GF_EXPECT (fromFileText (toFileText (data), parsed).wasOk());
}

GF_TEST (EventsCodec, direct_outcome_decoder_rejects_wrapping_indexes_without_touching_outputs)
{
    ReplayData data;
    data.formatVersion = 3;
    data.records.resize (1);
    FillRecipe recipe = eventRecipe();
    const auto original = recipe.seed;
    auto extras = eventExtras();
    for (const auto index : { (size_t) 0, (size_t) 1, std::numeric_limits<size_t>::max(),
                              std::numeric_limits<size_t>::max() - 1 })
    {
        GF_EXPECT (! outcomeDetailsAt (data, index, recipe, extras));
        GF_EXPECT (recipe.seed == original && extras == eventExtras());
    }
}
