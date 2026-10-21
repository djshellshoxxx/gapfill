// F-L07 performance scenes with quantised recall (docs/dev/12 F-L07).

#include "../support/AllocGuard.h"
#include "../support/LiveRig.h"

#include "features/scenes/SceneController.h"
#include "features/scenes/SceneOverlay.h"
#include "features/state/StateSubtrees.h"

#include <cmath>

using namespace gapfill;
using namespace gapfill::scenes;
using namespace gftest;

namespace
{
    constexpr double kSr = 48000.0;

    Overlay makeOverlay (std::uint32_t id, Quant q, std::initializer_list<std::pair<const char*, float>> values,
                         bool wait = false)
    {
        Overlay o;
        o.kind = Overlay::Set;
        o.id = id;
        o.quant = (std::uint8_t) q;
        o.waitForTransport = wait ? 1 : 0;

        for (const auto& [name, real] : values)
        {
            o.idx[o.n] = (std::uint16_t) paramIndexOf (name);
            o.real[o.n] = real;
            ++o.n;
        }

        return o;
    }

    TransportSnap snap (double ppq, int n = 512, double bpm = 120.0, bool valid = true)
    {
        TransportSnap t;
        t.valid = valid;
        t.ppq = ppq;
        t.sampleRate = kSr;
        t.samplesPerBeat = 60.0 / bpm * kSr;
        t.numSamples = n;
        return t;
    }

    struct EngineRig
    {
        EngineRig() { initTables(); initOverlayTargets(); }
        TripleBuffer<Overlay> pub;
        SceneEngine engine;
    };
}

GF_TEST (Scenes, quantiser_beat_bar_and_phrase_boundaries_are_exact)
{
    using quantiser::nextBoundary;
    GF_EXPECT_NEAR (nextBoundary (Quant::Beat, 5.3, 0.0, 4, 4, 0, 0), 6.0, 1e-9);
    GF_EXPECT_NEAR (nextBoundary (Quant::Bar, 5.3, 0.0, 4, 4, 0, 0), 8.0, 1e-9);
    GF_EXPECT_NEAR (nextBoundary (Quant::Bar, 4.0, 0.0, 4, 4, 0, 0), 8.0, 1e-9);          // strictly after
    GF_EXPECT_NEAR (nextBoundary (Quant::Bar, 5.3, 0.0, 3, 4, 0, 0), 6.0, 1e-9);          // 3/4
    GF_EXPECT_NEAR (nextBoundary (Quant::Bar, 5.3, 0.0, 6, 8, 0, 0), 6.0, 1e-9);          // 6/8 = 3 quarters
    GF_EXPECT_NEAR (nextBoundary (Quant::Beat, 5.3, 0.0, 6, 8, 0, 0), 5.5, 1e-9);         // eighth-note beats
    GF_EXPECT_NEAR (nextBoundary (Quant::Phrase, 5.3, 0.0, 4, 4, 4, 0), 16.0, 1e-9);
    GF_EXPECT_NEAR (nextBoundary (Quant::Phrase, 5.3, 0.0, 4, 4, 4, 1), 20.0, 1e-9);      // offset 1 bar
    GF_EXPECT_NEAR (nextBoundary (Quant::Phrase, 5.3, 0.0, 4, 4, 0, 0), 8.0, 1e-9);       // no phrase grid = bar
    GF_EXPECT_NEAR (nextBoundary (Quant::Bar, 7.9995, 0.001, 4, 4, 0, 0), 12.0, 1e-9);    // 1 ms guard
    GF_EXPECT_NEAR (nextBoundary (Quant::Bar, 5.3, 0.0, 0, 0, 0, 0), 8.0, 1e-9);          // unknown = 4/4
}

