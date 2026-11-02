#include "../TestFramework.h"
#include "features/export/ExportService.h"

#include <atomic>
#include <chrono>
#include <semaphore>
#include <thread>

using namespace gapfill;
using namespace gapfill::exporting;

namespace
{
    struct Gate
    {
        explicit Gate (ExportService::ProbePoint at) : point (at) {}
        static void callback (void* context, ExportService::ProbePoint at) noexcept
        {
            auto& gate = *static_cast<Gate*> (context);
            if (at == gate.point && ! gate.enteredOnce.exchange (true))
            {
                gate.entered.release();
                gate.proceed.acquire();
            }
        }
        void release() { if (! released.exchange (true)) proceed.release(); }
        ExportService::ProbePoint point;
        std::binary_semaphore entered { 0 }, proceed { 0 };
        std::atomic<bool> enteredOnce { false }, released { false };
    };

    struct ReleaseAndJoin
    {
        Gate& gate;
        std::thread& thread;
        ~ReleaseAndJoin() { gate.release(); if (thread.joinable()) thread.join(); }
    };

    struct ExportFixture
    {
        explicit ExportFixture (double rate = 8000.0) { service.prepare (rate); service.armForMs (60000); input.setSize (2, 256); }
        void block (int samples = 256)
        {
            if (input.getNumSamples() != samples) input.setSize (2, samples);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < samples; ++i)
                    input.setSample (ch, i, (float) (position + i) / 1000000.0f);
            service.process (input, 0, samples, position);
            position += samples;
        }
        ExportService service;
        juce::AudioBuffer<float> input;
        std::int64_t position = 0;
    };
}

GF_TEST (ExportOwnership, audio_claim_prevents_ui_from_reusing_the_request_fields)
{
    ExportFixture fixture;
    for (int i = 0; i < 32; ++i) fixture.block();
    GF_EXPECT (fixture.service.request ({ 1000, 6000 }) == RequestResult::Accepted);
    Gate gate (ExportService::ProbePoint::AfterClaim);
    fixture.service.setProbeForTests (&Gate::callback, &gate);
    std::thread audio ([&] { fixture.block(); });
    ReleaseAndJoin finish { gate, audio };
    GF_EXPECT (gate.entered.try_acquire_for (std::chrono::seconds (2)));
    // Audio is about to read the plain header. A successful UI cancellation here
    // would permit request() to overwrite it while that read is in flight.
    GF_EXPECT (! fixture.service.cancelPending());
    GF_EXPECT (fixture.service.request ({ 2000, 1000 }) == RequestResult::Busy);
}

GF_TEST (ExportOwnership, cancelling_a_claim_waits_for_audio_acknowledgement_before_reuse)
{
    ExportFixture fixture;
    for (int i = 0; i < 32; ++i) fixture.block();
    GF_EXPECT (fixture.service.request ({ 1000, 6000 }) == RequestResult::Accepted);
    Gate gate (ExportService::ProbePoint::AfterClaim);
    fixture.service.setProbeForTests (&Gate::callback, &gate);
    std::thread audio ([&] { fixture.block(); });
    ReleaseAndJoin finish { gate, audio };
    GF_EXPECT (gate.entered.try_acquire_for (std::chrono::seconds (2)));
    GF_EXPECT (fixture.service.cancel());
    GF_EXPECT (fixture.service.getState() == ExportState::Claimed);
    GF_EXPECT (fixture.service.request ({ 2000, 1000 }) == RequestResult::Busy);
    juce::AudioBuffer<float> untouched (1, 1);
    untouched.setSample (0, 0, 0.75f);
    Failure why;
    GF_EXPECT (! fixture.service.take (untouched, why) && why == Failure::None);
    GF_EXPECT (untouched.getNumChannels() == 1 && untouched.getSample (0, 0) == 0.75f);
    gate.release();
    audio.join();
    GF_EXPECT (fixture.service.getState() == ExportState::Idle);
    GF_EXPECT (fixture.service.request ({ 2000, 1000 }) == RequestResult::Accepted);
}

