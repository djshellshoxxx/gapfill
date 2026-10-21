// C-2 Performance Macros and F-M23 advanced modulation targets (docs/dev/04 C-2, docs/dev/12 F-M23).

#include "../support/AllocGuard.h"
#include "../support/LiveRig.h"

#include "features/macros/MacroModel.h"
#include "features/state/StateSubtrees.h"

#include <cmath>
#include <cstring>

using namespace gapfill;
using namespace gapfill::macros;
using namespace gftest;

namespace
{
    constexpr double kSr = 48000.0;

    Mapping map (int target, float lo, float hi, int curve = 0)
    {
        Mapping m;
        m.target = (std::uint8_t) target;
        m.min01 = lo;
        m.max01 = hi;
        m.curve = (std::uint8_t) curve;
        return m;
    }

    MacroConfig emptyConfig()
    {
        MacroConfig c = defaultConfig();

        for (int m = 0; m < kNumMacros; ++m)
        {
            c.count[m] = 0;

            for (auto& mp : c.map[m])
                mp = Mapping {};
        }

        return c;
    }

    struct Rig
    {
        Rig() { initTargets(); }
        MacroRuntime rt;
        EngineSettings s;
        std::uint32_t owned = 0;

        void run (float a, float c = 0.0f, float sp = 0.0f, float d = 0.0f)
        {
            const float knobs[4] = { a, c, sp, d };
            rt.evaluateBlock (s, knobs, owned);
        }
    };

    int idOf (const char* id)
    {
        for (int i = 0; i < kNumTargets; ++i)
            if (std::strcmp (targetInfo (i).id, id) == 0)
                return i;

        return -1;
    }
}

GF_TEST (Macros, factory_defaults_match_the_spec_table)
{
    const auto c = defaultConfig();
    GF_EXPECT (c.count[0] == 2 && c.count[1] == 3 && c.count[2] == 3 && c.count[3] == 3);
    GF_EXPECT (std::strcmp (c.names[0], "Activity") == 0 && std::strcmp (c.names[3], "Destruction") == 0);

    // Activity: Density 25-90, Fill 20-75
    GF_EXPECT (std::strcmp (targetInfo (c.map[0][0].target).id, "density") == 0);
    GF_EXPECT_NEAR (c.map[0][0].min01, 0.25, 1e-6);
    GF_EXPECT_NEAR (c.map[0][0].max01, 0.90, 1e-6);
    GF_EXPECT (std::strcmp (targetInfo (c.map[0][1].target).id, "fill") == 0);
    // Space: Width 60-150 of a 0-200 range
    GF_EXPECT (std::strcmp (targetInfo (c.map[2][0].target).id, "width") == 0);
    GF_EXPECT_NEAR (c.map[2][0].min01 * 200.0, 60.0, 1e-3);
    GF_EXPECT_NEAR (c.map[2][0].max01 * 200.0, 150.0, 1e-3);
    GF_EXPECT (std::strcmp (targetInfo (c.map[1][2].target).id, "wScramble") == 0);
    GF_EXPECT (std::strcmp (targetInfo (c.map[3][2].target).id, "wDistort") == 0);

    // no target is mapped twice
    bool seen[kNumTargets] {};

    for (int m = 0; m < kNumMacros; ++m)
        for (int j = 0; j < c.count[m]; ++j)
        {
            GF_EXPECT (! seen[c.map[m][j].target]);
            seen[c.map[m][j].target] = true;
        }
}

GF_TEST (Macros, target_table_is_ordered_and_f_m23_targets_are_appended)
{
    const char* c2[] = { "fill", "intensity", "complexity", "chaos", "density", "width", "wReverse", "wStutter", "wDelay",
                         "wGranular", "wFreeze", "wPitchRise", "wPitchFall", "wFilter", "wTexture", "wCrush", "wDistort",
                         "wScramble", "wSpectral", "wComb", "wTapeStop" };

    for (int i = 0; i < kNumC2Targets; ++i)
        GF_EXPECT_MSG (std::strcmp (targetInfo (i).id, c2[i]) == 0, c2[i]);

    const char* m23[] = { "swellAmount", "cloudAmount", "ratAmount", "rcAmount", "tdAmount", "resAmount", "fsdAmount",
                          "dubAmount", "fmtAmount", "sfzAmount", "cvAmount", "tensionAmount", "energyAdapt", "scaleAmount",
                          "grooveAmount" };

    GF_EXPECT (kNumTargets == 36);

    for (int i = 0; i < 15; ++i)
        GF_EXPECT_MSG (std::strcmp (targetInfo (21 + i).id, m23[i]) == 0, m23[i]);

    // C-2 ranges match the real parameters
    GapFillAudioProcessor proc;

    for (int i = 0; i < kNumC2Targets; ++i)
    {
        auto* p = proc.getValueTreeState().getParameter (targetInfo (i).id);
        GF_EXPECT_MSG (p != nullptr, targetInfo (i).id);
        const auto range = p->getNormalisableRange();
        GF_EXPECT_NEAR (range.start, targetInfo (i).lo, 1e-4);
        GF_EXPECT_NEAR (range.end, targetInfo (i).hi, 1e-4);
        GF_EXPECT (range.skew == 1.0f);                        // linear, so lerp in real units == lerp in normalised units
    }
}

