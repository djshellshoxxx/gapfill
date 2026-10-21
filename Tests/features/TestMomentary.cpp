// F-M10 hold/latch controller and F-S07 Stutter Storm pattern (state and timing only:
// the performance voice that renders the engaged effect is E-1's, see docs/progress/WP6.md).

#include "../support/LiveRig.h"

#include "features/momentary/HoldController.h"

#include <cmath>

using namespace gapfill;
using namespace gapfill::momentary;
using namespace gftest;

namespace
{
    constexpr std::uint8_t bit (Effect e) { return (std::uint8_t) (1u << (int) e); }
}

GF_TEST (Momentary, momentary_mode_is_engaged_while_any_source_is_down)
{
    HoldController h;
    h.setLevel (Effect::Stutter, Source::Midi, true);
    GF_EXPECT (h.engagedMask() == bit (Effect::Stutter));
    h.setLevel (Effect::Stutter, Source::Gui, true);
    h.setLevel (Effect::Stutter, Source::Midi, false);
    GF_EXPECT (h.engagedMask() == bit (Effect::Stutter));          // the GUI still holds it
    h.setLevel (Effect::Stutter, Source::Gui, false);
    GF_EXPECT (h.engagedMask() == 0 && h.topEffect() == -1);
}

GF_TEST (Momentary, latch_toggles_on_the_rising_edge_of_every_source_and_gui_toggle)
{
    HoldController h;
    h.setLatch (true);
    h.setLevel (Effect::Freeze, Source::Midi, true);
    h.setLevel (Effect::Freeze, Source::Midi, false);
    GF_EXPECT (h.engagedMask() == bit (Effect::Freeze));
    h.setLevel (Effect::Freeze, Source::Param, true);               // automation lane goes high: toggles off
    GF_EXPECT (h.engagedMask() == 0);
    h.setLevel (Effect::Freeze, Source::Param, false);
    h.toggle (Effect::Freeze);
    GF_EXPECT (h.engagedMask() == bit (Effect::Freeze));
    h.toggle (Effect::Freeze);
    GF_EXPECT (h.engagedMask() == 0);

    HoldController m;
    m.toggle (Effect::Freeze);                                       // toggle is a Latch-mode gesture
    GF_EXPECT (m.engagedMask() == 0);
}

GF_TEST (Momentary, newest_pressed_wins_and_falls_back_to_the_previous_hold)
{
    HoldController h;
    h.setLevel (Effect::Stutter, Source::Gui, true);
    h.setLevel (Effect::Reverse, Source::Gui, true);
    h.setLevel (Effect::TapeStop, Source::Gui, true);
    GF_EXPECT (h.topEffect() == (int) Effect::TapeStop);
    GF_EXPECT (h.engagedMask() == (bit (Effect::Stutter) | bit (Effect::Reverse) | bit (Effect::TapeStop)));
    h.setLevel (Effect::TapeStop, Source::Gui, false);
    GF_EXPECT (h.topEffect() == (int) Effect::Reverse);
    h.setLevel (Effect::Reverse, Source::Gui, false);
    GF_EXPECT (h.topEffect() == (int) Effect::Stutter);
    h.setLevel (Effect::Stutter, Source::Gui, false);
    GF_EXPECT (h.topEffect() == -1);
}

GF_TEST (Momentary, lost_release_times_out_in_momentary_mode_only_and_needs_a_re_press)
{
    HoldController h;
    h.setLevel (Effect::Stutter, Source::Midi, true);
    h.tick (14 * 48000, 15 * 48000);
    GF_EXPECT (h.engagedMask() == bit (Effect::Stutter) && h.timedOutMask() == 0);
    h.tick (2 * 48000, 15 * 48000);
    GF_EXPECT (h.engagedMask() == 0 && h.timedOutMask() == bit (Effect::Stutter));

    h.tick (100 * 48000, 15 * 48000);
    GF_EXPECT (h.engagedMask() == 0);                                // stays released while the control is still "down"
    h.setLevel (Effect::Stutter, Source::Midi, false);              // release...
    GF_EXPECT (h.timedOutMask() == 0);
    h.setLevel (Effect::Stutter, Source::Midi, true);               // ...and press again
    GF_EXPECT (h.engagedMask() == bit (Effect::Stutter));

    HoldController latch;
    latch.setLatch (true);
    latch.toggle (Effect::Stutter);
    latch.tick (1000 * 48000, 15 * 48000);
    GF_EXPECT (latch.engagedMask() == bit (Effect::Stutter));       // latch has no timeout
}

GF_TEST (Momentary, panic_clears_everything_and_remembers_nothing)
{
    HoldController h;
    h.setLevel (Effect::Stutter, Source::Midi, true);
    h.setLevel (Effect::Freeze, Source::Gui, true);
    h.panic();
    GF_EXPECT (h.engagedMask() == 0 && h.timedOutMask() == 0 && h.topEffect() == -1);
    h.setLevel (Effect::Stutter, Source::Midi, true);               // a held controller re-engages only on a fresh press
    GF_EXPECT (h.engagedMask() == bit (Effect::Stutter));

    HoldController l;
    l.setLatch (true);
    l.toggle (Effect::Reverse);
    l.panic();
    GF_EXPECT (l.engagedMask() == 0);
    l.setLatch (false);
    GF_EXPECT (l.engagedMask() == 0);
}

