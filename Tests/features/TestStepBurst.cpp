// F-S11 preset-step Undo coalescing rule, extracted from the editor (refactor, behaviour unchanged).
// The expectations below restate the original inline expression
//   lastStep != 0 && now - lastStep <= 3000u && pushCount == pushesAfterLastStep
// and are independent of any clock. The integration behaviour stays pinned by
// GapClose.a_burst_of_preset_steps_leaves_one_undo_entry_and_undo_restores_the_start.

#include "../TestFramework.h"

#include "features/presetbrowser/StepBurst.h"

using gapfill::presets::StepBurst;

GF_TEST (StepBurst, the_first_step_never_continues_a_burst)
{
    StepBurst burst;
    GF_EXPECT (! burst.isContinuation (100000u, 0u));
    GF_EXPECT (! burst.isContinuation (0u, 0u));
}

GF_TEST (StepBurst, a_step_inside_the_window_with_no_other_undo_push_continues)
{
    StepBurst burst;
    burst.record (10000u, 5u);
    GF_EXPECT (burst.isContinuation (10001u, 5u));
    GF_EXPECT (burst.isContinuation (13000u, 5u));            // window is inclusive
    GF_EXPECT (! burst.isContinuation (13001u, 5u));
}

GF_TEST (StepBurst, another_undo_push_in_between_ends_the_burst)
{
    StepBurst burst;
    burst.record (10000u, 5u);
    GF_EXPECT (! burst.isContinuation (10100u, 6u));
    GF_EXPECT (! burst.isContinuation (10100u, 4u));
}

GF_TEST (StepBurst, forget_starts_a_new_burst)
{
    StepBurst burst;
    burst.record (10000u, 5u);
    burst.forget();
    GF_EXPECT (! burst.isContinuation (10001u, 5u));
    GF_EXPECT (! burst.isContinuation (10001u, 0u));
}

GF_TEST (StepBurst, a_wrapped_millisecond_counter_still_measures_the_gap)
{
    StepBurst burst;
    burst.record (0xFFFFFF00u, 1u);
    GF_EXPECT (burst.isContinuation (0x00000100u, 1u));        // 512 ms later across the wrap
    GF_EXPECT (! burst.isContinuation (0x00001000u, 1u));
}

GF_TEST (StepBurst, a_step_recorded_at_time_zero_is_still_remembered)
{
    StepBurst burst;
    burst.record (0u, 2u);                                      // stored as 1: 0 would mean "no previous step"
    GF_EXPECT (burst.isContinuation (500u, 2u));
}