GF_TEST (Macros, value_zero_and_max_hit_min_and_max_and_each_curve_is_monotonic_and_bounded)
{
    Rig r;
    r.rt.cfg = emptyConfig();
    r.rt.cfg.map[0][0] = map (idOf ("fill"), 0.2f, 0.7f);
    r.rt.cfg.count[0] = 1;

    r.run (0.0f);
    GF_EXPECT_NEAR (r.s.fill01, 0.2, 1e-6);
    r.run (1.0f);
    GF_EXPECT_NEAR (r.s.fill01, 0.7, 1e-6);
    r.run (0.5f);
    GF_EXPECT_NEAR (r.s.fill01, 0.45, 1e-6);

    for (int curve = 0; curve < 4; ++curve)
    {
        float previous = -1.0f;

        for (int i = 0; i <= 200; ++i)
        {
            const auto x = (float) i / 200.0f;
            const auto y = mappingValue (map (0, 0.1f, 0.9f, curve), x);
            GF_EXPECT (y >= 0.1f - 1e-6f && y <= 0.9f + 1e-6f);
            GF_EXPECT (y >= previous - 1e-6f);
            previous = y;
        }

        GF_EXPECT_NEAR (mappingValue (map (0, 0.1f, 0.9f, curve), 0.0f), 0.1, 1e-6);
        GF_EXPECT_NEAR (mappingValue (map (0, 0.1f, 0.9f, curve), 1.0f), 0.9, 1e-6);
    }

    GF_EXPECT_NEAR (applyCurve (1, 0.5f), 0.25, 1e-6);
    GF_EXPECT_NEAR (applyCurve (2, 0.5f), 0.75, 1e-6);
    GF_EXPECT_NEAR (applyCurve (3, 0.5f), 0.5, 1e-6);
}

GF_TEST (Macros, an_inverted_mapping_works_and_nothing_exceeds_the_target_range)
{
    Rig r;
    r.rt.cfg = emptyConfig();
    r.rt.cfg.map[0][0] = map (idOf ("density"), 0.9f, 0.1f);          // inverted
    r.rt.cfg.count[0] = 1;
    r.run (0.0f);
    GF_EXPECT_NEAR (r.s.density01, 0.9, 1e-6);
    r.run (1.0f);
    GF_EXPECT_NEAR (r.s.density01, 0.1, 1e-6);

    // property: extreme and non-finite values never leave the target range
    juce::Random rnd (4);

    for (int round = 0; round < 500; ++round)
    {
        MacroRuntime rt;
        rt.cfg = emptyConfig();

        for (int t = 0; t < 8; ++t)
        {
            auto mp = map (rnd.nextInt (kNumC2Targets), rnd.nextFloat() * 4.0f - 1.5f, rnd.nextFloat() * 4.0f - 1.5f, rnd.nextInt (4));
            mp.bipolar = (std::uint8_t) rnd.nextInt (2);
            mp.smoothMs = rnd.nextFloat() * 900.0f;
            rt.cfg.map[0][t] = mp;
        }

        rt.cfg.count[0] = 8;
        EngineSettings s;
        std::uint32_t owned = 0;
        const float knobs[4] = { round % 7 == 0 ? std::nanf ("") : rnd.nextFloat() * 3.0f - 1.0f, 0.5f, 0.5f, 0.5f };
        rt.evaluateBlock (s, knobs, owned);

        for (float v : { s.fill01, s.intensity01, s.complexity01, s.chaos01, s.density01 })
            GF_EXPECT (std::isfinite (v) && v >= 0.0f && v <= 1.0f);

        for (int a = 0; a < numAlgos; ++a)
            GF_EXPECT (std::isfinite (s.weights[a]) && s.weights[a] >= 0.0f && s.weights[a] <= 1.0f);

        if (s.perf.macroWidthOn)
            GF_EXPECT (s.perf.macroWidth01 >= 0.0f && s.perf.macroWidth01 <= 2.0f);
    }
}