GF_TEST (Scenes, engine_applies_exactly_at_the_bar_boundary_in_ppq)
{
    EngineRig r;
    r.pub.publish (makeOverlay (7, Quant::Bar, { { "fill", 80.0f } }));

    auto res = r.engine.beginChunk (r.pub, snap (5.3, 512));         // 512 samples = 1.067 quarters
    GF_EXPECT (res.appliedId == 0 && r.engine.hasPending());
    const auto& v = r.engine.view();
    GF_EXPECT_NEAR (v.boundaryPpq, 8.0, 1e-9);
    GF_EXPECT (! v.appliesAt (7.999999, true));
    GF_EXPECT (v.appliesAt (8.0, true));
    GF_EXPECT (! v.appliesAt (9.0, false));                           // no PPQ: not a quantised decision

    // The chunk that contains the boundary reports it (7.95 + 0.1 quarters crosses 8.0).
    res = r.engine.beginChunk (r.pub, snap (7.95, 2400));               // 7.5 .. 8.57
    GF_EXPECT (res.appliedId == 7 && r.engine.isApplied());
    GF_EXPECT (r.engine.getBoundaryPpqForUi() < 0.0);

    // Applying to settings uses real units.
    EngineSettings s;
    r.engine.view().applyTo (s);
    GF_EXPECT_NEAR (s.fill01, 0.8, 1e-6);
}

GF_TEST (Scenes, immediate_mode_applies_in_the_same_chunk)
{
    EngineRig r;
    r.pub.publish (makeOverlay (1, Quant::Immediate, { { "chaos", 10.0f }, { "wStutter", 90.0f }, { "fillType", 3.0f } }));
    const auto res = r.engine.beginChunk (r.pub, snap (5.3));
    GF_EXPECT (res.appliedId == 1);
    GF_EXPECT (r.engine.view().appliesAt (0.0, false));

    EngineSettings s;
    r.engine.view().applyTo (s);
    GF_EXPECT_NEAR (s.chaos01, 0.10, 1e-6);
    GF_EXPECT_NEAR (s.weights[(int) Algo::Stutter], 0.90, 1e-6);
    GF_EXPECT (s.fillTypeIndex == 3);
}

GF_TEST (Scenes, stopped_or_missing_transport_falls_back_to_immediate)
{
    EngineRig r;
    r.pub.publish (makeOverlay (2, Quant::Bar, { { "fill", 80.0f } }));
    auto res = r.engine.beginChunk (r.pub, snap (0.0, 512, 120.0, false));
    GF_EXPECT (res.appliedId == 2 && res.usedTransportFallback);

    // Pending on a running transport, then the transport stops: applies at once.
    EngineRig r2;
    r2.pub.publish (makeOverlay (3, Quant::Bar, { { "fill", 80.0f } }));
    GF_EXPECT (r2.engine.beginChunk (r2.pub, snap (1.0)).appliedId == 0);
    res = r2.engine.beginChunk (r2.pub, snap (0.0, 512, 120.0, false));
    GF_EXPECT (res.appliedId == 3 && res.usedTransportFallback);

    // "Wait for transport" option: stays pending until a transport exists.
    EngineRig r3;
    r3.pub.publish (makeOverlay (4, Quant::Bar, { { "fill", 80.0f } }, true));
    GF_EXPECT (r3.engine.beginChunk (r3.pub, snap (0.0, 512, 120.0, false)).appliedId == 0);
    GF_EXPECT (r3.engine.hasPending());
    GF_EXPECT (r3.engine.beginChunk (r3.pub, snap (7.95, 2400)).appliedId == 4);
}

GF_TEST (Scenes, loop_wrap_and_seek_rearm_the_pending_change_instead_of_dropping_it)
{
    EngineRig r;
    r.pub.publish (makeOverlay (5, Quant::Bar, { { "fill", 80.0f } }));
    r.engine.beginChunk (r.pub, snap (5.3));
    GF_EXPECT_NEAR (r.engine.view().boundaryPpq, 8.0, 1e-9);

    // loop: PPQ jumps back to 1.0
    auto res = r.engine.beginChunk (r.pub, snap (1.0));
    GF_EXPECT (res.appliedId == 0 && r.engine.hasPending());
    GF_EXPECT_NEAR (r.engine.view().boundaryPpq, 4.0, 1e-9);

    // seek far ahead
    res = r.engine.beginChunk (r.pub, snap (30.5));
    GF_EXPECT (res.appliedId == 0 && r.engine.hasPending());
    GF_EXPECT_NEAR (r.engine.view().boundaryPpq, 32.0, 1e-9);

    // applied once, never twice
    GF_EXPECT (r.engine.beginChunk (r.pub, snap (31.95, 2400)).appliedId == 5);
    GF_EXPECT (r.engine.beginChunk (r.pub, snap (32.5)).appliedId == 0);
}

