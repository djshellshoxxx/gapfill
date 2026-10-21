// F-S08 Glitch Roulette (docs/dev/12 F-S08).

#include "../support/LiveRig.h"

#include "features/live/Roulette.h"
#include "Presets.h"

#include <map>
#include <set>

using namespace gapfill;
using namespace gapfill::live;
using namespace gftest;

namespace
{
    std::map<juce::String, float> snapshot (GapFillAudioProcessor& proc)
    {
        std::map<juce::String, float> m;

        for (const auto& id : allParameterIDs())
            m[id] = pv (proc.getValueTreeState().getRawParameterValue (id));

        return m;
    }
}

GF_TEST (Roulette, never_lands_on_the_same_profile_twice_in_a_row)
{
    GapFillAudioProcessor proc;
    juce::Random rnd (99);
    GlitchRoulette roulette (rnd);
    int previous = -1;
    std::set<int> seen;

    for (int i = 0; i < 300; ++i)
    {
        const auto r = roulette.spin (proc);
        GF_EXPECT (r.profile >= 0 && r.profile < getNumSurpriseProfiles() && r.name.isNotEmpty());
        GF_EXPECT_MSG (r.profile != previous, "spin " + juce::String (i));
        previous = r.profile;
        seen.insert (r.profile);
    }

    GF_EXPECT ((int) seen.size() == getNumSurpriseProfiles());     // every profile is reachable
}

GF_TEST (Roulette, uses_the_surprise_path_so_undo_restores_exactly)
{
    GapFillAudioProcessor proc;
    setParams (proc, { { "fill", 12.0f }, { "chaos", 88.0f }, { "dry", 70.0f } });
    const auto before = snapshot (proc);
    const auto depth = proc.getUndoDepth();

    juce::Random rnd (5);
    GlitchRoulette roulette (rnd);
    roulette.spin (proc);

    GF_EXPECT (proc.getUndoDepth() == depth + 1);                  // exactly one snapshot
    GF_EXPECT (snapshot (proc) != before);

    GF_EXPECT (proc.undo());
    const auto after = snapshot (proc);

    for (const auto& [id, v] : before)
        GF_EXPECT_MSG (std::abs (after.at (id) - v) < 1.0e-4f, id);
}

GF_TEST (Roulette, only_creative_parameters_change)
{
    GapFillAudioProcessor proc;
    juce::Random rnd (17);
    GlitchRoulette roulette (rnd);
    const auto creative = creativeParameterIDs();
    const auto performance = performanceParameterIDs();

    for (int i = 0; i < 40; ++i)
    {
        const auto before = snapshot (proc);
        roulette.spin (proc);
        const auto after = snapshot (proc);

        for (const auto& [id, v] : before)
        {
            if (std::abs (after.at (id) - v) < 1.0e-6f)
                continue;

            GF_EXPECT_MSG (creative.contains (id), "changed non-creative parameter " + id);
            GF_EXPECT_MSG (! performance.contains (id), "changed performance parameter " + id);
        }
    }

    GapFillAudioProcessor fresh;
    const auto defaults = snapshot (fresh);
    const auto now = snapshot (proc);

    for (const char* untouched : { "dry", "wet", "output", "bypass", "lookahead", "capture" })
        GF_EXPECT_MSG (std::abs (now.at (untouched) - defaults.at (untouched)) < 1e-6f, untouched);
}

GF_TEST (Roulette, reduced_motion_skips_the_reel_animation)
{
    GF_EXPECT (GlitchRoulette::animationFrames (true) == 0);
    GF_EXPECT (GlitchRoulette::animationFrames (false) == 3);
}

GF_TEST (Roulette, deterministic_with_an_injected_random_source)
{
    GapFillAudioProcessor a, b;
    juce::Random ra (2026), rb (2026);
    GlitchRoulette roulA (ra), roulB (rb);

    for (int i = 0; i < 25; ++i)
    {
        const auto x = roulA.spin (a);
        const auto y = roulB.spin (b);
        GF_EXPECT (x.profile == y.profile && x.name == y.name);
    }

    GF_EXPECT (snapshot (a) == snapshot (b));
}

GF_TEST (Roulette, midi_command_spins_through_the_processor)
{
    GapFillAudioProcessor proc;
    auto& live = proc.getLiveControl();
    commands::Binding b;
    b.cmd = CommandType::Roulette;
    b.type = commands::MsgType::Note;
    b.number = 60;
    live.bindings().add (b);

    prepareProcessor (proc, 48000.0, 256, false);
    juce::AudioBuffer<float> work (2, 256);
    juce::MidiBuffer midi;
    work.clear();
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    proc.processBlock (work, midi);
    midi.clear();
    proc.processBlock (work, midi);
    proc.flushLatencyReportForTests();

    GF_EXPECT (live.roulette().getLastProfile() >= 0 && live.roulette().getLastName().isNotEmpty());
    GF_EXPECT (proc.canUndo());
}