GF_TEST (Macros, off_is_the_identity_and_the_stage_never_touches_unmapped_fields)
{
    Rig r;
    EngineSettings before;
    Effective::MacroOutputs out;
    Effective::MacroState off;
    off.on = false;
    off.runtime = &r.rt;
    EngineSettings s;
    Effective::applyMacros (s, off, out);
    GF_EXPECT (std::memcmp (&s, &before, sizeof (EngineSettings)) == 0);

    Effective::MacroState noRuntime;
    noRuntime.on = true;
    Effective::applyMacros (s, noRuntime, out);
    GF_EXPECT (std::memcmp (&s, &before, sizeof (EngineSettings)) == 0);

    // on, with the factory mappings: only mapped fields change
    Effective::MacroState on;
    on.on = true;
    on.runtime = &r.rt;
    r.rt.cfg = defaultConfig();
    EngineSettings s2;
    Effective::applyMacros (s2, on, out);
    GF_EXPECT (s2.seed == before.seed && s2.minGapMs == before.minGapMs && s2.protNext01 == before.protNext01);
    GF_EXPECT (s2.fill01 != before.fill01 && s2.perf.macroWidthOn);
    GF_EXPECT ((s2.perf.macroOwnedMask & 1u) != 0);                  // fill is owned
}

GF_TEST (Macros, a_target_belongs_to_one_macro_and_the_lower_index_wins)
{
    initTargets();
    MacroModel model;
    model.restore ({});                                                      // factory
    GF_EXPECT (model.ownerOf (idOf ("fill")) == 0);

    int owner = -1;
    GF_EXPECT (model.addMapping (3, map (idOf ("fill"), 0, 1), &owner) == MacroModel::AddResult::Claimed && owner == 0);

    // load: the second claim is dropped
    auto tree = model.toValueTree();
    tree.getChild (3).setProperty ("m", "0,0,1000,0,0,0;1,100,900,1,0,0", nullptr);      // macro 3 claims fill (owned by macro 0) + intensity
    GF_EXPECT (MacroModel::validateTree (tree));
    MacroModel loaded;
    loaded.restore (tree);
    GF_EXPECT (loaded.ownerOf (idOf ("fill")) == 0);
    GF_EXPECT (loaded.get().count[3] == 1 && loaded.get().map[3][0].target == idOf ("intensity"));

    // the runtime guards too: if a table slipped through, macro 0 still wins
    Rig r;
    r.rt.cfg = emptyConfig();
    r.rt.cfg.map[0][0] = map (idOf ("fill"), 0.1f, 0.1f);
    r.rt.cfg.count[0] = 1;
    r.rt.cfg.map[3][0] = map (idOf ("fill"), 0.9f, 0.9f);
    r.rt.cfg.count[3] = 1;
    r.run (0.5f, 0.5f, 0.5f, 0.5f);
    GF_EXPECT_NEAR (r.s.fill01, 0.1, 1e-6);
}

GF_TEST (Macros, model_enforces_eight_mappings_and_unavailable_targets_and_sanitises_names)
{
    initTargets();
    MacroModel model;
    model.resetAll();
    auto& m = model;

    // fill macro 0 up to 8 with free targets
    int added = m.get().count[0];

    for (int t = 6; added < kMaxMappings && t < kNumC2Targets; ++t)
        if (m.ownerOf (t) < 0 && m.addMapping (0, map (t, 0, 1)) == MacroModel::AddResult::Ok)
            ++added;

    GF_EXPECT (m.get().count[0] == 8);
    GF_EXPECT (m.addMapping (0, map (idOf ("wComb"), 0, 1)) != MacroModel::AddResult::Ok);

    bool none[kNumTargets] {};
    m.setTargetAvailability (none);
    GF_EXPECT (m.addMapping (1, map (idOf ("swellAmount"), 0, 1)) == MacroModel::AddResult::Unavailable);
    GF_EXPECT (m.addMapping (9, map (0, 0, 1)) == MacroModel::AddResult::Invalid);

    GF_EXPECT (MacroModel::sanitiseName ("  Big\tDrop \n", "x") == "BigDrop");
    GF_EXPECT (MacroModel::sanitiseName ("", "fallback") == "fallback");
    GF_EXPECT (MacroModel::sanitiseName ("\x01\x02", "fallback") == "fallback");
    GF_EXPECT (MacroModel::sanitiseName ("ABCDEFGHIJKLMNOPQRSTUVWXYZ", "x").getNumBytesAsUTF8() == 16);
    GF_EXPECT (MacroModel::sanitiseName (juce::String::fromUTF8 ("\xc3\xa4\xc3\xa4\xc3\xa4\xc3\xa4\xc3\xa4\xc3\xa4\xc3\xa4\xc3\xa4\xc3\xa4"), "x")
                   .getNumBytesAsUTF8() <= 16);
    GF_EXPECT (m.rename (2, "Dub Wash") && std::strcmp (m.get().names[2], "Dub Wash") == 0);
    GF_EXPECT (m.rename (2, "") && std::strcmp (m.get().names[2], "Space") == 0);
}

