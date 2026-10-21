// F-S01 CPU and latency display (docs/dev/12 F-S01).

#include "../support/AllocGuard.h"
#include "../support/TestRig.h"

#include "features/telemetry/Telemetry.h"

using namespace gapfill;
using namespace gftest;

GF_TEST (Telemetry, cpu_estimate_monotonic_with_load)
{
    // Steady load: the estimate rises towards the load and orders loads correctly.
    float previous = -1.0f;

    for (const double load : { 0.02, 0.10, 0.30, 0.60 })
    {
        telemetry::CpuMeter meter;
        float last = 0.0f;

        for (int block = 0; block < 400; ++block)
        {
            const auto before = meter.getCpu01();
            meter.record (load * 0.010, 0.010);              // 10 ms blocks
            GF_EXPECT (meter.getCpu01() >= before - 1.0e-6f);   // never falls while the load is constant or rising
            last = meter.getCpu01();
        }

        GF_EXPECT_NEAR (last, load, 0.02);
        GF_EXPECT (last > previous);
        previous = last;
    }
}

GF_TEST (Telemetry, estimate_is_independent_of_the_block_size)
{
    telemetry::CpuMeter small, large;

    for (int i = 0; i < 4000; ++i)            // 2 s in 0.5 ms blocks
        small.record (0.25 * 0.0005, 0.0005);

    for (int i = 0; i < 40; ++i)              // 2 s in 50 ms blocks
        large.record (0.25 * 0.05, 0.05);

    GF_EXPECT_NEAR (small.getCpu01(), large.getCpu01(), 0.02);
}

GF_TEST (Telemetry, overruns_are_counted_and_garbage_input_is_ignored)
{
    telemetry::CpuMeter meter;
    meter.record (0.004, 0.010);
    GF_EXPECT (meter.getOverrunCount() == 0);
    meter.record (0.012, 0.010);
    GF_EXPECT (meter.getOverrunCount() == 1);

    const auto cpu = meter.getCpu01();
    meter.record (-1.0, 0.010);
    meter.record (std::nan (""), 0.010);
    meter.record (0.001, 0.0);
    GF_EXPECT (meter.getCpu01() == cpu && meter.getOverrunCount() == 1);
}

GF_TEST (Telemetry, labels_say_estimated)
{
    GF_EXPECT (telemetry::formatCpu (0.125f, true) == "CPU ~13% (estimated)" || telemetry::formatCpu (0.125f, true) == "CPU ~12% (estimated)");
    GF_EXPECT (telemetry::formatCpu (0.5f, true).contains ("estimated"));
    GF_EXPECT (! telemetry::formatCpu (0.5f, false).contains ("%"));
    GF_EXPECT (telemetry::describeTelemetry (512, 48000.0, 0.2f, true).contains ("not the host's CPU meter"));
    GF_EXPECT (telemetry::formatLatency (480, 48000.0) == "PDC 480 smp (10.0 ms)");
    GF_EXPECT (telemetry::formatLatency (-5, 48000.0) == "PDC 0 smp (0.0 ms)");
}

GF_TEST (Telemetry, latency_matches_reported)
{
    GapFillAudioProcessor proc;
    setParams (proc, { { "lookahead", 40.0f } });
    prepareProcessor (proc, 48000.0, 512, false);
    proc.flushLatencyReportForTests();

    GF_EXPECT (proc.getLatencySamples() == 1920);
    const auto text = telemetry::formatLatency (proc.getLatencySamples(), 48000.0);
    GF_EXPECT (text == "PDC 1920 smp (40.0 ms)");
    GF_EXPECT (proc.getMeterState().latencySamples.load() == proc.getLatencySamples());
}

GF_TEST (Telemetry, disabled_when_editor_closed_zero_cost)
{
    // Disabled: begin() hands back the "off" token and end() changes nothing.
    telemetry::CpuMeter meter;
    GF_EXPECT (! meter.isEnabled());
    GF_EXPECT (meter.begin() < 0);
    meter.end (meter.begin(), 512, 48000.0);
    GF_EXPECT (meter.getCpu01() == 0.0f);

    // A processor without an editor (no visualiser) never measures.
    GapFillAudioProcessor closed;
    renderGoldenScenarioA (closed);
    closed.flushLatencyReportForTests();
    GF_EXPECT (! closed.getExplainState().cpu.isEnabled());
    GF_EXPECT (closed.getExplainState().cpu.getCpu01() == 0.0f);

    // With the visualiser (editor open) the timer enables it and blocks are measured.
    GapFillAudioProcessor open;
    open.getVisualisationFifo().setEnabled (true);
    open.flushLatencyReportForTests();
    GF_EXPECT (open.getExplainState().cpu.isEnabled());
    renderGoldenScenarioA (open);
    GF_EXPECT (open.getExplainState().cpu.getCpu01() > 0.0f);
}

GF_TEST (Telemetry, measuring_does_not_allocate_or_change_the_sound)
{
    constexpr double sr = 48000.0;
    constexpr int block = 512;

    GapFillAudioProcessor measured, silent;
    measured.getVisualisationFifo().setEnabled (true);
    measured.flushLatencyReportForTests();
    setParams (measured, { { "fill", 80.0f } });
    setParams (silent, { { "fill", 80.0f } });

    const auto a = renderGoldenScenarioA (measured);
    const auto b = renderGoldenScenarioA (silent);
    GF_EXPECT (compareBuffers (a.out, b.out, 0.0).firstBad < 0);       // bit-identical

    // Steady state: zero allocations with the meter, health monitor and recorder running.
    GapFillAudioProcessor proc;
    proc.getVisualisationFifo().setEnabled (true);
    proc.flushLatencyReportForTests();
    proc.getExplainState().recorder.start ("<state/>", sr, block, "test", 2);
    prepareProcessor (proc, sr, block, false);
    TestPlayHead ph;
    ph.bpm = goldenBpm;
    proc.setPlayHead (&ph);

    const auto input = drumLoop (sr, goldenBpm, 2);
    juce::AudioBuffer<float> work (2, block);
    juce::MidiBuffer midi;
    long allocations = 0;
    int guarded = 0;
    VizFrame frames[512];

    for (int pos = 0; pos + block <= input.getNumSamples(); pos += block)
    {
        for (int ch = 0; ch < 2; ++ch)
            work.copyFrom (ch, 0, input, ch, pos, block);

        if (pos >= (int) sr)
        {
            AllocGuard guard;
            proc.processBlock (work, midi);
            allocations += guard.count();
            ++guarded;
        }
        else
        {
            proc.processBlock (work, midi);
        }

        proc.getVisualisationFifo().pop (frames, 512);
        ph.advance (block, sr);
    }

    proc.setPlayHead (nullptr);
    GF_EXPECT (guarded > 20);
    GF_EXPECT_MSG (allocations == 0, juce::String (allocations) + " allocations on the audio thread");
}
