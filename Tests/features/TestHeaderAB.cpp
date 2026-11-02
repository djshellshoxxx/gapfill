#include "../TestFramework.h"
#include "features/abcompare/HeaderAB.h"

using namespace gapfill::abcompare;
using namespace gftest;

namespace
{
    juce::ValueTree sound (int value)
    {
        juce::ValueTree state ("sound");
        state.setProperty ("fill", value, nullptr);
        return state;
    }
}

GF_TEST (HeaderAB, swaps_capture_each_departing_side_and_own_independent_trees)
{
    HeaderAB model;
    auto first = sound (20);
    auto target = model.swap (first);
    GF_EXPECT (model.isUsingB() && (int) target["fill"] == 20);
    first.setProperty ("fill", 999, nullptr);
    GF_EXPECT ((int) model.snapshot (false)["fill"] == 20);
    target = model.swap (sound (80));
    GF_EXPECT (! model.isUsingB() && (int) target["fill"] == 20);
    target = model.swap (sound (30));
    GF_EXPECT (model.isUsingB() && (int) target["fill"] == 80);
    target.setProperty ("fill", 888, nullptr);
    GF_EXPECT ((int) model.snapshot (true)["fill"] == 80);
}

GF_TEST (HeaderAB, copy_keeps_current_side_and_copies_both_snapshots)
{
    HeaderAB model;
    model.swap (sound (20));
    model.copyToOther (sound (65));
    GF_EXPECT (model.isUsingB());
    GF_EXPECT ((int) model.snapshot (false)["fill"] == 65 && (int) model.snapshot (true)["fill"] == 65);
    GF_EXPECT ((int) model.swap (sound (70))["fill"] == 65);
}

GF_TEST (HeaderAB, host_restore_requests_reset_without_touching_message_owned_trees)
{
    HeaderAB model;
    model.swap (sound (20));
    model.captureActive (sound (80));
    model.requestReset();
    GF_EXPECT (model.isUsingB() && (int) model.snapshot (false)["fill"] == 20);
    model.synchronize (sound (45));
    GF_EXPECT (! model.isUsingB());
    GF_EXPECT ((int) model.snapshot (false)["fill"] == 45 && (int) model.snapshot (true)["fill"] == 45);
}

GF_TEST (HeaderAB, sound_metadata_is_copied_and_performance_nodes_are_excluded)
{
    HeaderAB model;
    model.setPerformanceIds ({ "fl11_chan" });
    auto state = sound (20);
    juce::ValueTree routing ("PARAM");
    routing.setProperty ("id", "fl11_chan", nullptr);
    routing.setProperty ("value", 7, nullptr);
    state.appendChild (routing, nullptr);
    juce::ValueTree versions ("ModelVersions");
    versions.setProperty ("cloud", 1, nullptr);
    state.appendChild (versions, nullptr);
    model.captureActive (state);
    GF_EXPECT (! model.snapshot (false).getChildWithProperty ("id", "fl11_chan").isValid());
    GF_EXPECT ((int) model.snapshot (false).getChildWithName ("ModelVersions")["cloud"] == 1);
}