GF_TEST (ExportOwnership, cancellation_before_claim_cannot_make_audio_read_a_mixed_request)
{
    ExportFixture fixture;
    for (int i = 0; i < 32; ++i) fixture.block();
    GF_EXPECT (fixture.service.request ({ 1000, 6000 }) == RequestResult::Accepted);
    Gate gate (ExportService::ProbePoint::BeforeClaim);
    fixture.service.setProbeForTests (&Gate::callback, &gate);
    std::thread audio ([&] { fixture.block(); });
    ReleaseAndJoin finish { gate, audio };
    GF_EXPECT (gate.entered.try_acquire_for (std::chrono::seconds (2)));
    GF_EXPECT (fixture.service.cancelPending());
    GF_EXPECT (fixture.service.request ({ 2000, 1000 }) == RequestResult::Accepted);
    gate.release();
    audio.join();
    juce::AudioBuffer<float> output;
    Failure why;
    GF_EXPECT (fixture.service.take (output, why));
    GF_EXPECT (output.getNumSamples() == 1400); // exactly the second header + 20/30 ms edges
    GF_EXPECT_NEAR (output.getSample (0, 0), 0.00184f, 0.0000001);
    GF_EXPECT_NEAR (output.getSample (0, 1399), 0.003239f, 0.0000001);
}

GF_TEST (ExportOwnership, stopped_copy_cancellation_keeps_audio_storage_owned_until_resumption)
{
    ExportFixture fixture;
    for (int i = 0; i < 32; ++i) fixture.block();
    GF_EXPECT (fixture.service.request ({ 1000, 6000 }) == RequestResult::Accepted);
    fixture.block();
    GF_EXPECT (fixture.service.getState() == ExportState::Copying);
    GF_EXPECT (fixture.service.cancel());
    GF_EXPECT (fixture.service.getState() == ExportState::Copying);
    GF_EXPECT (fixture.service.request ({ 2000, 1000 }) == RequestResult::Busy);
    fixture.block(); // the next real audio callback acknowledges the cancelled generation
    GF_EXPECT (fixture.service.getState() == ExportState::Idle);
    GF_EXPECT (fixture.service.request ({ 2000, 1000 }) == RequestResult::Accepted);
}

GF_TEST (ExportOwnership, cancel_ready_releases_storage_without_another_audio_callback)
{
    ExportFixture fixture;
    for (int i = 0; i < 32; ++i) fixture.block();
    GF_EXPECT (fixture.service.request ({ 2000, 1000 }) == RequestResult::Accepted);
    fixture.block();
    GF_EXPECT (fixture.service.getState() == ExportState::Ready);
    GF_EXPECT (fixture.service.cancel());
    GF_EXPECT (fixture.service.getState() == ExportState::Idle);
    GF_EXPECT (fixture.service.request ({ 3000, 1000 }) == RequestResult::Accepted);
}

GF_TEST (ExportOwnership, cancel_at_completion_never_publishes_old_audio_to_the_ui)
{
    ExportFixture fixture;
    for (int i = 0; i < 32; ++i) fixture.block();
    GF_EXPECT (fixture.service.request ({ 2000, 1000 }) == RequestResult::Accepted);
    Gate gate (ExportService::ProbePoint::BeforePublish);
    fixture.service.setProbeForTests (&Gate::callback, &gate);
    std::thread audio ([&] { fixture.block(); });
    ReleaseAndJoin finish { gate, audio };
    GF_EXPECT (gate.entered.try_acquire_for (std::chrono::seconds (2)));
    GF_EXPECT (fixture.service.cancel());
    GF_EXPECT (fixture.service.getState() == ExportState::Copying);
    gate.release();
    audio.join();
    juce::AudioBuffer<float> output;
    Failure why;
    GF_EXPECT (! fixture.service.take (output, why));
    GF_EXPECT (fixture.service.request ({ 3000, 1000 }) == RequestResult::Accepted);
}

