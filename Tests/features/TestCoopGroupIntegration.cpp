// F-X06 group-scene production integration without an editor. Each receiver
// resolves its own scene bank; a late receiver rejects rather than applying at
// another boundary. Already-applied peers are not a distributed transaction.
#include "../support/AllocGuard.h"
#include "../support/TestRig.h"
#include "features/scenes/SceneModel.h"

#include <array>
#include <memory>

using namespace gapfill;
using namespace gftest;

namespace
{
    constexpr double kRate = 48000;
    constexpr int kBlock = 512;
    constexpr double kTarget = 8;

    struct GroupRig
    {
        std::array<std::unique_ptr<GapFillAudioProcessor>, 2> members;
        std::array<TestPlayHead, 2> heads;
        juce::AudioBuffer<float> work { 2, kBlock };
        juce::MidiBuffer midi;

        GroupRig()
        {
            for (int i = 0; i < 2; ++i)
            {
                auto& proc = members[(std::size_t) i];
                proc = std::make_unique<GapFillAudioProcessor>();
                setParams (*proc, { { "coopDeterministic", 1 }, { "coopDetGroup", 8 }, { "coopSlot", (float) (i + 1) } });
                prepareProcessor (*proc, kRate, kBlock, false);
                heads[(std::size_t) i].tsNum = heads[(std::size_t) i].tsDen = 4;
                proc->setPlayHead (&heads[(std::size_t) i]);
                GF_EXPECT (proc->getActiveEditor() == nullptr);
            }
            // Register both members before any callback; consume first-Start
            // cancellations before publishing the test request.
            stepBoth();
            pumpBoth();
        }

        ~GroupRig()
        {
            for (auto& proc : members)
            {
                proc->setPlayHead (nullptr);
                proc->releaseResources();
            }
        }

        GapFillAudioProcessor& at (int i) { return *members[(std::size_t) i]; }
        scenes::SceneController& scenesAt (int i) { return at (i).getLiveControl().scenes(); }

        void stepOne (int i)
        {
            work.clear();
            midi.clear();
            at (i).processBlock (work, midi);
            heads[(std::size_t) i].advance (kBlock, kRate);
        }
        void stepBoth() { stepOne (0); stepOne (1); }
        void pumpOne (int i) { at (i).getLiveControl().pump (at (i)); }
        void pumpBoth() { pumpOne (0); pumpOne (1); }

        void addScenes (int i, float groupFill = 73)
        {
            setParams (at (i), { { "fill", groupFill } });
            GF_EXPECT (scenesAt (i).saveCurrent (0, "Group sound", scenes::ScopeCreative));
            setParams (at (i), { { "fill", 23 } });
            GF_EXPECT (scenesAt (i).saveCurrent (1, "New local sound", scenes::ScopeCreative));
            scenesAt (i).getBank().at (1).quant = scenes::Quant::Immediate;
            setParams (at (i), { { "fill", 15 } });
        }

        void request()
        {
            GF_EXPECT (at (0).getCoop().requestGroupScene (0, kTarget));
        }

        void finishBoundary()
        {
            for (int count = 0; count < 500 && heads[0].ppq < kTarget + 0.2; ++count)
            {
                stepBoth();
                pumpBoth();
            }
            GF_EXPECT (heads[0].ppq >= kTarget + 0.1);
        }
    };
}

GF_TEST (CoopGroupIntegration, closed_editor_recall_uses_one_fixed_ppq_and_each_receivers_bank)
{
    GroupRig rig;
    rig.addScenes (0, 73);
    rig.addScenes (1, 42);
    rig.request();
    rig.pumpBoth();
    GF_EXPECT (rig.scenesAt (0).getPendingIndex() == 0 && rig.scenesAt (1).getPendingIndex() == 0);
    rig.stepBoth();
    for (int i = 0; i < 2; ++i)
    {
        const auto& view = rig.at (i).getLiveControl().sceneView();
        GF_EXPECT (view.active && ! view.immediate && view.boundaryPpq == kTarget);
        GF_EXPECT (! view.appliesAt (kTarget - 1e-8, true) && view.appliesAt (kTarget, true));
        GF_EXPECT (rig.scenesAt (i).getCommitCount() == 0);
    }
    rig.finishBoundary();
    GF_EXPECT (rig.scenesAt (0).getCommitCount() == 1 && rig.scenesAt (1).getCommitCount() == 1);
    GF_EXPECT_NEAR (rig.at (0).getValueTreeState().getRawParameterValue ("fill")->load(), 73, 1e-6);
    GF_EXPECT_NEAR (rig.at (1).getValueTreeState().getRawParameterValue ("fill")->load(), 42, 1e-6);
}

GF_TEST (CoopGroupIntegration, cancelling_an_older_group_request_keeps_a_newer_local_recall)
{
    GroupRig rig;
    rig.addScenes (0);
    rig.addScenes (1);
    rig.request();
    rig.pumpBoth();
    const auto groupId = rig.scenesAt (1).getPendingId();
    GF_EXPECT (rig.scenesAt (1).gotoScene (1));
    const auto localId = rig.scenesAt (1).getPendingId();
    GF_EXPECT (localId != 0 && localId != groupId);
    rig.at (0).getCoop().cancelGroupScenes();
    rig.pumpBoth();
    GF_EXPECT (rig.scenesAt (0).getPendingId() == 0 && rig.scenesAt (1).getPendingId() == localId);
    rig.stepOne (1);
    rig.pumpOne (1);
    GF_EXPECT (rig.scenesAt (1).getCommitCount() == 1 && rig.scenesAt (1).getCurrentIndex() == 1);
    GF_EXPECT_NEAR (rig.at (1).getValueTreeState().getRawParameterValue ("fill")->load(), 23, 1e-6);
}

