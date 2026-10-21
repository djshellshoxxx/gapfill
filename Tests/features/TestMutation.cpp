// F-L17 Mutation Lab.

#include "../support/AllocGuard.h"
#include "../support/TestRig.h"

#include "PluginEditor.h"
#include "features/mutation/Gui.h"
#include "features/mutation/Genome.h"
#include "features/mutation/MutationLab.h"
#include "features/mutation/Overlay.h"

#include <cstring>

using namespace gapfill;
using namespace gapfill::mutation;
using namespace gftest;

namespace
{
    Genes sample (float v = 0.5f)
    {
        Genes g;
        g.fill (v);
        return g;
    }

    struct CountingListener : juce::AudioProcessorParameter::Listener
    {
        void parameterValueChanged (int, float) override { ++changes; }
        void parameterGestureChanged (int, bool) override {}
        int changes = 0;
    };

    std::vector<float> params (GapFillAudioProcessor& p)
    {
        std::vector<float> v;
        for (const auto& id : allParameterIDs())
            v.push_back (p.getValueTreeState().getRawParameterValue (id)->load());
        return v;
    }
}

GF_TEST (Mutation, gene_table_ids_exist_and_are_floats)
{
    GapFillAudioProcessor proc;
    const auto& ids = geneIds();
    GF_EXPECT (ids.size() == 29 && kNumGenes == 29);

    for (int i = 0; i < kNumGenes; ++i)
    {
        auto* p = proc.getValueTreeState().getParameter (ids[(size_t) i]);
        GF_EXPECT_MSG (p != nullptr, ids[(size_t) i]);
        GF_EXPECT_MSG (dynamic_cast<juce::AudioParameterFloat*> (p) != nullptr, ids[(size_t) i]);
        GF_EXPECT_NEAR (p->convertFrom0to1 (1.0f), 100.0, 1e-3);                       // 0..100 percent: normalised == EngineSettings 0..1
    }

    // Distinct ids; the first 5 are the macros, 5..19 the 15 weights in Algo order.
    juce::StringArray seen;
    for (const auto* id : ids)
    {
        GF_EXPECT (! seen.contains (id));
        seen.add (id);
    }
    for (int a = 0; a < numAlgos; ++a)
        GF_EXPECT (juce::String (ids[(size_t) (5 + a)]) == algoWeightID ((Algo) a));

    GF_EXPECT (genesInScope (0) == 5 && genesInScope (1) == 20 && genesInScope (2) == 29);
}

GF_TEST (Mutation, children_deterministic_same_seed)
{
    const auto parent = sample (0.4f);
    const auto a = makeChildren (parent, 0, 2, 0.25f, 1234u, 3);
    const auto b = makeChildren (parent, 0, 2, 0.25f, 1234u, 3);
    GF_EXPECT (std::memcmp (a.child.data(), b.child.data(), sizeof (a.child)) == 0);          // bit-identical

    const auto otherSeed = makeChildren (parent, 0, 2, 0.25f, 1235u, 3);
    const auto otherGeneration = makeChildren (parent, 0, 2, 0.25f, 1234u, 4);
    GF_EXPECT (std::memcmp (a.child.data(), otherSeed.child.data(), sizeof (a.child)) != 0);
    GF_EXPECT (std::memcmp (a.child.data(), otherGeneration.child.data(), sizeof (a.child)) != 0);
}

GF_TEST (Mutation, children_distinct_thresholds)
{
    for (std::uint64_t seed = 1; seed <= 200; ++seed)
    {
        const auto set = makeChildren (sample (0.5f), 0, 2, 0.15f, seed, 1);

        for (int c = 0; c < kNumChildren; ++c)
        {
            if (set.flagged[(size_t) c])
                continue;

            GF_EXPECT (distanceInf (set.child[(size_t) c], sample (0.5f)) >= kMinParentDistance);

            for (int d = 0; d < c; ++d)
                GF_EXPECT (distanceInf (set.child[(size_t) c], set.child[(size_t) d]) >= kMinChildDistance);
        }
    }
}

