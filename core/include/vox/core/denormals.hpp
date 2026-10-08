#pragma once

#include <cstdint>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#define VOX_DENORMALS_X86
#elif defined(__aarch64__) || defined(_M_ARM64)
#define VOX_DENORMALS_ARM64
#endif

namespace vox {

/// Enables flush-to-zero and denormals-are-zero for the current thread while
/// in scope. Denormal floats in recursive filters can slow processing by two
/// orders of magnitude, which turns into audio dropouts.
class ScopedNoDenormals {
public:
    ScopedNoDenormals() noexcept
        : previous_(readControl()) {
#if defined(VOX_DENORMALS_X86)
        _mm_setcsr(previous_ | kFtzDaz);
#elif defined(VOX_DENORMALS_ARM64) && !defined(_MSC_VER)
        const std::uint64_t fpcr = previous_ | kArmFz;
        asm volatile("msr fpcr, %0" : : "r"(fpcr));
#endif
    }

    ~ScopedNoDenormals() {
#if defined(VOX_DENORMALS_X86)
        _mm_setcsr(previous_);
#elif defined(VOX_DENORMALS_ARM64) && !defined(_MSC_VER)
        asm volatile("msr fpcr, %0" : : "r"(previous_));
#endif
    }

    ScopedNoDenormals(const ScopedNoDenormals&) = delete;
    ScopedNoDenormals& operator=(const ScopedNoDenormals&) = delete;
    ScopedNoDenormals(ScopedNoDenormals&&) = delete;
    ScopedNoDenormals& operator=(ScopedNoDenormals&&) = delete;

private:
#if defined(VOX_DENORMALS_X86)
    using ControlWord = unsigned int;
    static constexpr ControlWord kFtzDaz = 0x8040U;
#else
    using ControlWord = std::uint64_t;
    static constexpr ControlWord kArmFz = ControlWord{1} << 24U;
#endif

    /// The current floating-point control register (0 where unsupported).
    static ControlWord readControl() noexcept {
#if defined(VOX_DENORMALS_X86)
        return _mm_getcsr();
#elif defined(VOX_DENORMALS_ARM64) && !defined(_MSC_VER)
        ControlWord fpcr = 0;
        asm volatile("mrs %0, fpcr" : "=r"(fpcr));
        return fpcr;
#else
        return 0;
#endif
    }

    ControlWord previous_;
};

} // namespace vox
