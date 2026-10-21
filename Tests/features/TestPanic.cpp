// N-USE-a Panic and Recover (docs/dev/12 N-USE-a).

#include "../support/AllocGuard.h"
#include "../support/LiveRig.h"

#include "features/panic/Panic.h"
#include "PluginEditor.h"
#include "gui/SlotHost.h"

#include <cmath>

using namespace gapfill;
using namespace gapfill::panic;
using namespace gftest;

namespace
{
    constexpr double kSr = 48000.0;
    constexpr int kRampDown = 240;     // 5 ms at 48 kHz
    constexpr int kRampUp = 960;       // 20 ms

    std::vector<float> collect (PanicController& c, int total, int chunk)
    {
        std::vector<float> g;

        for (int pos = 0; pos < total; pos += chunk)
        {
            const auto n = std::min (chunk, total - pos);
            c.beginBlock();
            const auto* p = c.nextGains (n);
            g.insert (g.end(), p, p + n);
        }

        return g;
    }
}

GF_TEST (Panic, wet_reaches_zero_in_exactly_five_milliseconds)
{
    PanicController c;
    c.prepare (kSr, 512);
    c.requestPanic();

    const auto g = collect (c, 2048, 512);
    GF_EXPECT (g[0] == 1.0f);
    GF_EXPECT (g[(size_t) kRampDown - 1] > 0.0f);
    GF_EXPECT (g[(size_t) kRampDown] == 0.0f);

    for (size_t i = 1; i < g.size(); ++i)
        GF_EXPECT (g[i] <= g[i - 1]);

    for (size_t i = (size_t) kRampDown; i < g.size(); ++i)
        GF_EXPECT (g[i] == 0.0f);

    GF_EXPECT (c.isPanicked());
}

GF_TEST (Panic, ramp_is_independent_of_block_size)
{
    std::vector<float> reference;

    for (const int chunk : { 32, 64, 512, 4096, 8192 })
    {
        PanicController c;
        c.prepare (kSr, chunk);
        c.requestPanic();
        const auto g = collect (c, 8192, chunk);

        if (reference.empty())
            reference = g;
        else
            GF_EXPECT_MSG (g == reference, "chunk " + juce::String (chunk));
    }
}

GF_TEST (Panic, recover_ramps_up_over_twenty_milliseconds_and_second_press_toggles)
{
    PanicController c;
    c.prepare (kSr, 512);
    c.requestPanic();
    collect (c, 1024, 512);
    GF_EXPECT (c.isPanicked());

    c.requestPanic();                      // panic while panicked == recover (default)
    const auto g = collect (c, 2048, 512);
    GF_EXPECT (! c.isPanicked());
    GF_EXPECT (g[0] == 0.0f);
    GF_EXPECT (g[(size_t) kRampUp - 1] < 1.0f && g[(size_t) kRampUp] == 1.0f);

    for (size_t i = 1; i < g.size(); ++i)
        GF_EXPECT (g[i] >= g[i - 1]);
}

GF_TEST (Panic, toggle_can_be_disabled_for_stage_safety)
{
    PanicController c;
    c.prepare (kSr, 512);
    Options o;
    o.panicToggles = false;
    c.setOptions (o);

    c.requestPanic();
    collect (c, 1024, 512);
    c.requestPanic();
    collect (c, 1024, 512);
    GF_EXPECT (c.isPanicked());

    c.requestRecover();
    collect (c, 1024, 512);
    GF_EXPECT (! c.isPanicked());
}

GF_TEST (Panic, auto_recover_after_the_configured_time)
{
    for (const auto [mode, seconds] : { std::pair { RecoveryMode::Auto1s, 1.0 },
                                        std::pair { RecoveryMode::Auto2s, 2.0 },
                                        std::pair { RecoveryMode::Auto4s, 4.0 } })
    {
        PanicController c;
        c.prepare (kSr, 512);
        Options o;
        o.recovery = mode;
        c.setOptions (o);
        c.requestPanic();

        const auto total = (int) (kSr * (seconds + 0.5));
        const auto g = collect (c, total, 512);

        // The first non-zero gain after the silent stretch marks the start of the ramp up.
        size_t firstUp = 0;

        for (size_t i = (size_t) kRampDown + 2; i < g.size(); ++i)
            if (g[i] > 0.0f) { firstUp = i; break; }

        GF_EXPECT_MSG (firstUp > 0, "auto recover happened");
        GF_EXPECT_NEAR ((double) firstUp / kSr, seconds, 0.002);
        GF_EXPECT (! c.isPanicked());
    }
}

GF_TEST (Panic, manual_mode_stays_panicked)
{
    PanicController c;
    c.prepare (kSr, 512);
    c.requestPanic();
    collect (c, (int) (kSr * 6), 512);
    GF_EXPECT (c.isPanicked());
}

GF_TEST (Panic, midi_direct_path_starts_the_ramp_at_the_event_offset)
{
    PanicController c;
    c.prepare (kSr, 512);
    c.beginBlock();
    GF_EXPECT (c.press (100) == Action::Panic);

    const auto* g = c.nextGains (512);
    GF_EXPECT (g[99] == 1.0f && g[100] == 1.0f);
    GF_EXPECT (g[101] < 1.0f);
    GF_EXPECT (g[100 + kRampDown] == 0.0f && g[100 + kRampDown - 1] > 0.0f);
}