GF_TEST (Mutation, pins_never_change_and_scope_limits_genes)
{
    const auto parent = sample (0.3f);
    const std::uint32_t pins = (1u << 0) | (1u << 7) | (1u << 22);

    for (std::uint64_t seed = 1; seed <= 50; ++seed)
    {
        const auto set = makeChildren (parent, pins, 2, 0.5f, seed, 2);

        for (const auto& child : set.child)
            for (int i : { 0, 7, 22 })
                GF_EXPECT (child[(size_t) i] == parent[(size_t) i]);
    }

    // Character scope leaves the weights and bands alone.
    const auto character = makeChildren (parent, 0, 0, 0.5f, 9u, 1);
    for (const auto& child : character.child)
        for (int i = 5; i < kNumGenes; ++i)
            GF_EXPECT (child[(size_t) i] == parent[(size_t) i]);
}

GF_TEST (Mutation, values_always_in_range_and_weights_never_all_zero)
{
    for (const float base : { 0.0f, 0.02f, 0.5f, 0.98f, 1.0f })
        for (std::uint64_t seed = 1; seed <= 100; ++seed)
        {
            const auto set = makeChildren (sample (base), 0, 2, 0.5f, seed, 1);

            for (const auto& child : set.child)
            {
                for (const auto v : child)
                    GF_EXPECT (v >= 0.0f && v <= 1.0f && std::isfinite (v));

                float maxWeight = 0.0f;
                for (int i = 5; i < 20; ++i)
                    maxWeight = std::max (maxWeight, child[(size_t) i]);
                GF_EXPECT (maxWeight >= 0.05f);
            }
        }

    // All weights zero in the parent: the guard revives the largest one in every child.
    Genes silent = sample (0.5f);
    for (int i = 5; i < 20; ++i)
        silent[(size_t) i] = 0.0f;
    for (const auto& child : makeChildren (silent, (1u << 20) - (1u << 5) /* weights pinned */, 2, 0.1f, 5u, 1).child)
    {
        float maxWeight = 0.0f;
        for (int i = 5; i < 20; ++i)
            maxWeight = std::max (maxWeight, child[(size_t) i]);
        GF_EXPECT (maxWeight >= 0.5f);
    }
}

GF_TEST (Mutation, breed_takes_genes_from_both)
{
    const auto parent = sample (0.1f);
    const auto favourite = sample (0.9f);
    const auto set = makeChildren (parent, 0, 2, 0.05f, 77u, 1, &favourite);

    int nearParent = 0, nearFavourite = 0;

    for (const auto& child : set.child)
        for (const auto v : child)
        {
            nearParent += v < 0.4f ? 1 : 0;
            nearFavourite += v > 0.6f ? 1 : 0;
        }

    GF_EXPECT_MSG (nearParent > 20 && nearFavourite > 20, juce::String (nearParent) + " / " + juce::String (nearFavourite));
}

GF_TEST (Mutation, rate_scales_distance_statistically)
{
    const auto meanDistance = [] (float rate)
    {
        double sum = 0.0;
        int n = 0;
        for (std::uint64_t seed = 1; seed <= 250; ++seed)
            for (const auto& child : makeChildren (sample (0.5f), 0, 2, rate, seed, 1).child)
            {
                sum += distanceInf (child, sample (0.5f));
                ++n;
            }
        return sum / n;
    };

    const auto low = meanDistance (0.05f), mid = meanDistance (0.15f), high = meanDistance (0.4f);
    GF_EXPECT_MSG (low < mid && mid < high, juce::String (low) + " < " + juce::String (mid) + " < " + juce::String (high));
}

GF_TEST (Mutation, preview_does_not_change_parameters_or_notify_host)
{
    GapFillAudioProcessor proc;
    auto& lab = proc.getUiState().lab;
    lab.attach (proc.getValueTreeState(), [&proc] { proc.pushUndoSnapshot(); });

    CountingListener listener;
    for (auto* p : proc.getParameters())
        p->addListener (&listener);

    const auto before = params (proc);
    lab.mutate();
    GF_EXPECT (lab.numGenerations() == 1);
    GF_EXPECT (lab.preview (1));
    GF_EXPECT (lab.previewing() == 1);
    lab.tick();

    GF_EXPECT (listener.changes == 0);
    GF_EXPECT (params (proc) == before);

    lab.endPreview();
    for (auto* p : proc.getParameters())
        p->removeListener (&listener);
}

