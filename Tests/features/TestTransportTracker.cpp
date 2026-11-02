// W-03 released-predicate characterization plus separately specified behavior
// tests for endpoint-averaged ramps and callback-boundary tempo steps.
// Build without JUCE, before extraction (recorded old predicate):
//   g++ -std=c++20 -Wall -Wextra -Werror -DGAPFILL_TRANSPORT_STANDALONE
//       -DGAPFILL_TRANSPORT_REFERENCE_ONLY Tests/features/TestTransportTracker.cpp -o /tmp/gf-transport-old
// Build/run the extracted implementation: omit GAPFILL_TRANSPORT_REFERENCE_ONLY.

#include <cmath>
#include <cstdint>
#include <limits>

#if defined (GAPFILL_TRANSPORT_STANDALONE)
 #include <iostream>
 #include <stdexcept>
 #include <utility>
 #include <vector>

namespace standalone
{
    using Case = std::pair<const char*, void (*)()>;
    inline std::vector<Case>& cases() { static std::vector<Case> value; return value; }
    struct Register
    {
        Register (const char* name, void (*fn)()) { cases().emplace_back (name, fn); }
    };
}
 #define GF_TEST(group, name) \
    static void test_##name(); \
    static standalone::Register register_##name (#group "." #name, test_##name); \
    static void test_##name()
 #define GF_EXPECT(condition) \
    do { if (! (condition)) throw std::runtime_error (#condition); } while (false)
#else
 #include "../TestFramework.h"
 #include "../support/AllocGuard.h"
#endif

#if ! defined (GAPFILL_TRANSPORT_REFERENCE_ONLY)
 #include "../../Source/features/transport/TransportTracker.h"
#endif

namespace
{
    enum class Kind { None, Start, Stop, Discontinuity };

    struct Sample
    {
        bool playing = true, ppqValid = true;
        double ppq = 0.0, bpm = 120.0;
        int blockSamples = 512;
    };

    // Recorded from e89aecd PluginProcessor.cpp updateTempoInfo, lines 446–475.
    // This keeps the original compound predicate/projected-end state rather
    // than calling any production tracker helpers or deriving expectations.
    struct ReleasedPredicate
    {
        double sampleRate = 48000.0, expectedPpq = 0.0;
        bool wasPlaying = false, hadPosition = false;

        void reset() noexcept { expectedPpq = 0.0; wasPlaying = hadPosition = false; }
        void prepare (double sr) noexcept { sampleRate = sr; }

        Kind update (const Sample& s) noexcept
        {
            Kind result = Kind::None;

            if (s.playing)
            {
                const bool jumped = ! wasPlaying
                    || (s.ppqValid ? (! hadPosition || std::abs (s.ppq - expectedPpq) > 0.25) : hadPosition);

                if (jumped)
                    result = ! wasPlaying ? Kind::Start : Kind::Discontinuity;

                const double samplesPerBeat = 60.0 / s.bpm * sampleRate;
                expectedPpq = s.ppqValid ? s.ppq + (double) s.blockSamples / samplesPerBeat : 0.0;
                hadPosition = s.ppqValid;
            }
            else if (wasPlaying)
            {
                result = Kind::Stop;
            }

            wasPlaying = s.playing;
            return result;
        }
    };

   #if defined (GAPFILL_TRANSPORT_REFERENCE_ONLY)
    using Tracker = ReleasedPredicate;
   #else
    struct Tracker
    {
        gapfill::transport::TransportTracker tracker;
        void reset() noexcept { tracker.reset(); }
        void prepare (double sr) noexcept { tracker.prepare (sr); }

        Kind update (const Sample& s) noexcept
        {
            using JumpKind = gapfill::transport::JumpKind;
            const auto result = tracker.update ({ s.playing, s.ppqValid, s.ppq, s.bpm, s.blockSamples });

            switch (result)
            {
                case JumpKind::None: return Kind::None;
                case JumpKind::Start: return Kind::Start;
                case JumpKind::Stop: return Kind::Stop;
                case JumpKind::Discontinuity: return Kind::Discontinuity;
            }

            return Kind::None;
        }
    };
   #endif
}

GF_TEST (TransportTracker, start_stop_and_restart_are_distinct_one_shot_events)
{
    Tracker tracker;
    GF_EXPECT (tracker.update ({ false, false }) == Kind::None);
    GF_EXPECT (tracker.update ({ true, true }) == Kind::Start);
    GF_EXPECT (tracker.update ({ false, true }) == Kind::Stop);
    GF_EXPECT (tracker.update ({ false, true }) == Kind::None);
    GF_EXPECT (tracker.update ({ true, true, 99.0 }) == Kind::Start);
}

GF_TEST (TransportTracker, loss_and_recovery_of_ppq_are_each_one_discontinuity)
{
    Tracker tracker;
    GF_EXPECT (tracker.update ({}) == Kind::Start);
    GF_EXPECT (tracker.update ({ true, false }) == Kind::Discontinuity);
    GF_EXPECT (tracker.update ({ true, false }) == Kind::None);
    GF_EXPECT (tracker.update ({ true, true, 16.0 }) == Kind::Discontinuity);
    GF_EXPECT (tracker.update ({ true, true, 16.0 + 512.0 / 24000.0 }) == Kind::None);
}

GF_TEST (TransportTracker, starting_without_ppq_does_not_reset_every_callback)
{
    Tracker tracker;
    Sample noPosition { true, false, std::numeric_limits<double>::quiet_NaN() };
    GF_EXPECT (tracker.update (noPosition) == Kind::Start);

    for (int i = 0; i < 100; ++i)
        GF_EXPECT (tracker.update (noPosition) == Kind::None);
}

GF_TEST (TransportTracker, prediction_uses_the_previous_complete_callback_length)
{
    Tracker tracker;
    double ppq = 0.0;
    bool first = true;

    for (int size : { 8192, 32, 4096, 64, 512, 7000, 16, 65536 })
    {
        GF_EXPECT (tracker.update ({ true, true, ppq, 120.0, size }) == (first ? Kind::Start : Kind::None));
        ppq += (double) size / 24000.0;
        first = false;
    }
}

GF_TEST (TransportTracker, quarter_beat_tolerance_is_inclusive)
{
    Tracker tracker;
    GF_EXPECT (tracker.update ({ true, true, 0.0, 120.0, 6000 }) == Kind::Start);
    GF_EXPECT (tracker.update ({ true, true, 0.5 }) == Kind::None); // exactly +0.25 from expected
    tracker.reset();
    GF_EXPECT (tracker.update ({ true, true, 0.0, 120.0, 6000 }) == Kind::Start);
    GF_EXPECT (tracker.update ({ true, true, std::nextafter (0.5, 1.0) }) == Kind::Discontinuity);
}

GF_TEST (TransportTracker, forward_seek_and_loop_wrap_are_each_one_discontinuity)
{
    Tracker tracker;
    GF_EXPECT (tracker.update ({ true, true, 8.0, 120.0, 6000 }) == Kind::Start);
    GF_EXPECT (tracker.update ({ true, true, 16.0, 120.0, 6000 }) == Kind::Discontinuity);
    GF_EXPECT (tracker.update ({ true, true, 16.25, 120.0, 6000 }) == Kind::None);
    GF_EXPECT (tracker.update ({ true, true, 4.0, 120.0, 6000 }) == Kind::Discontinuity);
    GF_EXPECT (tracker.update ({ true, true, 4.25, 120.0, 6000 }) == Kind::None);
}

GF_TEST (TransportTracker, tempo_step_preserves_the_previous_tempo_prediction)
{
    Tracker tracker;
    GF_EXPECT (tracker.update ({ true, true, 0.0, 120.0, 8192 }) == Kind::Start);
    GF_EXPECT (tracker.update ({ true, true, 8192.0 / 24000.0, 900.0, 32 }) == Kind::None);
}

GF_TEST (TransportTracker, negative_preroll_position_is_valid)
{
    Tracker tracker;
    GF_EXPECT (tracker.update ({ true, true, -8.0, 120.0, 6000 }) == Kind::Start);
    GF_EXPECT (tracker.update ({ true, true, -7.75, 120.0, 6000 }) == Kind::None);
}

GF_TEST (TransportTracker, prepare_updates_rate_without_inventing_a_transport_reset)
{
    Tracker tracker;
    GF_EXPECT (tracker.update ({ true, true, 8.0, 120.0, 6000 }) == Kind::Start);
    tracker.prepare (96000.0);
    GF_EXPECT (tracker.update ({ true, true, 8.25, 120.0, 12000 }) == Kind::None);
    GF_EXPECT (tracker.update ({ true, true, 8.5 }) == Kind::None);
    tracker.reset();
    GF_EXPECT (tracker.update ({ true, true, 8.5 }) == Kind::Start);
}

#if ! defined (GAPFILL_TRANSPORT_REFERENCE_ONLY)
GF_TEST (TransportTracker, constant_tempo_mixed_callbacks_match_the_released_predicate)
{
    for (double sr : { 8000.0, 44100.0, 48000.0, 96000.0, 192000.0 })
    for (double bpm : { 20.0, 120.0, 999.0 })
    {
        Tracker tracker;
        ReleasedPredicate released;
        tracker.prepare (sr);
        released.prepare (sr);
        Sample s;
        std::uint32_t random = 0x10203040u;

        for (int i = 0; i < 20000; ++i)
        {
            random = random * 1664525u + 1013904223u;
            s.playing = random % 13u != 0;
            s.ppqValid = random % 17u != 0;
            s.blockSamples = 16 + (int) (random % 65521u);
            s.bpm = bpm;

            if (random % 11u == 0)
                s.ppq -= 8.0;                            // loop/seek

            GF_EXPECT (tracker.update (s) == released.update (s));
            s.ppq += (double) s.blockSamples / (60.0 / s.bpm * sr);
        }
    }
}

GF_TEST (TransportTracker, linear_tempo_ramp_integral_has_no_false_jump_at_variable_lengths)
{
    // BPM(t)=60+7.5t, so PPQ(t)=t+t^2/16; at16s this is32beats/eight4/4bars.
    // This independently specified integral is not the tracker's formula.
    constexpr int pattern[] { 65536, 32, 8192, 64, 512, 4096 };
    for (double sr : { 8000.0, 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        Tracker tracker;
        tracker.prepare (sr);
        const auto total = (int) (16.0 * sr);
        int elapsed = 0, step = 0;
        while (elapsed < total)
        {
            const auto remaining = total - elapsed;
            const auto wanted = pattern[step % 6];
            const auto length = wanted < remaining ? wanted : remaining;
            const auto time = (double) elapsed / sr;
            GF_EXPECT (tracker.update ({ true, true, time + time * time / 16.0,
                                         60.0 + 7.5 * time, length }) == (step == 0 ? Kind::Start : Kind::None));
            elapsed += length;
            ++step;
        }
        GF_EXPECT (tracker.update ({ true, true, 32.0, 180.0, 32 }) == Kind::None);
    }
}

GF_TEST (TransportTracker, released_predictor_demonstrates_linear_ramp_false_jump)
{
    ReleasedPredicate released;
    released.prepare (8000.0);
    GF_EXPECT (released.update ({ true, true, 0.0, 60.0, 65536 }) == Kind::Start);
    const auto time = 65536.0 / 8000.0;
    GF_EXPECT (released.update ({ true, true, time + time * time / 16.0,
                                  60.0 + 7.5 * time, 32 }) == Kind::Discontinuity);
}

GF_TEST (TransportTracker, instantaneous_tempo_steps_up_and_down_keep_previous_interval)
{
    for (const auto oldBpm : { 20.0, 999.0 })
    {
        Tracker tracker;
        tracker.prepare (8000.0);
        const auto newBpm = oldBpm < 100.0 ? 999.0 : 20.0;
        GF_EXPECT (tracker.update ({ true, true, -8.0, oldBpm, 65536 }) == Kind::Start);
        const auto actual = -8.0 + 65536.0 / (60.0 / oldBpm * 8000.0);
        GF_EXPECT (tracker.update ({ true, true, actual, newBpm, 32 }) == Kind::None);
        GF_EXPECT (tracker.update ({ true, true, actual + 32.0 / (60.0 / newBpm * 8000.0), newBpm, 512 }) == Kind::None);
    }
}

GF_TEST (TransportTracker, tolerance_is_applied_to_each_hypothesis_not_the_interval_between)
{
    // Previous60BPM over1s predicts1beat; endpoint-average330BPM predicts5.5.
    const double positions[] { 1.25, 5.25, 5.75, std::nextafter (1.25, 2.0),
                               std::nextafter (5.25, 5.0), std::nextafter (5.75, 6.0), 3.0 };
    for (int index = 0; index < 7; ++index)
    {
        Tracker tracker;
        GF_EXPECT (tracker.update ({ true, true, 0.0, 60.0, 48000 }) == Kind::Start);
        GF_EXPECT (tracker.update ({ true, true, positions[index], 600.0, 512 })
                     == (index < 3 ? Kind::None : Kind::Discontinuity));
    }
}

GF_TEST (TransportTracker, seek_outside_both_tempo_hypotheses_resets_exactly_once)
{
    Tracker tracker;
    GF_EXPECT (tracker.update ({ true, true, 16.0, 60.0, 48000 }) == Kind::Start);
    GF_EXPECT (tracker.update ({ true, true, 32.0, 600.0, 512 }) == Kind::Discontinuity);
    GF_EXPECT (tracker.update ({ true, true, 32.0 + 512.0 / 4800.0, 600.0, 32 }) == Kind::None);
}

GF_TEST (TransportTracker, reprepare_keeps_previous_interval_at_its_own_sample_rate)
{
    Tracker tracker;
    tracker.prepare (8000.0);
    GF_EXPECT (tracker.update ({ true, true, 8.0, 60.0, 65536 }) == Kind::Start);
    tracker.prepare (192000.0);
    // The completed interval used8kHz, even though the next callback uses192kHz.
    const auto time = 65536.0 / 8000.0;
    GF_EXPECT (tracker.update ({ true, true, 8.0 + time * 2.0, 180.0, 192000 }) == Kind::None);
    GF_EXPECT (tracker.update ({ true, true, 8.0 + time * 2.0 + 3.0, 180.0, 512 }) == Kind::None);
}

#if ! defined (GAPFILL_TRANSPORT_STANDALONE)
GF_TEST (TransportTracker, prediction_update_has_no_audio_thread_allocation)
{
    Tracker tracker;
    tracker.prepare (48000.0);
    gftest::AllocGuard guard;
    for (int index = 0; index < 2000; ++index)
        (void) tracker.update ({ true, true, (double) index / 20.0, 60.0 + (double) (index % 800),
                                 index % 2 == 0 ? 65536 : 32 });
    GF_EXPECT (guard.count() == 0);
}
#endif
#endif

#if defined (GAPFILL_TRANSPORT_STANDALONE)
int main()
{
    int failed = 0;

    for (const auto& [name, run] : standalone::cases())
    {
        try { run(); }
        catch (const std::exception& e) { ++failed; std::cerr << name << ": " << e.what() << '\n'; }
    }

    std::cout << standalone::cases().size() - (std::size_t) failed << " passed, " << failed << " failed\n";
    return failed != 0 ? 1 : 0;
}
#endif
