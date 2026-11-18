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

} // namespace gftest