GF_TEST (Macros, state_round_trips_exactly_and_the_worst_case_fits_2_kib)
{
    initTargets();
    MacroModel a;
    auto c = emptyConfig();
    std::strcpy (c.names[0], "Verse Lift");

    for (int m = 0; m < kNumMacros; ++m)
    {
        for (int j = 0; j < kMaxMappings; ++j)
        {
            auto mp = map (m * 8 + j, 0.123f, 0.987f, j % 4);
            mp.smoothMs = 250.0f;
            mp.bipolar = (std::uint8_t) (j % 2);
            c.map[m][j] = mp;
        }

        c.count[m] = kMaxMappings;
        c.lfo[m] = { 1, (std::uint8_t) (m % 5), (std::uint8_t) (m % 2), (std::uint8_t) (3 + m), 12.34f, 0.5f, 270.0f };
    }

    // all 32 mappings use distinct targets 0..31
    bool all[kNumTargets];

    for (auto& x : all)
        x = true;

    a.setTargetAvailability (all);
    a.restore ({});

    for (int m = 0; m < kNumMacros; ++m)
    {
        a.resetMacro (m);

        while (a.get().count[m] > 0)
            a.removeMapping (m, 0);
    }

    for (int m = 0; m < kNumMacros; ++m)
        for (int j = 0; j < kMaxMappings; ++j)
            GF_EXPECT (a.addMapping (m, c.map[m][j]) == MacroModel::AddResult::Ok);

    for (int m = 0; m < kNumMacros; ++m)
        a.setLfo (m, c.lfo[m]);

    a.rename (0, "Verse Lift");

    const auto tree = a.toValueTree();
    GF_EXPECT (MacroModel::validateTree (tree));
    GF_EXPECT_MSG (state::serialisedSize (tree) <= 2 * 1024, "worst case size " + juce::String ((int) state::serialisedSize (tree)));

    MacroModel b;
    b.restore (tree);
    GF_EXPECT (std::memcmp (&a.get(), &b.get(), sizeof (MacroConfig)) == 0);
    GF_EXPECT (std::strcmp (b.get().names[0], "Verse Lift") == 0);
}

GF_TEST (Macros, invalid_subtree_is_dropped_atomically_and_the_rest_of_the_state_still_loads)
{
    GapFillAudioProcessor a;
    setParams (a, { { "chaos", 61.0f }, { "macrosOn", 1.0f } });
    a.getLiveControl().macroMappings().rename (0, "Custom");
    juce::MemoryBlock blob;
    a.getStateInformation (blob);

    auto xml = juce::AudioProcessor::getXmlFromBinary (blob.getData(), (int) blob.getSize());
    auto* m = xml->getChildByName ("Macros");
    GF_EXPECT (m != nullptr);

    for (const char* bad : { "99,0,1000,0,0,0", "0,2000,0,0,0,0", "0,0,1000,9,0,0", "0,0,1000,0,900,0", "0,0,1000,0,0,5", "x,y,z",
                             "0,0,1000;0,0,1000;0,0,1000;0,0,1000;0,0,1000;0,0,1000;0,0,1000;0,0,1000;0,0,1000" })
    {
        auto copy = std::make_unique<juce::XmlElement> (*xml);
        copy->getChildByName ("Macros")->getChildElement (1)->setAttribute ("m", bad);
        juce::MemoryBlock damaged;
        juce::AudioProcessor::copyXmlToBinary (*copy, damaged);

        GapFillAudioProcessor b;
        b.getLiveControl().macroMappings().rename (1, "Other");
        b.setStateInformation (damaged.getData(), (int) damaged.getSize());

        GF_EXPECT_MSG (std::strcmp (b.getLiveControl().macroMappings().get().names[0], "Activity") == 0, bad);     // whole subtree reset
        GF_EXPECT_MSG (std::strcmp (b.getLiveControl().macroMappings().get().names[1], "Chaos") == 0, bad);
        GF_EXPECT_NEAR (pv (b.getValueTreeState().getRawParameterValue ("chaos")), 61.0, 0.01);                   // the rest loaded
        GF_EXPECT (pb (b.getValueTreeState().getRawParameterValue ("macrosOn")));
    }

    // a valid subtree restores
    GapFillAudioProcessor ok;
    ok.setStateInformation (blob.getData(), (int) blob.getSize());
    GF_EXPECT (std::strcmp (ok.getLiveControl().macroMappings().get().names[0], "Custom") == 0);
}