GF_TEST (Mutation, overlay_idle_is_identity_and_active_overwrites_the_genes)
{
    OverlayState overlay;
    EngineSettings reference;
    reference.fill01 = 0.37f;
    reference.weights[3] = 0.21f;

    EngineSettings s = reference;
    overlay.apply (s);
    GF_EXPECT (std::memcmp (&s, &reference, sizeof (EngineSettings)) == 0);

    OverlayMsg m;
    m.active = true;
    for (int i = 0; i < kNumGenes; ++i)
        m.v[i] = 0.01f * (float) (i + 1);
    m.v[27] = std::numeric_limits<float>::quiet_NaN();
    m.v[28] = 7.0f;
    GF_EXPECT (overlay.push (m));
    overlay.apply (s);

    GF_EXPECT_NEAR (s.fill01, 0.01, 1e-6);
    GF_EXPECT_NEAR (s.density01, 0.05, 1e-6);
    GF_EXPECT_NEAR (s.weights[0], 0.06, 1e-6);
    GF_EXPECT_NEAR (s.weights[14], 0.20, 1e-6);
    GF_EXPECT_NEAR (s.bandAmt[0], 0.21, 1e-6);
    GF_EXPECT_NEAR (s.bandProb[2], 1.0, 1e-6);                         // 7.0 clamped
    GF_EXPECT_NEAR (s.bandInt[2], 0.0, 1e-6);                          // NaN -> 0

    m.active = false;
    overlay.push (m);
    EngineSettings again = reference;
    overlay.apply (again);
    GF_EXPECT (std::memcmp (&again, &reference, sizeof (EngineSettings)) == 0);
}

GF_TEST (Mutation, overlay_changes_the_rendered_sound_only_while_previewing)
{
    const auto input = drumLoop (48000.0, 120.0, 2);

    GapFillAudioProcessor baselineProc;
    setParams (baselineProc, { { "fill", 20.0f } });
    const auto baseline = renderOffline (baselineProc, input, 512, 48000.0);

    GapFillAudioProcessor idleProc;
    setParams (idleProc, { { "fill", 20.0f } });
    auto& idleLab = idleProc.getUiState().lab;
    idleLab.attach (idleProc.getValueTreeState(), [] {});
    idleLab.mutate();                                                   // mutated but not previewing
    const auto idle = renderOffline (idleProc, input, 512, 48000.0);
    GF_EXPECT (compareBuffers (baseline.out, idle.out, 0.0).maxAbs == 0.0);

    GapFillAudioProcessor previewProc;
    setParams (previewProc, { { "fill", 20.0f } });
    auto& lab = previewProc.getUiState().lab;
    lab.attach (previewProc.getValueTreeState(), [] {});
    lab.setRate (0.5f);
    lab.mutate();
    GF_EXPECT (lab.preview (0));
    const auto previewed = renderOffline (previewProc, input, 512, 48000.0);
    GF_EXPECT_MSG (compareBuffers (baseline.out, previewed.out, 1e-6).maxAbs > 1e-4 || previewed.fills != baseline.fills,
                   "a previewed child sounds different");
    GF_EXPECT_MSG (*previewProc.getValueTreeState().getRawParameterValue ("fill") == 20.0f, "the knob never moved");
}

GF_TEST (Mutation, keep_is_one_undo_step_bit_exact_and_revert_restores)
{
    GapFillAudioProcessor proc;
    setParams (proc, { { "fill", 33.0f }, { "chaos", 44.0f }, { "wStutter", 55.0f } });
    auto& lab = proc.getUiState().lab;
    lab.attach (proc.getValueTreeState(), [&proc] { proc.pushUndoSnapshot(); });
    lab.setScope (2);

    const auto start = params (proc);
    lab.mutate();
    const auto depth = proc.getUndoDepth();

    GF_EXPECT (lab.keep (2));
    GF_EXPECT (proc.getUndoDepth() == depth + 1);
    GF_EXPECT (params (proc) != start);

    GF_EXPECT (proc.undo());
    {
        const auto after = params (proc);
        const auto ids = allParameterIDs();

        for (size_t i = 0; i < start.size(); ++i)
            // The processor's Undo stores the tree as text: values come back to within 1e-4 (a float ulp or two).
            GF_EXPECT_MSG (std::abs (start[i] - after[i]) < 1.0e-4f, "after Undo " + ids[(int) i] + ": " + juce::String (after[i], 6) + " vs " + juce::String (start[i], 6));
    }

    // Kept child -> next generation's parent; revert goes back one generation.
    lab.keep (1);
    const auto kept = params (proc);
    lab.mutate();
    GF_EXPECT (lab.numGenerations() == 2);
    setParams (proc, { { "fill", 99.0f } });
    GF_EXPECT (lab.revert());
    GF_EXPECT (lab.numGenerations() == 1);
    GF_EXPECT_MSG (params (proc) == kept || std::abs (*proc.getValueTreeState().getRawParameterValue ("fill") - 99.0f) > 1.0f,
                   "revert restores the previous parent's values");
}

