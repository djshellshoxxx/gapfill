#include "../TestFramework.h"

#include "features/midiout/MidiScheduler.h"
#include "features/midiout/MidiOut.h"
#include "../support/AllocGuard.h"

#include <array>
#include <algorithm>
#include <limits>

using namespace gapfill::midiout;

namespace
{
    struct BlockResult
    {
        std::array<MidiPacket, kSchedulerMaxEvents> packets {};
        int count = 0;
    };

    BlockResult block (MidiScheduler& scheduler, const MidiOutSettings& settings, const EngineEventBuffer& events,
                       std::int64_t start = 0, int samples = 512)
    {
        scheduler.beginBlock (settings, start, samples, start);
        scheduler.consume (events);
        BlockResult result;
        result.count = scheduler.render (result.packets);
        return result;
    }

    bool on (const MidiPacket& packet) { return (packet.status & 0xf0) == 0x90; }
    bool off (const MidiPacket& packet) { return (packet.status & 0xf0) == 0x80; }
}

GF_TEST (MidiExtended, hit_offsets_are_exact_at_every_rate_block_size_and_lookahead)
{
    for (double rate : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
        for (int samples : { 32, 64, 512, 4096, 8192 })
            for (int lookaheadMs : { 0, 250 })
            {
                MidiScheduler scheduler; scheduler.prepare (rate);
                MidiOutSettings settings; settings.on = true;
                const auto start = -(std::int64_t) (rate * lookaheadMs / 1000.0);
                EngineEventBuffer events;
                events.push ({ OutEvent::Hit, start + samples / 2, 1.0f, 0 });
                const auto result = block (scheduler, settings, events, start, samples);
                GF_EXPECT (result.count >= 1 && on (result.packets[0]));
                GF_EXPECT (result.packets[0].offset == samples / 2 && result.packets[0].data2 == 127);
            }
}

GF_TEST (MidiExtended, fixed_velocity_and_strength_following_are_bounded)
{
    for (bool follow : { false, true })
        for (float strength : { 0.0f, 0.5f, 1.0f })
        {
            MidiScheduler scheduler; scheduler.prepare (48000.0);
            MidiOutSettings settings; settings.on = true; settings.velFollow = follow;
            EngineEventBuffer events; events.push ({ OutEvent::Hit, 10, strength, 0 });
            const auto result = block (scheduler, settings, events);
            const int expected = follow ? (strength == 0.0f ? 1 : strength == 0.5f ? 64 : 127) : 100;
            GF_EXPECT (result.count == 1 && result.packets[0].data2 == expected);
        }
}

GF_TEST (MidiExtended, hit_note_off_is_scheduled_across_host_blocks)
{
    MidiScheduler scheduler; scheduler.prepare (48000.0);
    MidiOutSettings settings; settings.on = true; settings.hitLenMs = 10.0f;
    EngineEventBuffer events; events.push ({ OutEvent::Hit, 20, 1.0f, 0 });
    GF_EXPECT (block (scheduler, settings, events, 0, 128).count == 1);
    EngineEventBuffer empty;
    GF_EXPECT (block (scheduler, settings, empty, 128, 128).count == 0);
    GF_EXPECT (block (scheduler, settings, empty, 256, 128).count == 0);
    const auto result = block (scheduler, settings, empty, 384, 128);
    GF_EXPECT (result.count == 1 && off (result.packets[0]) && result.packets[0].offset == 116);
}

GF_TEST (MidiExtended, gap_and_fill_have_minimum_and_planned_durations)
{
    MidiScheduler scheduler; scheduler.prepare (48000.0);
    MidiOutSettings settings; settings.on = true; settings.gapOn = settings.fillOn = true;
    EngineEventBuffer events;
    events.push ({ OutEvent::GapOpen, 10, 1.0f, 0 });
    events.push ({ OutEvent::GapClose, 11, 0.0f, 0 });
    events.push ({ OutEvent::FillStart, 100, 0.8f, 100 });
    const auto result = block (scheduler, settings, events);
    GF_EXPECT (result.count == 4);
    GF_EXPECT (on (result.packets[0]) && result.packets[0].offset == 10);
    GF_EXPECT (off (result.packets[1]) && result.packets[1].offset == 58);
    GF_EXPECT (on (result.packets[2]) && result.packets[2].offset == 100);
    GF_EXPECT (off (result.packets[3]) && result.packets[3].offset == 200);
}

GF_TEST (MidiExtended, early_hits_and_output_gap_events_are_merged_in_sample_order)
{
    MidiScheduler scheduler; scheduler.prepare (48000.0);
    MidiOutSettings settings; settings.on = true; settings.early = true; settings.gapOn = true;
    settings.noteGap = settings.noteHit;
    scheduler.beginBlock (settings, -4800, 512, 0);
    EngineEventBuffer early; early.push ({ OutEvent::Hit, 400, 1.0f, 0 });
    scheduler.consumeEarlyHits (early);
    EngineEventBuffer output;
    output.push ({ OutEvent::GapOpen, -4700, 1.0f, 0 });
    output.push ({ OutEvent::Hit, -4600, 1.0f, 0 });
    scheduler.consume (output);
    std::array<MidiPacket, kSchedulerMaxEvents> packets {};
    GF_EXPECT (scheduler.render (packets) == 3);
    GF_EXPECT (on (packets[0]) && packets[0].offset == 100);
    GF_EXPECT (off (packets[1]) && packets[1].offset == 400);
    GF_EXPECT (on (packets[2]) && packets[2].offset == 400);
}

GF_TEST (MidiExtended, confidence_cc_is_deduplicated_and_rate_limited)
{
    MidiScheduler scheduler; scheduler.prepare (48000.0);
    MidiOutSettings settings; settings.on = true; settings.hitOn = false; settings.cc = 74;
    int sent = 0;
    for (int start = 0; start < 48000; start += 64)
    {
        EngineEventBuffer events;
        events.push ({ OutEvent::Confidence, start, (start / 64) % 2 == 0 ? 0.0f : 1.0f, 0 });
        const auto result = block (scheduler, settings, events, start, 64);
        sent += result.count;
        for (int i = 0; i < result.count; ++i)
            GF_EXPECT ((result.packets[(size_t) i].status & 0xf0) == 0xb0 && result.packets[(size_t) i].data1 == 74);
    }
    GF_EXPECT (sent > 0 && sent <= 66);
    MidiScheduler invalid; invalid.prepare (48000.0);
    settings.cc = 120;
    EngineEventBuffer events; events.push ({ OutEvent::Confidence, 0, 1.0f, 0 });
    GF_EXPECT (block (invalid, settings, events).count == 0);
}

GF_TEST (MidiExtended, overflow_keeps_gap_closure_and_never_exceeds_the_event_budget)
{
    MidiScheduler scheduler; scheduler.prepare (48000.0);
    MidiOutSettings settings; settings.on = true; settings.gapOn = true;
    EngineEventBuffer events;
    events.push ({ OutEvent::GapOpen, 0, 1.0f, 0 });
    for (int i = 1; i < EngineEventBuffer::capacity; ++i)
        events.push ({ OutEvent::Hit, i, 1.0f, 0 });
    GF_EXPECT (events.push ({ OutEvent::GapClose, 100, 0.0f, 0 }));
    const auto result = block (scheduler, settings, events);
    GF_EXPECT (result.count <= 32 && events.dropped > 0);
    bool gapOff = false;
    for (int i = 0; i < result.count; ++i)
        gapOff = gapOff || (off (result.packets[(size_t) i]) && result.packets[(size_t) i].data1 == settings.noteGap);
    GF_EXPECT (gapOff);
}

GF_TEST (MidiExtended, random_sequences_have_no_unpaired_notes_at_quiescent_points)
{
    MidiScheduler scheduler; scheduler.prepare (48000.0);
    int sounding[16][128] {};
    std::uint32_t random = 17;
    auto next = [&] { random = random * 1664525u + 1013904223u; return random; };
    auto account = [&] (const BlockResult& result)
    {
        GF_EXPECT (result.count >= 0 && result.count <= 32);
        int lastOffset = -1;
        for (int i = 0; i < result.count; ++i)
        {
            const auto& packet = result.packets[(size_t) i];
            GF_EXPECT (packet.offset >= lastOffset); lastOffset = packet.offset;
            auto& count = sounding[packet.status & 15][packet.data1];
            if (on (packet)) ++count;
            if (off (packet)) --count;
            GF_EXPECT (count >= 0 && count <= 1);
        }
    };
    for (int sequence = 0; sequence < 1000; ++sequence)
    {
        MidiOutSettings settings;
        settings.on = next() % 5 != 0;
        settings.gapOn = settings.fillOn = true;
        settings.channel = 1 + (int) (next() % 16);
        settings.noteHit = 36 + (int) (next() % 3);
        settings.noteGap = 36 + (int) (next() % 3);
        settings.noteFill = 36 + (int) (next() % 3);
        const std::int64_t start = (std::int64_t) sequence * 512;
        EngineEventBuffer events;
        for (int i = 0; i < 16; ++i)
            events.push ({ (OutEvent::Type) (next() % 5), start + (int) (next() % 512), 0.7f, 80 });
        if (next() % 7 == 0) scheduler.requestFlush();
        account (block (scheduler, settings, events, start));
    }
    MidiOutSettings offSettings;
    account (block (scheduler, offSettings, {}, 512000));
    for (const auto& channel : sounding)
        for (int count : channel)
            GF_EXPECT (count == 0);
}

GF_TEST (MidiExtended, identical_sample_retriggers_coalesce_without_extra_instrument_voices)
{
    MidiScheduler scheduler; scheduler.prepare (48000.0);
    MidiOutSettings settings; settings.on = true;
    EngineEventBuffer events;
    events.push ({ OutEvent::Hit, 10, 1.0f, 0 });
    events.push ({ OutEvent::Hit, 10, 0.5f, 0 });
    const auto result = block (scheduler, settings, events);
    GF_EXPECT (result.count == 1 && on (result.packets[0]));
    settings.on = false;
    const auto ended = block (scheduler, settings, {}, 512);
    GF_EXPECT (ended.count == 1 && off (ended.packets[0]));
}

GF_TEST (MidiExtended, flush_after_consumption_discards_new_notes_and_preserves_old_offs)
{
    MidiScheduler scheduler; scheduler.prepare (48000.0);
    MidiOutSettings settings; settings.on = true;
    EngineEventBuffer first; first.push ({ OutEvent::Hit, 10, 1.0f, 0 });
    GF_EXPECT (block (scheduler, settings, first).count == 1);
    scheduler.beginBlock (settings, 512, 512, 512);
    EngineEventBuffer next; next.push ({ OutEvent::Hit, 600, 1.0f, 0 });
    scheduler.consume (next);
    scheduler.requestFlush();
    std::array<MidiPacket, kSchedulerMaxEvents> packets {};
    GF_EXPECT (scheduler.render (packets) == 1 && off (packets[0]) && packets[0].offset == 0);
    GF_EXPECT (! scheduler.hasSoundingNotes());
}

GF_TEST (MidiExtended, changing_one_note_does_not_close_an_unrelated_gap_note)
{
    MidiScheduler scheduler; scheduler.prepare (48000.0);
    MidiOutSettings settings; settings.on = true; settings.gapOn = true;
    EngineEventBuffer first;
    first.push ({ OutEvent::GapOpen, 0, 1.0f, 0 }); first.push ({ OutEvent::Hit, 10, 1.0f, 0 });
    GF_EXPECT (block (scheduler, settings, first).count == 2);
    settings.noteHit = 40;
    EngineEventBuffer next; next.push ({ OutEvent::Hit, 600, 1.0f, 0 });
    const auto result = block (scheduler, settings, next, 512);
    GF_EXPECT (result.count == 2 && off (result.packets[0]) && result.packets[0].data1 == 36);
    GF_EXPECT (on (result.packets[1]) && result.packets[1].data1 == 40);
}

GF_TEST (MidiExtended, juce_adapter_clears_input_and_appends_to_legacy_handoff_without_allocating)
{
    MidiOutEmitter legacy;
    Settings old; old.mode = ModeGaps;
    legacy.beginBlock (old, 512);
    gapfill::TransitionLog transitions;
    transitions.push (gapfill::Transition::GapOpen, 0, 255);
    legacy.consume (transitions, 0, 0, 512);
    juce::MidiBuffer midi; midi.ensureSize (2048);
    legacy.finish (midi);
    old.mode = ModeOff; legacy.beginBlock (old, 512);
    MidiScheduler scheduler; scheduler.prepare (48000.0);
    MidiOutSettings settings; settings.on = true; settings.noteHit = 40;
    scheduler.beginBlock (settings, 512, 512, 512, 31);
    EngineEventBuffer events; events.push ({ OutEvent::Hit, 600, 1.0f, 0 }); scheduler.consume (events);
    midi.clear(); midi.addEvent (juce::MidiMessage::controllerEvent (1, 1, 127), 0);
    {
        gftest::AllocGuard guard;
        GF_EXPECT (legacy.finish (midi) == 1);
        GF_EXPECT (scheduler.finish (midi, false) == 1);
        GF_EXPECT (guard.count() == 0);
    }
    GF_EXPECT (midi.getNumEvents() == 2);
    auto iterator = midi.begin();
    GF_EXPECT ((*iterator).getMessage().isNoteOff() && (*iterator).getMessage().getNoteNumber() == 36);
    ++iterator;
    GF_EXPECT ((*iterator).getMessage().isNoteOn() && (*iterator).getMessage().getNoteNumber() == 40);
    MidiScheduler disabled; disabled.prepare (48000.0); disabled.beginBlock ({}, 0, 512, 0);
    GF_EXPECT (disabled.finish (midi) == 0 && midi.isEmpty());
}