GF_TEST (Macros, old_state_loads_with_macros_off_and_factory_mappings)
{
    GapFillAudioProcessor a;
    juce::MemoryBlock blob;
    a.getStateInformation (blob);
    auto xml = juce::AudioProcessor::getXmlFromBinary (blob.getData(), (int) blob.getSize());
    GF_EXPECT (xml->getChildByName ("Macros") == nullptr);                  // defaults write nothing

    GapFillAudioProcessor b;
    b.getLiveControl().macroMappings().rename (0, "Temp");
    b.setStateInformation (blob.getData(), (int) blob.getSize());
    GF_EXPECT (! pb (b.getValueTreeState().getRawParameterValue ("macrosOn")));
    GF_EXPECT (std::strcmp (b.getLiveControl().macroMappings().get().names[0], "Activity") == 0);
    GF_EXPECT (b.getLiveControl().macroMappings().get().count[0] == 2);
}

GF_TEST (Macros, parameters_are_registered_with_the_documented_defaults)
{
    GapFillAudioProcessor proc;
    auto def = [&] (const char* id) { return pv (proc.getValueTreeState().getRawParameterValue (id)); };
    GF_EXPECT_NEAR (def ("macroActivity"), 50.0, 1e-4);
    GF_EXPECT_NEAR (def ("macroChaos"), 25.0, 1e-4);
    GF_EXPECT_NEAR (def ("macroSpace"), 25.0, 1e-4);
    GF_EXPECT_NEAR (def ("macroDestruction"), 15.0, 1e-4);
    GF_EXPECT (def ("macrosOn") < 0.5f);
    GF_EXPECT (performanceParameterIDs().contains ("macrosOn"));
    GF_EXPECT (! randomisableParameterIDs().contains ("macrosOn"));
}

//==============================================================================
GF_TEST (Macros, processor_with_macros_off_is_neutral_and_macros_never_write_base_parameters)
{
    const auto input = drumLoop (kSr, 120.0, 3);

    GapFillAudioProcessor offProc;
    TestPlayHead ph1;
    const auto off = renderLive (offProc, input, 512, kSr, ph1, {});

    GapFillAudioProcessor onProc;
    setParams (onProc, { { "macrosOn", 1.0f }, { "macroActivity", 100.0f }, { "macroChaos", 100.0f } });
    const auto before = onProc.getValueTreeState().copyState().createCopy();
    TestPlayHead ph2;
    const auto on = renderLive (onProc, input, 512, kSr, ph2, {});

    auto sameValues = [] (juce::AudioProcessorValueTreeState& s, const juce::ValueTree& tree)
    {
        for (const auto& id : allParameterIDs())
        {
            const auto child = tree.getChildWithProperty ("id", id);
            const auto stored = child.isValid() ? (float) child.getProperty ("value") : 0.0f;
            if (child.isValid() && std::abs (stored - *s.getRawParameterValue (id)) > 1.0e-4f)
                return false;
        }

        return true;
    };

    GF_EXPECT (sameValues (onProc.getValueTreeState(), before));                   // base parameters are untouched
    GF_EXPECT (compareBuffers (off.out, on.out, 0.0).maxAbs > 0.0);                // macros changed what is heard
}

GF_TEST (Macros, the_activity_macro_owns_fill_while_it_maps_it)
{
    // Fill parameter 90 % but the macro maps Fill to 0..0: no fills may start.
    GapFillAudioProcessor proc;
    setParams (proc, { { "fill", 90.0f }, { "density", 100.0f }, { "macrosOn", 1.0f } });
    auto cfg = emptyConfig();
    cfg.map[0][0] = map (idOf ("fill"), 0.0f, 0.0f);
    cfg.count[0] = 1;
    proc.getLiveControl().macroMappings().restore ({});
    proc.getLiveControl().macroMappings().removeMapping (0, 0);
    proc.getLiveControl().macroMappings().removeMapping (0, 0);
    proc.getLiveControl().macroMappings().addMapping (0, cfg.map[0][0]);

    const auto input = drumLoop (kSr, 120.0, 4);
    TestPlayHead ph;
    const auto r = renderLive (proc, input, 512, kSr, ph, {});
    GF_EXPECT (r.fills == 0);

    GapFillAudioProcessor free;
    setParams (free, { { "fill", 90.0f }, { "density", 100.0f }, { "macrosOn", 0.0f } });
    TestPlayHead ph2;
    GF_EXPECT (renderLive (free, input, 512, kSr, ph2, {}).fills > 0);
}