GF_TEST (Scenes, tempo_change_before_the_boundary_keeps_the_ppq_boundary)
{
    EngineRig r;
    r.pub.publish (makeOverlay (6, Quant::Bar, { { "fill", 80.0f } }));
    r.engine.beginChunk (r.pub, snap (5.3, 512, 90.0));
    r.engine.beginChunk (r.pub, snap (5.6, 512, 174.0));
    GF_EXPECT_NEAR (r.engine.view().boundaryPpq, 8.0, 1e-9);
    GF_EXPECT (r.engine.beginChunk (r.pub, snap (7.9, 2400, 174.0)).appliedId == 6);
}

GF_TEST (Scenes, newer_request_replaces_pending_and_cancel_discards_it)
{
    EngineRig r;
    r.pub.publish (makeOverlay (10, Quant::Bar, { { "fill", 10.0f } }));
    r.engine.beginChunk (r.pub, snap (1.0));
    r.pub.publish (makeOverlay (11, Quant::Bar, { { "fill", 90.0f } }));
    r.engine.beginChunk (r.pub, snap (1.5));
    GF_EXPECT (r.engine.activeId() == 11);

    Overlay cancel;
    cancel.kind = Overlay::Cancel;
    cancel.id = 11;
    r.pub.publish (cancel);
    r.engine.beginChunk (r.pub, snap (2.0));
    GF_EXPECT (! r.engine.hasPending() && ! r.engine.view().active);
    GF_EXPECT (r.engine.beginChunk (r.pub, snap (7.95, 2400)).appliedId == 0);

    // panic path: cancelPending() drops the request but not an applied overlay
    r.pub.publish (makeOverlay (12, Quant::Bar, { { "fill", 90.0f } }));
    r.engine.beginChunk (r.pub, snap (2.0));
    r.engine.cancelPending();
    GF_EXPECT (r.engine.beginChunk (r.pub, snap (7.95, 2400)).appliedId == 0);

    r.pub.publish (makeOverlay (13, Quant::Immediate, { { "fill", 90.0f } }));
    GF_EXPECT (r.engine.beginChunk (r.pub, snap (2.0)).appliedId == 13);
    r.engine.cancelPending();
    GF_EXPECT (r.engine.isApplied());
}

GF_TEST (Scenes, same_id_with_fewer_entries_keeps_the_boundary)
{
    EngineRig r;
    r.pub.publish (makeOverlay (20, Quant::Bar, { { "fill", 90.0f }, { "chaos", 50.0f } }));
    r.engine.beginChunk (r.pub, snap (5.3));
    r.pub.publish (makeOverlay (20, Quant::Bar, { { "chaos", 50.0f } }));
    r.engine.beginChunk (r.pub, snap (5.9));
    GF_EXPECT_NEAR (r.engine.view().boundaryPpq, 8.0, 1e-9);
    GF_EXPECT (r.engine.view().n == 1);
}

GF_TEST (Scenes, macro_owned_targets_are_skipped_by_the_overlay)
{
    EngineRig r;
    r.pub.publish (makeOverlay (30, Quant::Immediate, { { "fill", 90.0f }, { "chaos", 60.0f } }));
    r.engine.beginChunk (r.pub, snap (0.0));

    EngineSettings s;
    s.fill01 = 0.2f;
    s.perf.macroOwnedMask = 1u << 0;                    // fill is mapped by a macro
    r.engine.view().applyTo (s);
    GF_EXPECT_NEAR (s.fill01, 0.2, 1e-6);
    GF_EXPECT_NEAR (s.chaos01, 0.6, 1e-6);
}

GF_TEST (Scenes, overlay_hot_path_does_not_allocate)
{
    EngineRig r;
    r.pub.publish (makeOverlay (40, Quant::Bar, { { "fill", 90.0f }, { "chaos", 60.0f } }));

    AllocGuard guard;
    EngineSettings s;

    for (int i = 0; i < 50; ++i)
    {
        r.engine.beginChunk (r.pub, snap (i * 0.5));
        r.engine.view().applyTo (s);
    }

    GF_EXPECT (guard.count() == 0);
}

//==============================================================================
namespace
{
    Scene sceneWith (const juce::String& name, std::initializer_list<std::pair<const char*, float>> normalised,
                     std::uint16_t scope = ScopeCreative)
    {
        Scene s;
        s.name = name;
        s.scope = scope;

        for (const auto& [id, v] : normalised)
            s.params.push_back ({ (std::uint16_t) paramIndexOf (id), v });

        return s;
    }
}

