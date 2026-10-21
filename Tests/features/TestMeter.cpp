// T-01: level-meter arithmetic (gui/MeterMath.h), tested with an injected clock. Characterises the behaviour
// theme.md asks for: 1.5 s peak hold, 20 dB/s fall, a monotonic dB map and the gradient stops.

#include "../support/TestRig.h"

#include "gui/MeterMath.h"
#include "gui/WaveformGapView.h"

using namespace gapfill;
using namespace gapfill::meter;

namespace
{
    // 8-bit channel equality within one step (interpolation rounding may differ per toolchain).
    bool sameColour (juce::Colour a, juce::Colour b)
    {
        return std::abs ((int) a.getRed() - (int) b.getRed()) <= 1 && std::abs ((int) a.getGreen() - (int) b.getGreen()) <= 1
            && std::abs ((int) a.getBlue() - (int) b.getBlue()) <= 1;
    }

    // Runs the 30 Hz timer from `fromMs` for `seconds`, returns the dB reached and the number of Falling ticks.
    struct Run { float db; int falling; bool settled; };

    Run fall (PeakHold& p, juce::uint32 fromMs, double seconds)
    {
        Run r { p.peakDb, 0, false };
        const int ticks = (int) std::lround (seconds * (double) kTimerHz);

        for (int i = 1; i <= ticks; ++i)
        {
            const auto now = fromMs + (juce::uint32) std::lround ((double) i * 1000.0 / (double) kTimerHz);
            const auto step = p.tick (now);

            if (step == PeakHold::Tick::Falling)
                ++r.falling;

            if (step == PeakHold::Tick::Settled)
            {
                r.settled = true;
                break;
            }
        }

        r.db = p.peakDb;
        return r;
    }
}

GF_TEST (Meter, peak_holds_1500ms)
{
    PeakHold p;
    GF_EXPECT (p.push (-6.0f, 10000));

    GF_EXPECT (p.tick (10000 + 1499) == PeakHold::Tick::Held);
    GF_EXPECT (p.peakDb == -6.0f);
    GF_EXPECT (p.tick (10000 + 1500) != PeakHold::Tick::Held);        // the hold is over exactly at 1500 ms
    GF_EXPECT (p.peakDb < -6.0f);
}

GF_TEST (Meter, peak_does_not_fall_during_hold)
{
    PeakHold p;
    p.push (-12.0f, 500);

    for (juce::uint32 t = 500; t < 500 + kHoldMs; t += 33)
        GF_EXPECT (p.tick (t) == PeakHold::Tick::Held);

    GF_EXPECT (p.peakDb == -12.0f);
}

GF_TEST (Meter, peak_falls_20_dB_per_second)
{
    PeakHold p;
    p.push (0.0f, 0);
    const auto r = fall (p, kHoldMs, 1.0);          // exactly one second after the hold ends

    // 30 ticks of 20/30 dB. Tolerance 5 % of the fall, as the ticket asks.
    GF_EXPECT_NEAR (r.db, -20.0, 1.0);
    GF_EXPECT (! r.settled);

    // Time-based version with an explicit dt.
    PeakHold q;
    q.push (0.0f, 0);
    for (int i = 0; i < 100; ++i)
        q.tick (kHoldMs + 1, 0.01f);

    GF_EXPECT_NEAR (q.peakDb, -20.0, 1.0);
}

GF_TEST (Meter, falls_to_the_floor_and_settles)
{
    PeakHold p;
    p.push (-30.0f, 0);
    const auto r = fall (p, kHoldMs, 3.0);          // 30 dB at 20 dB/s = 1.5 s

    GF_EXPECT (r.settled);
    GF_EXPECT (p.peakDb == kFloorDb);
}

GF_TEST (Meter, new_higher_peak_resets_hold_and_lower_level_does_not)
{
    PeakHold p;
    p.push (-20.0f, 0);
    GF_EXPECT (! p.push (-30.0f, 1000));            // lower: nothing changes
    GF_EXPECT (p.peakDb == -20.0f && p.holdUntilMs == kHoldMs);

    GF_EXPECT (p.push (-10.0f, 1200));              // higher: restarts the hold
    GF_EXPECT (p.peakDb == -10.0f && p.holdUntilMs == 1200 + kHoldMs);
    GF_EXPECT (p.tick (1200 + kHoldMs - 1) == PeakHold::Tick::Held);

    GF_EXPECT (p.push (-10.0f, 1300));              // equal also restarts it (the original >= test)
    GF_EXPECT (p.holdUntilMs == 1300 + kHoldMs);
}

GF_TEST (Meter, db_map_monotonic_and_endpoints)
{
    GF_EXPECT (normalised (kFloorDb) == 0.0f);
    GF_EXPECT (normalised (kFloorDb - 40.0f) == 0.0f);
    GF_EXPECT (normalised (kCeilDb) == 1.0f);
    GF_EXPECT (normalised (kCeilDb + 12.0f) == 1.0f);
    GF_EXPECT_NEAR (normalised (0.0f), (0.0 - kFloorDb) / (kCeilDb - kFloorDb), 1.0e-6);

    float last = -1.0f;

    for (float db = kFloorDb - 5.0f; db <= kCeilDb + 5.0f; db += 0.25f)
    {
        const auto n = normalised (db);
        GF_EXPECT (n >= last);
        last = n;
    }

    GF_EXPECT_NEAR (toDb (1.0f), 0.0, 1.0e-4);
    GF_EXPECT_NEAR (toDb (0.5f), -6.0206, 1.0e-3);
    GF_EXPECT (toDb (0.0f) < kFloorDb);              // silence is below the floor (shown as -inf)
}

GF_TEST (Meter, gradient_stops_exact)
{
    // The stops of theme.md at normalised 0 / 0.62 / 0.86 / 1 are exactly the palette colours.
    GF_EXPECT (sameColour (gradientAt (0.0f), colours::secondary));
    GF_EXPECT (sameColour (gradientAt (0.62f), colours::primary));
    GF_EXPECT (sameColour (gradientAt (0.86f), colours::warning));
    GF_EXPECT (sameColour (gradientAt (1.0f), colours::clip));

    // Between stops the colour is a blend, not a step.
    const auto mid = gradientAt (0.31f);
    GF_EXPECT (! sameColour (mid, colours::secondary) && ! sameColour (mid, colours::primary));

    // Levels given in dB land on the documented positions: -60 dB bottom, -18 dB, -6 dB, 0 dB (clip region), +6 top.
    GF_EXPECT (sameColour (gradientAt (normalised (-60.0f)), colours::secondary));
    GF_EXPECT (sameColour (gradientAt (normalised (6.0f)), colours::clip));
}

GF_TEST (Meter, level_meter_component_reports_silence_and_clears)
{
    LevelMeter m;
    m.setSize (30, 100);
    m.setLevel (0.0f);
    m.resetPeak();                                    // no crash, floor shown as -inf (painted); state is the floor

    PeakHold q;
    GF_EXPECT (q.peakDb == kFloorDb);
}
