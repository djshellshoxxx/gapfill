// W-03: variable-block transport prediction (docs/dev/features/W-03). The false-jump fix of PR #12 was already in;
// these tests complete the matrix of the ticket: block patterns, tempo ramp and step, real seeks, loop wrap,
// stop/start and "playing without a PPQ" (one reset, not one per callback).

#include "../support/TestRig.h"
#include "../TestSignals.h"

using namespace gapfill;
using namespace gftest;

namespace
{
    constexpr double kSr = 48000.0;

    struct Host
    {
        Host()
        {
            prepareProcessor (proc, kSr, 512, false);
            proc.setPlayHead (&playHead);
        }

        ~Host() { proc.setPlayHead (nullptr); }

        void block (int size)
        {
            juce::AudioBuffer<float> buf (2, size);
            buf.clear();
            proc.processBlock (buf, midi);
            playHead.advance (size, kSr);
        }

        int jumps() const { return proc.getTransportJumpCountForTests(); }

        GapFillAudioProcessor proc;
        TestPlayHead playHead;
        juce::MidiBuffer midi;
    };
}

GF_TEST (Transport, steady_playback_no_jump_at_alternating_block_sizes)
{
    Host h;
    const int pattern[] = { 8192, 32, 4096, 64, 512, 128, 8192, 256, 1024, 2048, 32, 32, 7000 };

    // About 60 s of steady playback.
    int played = 0;

    while (played < 60 * (int) kSr)
        for (int size : pattern)
        {
            h.block (size);
            played += size;
        }

    GF_EXPECT_MSG (h.jumps() == 1, "the only jump is the transport start; got " + juce::String (h.jumps()));
}

GF_TEST (Transport, tempo_ramp_60_to_180_over_8_bars_has_no_false_jump)
{
    Host h;
    h.playHead.tsNum = 4;
    h.playHead.tsDen = 4;
    h.playHead.bpm = 60.0;
    h.block (512);

    const double totalBeats = 32.0;                     // 8 bars

    for (int size : { 512, 4096, 256, 8192, 64 })
    {
        // Several passes per block size so every size meets the whole ramp.
        h.playHead.ppq = 0.0;
        h.block (512);
        const int base = h.jumps();

        while (h.playHead.ppq < totalBeats)
        {
            const double t = h.playHead.ppq / totalBeats;
            h.playHead.bpm = 60.0 + 120.0 * t;
            h.block (size);
        }

        GF_EXPECT_MSG (h.jumps() == base, "ramp produced false jumps at host block size " + juce::String (size));
        h.playHead.ppq += 8.0;                            // continue from a new place (a real seek, counted in `base` next time)
        h.block (512);
    }
}

GF_TEST (Transport, tempo_step_has_no_false_jump)
{
    Host h;

    for (int i = 0; i < 40; ++i)
        h.block (512);

    const int base = h.jumps();
    h.playHead.bpm = 150.0;                              // tempo step 120 -> 150

    for (int i = 0; i < 80; ++i)
        h.block (i % 2 ? 4096 : 512);

    GF_EXPECT (h.jumps() == base);
}

GF_TEST (Transport, seek_loop_wrap_and_stop_start_are_each_detected_once)
{
    Host h;

    for (int i = 0; i < 30; ++i)
        h.block (512);

    int expected = h.jumps();
    GF_EXPECT (expected == 1);

    // Forward seek by two bars.
    h.playHead.ppq += 8.0;
    h.block (512);
    GF_EXPECT (h.jumps() == ++expected);

    for (int i = 0; i < 30; ++i)
        h.block (512);

    GF_EXPECT (h.jumps() == expected);                   // continues without further jumps

    // Loop wrap: the position goes back by four beats at the end of a loop.
    h.playHead.ppq -= 4.0;
    h.block (512);
    GF_EXPECT (h.jumps() == ++expected);

    for (int i = 0; i < 30; ++i)
        h.block (512);

    GF_EXPECT (h.jumps() == expected);

    // A small nudge (< 0.25 beat) is not a seek.
    h.playHead.ppq += 0.1;
    h.block (512);
    GF_EXPECT (h.jumps() == expected);

    // Stop, wait, start again at the same place: the start is one reset, the stopped blocks none.
    h.playHead.playing = false;

    for (int i = 0; i < 20; ++i)
        h.block (512);

    GF_EXPECT (h.jumps() == expected);
    h.playHead.playing = true;

    for (int i = 0; i < 20; ++i)
        h.block (512);

    GF_EXPECT (h.jumps() == ++expected);
}