GF_TEST (Scenes, scope_flags_select_the_expected_parameters_and_never_ids_are_excluded)
{
    initTables();
    const auto& creative = idsForScope (ScopeCreative);
    GF_EXPECT (creative.size() == 6);
    const auto& weights = idsForScope (ScopeWeights);
    GF_EXPECT ((int) weights.size() == numAlgos);

    auto contains = [] (const std::vector<juce::String>& v, const char* id)
    {
        return std::find (v.begin(), v.end(), juce::String (id)) != v.end();
    };

    const auto& everything = idsForScope (kAllScopes);

    for (const char* never : { "dry", "wet", "output", "bypass", "lookahead", "seed", "capture", "audition" })
    {
        GF_EXPECT_MSG (isNeverId (never), never);
        GF_EXPECT_MSG (! contains (everything, never), never);
    }

    GF_EXPECT (contains (everything, "fill") && contains (everything, "wReverse"));
    GF_EXPECT (contains (everything, "protTrans") && contains (everything, "bandLowOn") && contains (everything, "minGap"));
    GF_EXPECT (! contains (idsForScope (kDefaultScope), "protTrans"));            // opt-in scopes
    GF_EXPECT (! contains (idsForScope (kDefaultScope), "minGap"));
    GF_EXPECT (idsForScope (kDefaultScope).size() <= (size_t) kMaxSceneParams);
}

GF_TEST (Scenes, capture_is_sparse_and_resolve_fills_the_rest_with_defaults)
{
    GapFillAudioProcessor proc;
    setParams (proc, { { "fill", 80.0f }, { "chaos", 25.0f } });          // chaos is already the default
    const auto s = captureScene (proc.getValueTreeState(), "A", ScopeCreative);
    GF_EXPECT (s.params.size() == 1 && paramIdAt (s.params[0].index) == "fill");

    std::vector<ResolvedParam> out;
    juce::String why;
    GF_EXPECT (resolveScene (proc.getValueTreeState(), s, out, why));
    GF_EXPECT (out.size() == 6);

    for (const auto& r : out)
        if (paramIdAt (r.index) == "fill")
            GF_EXPECT_NEAR (r.real, 80.0, 0.01);
        else if (paramIdAt (r.index) == "density")
            GF_EXPECT_NEAR (r.real, 65.0, 0.01);                          // default
}

GF_TEST (Scenes, scene_with_a_never_id_or_bad_value_is_invalid)
{
    GapFillAudioProcessor proc;
    initTables();
    std::vector<ResolvedParam> out;
    juce::String why;

    GF_EXPECT (! resolveScene (proc.getValueTreeState(), sceneWith ("x", { { "dry", 0.5f } }), out, why));
    GF_EXPECT (! resolveScene (proc.getValueTreeState(), sceneWith ("x", { { "fill", 1.5f } }), out, why));
    GF_EXPECT (! resolveScene (proc.getValueTreeState(), sceneWith ("x", { { "fill", std::nanf ("") } }), out, why));

    auto big = sceneWith ("x", {});

    for (int i = 0; i < kMaxSceneParams + 1; ++i)
        big.params.push_back ({ (std::uint16_t) paramIndexOf ("fill"), 0.5f });

    GF_EXPECT (! resolveScene (proc.getValueTreeState(), big, out, why));
}