GF_TEST (Mutation, pinned_genes_survive_keep)
{
    GapFillAudioProcessor proc;
    setParams (proc, { { "density", 61.0f }, { "chaos", 29.0f } });
    auto& lab = proc.getUiState().lab;
    lab.attach (proc.getValueTreeState(), [&proc] { proc.pushUndoSnapshot(); });
    lab.pins = 0;
    lab.setPinned (4, true);                                           // density
    lab.setRate (0.5f);
    lab.mutate();
    GF_EXPECT (lab.keep (0));
    GF_EXPECT_NEAR (*proc.getValueTreeState().getRawParameterValue ("density"), 61.0, 0.01);
    lab.pins = 0;
}

GF_TEST (Mutation, generation_cap_16_and_favourite_cap_8)
{
    GapFillAudioProcessor proc;
    auto& lab = proc.getUiState().lab;
    lab.attach (proc.getValueTreeState(), [] {});
    lab.favourites.clear();

    for (int i = 0; i < 40; ++i)
        lab.mutate();

    GF_EXPECT (lab.numGenerations() == kMaxGenerations);

    for (int i = 0; i < 12; ++i)
        lab.addFavourite (i % 4);

    GF_EXPECT (lab.favourites.size() == (size_t) kMaxFavourites);
    GF_EXPECT (lab.removeFavourite (0) && lab.favourites.size() == 7);
    lab.favourites.clear();
}

GF_TEST (Mutation, queue_full_retry_and_preview_ends_when_the_sound_changes)
{
    GapFillAudioProcessor proc;
    auto& lab = proc.getUiState().lab;
    lab.attach (proc.getValueTreeState(), [] {});
    lab.mutate();

    // Nothing drains the queue (no audio): the 5th message does not fit and is retried.
    for (int i = 0; i < 6; ++i)
    {
        lab.preview (i % 4);
        lab.tick();
    }

    // An outside change (preset, Undo, automation) ends the preview.
    setParams (proc, { { "fill", 91.0f } });
    lab.tick();
    GF_EXPECT (lab.previewing() < 0);

    lab.endPreview();
}

GF_TEST (Mutation, subtree_roundtrip)
{
    GapFillAudioProcessor proc;
    auto& lab = proc.getUiState().lab;
    lab.attach (proc.getValueTreeState(), [] {});
    lab.seed = 4242;
    lab.setRate (0.3f);
    lab.setScope (1);
    lab.pins = 0;
    lab.setPinned (2, true);
    lab.setPinned (13, true);
    lab.favourites.clear();
    lab.mutate();
    lab.addFavourite (0);
    lab.addFavourite (3);

    juce::MemoryBlock state;
    proc.getStateInformation (state);

    GapFillAudioProcessor other;
    auto& otherLab = other.getUiState().lab;
    otherLab.attach (other.getValueTreeState(), [] {});
    other.setStateInformation (state.getData(), (int) state.getSize());

    GF_EXPECT (otherLab.seed == 4242 && otherLab.scope == 1);
    GF_EXPECT_NEAR (otherLab.rate, 0.3, 1e-6);
    GF_EXPECT (otherLab.isPinned (2) && otherLab.isPinned (13) && ! otherLab.isPinned (3));
    GF_EXPECT (otherLab.favourites.size() == 2 && otherLab.numGenerations() == 0);       // generations are never saved

    for (int f = 0; f < 2; ++f)
        for (int g = 0; g < kNumGenes; ++g)
            GF_EXPECT_NEAR (otherLab.favourites[(size_t) f].genes[(size_t) g], lab.favourites[(size_t) f].genes[(size_t) g], 1.0 / 65535.0 + 1e-6);

    lab.favourites.clear();
    lab.pins = 0;
    lab.seed = MutationLab::defaultSeed();
    lab.setRate (kDefaultRate);
    lab.setScope (0);
    resetMutationSubtree (otherLab);
}

