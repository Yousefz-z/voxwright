#pragma once

namespace vox::testing {

/// Wall-clock budgets in tests are written for optimized builds, which is
/// where the real-time claims are checked. Sanitizer instrumentation slows
/// processing several times over (ThreadSanitizer about 5x on the voice
/// chains, more on a loaded machine), so in those builds a timing check
/// multiplies its budget by this and only catches gross slowdowns.
/// Unoptimized (debug) builds are slower too: the slowest voice took 0.25 of
/// real time there on a CI runner, against at most 0.065 when optimized, so
/// they scale by 4 and must still keep up with real time.
#ifdef VOX_TESTING_INSTRUMENTED
inline constexpr double kTimingScale = 8.0;
#elif !defined(NDEBUG)
inline constexpr double kTimingScale = 4.0;
#else
inline constexpr double kTimingScale = 1.0;
#endif

} // namespace vox::testing