GF_TEST (Scenes, controller_commit_matches_overlay_and_updates_last_good)
{
    GapFillAudioProcessor proc;
    auto& ctl = proc.getLiveControl().scenes();
    ctl.getBank().store (0, sceneWith ("Verse", { { "fill", 0.8f }, { "chaos", 0.1f } }));
    ctl.getBank().store (1, sceneWith ("Chorus", { { "fill", 0.2f } }));
    ctl.getBank().options.quant = Quant::Immediate;

    GF_EXPECT (ctl.gotoScene (0));
    GF_EXPECT (ctl.getPendingIndex() == 0 && ctl.getPendingId() != 0);

    // The audio thread reports the boundary; the message thread commits.
    prepareProcessor (proc, kSr, 256, false);
    juce::AudioBuffer<float> work (2, 256);
    juce::MidiBuffer midi;
    work.clear();
    proc.processBlock (work, midi);
    proc.flushLatencyReportForTests();                                    // timer: pump + commit

    GF_EXPECT (ctl.getCommitCount() == 1 && ctl.getCurrentIndex() == 0 && ctl.getLastGoodIndex() == 0);
    GF_EXPECT_NEAR (pv (proc.getValueTreeState().getRawParameterValue ("fill")), 80.0, 0.01);
    GF_EXPECT_NEAR (pv (proc.getValueTreeState().getRawParameterValue ("chaos")), 10.0, 0.01);
    GF_EXPECT_NEAR (pv (proc.getValueTreeState().getRawParameterValue ("density")), 65.0, 0.01);   // default for the scope

    // never-ids untouched
    setParams (proc, { { "dry", 33.0f }, { "wet", 44.0f }, { "output", -3.0f } });
    GF_EXPECT (ctl.gotoScene (1));
    work.clear();
    proc.processBlock (work, midi);
    proc.flushLatencyReportForTests();
    GF_EXPECT_NEAR (pv (proc.getValueTreeState().getRawParameterValue ("fill")), 20.0, 0.01);
    GF_EXPECT_NEAR (pv (proc.getValueTreeState().getRawParameterValue ("dry")), 33.0, 0.01);
    GF_EXPECT_NEAR (pv (proc.getValueTreeState().getRawParameterValue ("wet")), 44.0, 0.01);
    GF_EXPECT_NEAR (pv (proc.getValueTreeState().getRawParameterValue ("output")), -3.0, 0.01);
}

GF_TEST (Scenes, invalid_scene_is_never_applied_and_last_good_is_kept)
{
    GapFillAudioProcessor proc;
    auto& ctl = proc.getLiveControl().scenes();
    ctl.getBank().options.quant = Quant::Immediate;
    ctl.getBank().store (0, sceneWith ("Good", { { "fill", 0.5f } }));
    ctl.getBank().store (1, sceneWith ("Bad", { { "dry", 0.5f } }));

    prepareProcessor (proc, kSr, 256, false);
    juce::AudioBuffer<float> work (2, 256);
    juce::MidiBuffer midi;
    work.clear();

    GF_EXPECT (ctl.gotoScene (0));
    proc.processBlock (work, midi);
    proc.flushLatencyReportForTests();
    GF_EXPECT (ctl.getLastGoodIndex() == 0);

    GF_EXPECT (! ctl.gotoScene (1));
    GF_EXPECT (ctl.getStatus().contains ("invalid"));
    GF_EXPECT (! ctl.gotoScene (9));
    GF_EXPECT (ctl.getLastGoodIndex() == 0 && ctl.getPendingIndex() == -1);

    // go to another scene, then "last good" returns to the previous one
    ctl.getBank().store (2, sceneWith ("Other", { { "fill", 0.9f } }));
    GF_EXPECT (ctl.gotoScene (2));
    proc.processBlock (work, midi);
    proc.flushLatencyReportForTests();
    GF_EXPECT (ctl.getLastGoodIndex() == 2);
}

GF_TEST (Scenes, manual_edit_removes_only_that_overlay_entry)
{
    GapFillAudioProcessor proc;
    auto& ctl = proc.getLiveControl().scenes();
    ctl.getBank().options.quant = Quant::Bar;
    ctl.getBank().store (0, sceneWith ("A", { { "fill", 0.9f }, { "chaos", 0.8f } }));
    GF_EXPECT (ctl.gotoScene (0));

    setParams (proc, { { "chaos", 5.0f } });                  // the user touches chaos while the change is pending
    ctl.poll();

    Overlay seen;
    prepareProcessor (proc, kSr, 256, false);
    GF_EXPECT (ctl.publisher().consume (seen));               // the audio side's view of the latest publication
    GF_EXPECT (seen.kind == Overlay::Set && seen.n == 6 - 1);  // creative scope: 6 ids minus chaos
    bool hasChaos = false;

    for (int i = 0; i < seen.n; ++i)
        hasChaos = hasChaos || paramIdAt (seen.idx[i]) == "chaos";

    GF_EXPECT (! hasChaos);
}