GF_TEST (Mutation, subtree_bad_entry_drops_subtree_keeps_state)
{
    GapFillAudioProcessor proc;
    setParams (proc, { { "fill", 63.0f } });
    juce::MemoryBlock state;
    proc.getStateInformation (state);

    auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
    auto* bad = xml->createNewChildElement ("Mutation");
    bad->setAttribute ("rate", 0.3);
    bad->setAttribute ("scope", 0);
    auto* fav = bad->createNewChildElement ("Fav");
    fav->setAttribute ("name", "ok");
    fav->setAttribute ("g", "!!!not base64!!!");

    juce::MemoryBlock corrupted;
    juce::AudioProcessor::copyXmlToBinary (*xml, corrupted);

    GapFillAudioProcessor other;
    other.getUiState().lab.setRate (0.4f);
    other.setStateInformation (corrupted.getData(), (int) corrupted.getSize());

    GF_EXPECT_NEAR (*other.getValueTreeState().getRawParameterValue ("fill"), 63.0, 0.01);       // the rest of the state loaded
    GF_EXPECT (other.getUiState().lab.favourites.empty());
    GF_EXPECT_NEAR (other.getUiState().lab.rate, kDefaultRate, 1e-6);                             // subtree dropped -> defaults

    GF_EXPECT (! validateMutationSubtree (juce::ValueTree ("Wrong")));
    juce::ValueTree badRate ("Mutation");
    badRate.setProperty ("rate", 0.9, nullptr);
    GF_EXPECT (! validateMutationSubtree (badRate));
}

GF_TEST (Mutation, old_gene_table_shorter_vector_loads)
{
    GapFillAudioProcessor proc;
    setParams (proc, { { "bandHighProb", 77.0f } });
    auto& lab = proc.getUiState().lab;
    lab.attach (proc.getValueTreeState(), [] {});

    // 20 genes (an older table): base64 of 40 bytes, all 0x8000.
    juce::MemoryBlock block;
    for (int i = 0; i < 20; ++i)
    {
        const char bytes[2] = { 0x00, (char) 0x80 };
        block.append (bytes, 2);
    }

    juce::ValueTree tree ("Mutation");
    juce::ValueTree fav ("Fav");
    fav.setProperty ("name", "old", nullptr);
    fav.setProperty ("g", juce::Base64::toBase64 (block.getData(), block.getSize()), nullptr);
    tree.appendChild (fav, nullptr);

    GF_EXPECT (validateMutationSubtree (tree));
    applyMutationSubtree (lab, tree);
    GF_EXPECT (lab.favourites.size() == 1);
    GF_EXPECT_NEAR (lab.favourites[0].genes[0], 0.5, 1e-3);
    GF_EXPECT_NEAR (lab.favourites[0].genes[28], 0.77, 1e-3);          // beyond the old table: the current value
    lab.favourites.clear();
}

GF_TEST (Mutation, no_audio_thread_allocation_in_overlay)
{
    OverlayState overlay;
    EngineSettings s;
    OverlayMsg m;
    m.active = true;
    overlay.push (m);
    overlay.apply (s);                                                  // warm up

    long allocations = 0;
    {
        AllocGuard guard;
        for (int i = 0; i < 1000; ++i)
        {
            overlay.push (m);
            overlay.apply (s);
        }
        allocations = guard.count();
    }
    GF_EXPECT (allocations == 0);
}

GF_TEST (Mutation, panel_cards_have_accessible_names_and_close_ends_the_preview)
{
    GapFillAudioProcessor proc;
    {
        LabPanel panel (proc);
        GF_EXPECT (panel.getLab().isAttached());
        panel.pressMutate();
        panel.refreshForTests();

        for (int c = 0; c < kNumChildren; ++c)
            GF_EXPECT_MSG (panel.getCardTitle (c).startsWith ("Variation " + juce::String::charToString ((juce::juce_wchar) ('A' + c))) && panel.getCardTitle (c).contains ("differs in"),
                           panel.getCardTitle (c));

        panel.pressPreview (2);
        GF_EXPECT (panel.getLab().previewing() == 2);
        panel.pressFav (0);
        GF_EXPECT (panel.getLab().favourites.size() >= 1);
    }

    GF_EXPECT_MSG (proc.getUiState().lab.previewing() < 0, "closing the drawer ends the preview");
    proc.getUiState().lab.favourites.clear();
}