GF_TEST (Transport, playing_without_ppq_resets_once_not_every_block)
{
    Host h;
    h.playHead.providePpq = false;

    for (int i = 0; i < 100; ++i)
        h.block (512);

    // The start counts once; the missing position must not re-trigger a reset (and a wet panic) in every callback.
    GF_EXPECT_MSG (h.jumps() <= 2, "playing without PPQ reset " + juce::String (h.jumps()) + " times in 100 blocks");

    // Position appears: that is a (single) transition.
    const int base = h.jumps();
    h.playHead.providePpq = true;
    h.playHead.ppq = 16.0;

    for (int i = 0; i < 20; ++i)
        h.block (512);

    GF_EXPECT (h.jumps() == base + 1);

    // And going away again is one more.
    h.playHead.providePpq = false;

    for (int i = 0; i < 20; ++i)
        h.block (512);

    GF_EXPECT (h.jumps() == base + 2);
}

GF_TEST (Transport, fill_start_positions_do_not_depend_on_the_host_block_pattern)
{
    // Same input and transport, rendered with two different host block patterns: the wet signal is the same.
    const auto input = makeDrumLoop (kSr, 3.0, 120.0, 2);

    auto render = [&] (const std::vector<int>& pattern)
    {
        GapFillAudioProcessor proc;
        prepareProcessor (proc, kSr, 512, false);
        auto set = [&] (const char* id, float v)
        {
            auto* p = proc.getValueTreeState().getParameter (id);
            p->setValueNotifyingHost (p->convertTo0to1 (v));
        };
        set ("dry", 0.0f);
        set ("wet", 100.0f);
        set ("fill", 90.0f);
        set ("seed", 11.0f);

        TestPlayHead ph;
        proc.setPlayHead (&ph);
        juce::MidiBuffer midi;
        juce::AudioBuffer<float> out (2, input.getNumSamples());
        out.clear();

        int pos = 0, k = 0;

        while (pos < input.getNumSamples())
        {
            const int size = juce::jmin (pattern[(size_t) (k++ % (int) pattern.size())], input.getNumSamples() - pos);
            juce::AudioBuffer<float> blk (2, size);

            for (int ch = 0; ch < 2; ++ch)
                blk.copyFrom (ch, 0, input, ch, pos, size);

            proc.processBlock (blk, midi);

            for (int ch = 0; ch < 2; ++ch)
                out.copyFrom (ch, pos, blk, ch, 0, size);

            ph.advance (size, kSr);
            pos += size;
        }

        proc.setPlayHead (nullptr);
        return out;
    };

    const auto a = render ({ 512 });
    const auto b = render ({ 8192, 32, 4096, 64, 512 });

    // Tolerance, not equality: the chunking of the DSP differs slightly (sub-chunk boundaries), the decisions must not.
    double energyA = 0.0, energyB = 0.0, diff = 0.0;

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < a.getNumSamples(); ++i)
        {
            const double x = a.getSample (ch, i), y = b.getSample (ch, i);
            energyA += x * x;
            energyB += y * y;
            diff += (x - y) * (x - y);
        }

    GF_EXPECT (energyA > 1.0e-6);
    GF_EXPECT_MSG (diff <= 1.0e-3 * (energyA + energyB) + 1.0e-9,
                   "wet output depends on the host block pattern: diff " + juce::String (diff) + " of " + juce::String (energyA));
}
