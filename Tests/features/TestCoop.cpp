// F-X06 Multi-instance cooperation, safe subset (docs/dev/12 F-X06).

#include "../support/TestRig.h"
#include "../support/AllocGuard.h"
#include "../support/VoiceRig.h"

#include "features/coop/Coop.h"

#include <atomic>
#include <cmath>
#include <thread>

using namespace gapfill;
using namespace gftest;
using namespace gapfill::coop;

namespace
{
    constexpr double kSr = 48000.0;

    juce::SharedResourcePointer<CoopRegistry>& reg()
    {
        static juce::SharedResourcePointer<CoopRegistry> r;
        return r;
    }

    int liveSlots()
    {
        int n = 0;
        for (auto& s : reg()->slots)
            n += s.id.load() != 0 ? 1 : 0;
        return n;
    }

    TransitionLog fillAt (std::int64_t pos, int length)
    {
        TransitionLog l;
        l.push (Transition::FillStart, pos, 100, length);
        return l;
    }
}

GF_TEST (Coop, the_registry_is_plain_lock_free_data)
{
    static_assert (std::is_trivially_destructible_v<Slot>);
    GF_EXPECT (std::atomic<double>::is_always_lock_free);
    GF_EXPECT (sizeof (CoopRegistry) < 2048);
    GF_EXPECT (kMaxSlots == 32);
}

GF_TEST (Coop, slot_lifecycle_claim_refresh_release_and_destructor)
{
    const auto before = liveSlots();
    {
        CoopMember a;
        GF_EXPECT (a.getId() == 0 && liveSlots() == before);              // nothing claimed while Off
        a.beginChunk (GroupA, AvoidOverlap, kSr, 0.0, 1000);
        GF_EXPECT (a.getId() != 0 && liveSlots() == before + 1);

        CoopMember b;
        b.beginChunk (GroupA, AvoidOverlap, kSr, 0.0, 1000);
        GF_EXPECT (b.getId() != a.getId() && liveSlots() == before + 2);

        a.beginChunk (GroupOff, AvoidOverlap, kSr, 0.0, 1100);            // group Off frees the slot
        GF_EXPECT (a.getId() == 0 && liveSlots() == before + 1);
    }
    GF_EXPECT (liveSlots() == before);                                     // destructors released everything
}

GF_TEST (Coop, a_lone_member_and_members_of_other_groups_are_inert)
{
    CoopMember a, b;
    auto s = a.beginChunk (GroupA, AvoidOverlap, kSr, 0.0, 1000);
    GF_EXPECT (s.view == nullptr && a.getPeerCount() == 0);
    GF_EXPECT (a.describe() == "Co-op: no other GapFill found");

    s = b.beginChunk (GroupB, AvoidOverlap, kSr, 0.0, 1000);
    GF_EXPECT (s.view == nullptr);

    s = a.beginChunk (GroupA, AvoidOverlap, kSr, 0.0, 1010);
    GF_EXPECT (s.view == nullptr);
}

GF_TEST (Coop, a_slot_that_was_not_refreshed_for_two_seconds_is_ignored)
{
    CoopMember a, b;
    a.beginChunk (GroupA, AvoidOverlap, kSr, 0.0, 1000);
    b.beginChunk (GroupA, AvoidOverlap, kSr, 0.0, 1000);

    auto s = a.beginChunk (GroupA, AvoidOverlap, kSr, 0.0, 2500);          // b last seen 1500 ms ago
    GF_EXPECT (s.view != nullptr && a.getPeerCount() == 1);
    s = a.beginChunk (GroupA, AvoidOverlap, kSr, 0.0, 3100);               // 2100 ms ago: stale
    GF_EXPECT (s.view == nullptr && a.getPeerCount() == 0);
    b.beginChunk (GroupA, AvoidOverlap, kSr, 0.0, 3200);                   // refreshed: visible again
    s = a.beginChunk (GroupA, AvoidOverlap, kSr, 0.0, 3300);
    GF_EXPECT (s.view != nullptr);
}