GF_TEST (Momentary, delayed_engage_never_delays_the_release)
{
    HoldController h;
    h.setLevel (Effect::Stutter, Source::Gui, true);
    h.engageAt (Effect::Stutter, 1000);
    GF_EXPECT (h.engagedMask() == 0);
    h.advanceTo (999);
    GF_EXPECT (h.engagedMask() == 0);
    h.advanceTo (1000);
    GF_EXPECT (h.engagedMask() == bit (Effect::Stutter));

    h.setLevel (Effect::Stutter, Source::Gui, false);
    h.setLevel (Effect::Freeze, Source::Gui, true);
    h.engageAt (Effect::Freeze, 5000);
    h.setLevel (Effect::Freeze, Source::Gui, false);                 // released before the engage: cancelled
    h.advanceTo (6000);
    GF_EXPECT (h.engagedMask() == 0);
}

GF_TEST (Momentary, seeded_fuzz_leaves_nothing_stuck_after_panic)
{
    juce::Random rnd (77);
    HoldController h;

    for (int seq = 0; seq < 1000; ++seq)
    {
        for (int step = 0; step < 40; ++step)
        {
            switch (rnd.nextInt (5))
            {
                case 0: h.setLevel ((Effect) rnd.nextInt (kNumEffects), (Source) rnd.nextInt (3), rnd.nextBool()); break;
                case 1: h.toggle ((Effect) rnd.nextInt (kNumEffects)); break;
                case 2: h.tick (rnd.nextInt (96000), 48000 * 2); break;
                case 3: h.setLatch (rnd.nextBool()); break;
                default: h.engageAt ((Effect) rnd.nextInt (kNumEffects), rnd.nextInt (50000)); h.advanceTo (rnd.nextInt (50000)); break;
            }

            GF_EXPECT ((h.engagedMask() & ~((1u << kNumEffects) - 1)) == 0);
        }

        h.panic();
        GF_EXPECT (h.engagedMask() == 0 && h.topEffect() == -1);
    }
}

//==============================================================================
GF_TEST (Storm, pattern_steps_land_exactly_on_the_thirty_second_grid_at_any_tempo)
{
    GF_EXPECT (kStormGridUnits == 11);
    int total = 0;

    for (int i = 0; i < kStormSteps; ++i)
        total += stormStepUnits (i);

    GF_EXPECT (total == kStormGridUnits);
    GF_EXPECT (stormStepUnits (0) == 2 && stormStepUnits (1) == 2 && stormStepUnits (2) == 1 && stormStepUnits (4) == 2);

    for (const double bpm : { 60.0, 120.0, 174.0 })
    {
        const double sr = 48000.0;
        const double samplesPerQuarter = 60.0 / bpm * sr;
        int expectedIndex = 0, unitsLeft = stormStepUnits (0);
        int previous = -1;
        long long changes = 0;

        // walk one sample at a time over four pattern cycles
        const auto totalSamples = (long long) (4.0 * kStormGridUnits * kStormGridPpq * samplesPerQuarter);

        for (long long n = 0; n < totalSamples; ++n)
        {
            const auto step = stormStepAt ((double) n / samplesPerQuarter);

            if (step.index != previous)
            {
                // a new step must begin exactly at a multiple of 1/32 note
                const auto gridPos = step.stepStartPpq / kStormGridPpq;
                GF_EXPECT (std::abs (gridPos - std::round (gridPos)) < 1.0e-9);
                GF_EXPECT_MSG (std::abs ((double) n - step.stepStartPpq * samplesPerQuarter) <= 1.0, "bpm " + juce::String (bpm));
                previous = step.index;
                ++changes;
            }
        }

        GF_EXPECT (changes == 4 * kStormSteps);
        juce::ignoreUnused (expectedIndex, unitsLeft);
    }
}

GF_TEST (Storm, gate_is_80_percent_and_accent_is_on_the_first_step_only)
{
    const auto first = stormStepAt (0.0);
    GF_EXPECT (first.index == 0 && first.gateOpen && std::abs (first.accentDb - 1.5f) < 1e-6f);
    GF_EXPECT (stormStepAt (0.2).gateOpen);                          // step 0 is 0.25 ppq: 80 % = 0.2
    GF_EXPECT (! stormStepAt (0.21).gateOpen);
    GF_EXPECT (stormStepAt (0.25).index == 1 && stormStepAt (0.25).accentDb == 0.0f);

    // 1/32 steps are 0.125 long: gate closes after 0.1
    const auto third = stormStepAt (0.5);
    GF_EXPECT (third.index == 2 && third.gateOpen && ! stormStepAt (0.5 + 0.11).gateOpen);

    // the pattern repeats every 11 grid units, including before PPQ 0 (seek / pre-roll)
    for (double ppq = -3.0; ppq < 6.0; ppq += 0.0173)
        GF_EXPECT (stormStepAt (ppq).index == stormStepAt (ppq + kStormGridUnits * kStormGridPpq * 3.0).index);

    GF_EXPECT (kStormTailPpq == 0.5);
}