GF_TEST (ExportOwnership, ring_overwrite_fails_before_returning_corrupt_audio)
{
    for (const auto rate : { 8000.0, 44100.0, 48000.0, 192000.0 })
    {
        ExportFixture fixture (rate);
        const auto blocks = (fixture.service.getRingSize() + 8191) / 8192 + 4;
        for (int i = 0; i < blocks; ++i) fixture.block (8192);
        const auto firstRead = fixture.position + 8192 - fixture.service.getRingSize() + (std::int64_t) (0.5 * rate) + 4;
        const auto lead = (std::int64_t) std::llround (kLeadInSeconds * rate);
        GF_EXPECT (fixture.service.request ({ firstRead + lead, (int) (2.0 * rate) }) == RequestResult::Accepted);
        fixture.block (8192);
        GF_EXPECT (fixture.service.getState() == ExportState::Copying);
        for (int i = 0; i < 100 && fixture.service.getState() == ExportState::Copying; ++i) fixture.block (8192);
        juce::AudioBuffer<float> output;
        Failure why;
        GF_EXPECT (! fixture.service.take (output, why));
        GF_EXPECT (why == Failure::TooOld && output.getNumSamples() == 0);
        GF_EXPECT (fixture.service.getMaxCopyPerBlock() <= kCopyPerBlock);
    }
}

GF_TEST (ExportOwnership, source_discontinuity_cancels_a_copy_instead_of_mixing_generations)
{
    ExportFixture fixture;
    for (int i = 0; i < 32; ++i) fixture.block();
    GF_EXPECT (fixture.service.request ({ 1000, 6000 }) == RequestResult::Accepted);
    fixture.block();
    GF_EXPECT (fixture.service.getState() == ExportState::Copying);
    fixture.position += 8000;
    fixture.block();
    juce::AudioBuffer<float> output;
    Failure why;
    GF_EXPECT (! fixture.service.take (output, why));
    GF_EXPECT (why == Failure::NotRecorded);
}

GF_TEST (ExportOwnership, preparation_invalidates_old_ui_generation_and_pending_audio)
{
    ExportFixture fixture;
    for (int i = 0; i < 32; ++i) fixture.block();
    GF_EXPECT (fixture.service.request ({ 1000, 6000 }) == RequestResult::Accepted);
    const auto generation = fixture.service.getGeneration();
    fixture.block();
    fixture.service.prepare (48000.0); // audio stopped; allocation stays outside audio ownership
    GF_EXPECT (fixture.service.getGeneration() != generation);
    GF_EXPECT (fixture.service.getState() == ExportState::Idle);
}

GF_TEST (ExportOwnership, cancelled_late_ready_result_can_be_reclaimed_after_callbacks_stop)
{
    ExportFixture fixture;
    for (int i = 0; i < 32; ++i) fixture.block();
    GF_EXPECT (fixture.service.request ({ 2000, 1000 }) == RequestResult::Accepted);
    Gate gate (ExportService::ProbePoint::BeforeReadyStore);
    fixture.service.setProbeForTests (&Gate::callback, &gate);
    std::thread audio ([&] { fixture.block(); });
    ReleaseAndJoin finish { gate, audio };
    GF_EXPECT (gate.entered.try_acquire_for (std::chrono::seconds (2)));
    GF_EXPECT (fixture.service.cancel());
    gate.release();
    audio.join();
    GF_EXPECT (fixture.service.getState() == ExportState::Ready);
    // No further audio callback: request() can retire this cancelled terminal
    // buffer, but must never perform that retirement for Claimed/Copying.
    GF_EXPECT (fixture.service.request ({ 3000, 1000 }) == RequestResult::Accepted);
    fixture.block();
    juce::AudioBuffer<float> output;
    Failure why;
    GF_EXPECT (fixture.service.take (output, why));
    GF_EXPECT_NEAR (output.getSample (0, 0), 0.00284f, 0.0000001);
}

