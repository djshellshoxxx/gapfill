#include "../TestFramework.h"

#include "dsp/VisualisationState.h"
#include "features/timeline/Model.h"
#include <utility>

using namespace gapfill;
using namespace gapfill::timeline;

namespace
{
    FillEvent event (std::uint32_t epoch, std::uint32_t rate, std::int64_t position)
    {
        FillEvent e;
        e.type = FillEventType::Onset;
        e.timelineEpoch = epoch;
        e.sampleRateHz = rate;
        e.pos = position;
        return e;
    }

    VizFrame frame (std::uint32_t epoch, std::uint32_t rate, std::uint32_t position)
    {
        VizFrame f;
        f.timelineEpoch = epoch;
        f.sampleRateHz = rate;
        f.posLo = position;
        return f;
    }
}

GF_TEST (TimelineEpoch, event_first_and_frame_first_keep_the_same_new_generation)
{
    for (const bool frameFirst : { false, true })
    {
        TimelineModel model;
        model.frames.configure (48000.0, 16.0);
        model.pushEngineEvent (event (10, 48000, 8000));
        model.pushFrame (frame (10, 48000, 8000), 8000);
        model.noteDropped (8000);
        model.setPaused (true, 8000);
        const auto nextEvent = event (11, 192000, 64);
        const auto nextFrame = frame (11, 192000, 64);
        if (frameFirst)
        {
            GF_EXPECT (model.pushFrame (nextFrame, 64));
            model.pushEngineEvent (nextEvent);
        }
        else
        {
            model.pushEngineEvent (nextEvent);
            GF_EXPECT (model.pushFrame (nextFrame, 64));
        }
        GF_EXPECT (model.epoch() == 11);
        GF_EXPECT (model.displaySampleRate() == 192000.0);
        GF_EXPECT (model.frames.sampleRate() == 192000.0);
        GF_EXPECT (model.size() == 1 && model.at (0).pos == 64);
        GF_EXPECT (model.frames.size() == 1 && model.frames.at (0).position == 64);
        GF_EXPECT (! model.isPaused());
        GF_EXPECT (! model.isIncomplete (8000, 16000));
        model.pushEngineEvent (event (10, 48000, 9000));
        GF_EXPECT (! model.pushFrame (frame (10, 48000, 9000), 9000));
        GF_EXPECT (model.size() == 1 && model.frames.size() == 1);
        GF_EXPECT (model.displaySampleRate() == 192000.0);
    }
}

GF_TEST (TimelineEpoch, repeated_generation_preserves_records_and_frozen_position)
{
    TimelineModel model;
    GF_EXPECT (model.adoptEpoch (7, 44100));
    model.pushEngineEvent (event (7, 44100, 100));
    model.setPaused (true, 100);
    GF_EXPECT (model.adoptEpoch (7, 44100));
    GF_EXPECT (model.isPaused() && model.viewEnd (1000) == 100);
    GF_EXPECT (model.size() == 1);
    // A producer must not relabel one generation with another sample rate.
    GF_EXPECT (! model.adoptEpoch (7, 96000));
    GF_EXPECT (model.displaySampleRate() == 44100.0 && model.size() == 1);
}

GF_TEST (TimelineEpoch, modular_generation_wrap_rejects_stale_queued_records)
{
    TimelineModel model;
    GF_EXPECT (model.adoptEpoch (0xfffffffeu, 48000));
    GF_EXPECT (model.adoptEpoch (0xffffffffu, 48000));
    GF_EXPECT (model.adoptEpoch (1, 96000)); // zero is reserved for unmanaged records
    GF_EXPECT (! model.adoptEpoch (0xffffffffu, 48000));
    GF_EXPECT (model.epoch() == 1 && model.displaySampleRate() == 96000.0);
}

GF_TEST (TimelineEpoch, unmanaged_records_work_until_a_managed_generation_is_adopted)
{
    TimelineModel model;
    GF_EXPECT (model.adoptEpoch (0, 44100));
    model.pushEngineEvent (event (0, 0, 100));
    GF_EXPECT (model.size() == 1 && model.displaySampleRate() == 44100.0);
    GF_EXPECT (model.adoptEpoch (3, 48000));
    model.pushEngineEvent (event (0, 0, 200));
    GF_EXPECT (model.size() == 0);
    model.clear();
    GF_EXPECT (! model.adoptEpoch (2, 44100));
    GF_EXPECT (model.epoch() == 3); // clearing the view cannot reopen stale queues
}

GF_TEST (TimelineEpoch, frame_history_budget_does_not_grow_per_generation)
{
    TimelineModel model;
    model.frames.configure (192000.0, 16.0);
    const auto allocation = model.frames.memoryBytes();
    for (std::uint32_t epoch = 1; epoch <= 100; ++epoch)
    {
        GF_EXPECT (model.pushFrame (frame (epoch, 192000, 64), 64));
        GF_EXPECT (model.frames.memoryBytes() == allocation);
    }
    GF_EXPECT (allocation + sizeof (TimelineModel) < 1500000);
}