GF_TEST (Scenes, next_prev_clamp_wrap_and_rapid_presses_advance_the_pending_target)
{
    GapFillAudioProcessor proc;
    auto& ctl = proc.getLiveControl().scenes();
    ctl.getBank().options.quant = Quant::Bar;

    for (int i = 0; i < 4; ++i)
        ctl.getBank().store (i, sceneWith ("S" + juce::String (i), { { "fill", 0.1f * (float) (i + 1) } }));

    GF_EXPECT (ctl.next() && ctl.getPendingIndex() == 0);        // from "none" the first scene
    GF_EXPECT (ctl.next() && ctl.getPendingIndex() == 1);        // rapid presses advance the pending target
    GF_EXPECT (ctl.next() && ctl.next() && ctl.getPendingIndex() == 3);
    const auto id = ctl.getPendingId();
    GF_EXPECT (ctl.next() && ctl.getPendingIndex() == 3);        // clamped at the end, nothing republished
    GF_EXPECT (ctl.getPendingId() == id);

    ctl.getBank().options.wrap = true;
    GF_EXPECT (ctl.next() && ctl.getPendingIndex() == 0);
    GF_EXPECT (ctl.prev() && ctl.getPendingIndex() == 3);
    ctl.getBank().options.wrap = false;
    ctl.gotoScene (0);
    GF_EXPECT (ctl.prev() && ctl.getPendingIndex() == 0);        // clamped at the start

    ctl.cancel();
    GF_EXPECT (ctl.getPendingIndex() == -1 && ctl.getPendingId() == 0);
}

GF_TEST (Scenes, whole_processor_switch_lands_at_the_bar_for_every_block_size)
{
    // Base Fill is 0 (silent wet). The scene sets Fill 100 at the next bar; the first
    // wet sample must not precede the bar line, whatever the host block size.
    auto firstWet = [] (int block)
    {
        GapFillAudioProcessor proc;
        setParams (proc, { { "fill", 0.0f }, { "density", 100.0f }, { "dry", 0.0f } });
        auto& ctl = proc.getLiveControl().scenes();
        ctl.getBank().options.quant = Quant::Bar;
        ctl.getBank().store (0, sceneWith ("Fill", { { "fill", 1.0f }, { "density", 1.0f } }));

        TestPlayHead ph;
        ph.ppq = 0.5;                                          // half a beat into bar 1
        const auto input = drumLoop (kSr, 120.0, 6);
        bool requested = false;

        auto r = renderLive (proc, input, block, kSr, ph,
            [&] (int b, int, juce::MidiBuffer&)
            {
                if (! requested && b == 2)
                {
                    requested = true;
                    ctl.gotoScene (0);
                }

                proc.flushLatencyReportForTests();
            });

        for (int i = 0; i < r.out.getNumSamples(); ++i)
            if (std::abs (r.out.getSample (0, i)) > 1.0e-7f)
                return i;

        return -1;
    };

    // ppq 0.5 at 120 bpm: the first bar line is at ppq 4 = 3.5 beats = 1.75 s into the render.
    const auto barLineSample = (int) (3.5 * 0.5 * kSr);
    const auto reference = firstWet (512);
    GF_EXPECT_MSG (reference >= barLineSample, "first wet sample " + juce::String (reference));

    for (const int block : { 32, 64, 4096 })
        GF_EXPECT_MSG (firstWet (block) == reference, "block " + juce::String (block));
}

GF_TEST (Scenes, bank_state_round_trip_old_state_and_cap)
{
    initTables();
    SceneBank bank;
    bank.setName ("Gig");
    bank.options.wrap = true;
    bank.options.quant = Quant::Phrase;

    for (int i = 0; i < kMaxScenes; ++i)
    {
        Scene s;
        s.name = "Scene " + juce::String (i + 1);
        s.scope = kDefaultScope;
        s.quant = (Quant) (i % 5);

        s.scope = ScopeCreative | ScopeWeights | ScopeDetector | ScopeProtection | ScopeBands;
        const auto& ids = idsForScope (s.scope);

        for (size_t p = 0; p < 36; ++p)                       // a typical live scene: ~36 parameters off default
            s.params.push_back ({ (std::uint16_t) paramIndexOf (ids[(p + (size_t) (i % 3)) % ids.size()]),
                                  0.25f + 0.01f * (float) ((p + (size_t) i) % 50) });

        bank.store (i, s);
    }

    bool truncated = false;
    const auto tree = bank.toValueTree (24 * 1024 - 64, &truncated);
    GF_EXPECT (! truncated);
    GF_EXPECT (state::serialisedSize (tree) <= 24 * 1024);
    GF_EXPECT (SceneBank::validateTree (tree));

    SceneBank back;
    back.restore (tree);
    GF_EXPECT (back.size() == kMaxScenes && back.getName() == "Gig" && back.options.wrap);
    GF_EXPECT (back.options.quant == Quant::Phrase);
    GF_EXPECT (back.canonicalPayload() == bank.canonicalPayload());

    // beyond the cap the tail is left out and reported, never a corrupt subtree
    const auto small = bank.toValueTree (4 * 1024, &truncated);
    GF_EXPECT (truncated && SceneBank::validateTree (small));
    GF_EXPECT (state::serialisedSize (small) <= 4 * 1024);
}