GF_TEST (Macros, space_macro_drives_the_wet_width_and_effective_values_are_published)
{
    auto render = [] (bool macros)
    {
        GapFillAudioProcessor proc;
        setParams (proc, { { "fill", 100.0f }, { "density", 100.0f }, { "dry", 0.0f }, { "macrosOn", macros ? 1.0f : 0.0f } });
        auto& mm = proc.getLiveControl().macroMappings();

        // Space -> width 0 .. 0 : a mono wet bus
        for (int j = mm.get().count[2]; j > 0; --j)
            mm.removeMapping (2, 0);

        mm.addMapping (2, map (5, 0.0f, 0.0f));
        const auto input = drumLoop (kSr, 120.0, 3);
        TestPlayHead ph;
        auto r = renderLive (proc, input, 512, kSr, ph, {});
        return std::pair { std::move (r), proc.getLiveControl().getMacroEffective01 (5) };
    };

    const auto [mono, effMono] = render (true);
    const auto [wide, effWide] = render (false);

    double maxSide = 0.0, maxSideWide = 0.0;

    for (int i = 0; i < mono.out.getNumSamples(); ++i)
    {
        maxSide = std::max (maxSide, (double) std::abs (mono.out.getSample (0, i) - mono.out.getSample (1, i)));
        maxSideWide = std::max (maxSideWide, (double) std::abs (wide.out.getSample (0, i) - wide.out.getSample (1, i)));
    }

    GF_EXPECT_MSG (maxSide < 1.0e-6, "side " + juce::String (maxSide));
    GF_EXPECT (maxSideWide > 1.0e-3);
    GF_EXPECT_NEAR (effMono, 0.0, 1e-6);
    GF_EXPECT_NEAR (effWide, 0.0, 1e-6);                      // macros off: nothing published
}

GF_TEST (Macros, midi_learn_drives_a_macro_value)
{
    GapFillAudioProcessor proc;
    proc.getMidiLearnManager().setMapping ("macroActivity", 33);
    prepareProcessor (proc, kSr, 256, false);
    juce::AudioBuffer<float> work (2, 256);
    juce::MidiBuffer midi;
    work.clear();
    midi.addEvent (juce::MidiMessage::controllerEvent (1, 33, 127), 0);
    proc.processBlock (work, midi);
    proc.flushLatencyReportForTests();
    GF_EXPECT_NEAR (pv (proc.getValueTreeState().getRawParameterValue ("macroActivity")), 100.0, 0.5);
}

GF_TEST (Macros, effective_values_are_published_for_the_ui)
{
    GapFillAudioProcessor proc;
    setParams (proc, { { "macrosOn", 1.0f }, { "macroActivity", 100.0f } });
    prepareProcessor (proc, kSr, 256, false);
    juce::AudioBuffer<float> work (2, 256);
    juce::MidiBuffer midi;
    work.clear();
    proc.processBlock (work, midi);

    auto& live = proc.getLiveControl();
    GF_EXPECT_NEAR (live.getMacroEffective01 (idOf ("fill")), 0.75, 1e-5);       // Activity at 100 -> Fill max 0.75
    GF_EXPECT_NEAR (live.getMacroEffective01 (idOf ("density")), 0.90, 1e-5);
}

//==============================================================================
// F-M23

GF_TEST (ModulationTargets, smoothing_never_overshoots_and_converges)
{
    Rig r;
    r.rt.cfg = emptyConfig();
    auto mp = map (idOf ("chaos"), 0.0f, 1.0f);
    mp.smoothMs = 200.0f;
    r.rt.cfg.map[0][0] = mp;
    r.rt.cfg.count[0] = 1;
    r.rt.chunkSeconds = 512.0f / 48000.0f;

    r.run (0.0f);
    float previous = r.s.chaos01;

    for (int i = 0; i < 400; ++i)
    {
        r.run (1.0f);
        GF_EXPECT (r.s.chaos01 >= previous - 1e-7f && r.s.chaos01 <= 1.0f);
        previous = r.s.chaos01;
    }

    GF_EXPECT (previous > 0.99f);

    // after one time constant (200 ms) about 63 % of the step
    Rig q;
    q.rt.cfg = r.rt.cfg;
    q.run (0.0f);
    float v = 0.0f;
    const int steps = (int) std::lround (0.2 / (512.0 / 48000.0));

    for (int i = 0; i < steps; ++i)
    {
        q.run (1.0f);
        v = q.s.chaos01;
    }

    GF_EXPECT_NEAR (v, 0.632, 0.03);
}