GF_TEST (ExportOwnership, concurrent_requests_cancellations_and_takes_never_mix_header_or_audio_generations)
{
    ExportFixture fixture;
    std::atomic<std::int64_t> published { 0 };
    std::jthread audio ([&] (std::stop_token stop)
    {
        while (! stop.stop_requested())
        {
            fixture.block();
            published.store (fixture.position, std::memory_order_release);
            std::this_thread::yield();
        }
    });
    const auto start = juce::Time::getMillisecondCounter();
    const auto beforeDeadline = [&] { return juce::Time::getMillisecondCounter() - start < 10000u; };
    while (published.load (std::memory_order_acquire) < 10000 && beforeDeadline()) std::this_thread::yield();
    GF_EXPECT (published.load() >= 10000);
    int completed = 0;
    for (int i = 0; i < 1000; ++i)
    {
        FillSpan requested;
        RequestResult result = RequestResult::Busy;
        while (result == RequestResult::Busy && beforeDeadline())
        {
            requested = { published.load (std::memory_order_acquire) - 2000, 1000 };
            result = fixture.service.request (requested);
            if (result == RequestResult::Busy) std::this_thread::yield();
        }
        GF_EXPECT (result == RequestResult::Accepted);
        if (i % 3 != 0) { fixture.service.cancel(); continue; }
        juce::AudioBuffer<float> output;
        Failure why = Failure::None;
        bool taken = false;
        while (! taken && why == Failure::None && beforeDeadline())
        {
            taken = fixture.service.take (output, why);
            if (! taken && why == Failure::None) std::this_thread::yield();
        }
        GF_EXPECT (taken || why == Failure::TooOld || why == Failure::NotRecorded);
        if (taken)
        {
            GF_EXPECT (output.getNumSamples() == 1400);
            for (int sample = 0; sample < output.getNumSamples(); ++sample)
                GF_EXPECT_NEAR (output.getSample (0, sample),
                                (float) (requested.startPos - 160 + sample) / 1000000.0f, 0.000001);
            ++completed;
        }
    }
    audio.request_stop();
    audio.join();
    GF_EXPECT (completed > 0);
}

GF_TEST (ExportOwnership, preparation_serializes_buffer_reallocation_with_nonblocking_ui_operations)
{
    ExportFixture fixture;
    for (int i = 0; i < 32; ++i) fixture.block();
    GF_EXPECT (fixture.service.request ({ 2000, 1000 }) == RequestResult::Accepted);
    fixture.block();
    GF_EXPECT (fixture.service.getState() == ExportState::Ready);
    const auto generation = fixture.service.getGeneration();
    Gate gate (ExportService::ProbePoint::BeforePrepare);
    fixture.service.setProbeForTests (&Gate::callback, &gate);
    std::thread prepare ([&] { fixture.service.prepare (48000.0); }); // no audio callback in flight
    ReleaseAndJoin finish { gate, prepare };
    GF_EXPECT (gate.entered.try_acquire_for (std::chrono::seconds (2)));
    juce::AudioBuffer<float> untouched (1, 1);
    untouched.setSample (0, 0, 0.75f);
    Failure why;
    GF_EXPECT (! fixture.service.take (untouched, why) && why == Failure::None);
    GF_EXPECT (untouched.getNumChannels() == 1 && untouched.getSample (0, 0) == 0.75f);
    GF_EXPECT (fixture.service.request ({ 3000, 1000 }) == RequestResult::Busy);
    GF_EXPECT (! fixture.service.cancel());
    gate.release();
    prepare.join();
    GF_EXPECT (fixture.service.getGeneration() != generation);
    GF_EXPECT (fixture.service.getSampleRate() == 48000.0);
    GF_EXPECT (fixture.service.getState() == ExportState::Idle);
}

GF_TEST (ExportOwnership, completed_result_keeps_its_sample_rate_and_generation_after_reprepare)
{
    ExportFixture fixture;
    for (int i = 0; i < 32; ++i) fixture.block();
    GF_EXPECT (fixture.service.request ({ 2000, 1000 }) == RequestResult::Accepted);
    const auto generation = fixture.service.getGeneration();
    fixture.block();
    juce::AudioBuffer<float> output;
    Failure why;
    ResultInfo info;
    GF_EXPECT (fixture.service.take (output, why, info));
    fixture.service.prepare (48000.0);
    GF_EXPECT (info.sampleRate == 8000.0 && info.generation == generation);
    GF_EXPECT (output.getNumSamples() == 1400);
    GF_EXPECT_NEAR (output.getSample (0, 0), 0.00184f, 0.0000001);
}
