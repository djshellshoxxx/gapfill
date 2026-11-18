#pragma once

// Test-only deadline multiplier for instrumented builds. Sanitizers slow ordinary code
// 2-15x; a hang guard sized for Release would report steady progress as a hang. Only
// wall-clock *deadlines* use this. Workloads, iteration counts and rate assertions do not.

namespace gftest
{

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
 #define GFTEST_SANITIZED_BUILD 1
#elif defined(__has_feature)
 #if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
  #define GFTEST_SANITIZED_BUILD 1
 #endif
#endif

#ifdef GFTEST_SANITIZED_BUILD
inline constexpr int kDeadlineScale = 4;
#else
inline constexpr int kDeadlineScale = 1;
#endif

#if defined(__SANITIZE_THREAD__)
 #define GFTEST_TSAN_BUILD 1
#elif defined(__has_feature)
 #if __has_feature(thread_sanitizer)
  #define GFTEST_TSAN_BUILD 1
 #endif
#endif

/** Iteration divisor for GUI stress loops whose per-iteration cost (building the whole
    editor) TSan inflates ~75x, past the 150-minute sanitizer job. Owner approved on
    2026-11-18 (PR #40): EditorLifecycle runs 200 / 8 = 25 cycles under TSan only.
    Release, ASan and every platform keep the full count. */
#ifdef GFTEST_TSAN_BUILD
inline constexpr int kTsanIterationDivisor = 8;
#else
inline constexpr int kTsanIterationDivisor = 1;
#endif

} // namespace gftest