GF_TEST (ModulationTargets, bipolar_maps_minus_one_to_one_around_the_centre)
{
    auto mp = map (0, 0.2f, 0.8f);
    mp.bipolar = 1;
    GF_EXPECT_NEAR (mappingValue (mp, 0.0f), 0.2, 1e-6);
    GF_EXPECT_NEAR (mappingValue (mp, 0.5f), 0.5, 1e-6);
    GF_EXPECT_NEAR (mappingValue (mp, 1.0f), 0.8, 1e-6);

    mp.curve = 1;                                              // ease-in is symmetric around the centre
    GF_EXPECT_NEAR (mappingValue (mp, 0.25f) + mappingValue (mp, 0.75f), 1.0, 1e-5);
    GF_EXPECT (mappingValue (mp, 0.75f) < mappingValue (map (0, 0.2f, 0.8f), 0.75f));
}

GF_TEST (ModulationTargets, lfo_shapes_are_bounded_and_locked_to_the_ppq)
{
    LfoSettings l;
    l.enabled = 1;
    l.division = (std::uint8_t) Division::Quarter;           // one cycle per beat
    l.depth01 = 1.0f;

    for (int shape = 0; shape < 5; ++shape)
    {
        l.shape = (std::uint8_t) shape;

        for (int i = 0; i < 1000; ++i)
        {
            const auto v = lfoValue (l, i * 0.0173, 7);
            GF_EXPECT (v >= 0.0f && v <= 1.0f && std::isfinite (v));
        }
    }

    l.shape = 0;
    GF_EXPECT_NEAR (lfoCyclesAtPpq (l, 3.0), 3.0, 1e-9);                       // 1/4 division == 1 beat
    GF_EXPECT_NEAR (lfoValue (l, lfoCyclesAtPpq (l, 0.0), 1), 0.5, 1e-6);
    GF_EXPECT_NEAR (lfoValue (l, lfoCyclesAtPpq (l, 0.25), 1), 1.0, 1e-5);   // quarter cycle = sine peak
    GF_EXPECT_NEAR (lfoValue (l, lfoCyclesAtPpq (l, 1.25), 1), lfoValue (l, lfoCyclesAtPpq (l, 0.25), 1), 1e-6);   // periodic in PPQ

    l.division = (std::uint8_t) Division::Eighth;
    GF_EXPECT_NEAR (lfoCyclesAtPpq (l, 1.0), 2.0, 1e-9);

    l.phaseDeg = 90.0f;
    GF_EXPECT_NEAR (lfoValue (l, 0.0, 1), 1.0, 1e-5);

    l.depth01 = 0.5f;
    GF_EXPECT_NEAR (lfoValue (l, 0.0, 1), 0.75, 1e-5);
}

GF_TEST (ModulationTargets, sample_and_hold_is_deterministic_holds_within_a_cycle_and_depends_on_the_seed)
{
    LfoSettings l;
    l.shape = 4;
    l.depth01 = 1.0f;

    GF_EXPECT (lfoValue (l, 3.2, 5) == lfoValue (l, 3.2, 5));
    GF_EXPECT (lfoValue (l, 3.2, 5) == lfoValue (l, 3.9, 5));              // held within the cycle
    GF_EXPECT (lfoValue (l, 3.2, 5) != lfoValue (l, 4.2, 5));
    GF_EXPECT (lfoValue (l, 3.2, 5) != lfoValue (l, 3.2, 6));

    // distribution is spread over 0..1
    float lo = 1.0f, hi = 0.0f;

    for (int c = 0; c < 400; ++c)
    {
        const auto v = lfoValue (l, c + 0.5, 11);
        lo = std::min (lo, v);
        hi = std::max (hi, v);
    }

    GF_EXPECT (lo < 0.1f && hi > 0.9f);
}