GF_TEST (CoopGroupIntegration, panicked_receiver_discards_a_recall_and_recovery_cannot_resurrect_it)
{
    GroupRig rig;
    rig.addScenes (0);
    rig.addScenes (1);
    rig.at (1).getLiveControl().panic().requestPanic();
    rig.stepBoth();
    rig.pumpBoth();
    GF_EXPECT (rig.at (1).getLiveControl().panic().isPanicked());
    rig.request();
    rig.pumpBoth();
    rig.pumpOne (0);                  // consume cancellation broadcast by the panicked peer
    GF_EXPECT (rig.scenesAt (0).getPendingId() == 0 && rig.scenesAt (1).getPendingId() == 0);
    rig.at (1).getLiveControl().panic().requestRecover();
    rig.stepBoth();
    rig.pumpBoth();
    GF_EXPECT (! rig.at (1).getLiveControl().panic().isPanicked());
    for (int i = 0; i < 4; ++i) { rig.stepBoth(); rig.pumpBoth(); }
    GF_EXPECT (rig.scenesAt (0).getPendingId() == 0 && rig.scenesAt (1).getPendingId() == 0);
    GF_EXPECT (rig.scenesAt (0).getCommitCount() == 0 && rig.scenesAt (1).getCommitCount() == 0);
    rig.request();                    // an explicit fresh request after Recover is allowed
    rig.pumpBoth();
    GF_EXPECT (rig.scenesAt (0).getPendingId() != 0 && rig.scenesAt (1).getPendingId() != 0);
}

GF_TEST (CoopGroupIntegration, missing_scene_cancels_an_armed_peer_on_audio_before_the_next_message_pump)
{
    GroupRig rig;
    rig.addScenes (0);                // peer1 has no slot0 in its own bank
    rig.request();
    rig.pumpOne (0);
    rig.stepOne (0);                  // first peer adopts its externalGroup/cancelEpoch
    GF_EXPECT (rig.at (0).getLiveControl().sceneView().active);
    const auto epoch = rig.at (0).getCoop().groupCancelGeneration (8);
    rig.pumpOne (1);                  // receiver validation rejects and broadcasts cancellation
    GF_EXPECT (rig.at (0).getCoop().groupCancelGeneration (8) != epoch);
    long allocations = 0;
    {
        AllocGuard guard;
        rig.stepOne (0);              // no message pump before this RT epoch check
        allocations = guard.count();
    }
    GF_EXPECT (allocations == 0 && ! rig.at (0).getLiveControl().sceneView().active);
    rig.pumpOne (0);
    GF_EXPECT (rig.scenesAt (0).getPendingId() == 0 && rig.scenesAt (0).getCommitCount() == 0);
    GF_EXPECT (rig.scenesAt (1).getPendingId() == 0 && rig.scenesAt (1).getStatus().isNotEmpty());
}

GF_TEST (CoopGroupIntegration, cancellation_before_first_audio_adoption_rejects_the_stale_overlay)
{
    GroupRig rig;
    rig.addScenes (0);
    rig.addScenes (1);
    rig.request();
    rig.pumpBoth();
    rig.at (1).getCoop().cancelGroupScenes();
    // Message-side cancellation is intentionally not pumped: the coherent
    // epoch attached to the unconsumed overlay must guard first adoption.
    rig.stepBoth();
    GF_EXPECT (! rig.at (0).getLiveControl().sceneView().active && ! rig.at (1).getLiveControl().sceneView().active);
    rig.pumpBoth();
    GF_EXPECT (rig.scenesAt (0).getPendingId() == 0 && rig.scenesAt (1).getPendingId() == 0);
}

GF_TEST (CoopGroupIntegration, late_receiver_reports_missed_deadline_without_applying_at_another_boundary)
{
    GroupRig rig;
    rig.addScenes (0);
    rig.addScenes (1);
    rig.request();
    rig.pumpOne (0);                  // only the first member's timer sees this request in time
    for (int count = 0; count < 500 && rig.heads[0].ppq < kTarget + 0.1; ++count)
    {
        rig.stepBoth();
        rig.pumpOne (0);
    }
    GF_EXPECT (rig.scenesAt (0).getCommitCount() == 1);
    rig.pumpOne (1);                  // late adoption is rejected on its next callback
    rig.stepOne (1);
    rig.pumpOne (1);
    GF_EXPECT (rig.scenesAt (1).getCommitCount() == 0 && rig.scenesAt (1).getPendingId() == 0);
    GF_EXPECT (rig.scenesAt (1).getStatus().containsIgnoreCase ("missed"));
    for (int i = 0; i < 4; ++i) { rig.stepOne (1); rig.pumpOne (1); }
    GF_EXPECT (rig.scenesAt (1).getCommitCount() == 0);
    GF_EXPECT_NEAR (rig.at (1).getValueTreeState().getRawParameterValue ("fill")->load(), 15, 1e-6);
}