GF_TEST (TimelineEpoch, unusually_high_rates_keep_actual_time_coordinates_and_bounded_storage)
{
    TimelineModel model;
    model.frames.configure (48000.0, 16.0);
    GF_EXPECT (model.pushFrame (frame (1, 384000, 64), 64));
    GF_EXPECT (model.displaySampleRate() == 384000.0);
    GF_EXPECT (model.frames.sampleRate() == 384000.0);
    GF_EXPECT (model.frames.capacity() <= FrameHistory::maxColumns);
    GF_EXPECT (! model.adoptEpoch (2, 1));
    GF_EXPECT (model.epoch() == 1);
}

GF_TEST (TimelineEpoch, low_sample_rates_retain_the_full_window_with_actual_thirty_two_sample_frames)
{
    for (const auto rate : { 8000u, 16000u, 32000u })
    {
        TimelineModel model;
        model.frames.configure ((double) rate, 16.0);
        const auto number = rate * 16 / 32;
        for (std::uint32_t i = 0; i < number; ++i)
        {
            auto input = frame (1, rate, i * 32);
            input.periodSamples = 32;
            GF_EXPECT (model.pushFrame (input, (std::int64_t) input.posLo));
        }
        GF_EXPECT (model.frames.size() == (int) number);
        GF_EXPECT (model.frames.at (0).position == 0);
        GF_EXPECT (model.frames.at (model.frames.size() - 1).position + 32 == (std::int64_t) rate * 16);
        GF_EXPECT (model.frames.memoryBytes() + sizeof (TimelineModel) < 1500000);
    }
}

GF_TEST (TimelineEpoch, history_allocation_tracks_the_released_producer_period_and_leaves_snapshot_headroom)
{
    const std::pair<double, std::uint32_t> cases[] {
        { 8000.0, 32 }, { 16000.0, 32 }, { 32000.0, 32 }, { 33599.0, 32 }, { 33600.0, 64 },
        { 44100.0, 64 }, { 48000.0, 64 }, { 96000.0, 128 }, { 192000.0, 288 }
    };
    for (const auto& [rate, expectedPeriod] : cases)
    {
        GF_EXPECT (visualisation::periodForRate (rate) == expectedPeriod);
        TimelineModel model;
        model.frames.configure (rate, 16.0);
        const auto required = (int) std::ceil (rate * 16.0 / (double) expectedPeriod);
        GF_EXPECT (model.frames.capacity() == required);
        // The full native view test additionally measures its frozen buffers
        // and text. This reserves their entire bounded structured storage here.
        const auto structuredCaches = 2048 * sizeof (FrameHistory::Column)
                                    + TimelineModel::capacity * sizeof (TimelineEvent)
                                    + 20 * sizeof (TimelineEvent);
        GF_EXPECT (model.frames.memoryBytes() + sizeof (TimelineModel) + structuredCaches < 1200000);
    }
}

GF_TEST (TimelineEpoch, canonical_list_keeps_sixteen_gap_outcomes_at_or_before_the_inspected_edge)
{
    TimelineModel model;
    for (int i = 0; i < 30; ++i)
    {
        TimelineEvent outcome;
        outcome.type = i % 5 == 0 ? EventType::Preview : EventType::GapEval;
        outcome.pos = i * 1000;
        model.push (outcome);
        auto marker = outcome;
        marker.type = EventType::Onset;
        model.push (marker);
    }
    TimelineEvent outcomes[16];
    GF_EXPECT (model.latestOutcomes (outcomes, 16, 24000) == 16);
    for (int i = 0; i < 16; ++i)
    {
        GF_EXPECT (outcomes[i].pos == (24 - i) * 1000);
        GF_EXPECT (outcomes[i].type == EventType::GapEval || outcomes[i].type == EventType::Preview);
    }
    GF_EXPECT (model.latestOutcomes (outcomes, 16, -1) == 0);
    // Canvas callers retain the shipped generic-event contract.
    TimelineEvent generic[20];
    GF_EXPECT (model.latest (generic, 20) == 20);
    GF_EXPECT (generic[0].type == EventType::Onset && generic[0].pos == 29000);

    FillEvent started;
    started.type = FillEventType::Started;
    started.recipe.algo = 2;
    started.recipe.lengthMs = 123;
    TimelineEvent converted[2];
    GF_EXPECT (fromEngineEvent (started, converted) == 2);
    GF_EXPECT (converted[0].type == EventType::GapEval);
    GF_EXPECT (converted[0].algo == started.recipe.algo && converted[0].lenMs == started.recipe.lengthMs);
    started.type = FillEventType::LockSet;
    GF_EXPECT (fromEngineEvent (started, converted) == 0);
    started.type = FillEventType::LockCleared;
    GF_EXPECT (fromEngineEvent (started, converted) == 0);
}
