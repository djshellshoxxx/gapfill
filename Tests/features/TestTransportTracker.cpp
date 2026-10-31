// W-03 characterization of the released callback-level jump predicate.
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
GF_TEST (TransportTracker, seeded_mixed_callbacks_match_the_released_predicate)
{
    for (double sr : { 8000.0, 44100.0, 48000.0, 96000.0, 192000.0 })
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
            s.bpm = 20.0 + (double) (random % 900u);

            if (random % 11u == 0)
                s.ppq -= 8.0;                            // loop/seek

            GF_EXPECT (tracker.update (s) == released.update (s));
            s.ppq += (double) s.blockSamples / (60.0 / s.bpm * sr);
        }
    }
}
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
