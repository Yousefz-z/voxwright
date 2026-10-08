#pragma once

namespace vox::testing {

/// Wall-clock budgets in tests are written for optimized builds, which is
/// where the real-time claims are checked. Sanitizer instrumentation slows
/// processing several times over (ThreadSanitizer about 5x on the voice
/// chains, more on a loaded machine), so in those builds a timing check
/// multiplies its budget by this and only catches gross slowdowns.
#ifdef VOX_TESTING_INSTRUMENTED
inline constexpr double kTimingScale = 8.0;
#else
inline constexpr double kTimingScale = 1.0;
#endif

} // namespace vox::testing