GF_TEST (Coop, avoid_overlap_yields_while_another_member_has_a_fill_active)
{
    CoopMember a, b;
    a.beginChunk (GroupA, AvoidOverlap, kSr, 100.0, 1000);
    b.beginChunk (GroupA, AvoidOverlap, kSr, 100.0, 1000);

    // wall-clock domain: a fill of a that lasts 400 ms from chunk offset 0
    a.publish (fillAt (0, (int) (0.4 * kSr)), 0, 512, false, 0.0, 0.0);
    auto s = b.beginChunk (GroupA, AvoidOverlap, kSr, 100.0, 1005);
    GF_EXPECT (s.view != nullptr);

    GF_EXPECT (evaluate (*s.view, false, 0.0, 0, 0.0, 4.0) == RejectReason::CoopYield);                       // during
    GF_EXPECT (evaluate (*s.view, false, 0.0, (std::int64_t) (0.3 * kSr), 0.0, 4.0) == RejectReason::CoopYield);
    GF_EXPECT (evaluate (*s.view, false, 0.0, (std::int64_t) (0.5 * kSr), 0.0, 4.0) == RejectReason::None);   // after

    // an early end (protection fade) frees the slot
    TransitionLog end;
    end.push (Transition::FillEnd, 4800, 0, 0);
    a.publish (end, 0, 9600, false, 0.0, 0.0);
    s = b.beginChunk (GroupA, AvoidOverlap, kSr, 100.0, 1010);
    GF_EXPECT (evaluate (*s.view, false, 0.0, 9600, 0.0, 4.0) == RejectReason::None);
}

GF_TEST (Coop, avoid_overlap_in_the_musical_domain_uses_ppq)
{
    CoopMember a, b;
    const double spb = 24000.0;                                             // 120 bpm at 48 kHz
    a.beginChunk (GroupA, AvoidOverlap, kSr, 0.0, 1000);
    b.beginChunk (GroupA, AvoidOverlap, kSr, 0.0, 1000);
    a.publish (fillAt (0, 24000), 0, 512, true, 8.0, spb);                  // a fill of one beat starting at ppq 8
    const auto s = b.beginChunk (GroupA, AvoidOverlap, kSr, 0.0, 1001);

    GF_EXPECT (evaluate (*s.view, true, 8.5, 0, spb, 4.0) == RejectReason::CoopYield);
    GF_EXPECT (evaluate (*s.view, true, 9.2, 0, spb, 4.0) == RejectReason::None);
    GF_EXPECT (evaluate (*s.view, true, 7.9, 0, spb, 4.0) == RejectReason::None);
}

GF_TEST (Coop, members_in_different_time_domains_are_not_compared)
{
    CoopMember a, b;
    a.beginChunk (GroupA, AvoidOverlap, kSr, 0.0, 1000);
    b.beginChunk (GroupA, AvoidOverlap, kSr, 0.0, 1000);
    a.publish (fillAt (0, 48000), 0, 512, false, 0.0, 0.0);                 // wall domain
    const auto s = b.beginChunk (GroupA, AvoidOverlap, kSr, 0.0, 1001);
    GF_EXPECT (evaluate (*s.view, true, 1.0, 0, 24000.0, 4.0) == RejectReason::None);
}

GF_TEST (Coop, alternate_gives_each_member_every_nth_bar_in_instance_id_order)
{
    CoopMember a, b, c;
    for (auto* m : { &a, &b, &c })
        m->beginChunk (GroupA, Alternate, kSr, 0.0, 1000);

    std::vector<CoopMember*> byId { &a, &b, &c };
    std::sort (byId.begin(), byId.end(), [] (auto* x, auto* y) { return x->getId() < y->getId(); });

    const double spb = 24000.0;
    CoopSettings settings[3];
    CoopMember* order[3] { byId[0], byId[1], byId[2] };

    for (int i = 0; i < 3; ++i)
        order[i]->publish ({}, 0, 512, true, 0.0, spb);                    // every member is alive in the ppq domain

    for (int i = 0; i < 3; ++i)
        settings[i] = order[i]->beginChunk (GroupA, Alternate, kSr, 0.0, 1001);

    for (int bar = 0; bar < 12; ++bar)
    {
        int owners = 0;

        for (int i = 0; i < 3; ++i)
        {
            const auto verdict = evaluate (*settings[i].view, true, bar * 4.0 + 1.0, 0, spb, 4.0);
            owners += verdict == RejectReason::None ? 1 : 0;
            GF_EXPECT_MSG ((verdict == RejectReason::None) == (bar % 3 == i), "bar " + juce::String (bar) + " member " + juce::String (i));
        }

        GF_EXPECT (owners == 1);                                           // exactly one owner per bar
    }

    // deterministic: asking again gives the same answer; and without a host position it is paused
    GF_EXPECT (evaluate (*settings[0].view, true, 1.0, 0, spb, 4.0) == evaluate (*settings[0].view, true, 1.0, 0, spb, 4.0));
    GF_EXPECT (evaluate (*settings[1].view, false, 0.0, 0, 0.0, 4.0) == RejectReason::None);
}

