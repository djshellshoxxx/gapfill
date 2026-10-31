#include "../TestFramework.h"

#include "dsp/VisualisationState.h"
#include "features/timeline/Model.h"

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