GF_TEST (ModulationTargets, decision_stage_result_depends_only_on_the_ppq_not_on_block_state)
{
    initTargets();
    MacroRuntime a, b;
    a.cfg = emptyConfig();
    a.cfg.map[0][0] = map (idOf ("chaos"), 0.0f, 1.0f);
    a.cfg.count[0] = 1;
    a.cfg.lfo[0] = { 1, 4, 1, (std::uint8_t) Division::Quarter, 1.0f, 1.0f, 0.0f };      // sample & hold, synced
    b.cfg = a.cfg;
    a.freeTime = 0.0;
    b.freeTime = 123.456;
    b.chunkSeconds = 0.5f;
    a.seed = b.seed = 9;

    for (double ppq = 0.0; ppq < 32.0; ppq += 0.37)
    {
        EngineSettings sa, sb;
        a.evaluateDecision (sa, ppq);
        b.evaluateDecision (sb, ppq);
        GF_EXPECT (sa.chaos01 == sb.chaos01);
    }

    // free-running (non-synced) LFOs are left to the block stage
    a.cfg.lfo[0].tempoSync = 0;
    EngineSettings s;
    const auto before = s.chaos01;
    a.evaluateDecision (s, 5.0);
    GF_EXPECT (s.chaos01 == before);
}

GF_TEST (ModulationTargets, lfo_macro_results_are_block_size_invariant_in_the_whole_processor)
{
    auto run = [] (int block)
    {
        GapFillAudioProcessor proc;
        setParams (proc, { { "macrosOn", 1.0f }, { "fill", 80.0f }, { "dry", 0.0f } });
        auto& mm = proc.getLiveControl().macroMappings();
        mm.resetAll();
        mm.setLfo (0, { 1, 4, 1, (std::uint8_t) Division::Half, 1.0f, 1.0f, 0.0f });       // S&H, 2 beats per cycle

        const auto input = drumLoop (kSr, 120.0, 6);
        TestPlayHead ph;
        return renderLive (proc, input, block, kSr, ph, {});
    };

    const auto reference = run (512);
    GF_EXPECT (reference.fills > 0);

    for (const int block : { 32, 64, 4096 })
    {
        const auto other = run (block);
        GF_EXPECT_MSG (other.fills == reference.fills, "block " + juce::String (block));
    }
}

GF_TEST (ModulationTargets, old_four_field_mappings_load_unchanged)
{
    initTargets();
    juce::ValueTree t ("Macros");
    juce::ValueTree m0 ("M");
    m0.setProperty ("i", 0, nullptr);
    m0.setProperty ("n", "Activity", nullptr);
    m0.setProperty ("m", "4,250,900,0;0,200,750,1", nullptr);          // C-2 era tuples: target,min,max,curve
    t.appendChild (m0, nullptr);

    GF_EXPECT (MacroModel::validateTree (t));
    MacroModel model;
    model.restore (t);
    GF_EXPECT (model.get().count[0] == 2);
    GF_EXPECT (model.get().map[0][0].smoothMs == 0.0f && model.get().map[0][0].bipolar == 0);
    GF_EXPECT (model.get().map[0][1].curve == 1);
    GF_EXPECT (model.get().lfo[0].enabled == 0);
    GF_EXPECT (std::strcmp (model.get().names[1], "Chaos") == 0);       // macros missing from the tree keep their factory content
}

GF_TEST (ModulationTargets, unavailable_targets_are_refused_until_their_feature_exists)
{
    GapFillAudioProcessor proc;
    auto& mm = proc.getLiveControl().macroMappings();
    const bool exists = proc.getValueTreeState().getParameter ("swellAmount") != nullptr;
    const auto r = mm.addMapping (1, map (idOf ("swellAmount"), 0.0f, 1.0f));
    GF_EXPECT (exists ? r == MacroModel::AddResult::Ok : r == MacroModel::AddResult::Unavailable);
}

GF_TEST (Macros, hot_path_does_not_allocate)
{
    GapFillAudioProcessor proc;
    setParams (proc, { { "macrosOn", 1.0f }, { "fill", 80.0f } });
    proc.getLiveControl().macroMappings().setLfo (0, { 1, 0, 1, 2, 1.0f, 1.0f, 0.0f });
    prepareProcessor (proc, kSr, 256, false);
    TestPlayHead ph;
    proc.setPlayHead (&ph);
    juce::AudioBuffer<float> work (2, 256);
    juce::MidiBuffer midi;
    long allocations = 0;

    for (int block = 0; block < 300; ++block)
    {
        work.clear();
        AllocGuard guard;
        proc.processBlock (work, midi);

        if (block > 50)
            allocations += guard.count();

        ph.advance (256, kSr);
    }

    proc.setPlayHead (nullptr);
    GF_EXPECT (allocations == 0);
}