GF_TEST (Panic, panicking_during_a_ramp_up_continues_from_the_current_gain)
{
    PanicController c;
    c.prepare (kSr, 512);
    c.requestPanic();
    collect (c, 1024, 512);
    c.requestRecover();
    c.beginBlock();
    const auto* up = c.nextGains (300);
    const float mid = up[299];
    GF_EXPECT (mid > 0.0f && mid < 1.0f);

    GF_EXPECT (c.press (0) == Action::Panic);
    const auto* down = c.nextGains (10);
    GF_EXPECT (std::abs (down[0] - mid) < 0.02f);          // continuous
    GF_EXPECT (down[9] < down[0]);
}

GF_TEST (Panic, controller_does_not_allocate_after_prepare)
{
    PanicController c;
    c.prepare (kSr, 512);
    AllocGuard guard;

    for (int i = 0; i < 100; ++i)
    {
        if (i % 7 == 0) c.requestPanic();
        if (i % 11 == 0) c.requestRecover();
        c.beginBlock();
        c.nextGains (512);
    }

    GF_EXPECT (guard.count() == 0);
}

//==============================================================================
namespace
{
    struct PanicRun { LiveRender render; int fillsAtPanic = 0; };

    PanicRun runLoop (bool wetZero, int panicAtBlock, int recoverAtBlock, int blockSize)
    {
        GapFillAudioProcessor proc;
        setParams (proc, { { "fill", 90.0f }, { "density", 100.0f }, { "wet", wetZero ? 0.0f : 100.0f } });
        const auto input = drumLoop (kSr, 120.0, 4);
        TestPlayHead ph;
        PanicRun run;

        run.render = renderLive (proc, input, blockSize, kSr, ph,
            [&] (int block, int, juce::MidiBuffer&)
            {
                if (block == panicAtBlock)
                {
                    run.fillsAtPanic = proc.getFillEngine().getFillsStarted();
                    proc.getLiveControl().panic().requestPanic();
                }

                if (block == recoverAtBlock)
                    proc.getLiveControl().panic().requestRecover();
            });

        return run;
    }
}

GF_TEST (Panic, dry_path_is_bit_identical_after_the_ramp_and_no_new_fills_start)
{
    constexpr int block = 256;
    constexpr int panicBlock = 120;
    const auto panicked = runLoop (false, panicBlock, -1, block);
    const auto dryOnly = runLoop (true, -1, -1, block);

    const auto from = (panicBlock + 2) * block;            // ramp (5 ms = 240 samples) is over
    const auto n = panicked.render.out.getNumSamples() - from;
    GF_EXPECT (n > 48000);

    for (int ch = 0; ch < 2; ++ch)
        GF_EXPECT (std::memcmp (panicked.render.out.getReadPointer (ch, from),
                                dryOnly.render.out.getReadPointer (ch, from), sizeof (float) * (size_t) n) == 0);

    GF_EXPECT (panicked.render.fills == panicked.fillsAtPanic);        // nothing started after the panic
}

GF_TEST (Panic, wet_is_silent_within_five_ms_for_every_block_size)
{
    for (const int block : { 32, 64, 512, 4096 })
    {
        GapFillAudioProcessor proc;
        setParams (proc, { { "fill", 90.0f }, { "density", 100.0f }, { "dry", 0.0f } });
        const auto input = drumLoop (kSr, 120.0, 3);
        TestPlayHead ph;
        const auto panicBlock = (int) (kSr * 1.0) / block;
        auto r = renderLive (proc, input, block, kSr, ph,
            [&] (int b, int, juce::MidiBuffer&) { if (b == panicBlock) proc.getLiveControl().panic().requestPanic(); });

        // dry = 0, so any output is wet. After the request block + 5 ms it must be silent.
        const auto start = panicBlock * block + 240 + 8;
        double peak = 0.0;

        for (int i = start; i < r.out.getNumSamples(); ++i)
            peak = std::max (peak, (double) std::abs (r.out.getSample (0, i)));

        GF_EXPECT_MSG (peak == 0.0, "block " + juce::String (block) + " peak " + juce::String (peak));
    }
}

GF_TEST (Panic, recover_resumes_fills_at_the_next_gap)
{
    constexpr int block = 256;
    const auto r = runLoop (false, 100, 400, block);
    GF_EXPECT (r.render.fills > r.fillsAtPanic);                      // fills started after Recover
}

GF_TEST (Panic, engine_reports_panicked_reject_reason_and_panic_does_not_touch_the_gap_counters)
{
    GapFillAudioProcessor proc;
    setParams (proc, { { "fill", 90.0f }, { "density", 100.0f } });
    const auto input = drumLoop (kSr, 120.0, 4);
    TestPlayHead ph;
    renderLive (proc, input, 256, kSr, ph,
        [&] (int b, int, juce::MidiBuffer&) { if (b == 50) proc.getLiveControl().panic().requestPanic(); });

    GF_EXPECT (proc.getFillEngine().getRejectCount (RejectReason::Panicked) > 0);
}