GF_TEST (Coop, follow_allows_a_start_only_within_fifty_milliseconds_after_another_members_start)
{
    CoopMember leader, follower;
    leader.beginChunk (GroupA, AvoidOverlap, kSr, 500.0, 1000);
    follower.beginChunk (GroupA, Follow, kSr, 500.0, 1000);
    leader.publish (fillAt (0, 4800), 0, 512, false, 0.0, 0.0);              // leader starts at wall 500 ms
    const auto s = follower.beginChunk (GroupA, Follow, kSr, 500.0, 1001);

    GF_EXPECT (evaluate (*s.view, false, 0.0, 0, 0.0, 4.0) == RejectReason::None);                      // 0 ms after
    GF_EXPECT (evaluate (*s.view, false, 0.0, (std::int64_t) (0.040 * kSr), 0.0, 4.0) == RejectReason::None);
    GF_EXPECT (evaluate (*s.view, false, 0.0, (std::int64_t) (0.080 * kSr), 0.0, 4.0) == RejectReason::CoopYield);
    GF_EXPECT (evaluate (*s.view, false, 0.0, -(std::int64_t) (0.010 * kSr), 0.0, 4.0) == RejectReason::CoopYield);   // before the start
}

GF_TEST (Coop, registry_operations_do_not_allocate)
{
    CoopMember a, b;
    a.beginChunk (GroupA, AvoidOverlap, kSr, 0.0, 1000);
    b.beginChunk (GroupA, AvoidOverlap, kSr, 0.0, 1000);
    const auto log = fillAt (10, 1000);

    AllocGuard guard;

    for (int i = 0; i < 1000; ++i)
    {
        a.beginChunk (GroupA, Alternate, kSr, (double) i, 1000 + (std::uint32_t) i);
        a.publish (log, 0, 512, i % 2 == 0, (double) i, 24000.0);
        const auto s = b.beginChunk (GroupA, AvoidOverlap, kSr, (double) i, 1000 + (std::uint32_t) i);

        if (s.view != nullptr)
            evaluate (*s.view, true, (double) i, 5, 24000.0, 4.0);
    }

    GF_EXPECT (guard.count() == 0);
}

GF_TEST (Coop, claim_release_stress_from_many_threads_loses_no_slot)
{
    const auto before = liveSlots();
    std::atomic<int> failures { 0 };
    std::vector<std::thread> threads;

    for (int t = 0; t < 8; ++t)
        threads.emplace_back ([&failures, t]
        {
            for (int i = 0; i < 2000; ++i)
            {
                CoopMember m;
                const auto s = m.beginChunk (GroupA + (t & 1), (i + t) % NumModes, kSr, (double) i, 1000 + (std::uint32_t) i);

                if (m.getId() == 0)
                    continue;                                              // a full table is legal

                m.publish (fillAt (0, 100), 0, 512, (i & 1) != 0, (double) i, 24000.0);

                if (s.view != nullptr)
                {
                    evaluate (*s.view, true, (double) i, 3, 24000.0, 4.0);

                    for (int p = 0; p < s.view->numPeers; ++p)
                        if (s.view->peers[p].id == m.getId())
                            ++failures;                                    // a member must never see itself
                }
            }
        });

    for (auto& th : threads)
        th.join();

    GF_EXPECT (failures.load() == 0);
    GF_EXPECT_MSG (liveSlots() == before, "leaked slots " + juce::String (liveSlots() - before));
}

//==============================================================================
GF_TEST (Coop, parameters_are_registered_off_by_default_and_never_randomised)
{
    GapFillAudioProcessor proc;
    auto& v = proc.getValueTreeState();
    GF_EXPECT (v.getRawParameterValue ("coopGroup")->load() == 0.0f);
    GF_EXPECT (v.getRawParameterValue ("coopMode")->load() == 0.0f);

    for (auto* id : { "coopGroup", "coopMode" })
    {
        GF_EXPECT (performanceParameterIDs().contains (id));
        GF_EXPECT (! randomisableParameterIDs().contains (id));
    }
}

namespace
{
    struct Pair
    {
        int yieldsA = 0, yieldsB = 0, fillsA = 0, fillsB = 0;
        juce::AudioBuffer<float> outA, outB;
    };

    /** Two instances in one process, processed block by block in lock-step (A then B). */
    Pair runPair (float groupA, float groupB, float modeA, float modeB, int block = 512, bool transport = true)
    {
        GapFillAudioProcessor a, b;
        setParams (a, { { "fill", 100.0f }, { "density", 100.0f }, { "minGap", 20.0f }, { "coopGroup", groupA }, { "coopMode", modeA } });
        setParams (b, { { "fill", 100.0f }, { "density", 100.0f }, { "minGap", 20.0f }, { "coopGroup", groupB }, { "coopMode", modeB } });
        prepareProcessor (a, kSr, block, false);
        prepareProcessor (b, kSr, block, false);
        TestPlayHead pa, pb;
        pa.bpm = pb.bpm = 150.0;
        pa.provideInfo = pb.provideInfo = transport;
        a.setPlayHead (&pa);
        b.setPlayHead (&pb);

        const auto in = drumLoop (kSr, 150.0, 4);
        Pair p;
        p.outA.setSize (2, in.getNumSamples());
        p.outB.setSize (2, in.getNumSamples());
        juce::AudioBuffer<float> wa (2, block), wb (2, block);
        juce::MidiBuffer midi;

        for (int pos = 0; pos + block <= in.getNumSamples(); pos += block)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                wa.copyFrom (ch, 0, in, ch, pos, block);
                wb.copyFrom (ch, 0, in, ch, pos, block);
            }

