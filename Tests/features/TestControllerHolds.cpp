#include "../TestFramework.h"
#include "../support/AllocGuard.h"
#include "features/momentary/HoldController.h"
using namespace gapfill::momentary;
using namespace gftest;
GF_TEST (ControllerHolds, momentary_controller_releases_restore_gui_latches_without_toggling_them)
{
    HoldController hold; hold.setLatch (true);
    hold.setLevel (Effect::Stutter, Source::Gui, true);
    GF_EXPECT (hold.topEffect() == (int) Effect::Stutter);
    hold.setControllerLevel (Effect::Freeze, true);
    GF_EXPECT (hold.topEffect() == (int) Effect::Freeze && hold.engagedMask() == 3);
    hold.setControllerLevel (Effect::Freeze, false);
    GF_EXPECT (hold.topEffect() == (int) Effect::Stutter && hold.engagedMask() == 1);
    hold.setControllerLevel (Effect::Stutter, true); hold.clearControllerLevels();
    GF_EXPECT (hold.engagedMask() == 1);
}
GF_TEST (ControllerHolds, latest_engage_order_is_shared_across_controller_and_legacy_sources)
{
    HoldController hold;
    hold.setLevel (Effect::Freeze, Source::Param, true);
    hold.setControllerLevel (Effect::Stutter, true);
    GF_EXPECT (hold.topEffect() == (int) Effect::Stutter);
    hold.setLevel (Effect::TapeStop, Source::Gui, true);
    GF_EXPECT (hold.topEffect() == (int) Effect::TapeStop);
    hold.setControllerLevel (Effect::Reverse, true);
    GF_EXPECT (hold.topEffect() == (int) Effect::Reverse);
    hold.setControllerLevel (Effect::Reverse, false);
    GF_EXPECT (hold.topEffect() == (int) Effect::TapeStop);
    hold.clearControllerLevels();
    GF_EXPECT (hold.topEffect() == (int) Effect::TapeStop && hold.engagedMask() == ((1 << 1) | (1 << 3)));
}
GF_TEST (ControllerHolds, timeout_and_mode_changes_never_make_new_momentary_levels_sticky)
{
    HoldController hold; hold.setControllerLevel (Effect::Freeze, true);
    hold.setLatch (true);
    GF_EXPECT (hold.topEffect() == (int) Effect::Freeze);
    hold.setControllerLevel (Effect::Freeze, false, true);
    GF_EXPECT (hold.engagedMask() == 0 && (hold.timedOutMask() & 2) != 0);
    hold.setControllerLevel (Effect::Freeze, false);
    GF_EXPECT (hold.timedOutMask() == 0);
    hold.setControllerLevel (Effect::Reverse, true); hold.panic();
    GF_EXPECT (hold.engagedMask() == 0 && hold.topEffect() == -1);
}
GF_TEST (ControllerHolds, inactive_overlay_keeps_legacy_order_and_all_updates_allocate_nothing)
{
    HoldController expected, actual;
    AllocGuard guard;
    for (int i = 0; i < 10000; ++i)
    {
        const auto effect = (Effect) (i % kNumEffects);
        const auto source = (Source) ((i / 5) % 3);
        const bool on = (i % 2) == 0;
        expected.setLevel (effect, source, on); actual.setLevel (effect, source, on);
        actual.setControllerLevel (Effect::Reverse, true); actual.setControllerLevel (Effect::Reverse, false);
        GF_EXPECT (actual.engagedMask() == expected.engagedMask() && actual.topEffect() == expected.topEffect());
    }
    GF_EXPECT (guard.count() == 0);
}

GF_TEST (ControllerHolds, multiple_bindings_for_one_effect_keep_independent_levels_and_latest_order)
{
    HoldController hold;
    hold.setControllerLevel (Effect::Stutter, true, false, 0);
    hold.setControllerLevel (Effect::Freeze, true, false, 1);
    hold.setControllerLevel (Effect::Stutter, true, false, 2);
    GF_EXPECT (hold.topEffect() == (int) Effect::Stutter);
    hold.setControllerLevel (Effect::Stutter, false, false, 2);
    GF_EXPECT (hold.topEffect() == (int) Effect::Freeze && hold.engagedMask() == 3);
    hold.setControllerLevel (Effect::Freeze, false, false, 1);
    GF_EXPECT (hold.topEffect() == (int) Effect::Stutter && hold.engagedMask() == 1);
    hold.setControllerLevel (Effect::Stutter, false, false, 0);
    GF_EXPECT (hold.engagedMask() == 0);
}