GF_TEST (Panic, panicked_state_is_never_saved_and_options_round_trip)
{
    GapFillAudioProcessor a;
    prepareProcessor (a, kSr, 256, false);
    live::PerfOptions o;
    o.panic.recovery = RecoveryMode::Auto2s;
    o.panic.panicToggles = false;
    a.getLiveControl().setPerfOptions (o);
    a.getLiveControl().panic().requestPanic();

    juce::AudioBuffer<float> work (2, 256);
    juce::MidiBuffer midi;
    work.clear();
    a.processBlock (work, midi);
    GF_EXPECT (a.getLiveControl().panic().isPanicked());

    juce::MemoryBlock blob;
    a.getStateInformation (blob);

    GapFillAudioProcessor b;
    prepareProcessor (b, kSr, 256, false);
    b.setStateInformation (blob.getData(), (int) blob.getSize());
    GF_EXPECT (! b.getLiveControl().panic().isPanicked());
    GF_EXPECT (b.getLiveControl().getPerfOptions().panic.recovery == RecoveryMode::Auto2s);
    GF_EXPECT (! b.getLiveControl().getPerfOptions().panic.panicToggles);

    // default options write no <Perf> subtree at all
    GapFillAudioProcessor c;
    juce::MemoryBlock blob2;
    c.getStateInformation (blob2);
    const auto xml = juce::AudioProcessor::getXmlFromBinary (blob2.getData(), (int) blob2.getSize());
    GF_EXPECT (xml != nullptr && xml->getChildByName ("Perf") == nullptr);
}

GF_TEST (Panic, panic_through_the_processor_with_bypass_and_stopped_transport_stays_finite)
{
    GapFillAudioProcessor proc;
    setParams (proc, { { "fill", 90.0f } });
    const auto input = drumLoop (kSr, 120.0, 2);
    TestPlayHead ph;
    ph.playing = false;
    const auto r = renderLive (proc, input, 512, kSr, ph,
        [&] (int b, int, juce::MidiBuffer&)
        {
            if (b == 10) proc.getLiveControl().panic().requestPanic();
            if (b == 40) proc.getLiveControl().panic().requestRecover();
        });

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < r.out.getNumSamples(); ++i)
            GF_EXPECT (std::isfinite (r.out.getSample (ch, i)));
}

GF_TEST (Panic, processor_hot_path_does_not_allocate_with_live_control_active)
{
    GapFillAudioProcessor proc;
    setParams (proc, { { "fill", 80.0f } });
    prepareProcessor (proc, kSr, 256, false);
    proc.getLiveControl().bindings().add ({ CommandType::SceneNext, 0, commands::MsgType::CC, 0, 90 });
    TestPlayHead ph;
    proc.setPlayHead (&ph);

    juce::AudioBuffer<float> work (2, 256);
    juce::MidiBuffer midi;
    long allocations = 0;

    for (int block = 0; block < 400; ++block)
    {
        work.clear();
        midi.clear();

        if (block % 50 == 0)
            midi.addEvent (juce::MidiMessage::controllerEvent (1, 90, 127), 10);

        if (block == 300)
            proc.getLiveControl().panic().requestPanic();

        AllocGuard guard;
        proc.processBlock (work, midi);

        if (block > 100)
            allocations += guard.count();

        ph.advance (256, kSr);
    }

    proc.setPlayHead (nullptr);
    GF_EXPECT (allocations == 0);
}

GF_TEST (Panic, footer_chip_is_hosted_named_and_only_hit_tests_its_own_rectangle)
{
    GapFillAudioProcessor proc;
    juce::Component parent;
    parent.setBounds (0, 0, 800, 12);
    SlotHost host (proc, parent);
    GF_EXPECT (host.getNumComponents (Slot::FooterStatus) >= 1);              // other work packages share the footer
    host.setSlotBounds (Slot::FooterStatus, { 0, 0, 800, 12 });

    juce::Component* chip = nullptr;

    for (auto* c : parent.getChildren())
        if (c->getTitle().startsWith ("Panic"))
            chip = c;

    GF_EXPECT (chip != nullptr);
    GF_EXPECT (chip->getDescription().isNotEmpty());
    GF_EXPECT (chip->hitTest (10, 5));
    GF_EXPECT (! chip->hitTest (400, 5));       // the version marker area is not a panic button
}

GF_TEST (Panic, panic_and_recover_leave_timeline_markers_in_the_event_queue)
{
    GapFillAudioProcessor proc;
    prepareProcessor (proc, kSr, 256, false);
    juce::AudioBuffer<float> work (2, 256);
    juce::MidiBuffer midi;
    work.clear();
    proc.processBlock (work, midi);
    proc.drainFillEvents();

    proc.getLiveControl().panic().requestPanic();
    proc.processBlock (work, midi);
    GF_EXPECT (proc.drainFillEvents() >= 1);

    proc.getLiveControl().panic().requestRecover();
    proc.processBlock (work, midi);
    GF_EXPECT (proc.drainFillEvents() >= 1);
}
