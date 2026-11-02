#include "../TestFramework.h"
#include "../support/AllocGuard.h"
#include "features/capture2/CaptureLatch.h"

using namespace gapfill;
using namespace gftest;

namespace
{
    float sampleAt (std::int64_t index) { return (float) (index % 65521) * (0.5f / 65521.0f); }
    struct CaptureRig
    {
        explicit CaptureRig (double rate)
        {
            source.prepare (2, (int) (rate * 6));
            latch.prepare (rate, 2);
            latch.setDetailed (true);
        }
        void step (bool held, int count)
        {
            for (int channel = 0; channel < 2; ++channel)
                for (int i = 0; i < count; ++i) input.setSample (channel, i, sampleAt (position + i));
            if (! held) source.write (input, 0, count);
            latch.update (held, input, 0, count, source);
            position += count;
        }
        SourceHistory source;
        CaptureLatch latch;
        juce::AudioBuffer<float> input { 2, 8192 };
        std::int64_t position = 0;
    };
}

GF_TEST (CaptureDetailed, live_tap_is_held_only_and_short_requests_are_explicit)
{
    CaptureRig rig (48000);
    rig.step (false, 8192);
    GF_EXPECT (rig.latch.getLiveTap().getWritePosition() == 0);
    rig.latch.requestRecapture();
    GF_EXPECT (rig.latch.getDetailNotice() == CaptureLatch::NotHeld && ! rig.latch.isPending());
    rig.step (true, 64);
    rig.latch.requestRecapture();
    GF_EXPECT (rig.latch.getDetailNotice() == CaptureLatch::Short && ! rig.latch.isPending());
    GF_EXPECT (rig.latch.getMinimumHoldRemainingMs() > 0);
    rig.step (false, 64);
    GF_EXPECT (rig.latch.getStatus() == CaptureLatch::Off);
    GF_EXPECT (rig.latch.getLiveTap().getWritePosition() == 0);
}

GF_TEST (CaptureDetailed, busy_requests_do_not_replace_the_inflight_generation_or_window)
{
    CaptureRig rig (48000);
    rig.step (false, 8192);
    for (int i = 0; i < 16; ++i) rig.step (true, 4096);
    const auto end = rig.latch.getLiveTap().getWritePosition();
    rig.latch.requestRecapture();
    rig.step (true, 4096);
    const auto ticket = rig.latch.getCopyDescriptor();
    GF_EXPECT (rig.latch.isPending() && ticket.generation != 0);
    GF_EXPECT (ticket.liveStart + ticket.total == end);
    rig.latch.requestRecapture();
    GF_EXPECT (rig.latch.getDetailNotice() == CaptureLatch::Busy);
    GF_EXPECT (rig.latch.getCopyDescriptor().generation == ticket.generation);
    for (int i = 0; i < 20 && rig.latch.isPending(); ++i) rig.step (true, 4096);
    GF_EXPECT (! rig.latch.isPending() && rig.latch.getSnapshotCount() == 1);
    GF_EXPECT (rig.latch.getStatus() == CaptureLatch::HeldRecaptured);
    GF_EXPECT (rig.latch.getActiveSnapshotGeneration() == ticket.generation);
}

GF_TEST (CaptureDetailed, copied_pcm_ends_at_the_request_and_ticket_survives_same_chunk_completion)
{
    CaptureRig rig (8000);
    rig.latch.setLengthMs (250);
    rig.step (false, 4096);
    const auto heldStart = rig.position;
    rig.step (true, 2048);
    rig.latch.requestRecapture();
    rig.step (true, rig.latch.maxDetailedChunkSamples());
    const auto ticket = rig.latch.getCopyDescriptor();
    GF_EXPECT (! rig.latch.isPending() && ticket.total == 2000);
    GF_EXPECT (ticket.liveStart == 48 && ticket.generation != 0);
    const auto* snapshot = rig.latch.getActiveSnapshot();
    GF_EXPECT (snapshot != nullptr && snapshot->getWritePosition() == 2000);
    for (int i = 0; i < 2000; ++i)
        GF_EXPECT (snapshot->read (0, (double) i) == sampleAt (heldStart + ticket.liveStart + i));
}

GF_TEST (CaptureDetailed, overwritten_pending_pcm_is_rejected_instead_of_published)
{
    CaptureRig rig (8000);
    rig.latch.setLengthMs (2000);
    rig.step (false, 8192);
    for (int i = 0; i < 3; ++i) rig.step (true, 8192);
    rig.latch.requestRecapture();
    rig.step (true, 8192); // deliberately violates the adapter's safe servicing bound
    GF_EXPECT (rig.latch.getDetailNotice() == CaptureLatch::Overwritten);
    GF_EXPECT (! rig.latch.isPending() && rig.latch.getActiveSnapshot() == nullptr);
    GF_EXPECT (rig.latch.getSnapshotCount() == 0);
}

GF_TEST (CaptureDetailed, bounded_service_completes_two_seconds_at_every_supported_rate_without_allocating)
{
    for (const auto rate : { 8000.0, 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        CaptureRig rig (rate);
        rig.latch.setLengthMs (2000);
        rig.step (false, 8192);
        const int chunk = rig.latch.maxDetailedChunkSamples();
        const auto heldStart = rig.position;
        while (rig.latch.getLiveTap().getWritePosition() < (std::int64_t) (rate * 2)) rig.step (true, chunk);
        rig.latch.requestRecapture();
        int rounds = 0;
        {
            AllocGuard guard;
            do { rig.step (true, chunk); ++rounds; } while (rig.latch.isPending() && rounds < 100);
            GF_EXPECT (guard.count() == 0);
        }
        GF_EXPECT (rounds <= (int) std::ceil (rate * 2 / 4096));
        GF_EXPECT (rig.latch.getMaxCopyCount() <= 4096 && rig.latch.getSnapshotCount() == 1);
        const auto ticket = rig.latch.getCopyDescriptor();
        const auto* snapshot = rig.latch.getActiveSnapshot();
        GF_EXPECT (snapshot != nullptr && snapshot->getWritePosition() == ticket.total);
        for (int i = 0; i < ticket.total; i += 97)
            GF_EXPECT (snapshot->read (0, (double) i) == sampleAt (heldStart + ticket.liveStart + i));
    }
}

GF_TEST (CaptureDetailed, release_cancels_copy_and_future_recaptures_never_reuse_a_generation)
{
    CaptureRig rig (48000);
    for (int i = 0; i < 16; ++i) rig.step (true, 4096);
    rig.latch.requestRecapture(); rig.step (true, 4096);
    const auto old = rig.latch.getCopyDescriptor().generation;
    rig.step (false, 64);
    GF_EXPECT (! rig.latch.isPending() && rig.latch.getActiveSnapshotGeneration() == 0);
    for (int i = 0; i < 16; ++i) rig.step (true, 4096);
    rig.latch.requestRecapture(); rig.step (true, 4096);
    GF_EXPECT (rig.latch.getCopyDescriptor().generation > old);
}