GF_TEST (Storm, seed_is_deterministic_per_iteration_and_independent_of_the_legacy_stream)
{
    GF_EXPECT (stormSeed (7, 3) == stormSeed (7, 3));
    GF_EXPECT (stormSeed (7, 3) != stormSeed (7, 4));
    GF_EXPECT (stormSeed (7, 3) != stormSeed (8, 3));
}

//==============================================================================
GF_TEST (Momentary, commands_from_midi_and_gui_drive_the_controller_through_the_processor)
{
    GapFillAudioProcessor proc;
    auto& live = proc.getLiveControl();
    commands::Binding b;
    b.cmd = CommandType::MomentaryFreeze;
    b.type = commands::MsgType::Note;
    b.mode = commands::BindMode::Momentary;
    b.number = 61;
    live.bindings().add (b);

    prepareProcessor (proc, 48000.0, 256, false);
    juce::AudioBuffer<float> work (2, 256);
    juce::MidiBuffer midi;
    work.clear();

    midi.addEvent (juce::MidiMessage::noteOn (1, 61, (juce::uint8) 100), 0);
    proc.processBlock (work, midi);
    midi.clear();
    proc.processBlock (work, midi);                                         // executed one block later
    GF_EXPECT (live.getHoldEngagedMask() == bit (Effect::Freeze));

    midi.addEvent (juce::MidiMessage::noteOff (1, 61), 0);
    proc.processBlock (work, midi);
    midi.clear();
    proc.processBlock (work, midi);
    GF_EXPECT (live.getHoldEngagedMask() == 0);

    // GUI path: velocity != 0 is down, 0 is up
    Command down;
    down.type = CommandType::MomentaryStutter;
    down.velocity = 127;
    proc.postCommand (down);
    proc.processBlock (work, midi);
    GF_EXPECT (live.getHoldEngagedMask() == bit (Effect::Stutter));

    // Storm is a hold-type command too, and Panic releases everything
    Command storm;
    storm.type = CommandType::StutterStorm;
    storm.velocity = 127;
    proc.postCommand (storm);
    proc.processBlock (work, midi);
    GF_EXPECT ((live.getHoldEngagedMask() & bit (Effect::Storm)) != 0);

    live.panic().requestPanic();
    proc.processBlock (work, midi);
    GF_EXPECT (live.getHoldEngagedMask() == 0);
}

GF_TEST (Momentary, bypass_clears_holds_and_a_lost_release_times_out_in_the_processor)
{
    GapFillAudioProcessor proc;
    auto& live = proc.getLiveControl();
    live::PerfOptions o = live.getPerfOptions();
    o.holdTimeoutSeconds = 2;
    live.setPerfOptions (o);

    prepareProcessor (proc, 48000.0, 512, false);
    juce::AudioBuffer<float> work (2, 512);
    juce::MidiBuffer midi;
    work.clear();

    Command down;
    down.type = CommandType::MomentaryReverse;
    down.velocity = 127;
    proc.postCommand (down);

    for (int i = 0; i < 150; ++i)                                          // ~1.6 s: still held
        proc.processBlock (work, midi);

    GF_EXPECT (live.getHoldEngagedMask() == bit (Effect::Reverse));

    for (int i = 0; i < 40; ++i)                                           // past 2 s: auto-released
        proc.processBlock (work, midi);

    GF_EXPECT (live.getHoldEngagedMask() == 0 && live.getHoldTimedOutMask() == bit (Effect::Reverse));

    GapFillAudioProcessor other;
    setParams (other, { { "bypass", 1.0f } });
    prepareProcessor (other, 48000.0, 512, false);
    other.postCommand (down);
    other.processBlock (work, midi);
    GF_EXPECT (other.getLiveControl().getHoldEngagedMask() == 0);
}

GF_TEST (Momentary, hold_options_round_trip_and_are_clamped)
{
    GapFillAudioProcessor a;
    live::PerfOptions o = a.getLiveControl().getPerfOptions();
    o.holdLatch = true;
    o.holdTimeoutSeconds = 30;
    a.getLiveControl().setPerfOptions (o);
    juce::MemoryBlock blob;
    a.getStateInformation (blob);

    GapFillAudioProcessor b;
    b.setStateInformation (blob.getData(), (int) blob.getSize());
    GF_EXPECT (b.getLiveControl().getPerfOptions().holdLatch && b.getLiveControl().getPerfOptions().holdTimeoutSeconds == 30);

    o.holdTimeoutSeconds = 500;
    b.getLiveControl().setPerfOptions (o);
    GF_EXPECT (b.getLiveControl().getPerfOptions().holdTimeoutSeconds == 60);
}