GF_TEST (Scenes, malformed_scenes_subtree_is_dropped_and_the_rest_of_the_state_loads)
{
    GapFillAudioProcessor a;
    setParams (a, { { "chaos", 77.0f } });
    a.getLiveControl().scenes().getBank().store (0, sceneWith ("Keep", { { "fill", 0.5f } }));

    juce::MemoryBlock blob;
    a.getStateInformation (blob);
    auto xml = juce::AudioProcessor::getXmlFromBinary (blob.getData(), (int) blob.getSize());
    GF_EXPECT (xml != nullptr);

    auto* scenesXml = xml->getChildByName ("Scenes");
    GF_EXPECT (scenesXml != nullptr && scenesXml->getNumChildElements() == 1);

    // round trip
    {
        GapFillAudioProcessor b;
        b.setStateInformation (blob.getData(), (int) blob.getSize());
        GF_EXPECT (b.getLiveControl().scenes().getBank().size() == 1);
    }

    // corrupt the scene payload
    scenesXml->getChildElement (0)->setAttribute ("d", "!!!not base64!!!");
    juce::MemoryBlock bad;
    juce::AudioProcessor::copyXmlToBinary (*xml, bad);

    GapFillAudioProcessor c;
    c.getLiveControl().scenes().getBank().store (0, sceneWith ("Old", { { "fill", 0.1f } }));
    c.setStateInformation (bad.getData(), (int) bad.getSize());
    GF_EXPECT (c.getLiveControl().scenes().getBank().size() == 0);          // dropped and reset
    GF_EXPECT_NEAR (pv (c.getValueTreeState().getRawParameterValue ("chaos")), 77.0, 0.01);   // rest loaded

    // old state without the subtree: empty bank
    scenesXml = xml->getChildByName ("Scenes");
    xml->removeChildElement (scenesXml, true);
    juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary (*xml, old);
    GapFillAudioProcessor d;
    d.getLiveControl().scenes().getBank().store (0, sceneWith ("Old", { { "fill", 0.1f } }));
    d.setStateInformation (old.getData(), (int) old.getSize());
    GF_EXPECT (d.getLiveControl().scenes().getBank().size() == 0);
}

GF_TEST (Scenes, panic_cancels_a_pending_scene_change)
{
    GapFillAudioProcessor proc;
    auto& live = proc.getLiveControl();
    live.scenes().getBank().options.quant = Quant::Bar;
    live.scenes().getBank().store (0, sceneWith ("A", { { "fill", 0.9f } }));

    prepareProcessor (proc, kSr, 256, false);
    TestPlayHead ph;
    proc.setPlayHead (&ph);

    juce::AudioBuffer<float> work (2, 256);
    juce::MidiBuffer midi;
    work.clear();

    GF_EXPECT (live.scenes().gotoScene (0));
    proc.processBlock (work, midi);                              // pending, boundary at ppq 4
    GF_EXPECT (live.scenes().getPendingIndex() == 0);

    live.panic().requestPanic();
    proc.processBlock (work, midi);
    proc.flushLatencyReportForTests();                           // UiRequest::SceneCancel
    GF_EXPECT (live.scenes().getPendingIndex() == -1);

    for (int i = 0; i < 600; ++i)
    {
        ph.advance (256, kSr);
        proc.processBlock (work, midi);
    }

    proc.flushLatencyReportForTests();
    GF_EXPECT (live.scenes().getCommitCount() == 0);
    proc.setPlayHead (nullptr);
}