            a.processBlock (wa, midi);
            b.processBlock (wb, midi);

            for (int ch = 0; ch < 2; ++ch)
            {
                p.outA.copyFrom (ch, pos, wa, ch, 0, block);
                p.outB.copyFrom (ch, pos, wb, ch, 0, block);
            }

            pa.advance (block, kSr);
            pb.advance (block, kSr);
        }

        a.setPlayHead (nullptr);
        b.setPlayHead (nullptr);
        p.yieldsA = a.getFillEngine().getRejectCount (RejectReason::CoopYield);
        p.yieldsB = b.getFillEngine().getRejectCount (RejectReason::CoopYield);
        p.fillsA = a.getFillEngine().getFillsStarted();
        p.fillsB = b.getFillEngine().getFillsStarted();
        return p;
    }
}

GF_TEST (Coop, off_identity_and_single_instance_inert)
{
    GapFillAudioProcessor solo, grouped;
    setParams (solo, { { "fill", 100.0f }, { "density", 100.0f } });
    setParams (grouped, { { "fill", 100.0f }, { "density", 100.0f }, { "coopGroup", 1.0f }, { "coopMode", 0.0f } });
    TestPlayHead ph;
    RenderOptions o;
    o.playHead = &ph;
    const auto a = renderOffline (solo, drumLoop (kSr, 150.0, 3), 512, kSr, o).out;
    TestPlayHead ph2;
    o.playHead = &ph2;
    const auto b = renderOffline (grouped, drumLoop (kSr, 150.0, 3), 512, kSr, o).out;
    GF_EXPECT (buffersIdentical (a, b));                                   // alone in the group: nothing changes
    GF_EXPECT (grouped.getFillEngine().getRejectCount (RejectReason::CoopYield) == 0);
}

GF_TEST (Coop, avoid_overlap_two_instances_yield_to_each_other_and_the_reason_is_reachable)
{
    const auto off = runPair (0.0f, 0.0f, 0.0f, 0.0f);
    GF_EXPECT (off.yieldsA == 0 && off.yieldsB == 0 && off.fillsA > 0 && off.fillsB > 0);

    const auto grouped = runPair (1.0f, 1.0f, 0.0f, 0.0f);
    GF_EXPECT (grouped.yieldsA + grouped.yieldsB > 0);
    GF_EXPECT (grouped.fillsA + grouped.fillsB < off.fillsA + off.fillsB);

    const auto separate = runPair (1.0f, 2.0f, 0.0f, 0.0f);                 // different groups do not interact
    GF_EXPECT (separate.yieldsA == 0 && separate.yieldsB == 0);
}

GF_TEST (Coop, alternate_splits_the_bars_between_two_instances_and_is_reproducible)
{
    const auto x = runPair (1.0f, 1.0f, 1.0f, 1.0f);
    const auto y = runPair (1.0f, 1.0f, 1.0f, 1.0f);
    GF_EXPECT (x.yieldsA > 0 && x.yieldsB > 0);                             // each yields in the other's bars
    GF_EXPECT (x.yieldsA == y.yieldsA && x.yieldsB == y.yieldsB);
    GF_EXPECT (buffersIdentical (x.outA, y.outA) && buffersIdentical (x.outB, y.outB));
}

GF_TEST (Coop, alternate_is_paused_without_a_host_position)
{
    const auto r = runPair (1.0f, 1.0f, 1.0f, 1.0f, 512, false);
    GF_EXPECT (r.yieldsA == 0 && r.yieldsB == 0);
}

GF_TEST (Coop, releasing_resources_frees_the_slot)
{
    const auto before = liveSlots();
    GapFillAudioProcessor a, b;
    setParams (a, { { "coopGroup", 1.0f } });
    setParams (b, { { "coopGroup", 1.0f } });
    TestPlayHead pa, pb;
    RenderOptions o;
    o.playHead = &pa;
    renderOffline (a, drumLoop (kSr, 150.0, 1), 512, kSr, o);
    o.playHead = &pb;
    renderOffline (b, drumLoop (kSr, 150.0, 1), 512, kSr, o);
    GF_EXPECT (liveSlots() == before + 2);
    a.releaseResources();
    GF_EXPECT (liveSlots() == before + 1);
    b.releaseResources();
    GF_EXPECT (liveSlots() == before);
}
